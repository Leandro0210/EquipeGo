
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
#include <stdexcept>
#include <string>
#include <utility>

// =====================================================
// DISPATCHER - CICLO PRINCIPAL
// =====================================================

void Dispatcher::run(OrderBook &orderBook, EventLogger &logger,
                     SimulationClock &clock) {
  auto signal = fleet.getCapacitySignal();

  if (!signal) {
    throw std::runtime_error("Dispatcher sin CapacitySignal");
  }

  // Ahora los pedidos nuevos tambien despiertan
  // al Dispatcher mediante CapacitySignal.
  orderBook.setNotificationSignal(signal);

  // Los pedidos siguen siendo move-only.
  // Esta cola pertenece al hilo del Dispatcher.
  std::deque<Order> waiting;

  while (!stopRequested.load()) {

    // ---------------------------------------------
    // 1. GUARDAR VERSION ANTES DE CONSULTAR
    // ---------------------------------------------

    const std::uint64_t observedVersion = signal->version();

    // ---------------------------------------------
    // 2. SI NO TENEMOS PEDIDOS, ESPERAR UNO
    // ---------------------------------------------

    if (waiting.empty()) {

      std::optional<Order> first = orderBook.waitAndTake();

      if (!first) {
        break;
      }

      waiting.push_back(std::move(*first));
    }

    // ---------------------------------------------
    // 3. INCORPORAR LOS OTROS PEDIDOS DISPONIBLES
    // ---------------------------------------------

    while (!stopRequested.load()) {

      std::optional<Order> next = orderBook.tryTake();

      if (!next) {
        break;
      }

      waiting.push_back(std::move(*next));
    }

    // ---------------------------------------------
    // 4. EVALUAR TODOS LOS PEDIDOS EN ESPERA
    // ---------------------------------------------

    for (auto it = waiting.begin();
         it != waiting.end() && !stopRequested.load();) {

      Order &order = *it;

      const std::string orderId = order.orderId;

      const long long expirationTime =
          order.time + static_cast<long long>(config.dispatch.acceptTimeoutMs);

      const long long now = clock.getSimulatedTimeMs();

      // =========================================
      // PEDIDO VENCIDO
      // =========================================

      if (now >= expirationTime) {

        if (!order.transitionTo(OrderState::Rejected)) {
          throw std::logic_error("No se pudo rechazar: " + orderId);
        }

        logger.logEvent({{"event", "orderRejected"},
                         {"order", orderId},
                         {"reason", "noCourier"}},
                        now);

        ++rejectedCount;

        // Libera el cupo del libro de pedidos.
        orderBook.completeTaken();

        it = waiting.erase(it);
        continue;
      }

      // =========================================
      // COTIZAR Y BUSCAR REPARTIDOR
      // =========================================

      bool assigned = false;

      try {
        QuoteBatch batch = quoteCandidates(order);

        // Mejor ETA primero.
        std::sort(batch.quotes.begin(), batch.quotes.end(),
                  [](const EtaQuote &a, const EtaQuote &b) {
                    if (a.etaSimulatedSeconds != b.etaSimulatedSeconds) {

                      return a.etaSimulatedSeconds < b.etaSimulatedSeconds;
                    }

                    return a.courierIndex < b.courierIndex;
                  });

        for (const EtaQuote &quote : batch.quotes) {

          // Volver a comprobar el tiempo
          // despues del calculo ETA.
          if (stopRequested.load() ||
              clock.getSimulatedTimeMs() >= expirationTime) {
            break;
          }

          Courier &courier = fleet.at(quote.courierIndex);

          if (courier.tryAssignOrder(std::move(order))) {

            logger.logEvent({{"event", "orderAssigned"},
                             {"order", orderId},
                             {"courier", quote.courierId}},
                            clock.getSimulatedTimeMs());

            ++assignedCount;
            assigned = true;
            break;
          }
        }

      } catch (const std::exception &e) {

        logger.logEvent({{"event", "dispatchError"},
                         {"order", orderId},
                         {"message", e.what()}},
                        clock.getSimulatedTimeMs());
      }

      // =========================================
      // ASIGNACION EXITOSA
      // =========================================

      if (assigned) {

        orderBook.completeTaken();

        // El pedido ya pertenece a Courier.
        it = waiting.erase(it);
        continue;
      }

      // =========================================
      // SIN REPARTIDOR: NO BLOQUEAR LOS DEMAS
      // =========================================

      // Conservamos este pedido, pero seguimos
      // evaluando el siguiente.
      ++it;
    }

    if (stopRequested.load()) {
      break;
    }

    // No hay pedidos esperando; volver al libro.
    if (waiting.empty()) {
      continue;
    }

    // ---------------------------------------------
    // 5. CALCULAR EL VENCIMIENTO MAS PROXIMO
    // ---------------------------------------------

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
      // Hay un vencimiento para procesar.
      continue;
    }

    // ---------------------------------------------
    // 6. PASAR A TIEMPO REAL
    // ---------------------------------------------

    const double remainingRealMs =
        static_cast<double>(remainingSimulatedMs) / config.simulation.timeScale;

    const auto realWait =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double, std::milli>(remainingRealMs));

    const auto deadline = std::chrono::steady_clock::now() + realWait;

    // ---------------------------------------------
    // 7. ESPERAR SIN BUSY WAITING
    // ---------------------------------------------

    // Despierta por:
    // - un nuevo pedido
    // - capacidad liberada por una moto
    // - el vencimiento mas proximo
    // - apagado de la simulacion
    signal->waitUntilChange(observedVersion, deadline, stopRequested);
  }

  // =================================================
  // 8. CERRAR LOS PEDIDOS QUE SIGUEN EN ESPERA
  // =================================================

  for (Order &order : waiting) {

    if (!order.transitionTo(OrderState::Pending)) {

      throw std::logic_error("No se pudo finalizar pedido: " + order.orderId);
    }

    logger.logEvent({{"event", "orderPending"}, {"order", order.orderId}},
                    clock.getSimulatedTimeMs());

    ++pendingCount;

    orderBook.completeTaken();
  }
}

// =====================================================
// SOLICITAR APAGADO
// =====================================================

void Dispatcher::requestStop(OrderBook &orderBook) {
  stopRequested.store(true);

  // Despertar si espera pedidos.
  orderBook.requestStop();

  // Despertar si espera una moto disponible.
  auto signal = fleet.getCapacitySignal();

  if (signal) {
    signal->notifyChange();
  }
}
