
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/OrderBook.hpp"
#include "orders/OrderProducer.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

int main(int argc, char *argv[]) {
  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  try {
    // ================================================
    // PREPARAR COMPONENTES
    // ================================================

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    EventLogger logger(config.logFilePath);
    RoadNetwork roadNetwork(config);

    OrderBook orderBook(static_cast<std::size_t>(config.orders.maxPending));

    OrderProducer producer(config, orderBook, logger, clock, roadNetwork);

    // La flota utiliza el mismo mapa y reloj.
    Fleet fleet(config, roadNetwork, clock);

    logger.logStart();

    // ================================================
    // INICIAR HILOS
    // ================================================

    // Un hilo independiente por repartidor.
    fleet.start();

    std::cout << "Flota iniciada con " << fleet.size() << " repartidores.\n";

    // Hilo independiente del productor.
    std::thread producerThread(&OrderProducer::run, &producer);

    // ================================================
    // DURACION DE LA SIMULACION
    // ================================================

    if (config.simulation.durationS > 0) {
      const double realDurationSeconds =
          static_cast<double>(config.simulation.durationS) /
          config.simulation.timeScale;

      std::this_thread::sleep_for(
          std::chrono::duration<double>(realDurationSeconds));

      logger.logEvent({{"event", "simulationStopping"}},
                      clock.getSimulatedTimeMs());

      // Detener la generacion de pedidos.
      producer.requestStop();
    }

    // ================================================
    // FINALIZACION ORDENADA
    // ================================================

    // Esperar a que termine el productor.
    producerThread.join();

    // Cerrar el libro a futuros consumidores.
    orderBook.requestStop();

    // Despertar y detener todos los repartidores.
    fleet.requestStop();

    // Esperar los hilos de las motos.
    fleet.join();

    std::cout << "Flota detenida correctamente.\n";

    // DP.4 consumira los pedidos del libro.
    // Hasta entonces permanecen en espera.
    std::cout << "Pedidos esperando Dispatcher: " << orderBook.size() << '\n';

    return 0;

  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }
}
