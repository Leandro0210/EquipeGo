
#include "dispatch/Dispatcher.hpp"

#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "fleet/CapacitySignal.hpp"
#include "orders/OrderBook.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

// =====================================================
// EJECUCION AUTOMATICA DEL DISPATCHER
// =====================================================

void Dispatcher::run(OrderBook &orderBook, EventLogger &logger,
                     SimulationClock &clock) {
  auto signal = fleet.getCapacitySignal();

  if (!signal) {
    throw std::runtime_error("Dispatcher sin CapacitySignal");
  }

  // Consumidor de la cola FIFO.
  while (!stopRequested.load()) {

    // Espera un pedido sin consumir CPU.
    std::optional<Order> next = orderBook.waitAndTake();

    if (!next) {
      break;
    }

    // Transferencia de propiedad desde OrderBook.
    Order order = std::move(*next);

    const std::string orderId = order.orderId;

    // acceptTimeoutMs esta en tiempo SIMULADO.
    const long long expirationTime =
        order.time + static_cast<long long>(config.dispatch.acceptTimeoutMs);

    bool handled = false;

    // ============================================
    // INTENTOS DE ASIGNACION
    // ============================================

    while (!stopRequested.load()) {

      const long long now = clock.getSimulatedTimeMs();

      // El pedido excedio su tiempo de espera.
      if (now >= expirationTime) {

        order.transitionTo(OrderState::Rejected);

        logger.logEvent({{"event", "orderRejected"},
                         {"order", orderId},
                         {"reason", "noCourier"}},
                        now);

        ++rejectedCount;
        handled = true;
        break;
      }

      // Guardamos la version ANTES de intentar
      // asignar para no perder notificaciones.
      const std::uint64_t observedVersion = signal->version();

      std::optional<std::string> selected;

      try {
        selected = assignBestCourier(order);

      } catch (const std::exception &error) {

        // Un error de cotizacion no detiene
        // al despachador ni elimina el pedido.
        logger.logEvent({{"event", "dispatchError"},
                         {"order", orderId},
                         {"message", error.what()}},
                        clock.getSimulatedTimeMs());
      }

      if (selected) {

        logger.logEvent({{"event", "orderAssigned"},
                         {"order", orderId},
                         {"courier", *selected}},
                        clock.getSimulatedTimeMs());

        ++assignedCount;
        handled = true;
        break;
      }

      if (stopRequested.load()) {
        break;
      }

      // ========================================
      // ESPERAR CAPACIDAD O VENCIMIENTO
      // ========================================

      const long long remainingSimulatedMs =
          expirationTime - clock.getSimulatedTimeMs();

      if (remainingSimulatedMs <= 0) {
        // Volver al principio para rechazar.
        continue;
      }

      const double remainingRealMs = static_cast<double>(remainingSimulatedMs) /
                                     config.simulation.timeScale;

      const auto realWait =
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<double, std::milli>(remainingRealMs));

      const auto deadline = std::chrono::steady_clock::now() + realWait;

      // Espera bloqueante:
      // - una moto libera capacidad
      // - vence acceptTimeoutMs
      // - se solicita apagar la simulacion
      signal->waitUntilChange(observedVersion, deadline, stopRequested);
    }

    // ============================================
    // PEDIDO INTERRUMPIDO POR APAGADO
    // ============================================

    if (!handled) {

      if (order.transitionTo(OrderState::Pending)) {

        logger.logEvent({{"event", "orderPending"}, {"order", orderId}},
                        clock.getSimulatedTimeMs());

        ++pendingCount;
      }
    }
  }
}

// =====================================================
// APAGADO DEL DESPACHADOR
// =====================================================

void Dispatcher::requestStop(OrderBook &orderBook) {
  stopRequested.store(true);

  // Despertar si espera un nuevo pedido.
  orderBook.requestStop();

  // Despertar si espera capacidad libre.
  auto signal = fleet.getCapacitySignal();

  if (signal) {
    signal->notifyChange();
  }
}
