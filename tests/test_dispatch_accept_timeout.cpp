
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "dispatch/Dispatcher.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"
#include "orders/OrderBook.hpp"


#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

int main(int argc, char *argv[]) {

  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  // ================================================
  // 1. PREPARAR ESCENARIO CONTROLADO
  // ================================================

  config.fleet.couriers = 1;
  config.fleet.bagCapacity = 1;

  // Hacer que la moto tarde bastante en entregar.
  config.fleet.speedKmh = 1.0;

  // Un segundo simulado = un segundo real.
  config.simulation.timeScale = 1.0;

  // El segundo pedido solo puede esperar
  // 250 milisegundos simulados.
  config.dispatch.acceptTimeoutMs = 250;
  config.dispatch.quoteTimeoutMs = 100;

  SimulationClock clock;
  clock.start(config.simulation.timeScale);

  RoadNetwork network(config);

  const std::string logPath = "/tmp/equipego_dp4_accept_test.jsonl";

  const std::string targetId = "dp4_no_courier";

  std::size_t assignedCount = 0;
  std::size_t rejectedCount = 0;
  std::size_t pendingCount = 0;
  std::size_t remainingCount = 0;

  long long creationTime = 0;

  // ================================================
  // 2. INICIAR FLOTA Y DISPATCHER
  // ================================================

  {
    EventLogger logger(logPath);
    OrderBook book(4);
    Fleet fleet(config, network, clock);

    fleet.start();

    // --------------------------------------------
    // OCUPAR LA UNICA MOTO
    // --------------------------------------------

    Order busyOrder("dp4_busy", "r3", "n23", clock.getSimulatedTimeMs());

    const bool occupied = fleet.at(0).tryAssignOrder(std::move(busyOrder));

    if (!occupied || fleet.at(0).getReservedCount() != 1) {

      fleet.requestStop();
      fleet.join();

      std::cerr << "ERROR: no se pudo ocupar la moto\n";

      return 1;
    }

    // --------------------------------------------
    // CREAR SEGUNDO PEDIDO
    // --------------------------------------------

    creationTime = clock.getSimulatedTimeMs();

    Order waitingOrder(targetId, "r3", "n23", creationTime);

    if (!book.tryAdd(std::move(waitingOrder))) {

      fleet.requestStop();
      fleet.join();

      std::cerr << "ERROR: no se pudo agregar pedido\n";

      return 1;
    }

    Dispatcher dispatcher(config, network, fleet);

    // --------------------------------------------
    // DETENER LA PRUEBA DESPUES DEL TIMEOUT
    // --------------------------------------------

    // Esta pausa existe solamente en el test:
    // establece un limite a su duracion.
    // El Dispatcher real usa condition_variable.
    std::thread stopper([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));

      dispatcher.requestStop(book);
    });

    // Procesar el pedido desde OrderBook.
    try {
      dispatcher.run(book, logger, clock);

    } catch (const std::exception &e) {

      dispatcher.requestStop(book);
      stopper.join();

      fleet.requestStop();
      fleet.join();

      std::cerr << "ERROR del Dispatcher: " << e.what() << '\n';

      return 1;
    }

    stopper.join();

    assignedCount = dispatcher.getAssignedCount();
    rejectedCount = dispatcher.getRejectedCount();
    pendingCount = dispatcher.getPendingCount();
    remainingCount = book.size();

    fleet.requestStop();
    fleet.join();
  }

  // ================================================
  // 3. VERIFICAR EVENTO DEL LOG
  // ================================================

  // El EventLogger ya fue destruido y su
  // archivo quedo cerrado.

  std::ifstream logFile(logPath);

  if (!logFile.is_open()) {
    std::cerr << "ERROR: no se pudo leer el log\n";
    return 1;
  }

  std::size_t noCourierEvents = 0;
  std::size_t assignedEvents = 0;

  long long rejectionTime = -1;

  std::string line;

  while (std::getline(logFile, line)) {

    if (line.empty()) {
      continue;
    }

    const auto event = nlohmann::json::parse(line);

    if (event.value("order", std::string()) != targetId) {
      continue;
    }

    if (event.value("event", std::string()) == "orderAssigned") {

      ++assignedEvents;
    }

    if (event.value("event", std::string()) == "orderRejected" &&
        event.value("reason", std::string()) == "noCourier") {

      ++noCourierEvents;

      rejectionTime = event.value("t", -1LL);
    }
  }

  // ================================================
  // 4. COMPROBAR RESULTADOS
  // ================================================

  const bool correctRejection = rejectedCount == 1 && noCourierEvents == 1;

  const bool neverAssigned = assignedCount == 0 && assignedEvents == 0;

  const bool noMissingOrders = pendingCount == 0 && remainingCount == 0;

  const bool respectedTimeout =
      rejectionTime >= creationTime + config.dispatch.acceptTimeoutMs;

  std::cout << "=== TEST DP4 ACCEPT TIMEOUT ===\n";

  std::cout << "Plazo simulado: " << config.dispatch.acceptTimeoutMs << " ms\n";

  std::cout << "Tiempo simulado hasta rechazo: "
            << (rejectionTime - creationTime) << " ms\n";

  std::cout << "Rechazos noCourier: " << noCourierEvents << '\n';

  std::cout << "Asignaciones incorrectas: " << assignedEvents << '\n';

  std::cout << "Pedidos restantes en OrderBook: " << remainingCount << '\n';

  if (!correctRejection || !neverAssigned || !noMissingOrders ||
      !respectedTimeout) {

    std::cerr << "DP4 ACCEPT TIMEOUT TEST: ERROR\n";

    return 1;
  }

  std::cout << "Rechazo por noCourier: OK\n";

  std::cout << "Tiempo limite simulado respetado: OK\n";

  std::cout << "DP4 ACCEPT TIMEOUT TEST: OK\n";

  return 0;
}
