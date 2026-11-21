#include "analytics/EtaGpuDispatch.hpp"
#include "analytics/RestaurantOrderLive09.hpp"
#include "dispatch/Dispatcher.hpp"

#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "fleet/CapacitySignal.hpp"
#include "orders/OrderBook.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>


void Dispatcher::run(OrderBook &orderBook, EventLogger &logger,
                     SimulationClock &clock) {

  auto signal = fleet.getCapacitySignal();

  if (!signal) {
    throw std::runtime_error("Dispatcher sin CapacitySignal");
  }

  orderBook.setNotificationSignal(signal);

  // Scan09: indexacion constante de restaurantes, sin tocar OrderBook.
  const auto indiceRestaurantes09 =
      equipego::analytics::indexarRestaurantes09(config.restaurants);
  std::size_t lotesObservados09 = 0;
  std::size_t observacionesPedidos09 = 0;
  std::size_t gruposObservados09 = 0;
  std::size_t maximoGrupo09 = 0;

  std::deque<Order> waiting;

  while (!stopRequested.load()) {

    const std::uint64_t observedVersion = signal->version();


    if (waiting.empty()) {
      std::optional<Order> first = orderBook.waitAndTake();

      if (!first) {
        break;
      }

      waiting.push_back(std::move(*first));
    }


    while (!stopRequested.load()) {
      std::optional<Order> next = orderBook.tryTake();

      if (!next) {
        break;
      }

      waiting.push_back(std::move(*next));
    }


    // Vista CSR del lote realmente pendiente; no mueve pedidos ni
    // altera su orden FIFO de asignacion. Los pedidos pueden reaparecer
    // en varios lotes si siguen pendientes (contadores NO son unicos).
    if (!waiting.empty()) {
      // Deadline corto EN TIEMPO REAL: si CUDA se demora, CPU gana.
      const auto limite09 = std::chrono::steady_clock::now() +
          std::chrono::milliseconds(25);
      const auto resultado09 = equipego::analytics::agruparPendientesConGpu09(
          waiting, indiceRestaurantes09, config.restaurants.size(),
          gpuScanSubmitter, limite09, gpuScanMinOrders);
      const auto& vista09 = resultado09.vista;
      using Estado09 = equipego::analytics::EstadoScanGpu09;
      if (resultado09.estado != Estado09::SoloCpu) ++gpu09Intentos;
      if (resultado09.estado == Estado09::Usada) ++gpu09Usos;
      if (resultado09.estado == Estado09::SinTiempo) ++gpu09Timeouts;
      if (resultado09.estado == Estado09::FalloGpu) ++gpu09Errores;
      if (resultado09.estado == Estado09::DiferenciaCpuGpu) ++gpu09Diferencias;
      ++lotesObservados09;
      observacionesPedidos09 += waiting.size();
      gruposObservados09 += vista09.gruposNoVacios;
      maximoGrupo09 = std::max(maximoGrupo09, vista09.maximoGrupo);
    }

    for (auto it = waiting.begin();
         it != waiting.end() && !stopRequested.load();) {

      Order &order = *it;
      const std::string orderId = order.orderId;

      const long long expirationTime =
          order.time + static_cast<long long>(config.dispatch.acceptTimeoutMs);

      const long long now = clock.getSimulatedTimeMs();


      if (now >= expirationTime) {

        if (!order.transitionTo(OrderState::Rejected)) {
          throw std::logic_error("No se pudo rechazar: " + orderId);
        }

        logger.logEvent({{"event", "orderRejected"},
                         {"order", orderId},
                         {"reason", "noCourier"}},
                        now);

        ++rejectedCount;

        orderBook.completeTaken();

        it = waiting.erase(it);
        continue;
      }


      bool assigned = false;

      try {

        // CPU QuoteCalculator conserva su plazo habitual. GPU solo usa el
        // tiempo REAL restante del mismo quoteTimeoutMs y, si vence, CPU.
        const auto limiteGpu08 = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(config.dispatch.quoteTimeoutMs);
        QuoteBatch batch = quoteCandidates(order);

        // Feature 08: argmin CPU para elegir primer courier;
        // todos los demas se mantienen ordenados para reintentos.
        const auto estadoGpu08 = equipego::analytics::ordenarConGpu08Ocpu(
            batch.quotes, gpuEtaSubmitter, limiteGpu08, gpuMinQuotes);
        using EstadoGpu08 = equipego::analytics::EstadoEleccionGpu08;
        if (estadoGpu08 != EstadoGpu08::SoloCpu) ++gpu08Intentos;
        if (estadoGpu08 == EstadoGpu08::Usada) ++gpu08Usos;
        if (estadoGpu08 == EstadoGpu08::SinTiempo) ++gpu08Timeouts;
        if (estadoGpu08 == EstadoGpu08::FalloGpu) ++gpu08Errores;
        if (estadoGpu08 == EstadoGpu08::DiferenciaCpuGpu) ++gpu08Diferencias;

        for (const EtaQuote &quote : batch.quotes) {

          if (stopRequested.load() ||
              clock.getSimulatedTimeMs() >= expirationTime) {
            break;
          }

          Courier &courier = fleet.at(quote.courierIndex);

          const auto onAssigned = [&logger, &clock, &orderId, &quote]() {
            logger.logEvent({{"event", "orderAssigned"},
                             {"order", orderId},
                             {"courier", quote.courierId}},
                            clock.getSimulatedTimeMs());
          };

          if (courier.tryAssignOrder(std::move(order), onAssigned)) {

            ++assignedCount;
            assigned = true;
            break;
          }
        }

      } catch (const std::exception &error) {

        std::cerr << "Error despachando " << orderId << ": " << error.what()
                  << '\n';
      }

      if (assigned) {

        orderBook.completeTaken();
        it = waiting.erase(it);
        continue;
      }

      ++it;
    }

    if (stopRequested.load()) {
      break;
    }

    if (waiting.empty()) {
      continue;
    }


    long long earliestExpiration =
        waiting.front().time +
        static_cast<long long>(config.dispatch.acceptTimeoutMs);

    for (const Order &order : waiting) {

      const long long expiration =
          order.time + static_cast<long long>(config.dispatch.acceptTimeoutMs);

      earliestExpiration = std::min(earliestExpiration, expiration);
    }

    const long long remainingSimulatedMs =
        earliestExpiration - clock.getSimulatedTimeMs();

    if (remainingSimulatedMs <= 0) {
      continue;
    }

    const double remainingRealMs =
        static_cast<double>(remainingSimulatedMs) / config.simulation.timeScale;

    const auto realWait =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double, std::milli>(remainingRealMs));

    const auto deadline = std::chrono::steady_clock::now() + realWait;

    signal->waitUntilChange(observedVersion, deadline, stopRequested);
  }


  std::cerr << "SCAN09_CPU,batches=" << lotesObservados09
            << ",observaciones=" << observacionesPedidos09
            << ",grupos=" << gruposObservados09
            << ",max_grupo=" << maximoGrupo09 << '\n';

#ifdef EQUIPEGO_USE_CUDA
  std::cerr << "SCAN09_GPU,intentos=" << gpu09Intentos.load()
            << ",usos=" << gpu09Usos.load()
            << ",timeouts=" << gpu09Timeouts.load()
            << ",errores=" << gpu09Errores.load()
            << ",diferencias=" << gpu09Diferencias.load()
            << ",minimo_pedidos=" << gpuScanMinOrders << '\n';
#endif

  for (Order &order : waiting) {

    if (!order.transitionTo(OrderState::Pending)) {
      throw std::logic_error("No se pudo finalizar pedido: " + order.orderId);
    }

    ++pendingCount;
    orderBook.completeTaken();
  }
}


void Dispatcher::requestStop(OrderBook &orderBook) {

  stopRequested.store(true);

  orderBook.requestStop();

  auto signal = fleet.getCapacitySignal();

  if (signal) {
    signal->notifyChange();
  }
}