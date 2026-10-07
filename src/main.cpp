#include "core/Clock.hpp"
#include "config/Config.hpp"
#include "core/Logger.hpp"
#include "orders/OrderBook.hpp"
#include "orders/OrderProducer.hpp"
#include "map/RoadNetwork.hpp"

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
    // Reloj global de la simulación.
    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    // Registro de eventos.
    EventLogger logger(config.logFilePath);

    // Red de calles usada para comprobar alcanzabilidad.
    RoadNetwork roadNetwork(config);

    // Libro compartido de pedidos pendientes.
    OrderBook orderBook(static_cast<std::size_t>(config.orders.maxPending));

    // Productor que generará los pedidos.
    OrderProducer producer(config, orderBook, logger, clock, roadNetwork);

    // Todos los componentes ya están preparados.
    logger.logStart();

    // Primer hilo real de la simulación.
    std::thread producerThread(&OrderProducer::run, &producer);

    // durationS está expresado en tiempo simulado.
    if (config.simulation.durationS > 0) {
      const double realDurationSeconds =
          static_cast<double>(config.simulation.durationS) /
          config.simulation.timeScale;

      std::this_thread::sleep_for(
          std::chrono::duration<double>(realDurationSeconds));

      logger.logEvent({{"event", "simulationStopping"}},
                      clock.getSimulatedTimeMs());

      // Despierta al productor si estaba esperando.
      producer.requestStop();
    }

    // main espera a que el hilo productor termine.
    producerThread.join();

    return 0;

  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }
}
