
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

  orderBook.setNotificationSignal(signal);

  std::deque<Order> waiting;

  while (!stopRequested.load()) {

    // =========================================
    // 1. VERSION ACTUAL DEL CANAL
    // =========================================

    const std::uint64_t observedVersion = signal->version();

    // =========================================
    // 2. ESPERAR EL PRIMER PEDIDO
    // =========================================

    if (waiting.empty()) {
      std::optional<Order> first = orderBook.waitAndTake();

      if (!first) {
        break;
      }

      waiting.push_back(std::move(*first));
    }

    // =========================================
    // 3. RECUPERAR PEDIDOS DISPONIBLES
    // =========================================

    while (!stopRequested.load()) {
      std::optional<Order> next = orderBook.tryTake();

      if (!next) {
        break;
      }

      waiting.push_back(std::move(*next));
    }

    // =========================================
    // 4. EVALUAR PEDIDOS
    // =========================================

    for (auto it = waiting.begin();
         it != waiting.end() && !stopRequested.load();) {

      Order &order = *it;

      const std::string orderId = order.orderId;

      const long long expirationTime =
          order.time + static_cast<long long>(config.dispatch.acceptTimeoutMs);

      const long long now = clock.getSimulatedTimeMs();

      // =====================================
      // PEDIDO VENCIDO
      // =====================================

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

      // =====================================
      // COTIZAR Y BUSCAR REPARTIDOR
      // =====================================

      bool assigned = false;

      try {

        QuoteBatch batch = quoteCandidates(order);

        std::sort(batch.quotes.begin(), batch.quotes.end(),
                  [](const EtaQuote &a, const EtaQuote &b) {
                    if (a.etaSimulatedSeconds != b.etaSimulatedSeconds) {
                      return a.etaSimulatedSeconds < b.etaSimulatedSeconds;
                    }

                    return a.courierIndex < b.courierIndex;
                  });

        for (const EtaQuote &quote : batch.quotes) {

          if (stopRequested.load() ||
              clock.getSimulatedTimeMs() >= expirationTime) {
            break;
          }

          Courier &courier = fleet.at(quote.courierIndex);

          // El evento se registra DENTRO de
          // tryAssignOrder(), mientras la
          // moto sigue protegida por su mutex.
          //
          // No se registra nada si la moto
          // rechaza la asignacion.
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

      } catch (const std::exception &e) {

        logger.logEvent({{"event", "dispatchError"},
                         {"order", orderId},
                         {"message", e.what()}},
                        clock.getSimulatedTimeMs());
      }

      // =====================================
      // ASIGNACION EXITOSA
      // =====================================

      if (assigned) {

        orderBook.completeTaken();

        it = waiting.erase(it);
        continue;
      }

      // =====================================
      // SEGUIR CON OTROS PEDIDOS
      // =====================================

      ++it;
    }

    if (stopRequested.load()) {
      break;
    }

    if (waiting.empty()) {
      continue;
    }

    // =========================================
    // 5. VENCIMIENTO MAS PROXIMO
    // =========================================

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

    // =========================================
    // 6. CONVERSION A TIEMPO REAL
    // =========================================

    const double remainingRealMs =
        static_cast<double>(remainingSimulatedMs) / config.simulation.timeScale;

    const auto realWait =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double, std::milli>(remainingRealMs));

    const auto deadline = std::chrono::steady_clock::now() + realWait;

    // =========================================
    // 7. ESPERA SIN BUSY WAITING
    // =========================================

    signal->waitUntilChange(observedVersion, deadline, stopRequested);
  }

  // =================================================
  // 8. PEDIDOS PENDIENTES AL APAGAR
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

  orderBook.requestStop();

  auto signal = fleet.getCapacitySignal();

  if (signal) {
    signal->notifyChange();
  }
}
