
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "dispatch/Dispatcher.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/OrderBook.hpp"
#include "orders/OrderProducer.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

// =====================================================
// MAIN - EQUIPEGO
// =====================================================

int main(int argc, char *argv[]) {

  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  try {
    // ============================================
    // 1. PREPARAR COMPONENTES
    // ============================================

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    EventLogger logger(config.logFilePath);
    RoadNetwork roadNetwork(config);

    OrderBook orderBook(static_cast<std::size_t>(config.orders.maxPending));

    OrderProducer producer(config, orderBook, logger, clock, roadNetwork);

    Fleet fleet(config, roadNetwork, clock);

    Dispatcher dispatcher(config, roadNetwork, fleet);

    logger.logStart();

    // ============================================
    // 2. INICIAR HILOS
    // ============================================

    fleet.start();

    std::cout << "Flota iniciada con " << fleet.size() << " repartidores.\n";

    std::thread dispatcherThread(&Dispatcher::run, &dispatcher,
                                 std::ref(orderBook), std::ref(logger),
                                 std::ref(clock));

    std::thread producerThread(&OrderProducer::run, &producer);

    std::cout << "Dispatcher iniciado.\n";

    // ============================================
    // 3. DURACION DE LA SIMULACION
    // ============================================

    if (config.simulation.durationS > 0) {

      const double realDurationSeconds =
          static_cast<double>(config.simulation.durationS) /
          config.simulation.timeScale;

      std::this_thread::sleep_for(
          std::chrono::duration<double>(realDurationSeconds));

      logger.logEvent({{"event", "simulationStopping"}},
                      clock.getSimulatedTimeMs());
    }

    // ============================================
    // 4. DETENER PRODUCTOR
    // ============================================

    producer.requestStop();
    producerThread.join();

    const std::size_t createdCount = producer.getCreatedCount();

    const std::size_t producerRejected = producer.getRejectedCount();

    // ============================================
    // 5. DETENER DISPATCHER
    // ============================================

    dispatcher.requestStop(orderBook);
    dispatcherThread.join();

    const std::size_t assignedCount = dispatcher.getAssignedCount();

    const std::size_t dispatcherRejected = dispatcher.getRejectedCount();

    const std::size_t dispatcherPending = dispatcher.getPendingCount();

    // ============================================
    // 6. DETENER FLOTA
    // ============================================

    fleet.requestStop();
    fleet.join();

    // ============================================
    // 7. CONTABILIZAR PEDIDOS DE FLEET
    // ============================================

    std::vector<CourierOrderResult> fleetResults = fleet.takeFinishedOrders();

    const std::size_t fleetFinalizedCount = fleetResults.size();

    std::size_t deliveredCount = 0;
    std::size_t fleetPendingCount = 0;

    std::unordered_set<std::string> fleetOrderIds;

    for (const CourierOrderResult &result : fleetResults) {

      const Order &order = result.order;

      // Verificar que un pedido no aparezca
      // dos veces en los resultados de la flota.
      if (!fleetOrderIds.insert(order.orderId).second) {

        throw std::logic_error("Pedido duplicado en Fleet: " + order.orderId);
      }

      // ----------------------------------------
      // PEDIDO ENTREGADO
      // ----------------------------------------

      if (order.getState() == OrderState::Delivered) {

        ++deliveredCount;

        logger.logEvent({{"event", "orderDelivered"},
                         {"order", order.orderId},
                         {"courier", result.courierId},
                         {"recordedAtShutdown", true}},
                        clock.getSimulatedTimeMs());

        // ----------------------------------------
        // PEDIDO PENDIENTE EN MOTO
        // ----------------------------------------

      } else if (order.getState() == OrderState::Pending) {

        ++fleetPendingCount;

        logger.logEvent({{"event", "orderPending"},
                         {"order", order.orderId},
                         {"courier", result.courierId},
                         {"source", "fleet"}},
                        clock.getSimulatedTimeMs());

      } else {

        throw std::logic_error("Pedido sin estado final en Fleet: " +
                               order.orderId);
      }
    }

    // ============================================
    // 8. RECUPERAR PENDIENTES DE ORDERBOOK
    // ============================================

    std::vector<Order> remainingOrders = orderBook.takeRemaining();

    std::size_t bookPendingCount = 0;

    for (Order &order : remainingOrders) {

      if (!order.transitionTo(OrderState::Pending)) {

        throw std::logic_error("No se pudo finalizar: " + order.orderId);
      }

      logger.logEvent({{"event", "orderPending"},
                       {"order", order.orderId},
                       {"source", "orderBook"}},
                      clock.getSimulatedTimeMs());

      ++bookPendingCount;
    }

    // ============================================
    // 9. CALCULAR RESULTADOS GLOBALES
    // ============================================

    const std::size_t rejectedCount = producerRejected + dispatcherRejected;

    const std::size_t pendingCount =
        fleetPendingCount + dispatcherPending + bookPendingCount;

    const std::size_t totalFinalized =
        deliveredCount + rejectedCount + pendingCount;

    // Todos los pedidos asignados deben haber
    // terminado en Fleet.
    const bool fleetComplete = fleetFinalizedCount == assignedCount;

    // La suma de los estados finales debe ser
    // igual al total de pedidos creados.
    const bool accountingComplete = totalFinalized == createdCount;

    // ============================================
    // 10. RESUMEN FINAL
    // ============================================

    std::cout << "\n=== RESUMEN FINAL EQUIPEGO ===\n";

    std::cout << "Pedidos generados: " << createdCount << '\n';

    std::cout << "Pedidos asignados a motos: " << assignedCount << '\n';

    std::cout << "\n--- ESTADOS FINALES ---\n";

    std::cout << "Entregados: " << deliveredCount << '\n';

    std::cout << "Rechazados: " << rejectedCount << '\n';

    std::cout << "Pendientes: " << pendingCount << '\n';

    std::cout << "\n--- DETALLE DE PENDIENTES ---\n";

    std::cout << "En motos: " << fleetPendingCount << '\n';

    std::cout << "En Dispatcher: " << dispatcherPending << '\n';

    std::cout << "En OrderBook: " << bookPendingCount << '\n';

    std::cout << "\n--- AUDITORIA ---\n";

    std::cout << "Total contabilizado: " << totalFinalized << '\n';

    std::cout << "Pedidos restantes en OrderBook: " << orderBook.size() << '\n';

    std::cout << "Todos los asignados finalizados: "
              << (fleetComplete ? "OK" : "ERROR") << '\n';

    std::cout << "Balance global: " << (accountingComplete ? "OK" : "ERROR")
              << '\n';

    // ============================================
    // 11. VALIDAR INTEGRIDAD
    // ============================================

    if (!fleetComplete || !accountingComplete) {

      std::cerr << "ERROR: inconsistencia en "
                << "la contabilidad de pedidos\n";

      return 2;
    }

    std::cout << "Flota detenida correctamente.\n";

    return 0;

  } catch (const std::exception &error) {

    std::cerr << "Error: " << error.what() << '\n';

    return 1;
  }
}
