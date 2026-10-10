
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
#include <thread>
#include <vector>

int main(int argc, char *argv[]) {

  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  try {
    // ============================================
    // PREPARAR COMPONENTES
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
    // INICIAR COMPONENTES CONCURRENTES
    // ============================================

    fleet.start();

    std::cout << "Flota iniciada con " << fleet.size() << " repartidores.\n";

    std::thread dispatcherThread(&Dispatcher::run, &dispatcher,
                                 std::ref(orderBook), std::ref(logger),
                                 std::ref(clock));

    std::thread producerThread(&OrderProducer::run, &producer);

    std::cout << "Dispatcher iniciado.\n";

    // ============================================
    // DURACION DE SIMULACION
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
    // DETENER EL PRODUCTOR
    // ============================================

    producer.requestStop();
    producerThread.join();

    // ============================================
    // DETENER EL DISPATCHER
    // ============================================

    dispatcher.requestStop(orderBook);
    dispatcherThread.join();

    // ============================================
    // DETENER LA FLOTA
    // ============================================

    fleet.requestStop();
    fleet.join();

    // ============================================
    // FINALIZAR PEDIDOS QUE SIGUEN EN ORDERBOOK
    // ============================================

    std::vector<Order> remainingOrders = orderBook.takeRemaining();

    std::size_t bookPendingCount = 0;

    for (Order &order : remainingOrders) {

      if (!order.transitionTo(OrderState::Pending)) {

        throw std::logic_error("No se pudo finalizar el pedido: " +
                               order.orderId);
      }

      logger.logEvent({{"event", "orderPending"}, {"order", order.orderId}},
                      clock.getSimulatedTimeMs());

      ++bookPendingCount;
    }

    // ============================================
    // RESUMEN PROVISIONAL
    // ============================================

    const std::size_t dispatcherPending = dispatcher.getPendingCount();

    const std::size_t totalPending = dispatcherPending + bookPendingCount;

    std::cout << "\n=== RESUMEN DISPATCHER ===\n";

    std::cout << "Pedidos asignados: " << dispatcher.getAssignedCount() << '\n';

    std::cout << "Pedidos rechazados noCourier: "
              << dispatcher.getRejectedCount() << '\n';

    std::cout << "Pendientes en Dispatcher: " << dispatcherPending << '\n';

    std::cout << "Pendientes recuperados de OrderBook: " << bookPendingCount
              << '\n';

    std::cout << "Total pendientes sin asignar: " << totalPending << '\n';

    std::cout << "Pedidos restantes en OrderBook: " << orderBook.size() << '\n';

    std::cout << "Flota detenida correctamente.\n";

    return 0;

  } catch (const std::exception &error) {

    std::cerr << "Error: " << error.what() << '\n';

    return 1;
  }
}
