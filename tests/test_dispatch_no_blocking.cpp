
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
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

int main(int argc, char *argv[]) {

  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  config.fleet.couriers = 1;
  config.fleet.bagCapacity = 1;

  config.simulation.timeScale = 1.0;
  config.dispatch.acceptTimeoutMs = 400;
  config.dispatch.quoteTimeoutMs = 100;

  SimulationClock clock;
  clock.start(config.simulation.timeScale);

  RoadNetwork network(config);

  const std::string logPath = "/tmp/equipego_dp4_no_blocking.jsonl";

  const std::string blockedId = "dp4_blocked";
  const std::string goodId = "dp4_good";

  std::size_t assignedCount = 0;
  std::size_t rejectedCount = 0;
  std::size_t pendingCount = 0;
  std::size_t remainingCount = 0;

  {
    EventLogger logger(logPath);
    OrderBook book(2);
    Fleet fleet(config, network, clock);

    fleet.start();

    // Primer pedido: el calculador artificial
    // no podra obtener una ETA.
    const long long creationTime = clock.getSimulatedTimeMs();

    Order blockedOrder(blockedId, "r3", "n23", creationTime);

    // Segundo pedido: ETA valida.
    Order goodOrder(goodId, "r3", "n8", creationTime);

    if (!book.tryAdd(std::move(blockedOrder)) ||
        !book.tryAdd(std::move(goodOrder))) {

      fleet.requestStop();
      fleet.join();

      std::cerr << "ERROR: no se agregaron los pedidos\n";
      return 1;
    }

    // Verificar que la cola tiene un limite.
    if (!book.full()) {
      fleet.requestStop();
      fleet.join();

      std::cerr << "ERROR: maxPending no respetado\n";
      return 1;
    }

    QuoteCalculator calculator = [](const std::string &, const std::string &,
                                    const std::string &deliveryNode) -> double {
      if (deliveryNode == "n23") {
        throw std::runtime_error("Fallo ETA simulado");
      }

      return 10.0;
    };

    Dispatcher dispatcher(config, network, fleet, calculator);

    std::thread dispatcherThread([&] { dispatcher.run(book, logger, clock); });

    // Tiempo suficiente para que:
    // - el segundo pedido se asigne
    // - el primero venza a los 400 ms
    //
    // La pausa solo pertenece al test.
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    dispatcher.requestStop(book);
    dispatcherThread.join();

    assignedCount = dispatcher.getAssignedCount();
    rejectedCount = dispatcher.getRejectedCount();
    pendingCount = dispatcher.getPendingCount();
    remainingCount = book.size();

    fleet.requestStop();
    fleet.join();
  }

  // =================================================
  // LEER RESULTADOS DEL LOG
  // =================================================

  std::ifstream file(logPath);

  if (!file.is_open()) {
    std::cerr << "ERROR: no se pudo leer log\n";
    return 1;
  }

  int blockedRejected = 0;
  int goodAssigned = 0;
  int goodRejected = 0;

  long long blockedRejectionTime = -1;
  long long goodAssignmentTime = -1;

  std::string line;

  while (std::getline(file, line)) {

    if (line.empty()) {
      continue;
    }

    const auto event = nlohmann::json::parse(line);

    const std::string id = event.value("order", std::string());

    const std::string type = event.value("event", std::string());

    if (id == blockedId && type == "orderRejected" &&
        event.value("reason", std::string()) == "noCourier") {

      ++blockedRejected;

      blockedRejectionTime = event.value("t", -1LL);
    }

    if (id == goodId && type == "orderAssigned") {

      ++goodAssigned;

      goodAssignmentTime = event.value("t", -1LL);
    }

    if (id == goodId && type == "orderRejected") {
      ++goodRejected;
    }
  }

  const bool noBlocking =
      goodAssigned == 1 && blockedRejected == 1 && goodAssignmentTime >= 0 &&
      blockedRejectionTime >= 0 && goodAssignmentTime < blockedRejectionTime;

  const bool countsCorrect = assignedCount == 1 && rejectedCount == 1 &&
                             pendingCount == 0 && remainingCount == 0 &&
                             goodRejected == 0;

  std::cout << "=== TEST DP4 SIN BLOQUEO ENTRE PEDIDOS ===\n";

  std::cout << "Segundo pedido asignado: " << goodAssigned << '\n';

  std::cout << "Primer pedido rechazado noCourier: " << blockedRejected << '\n';

  std::cout << "Asignacion segundo pedido en: " << goodAssignmentTime
            << " ms\n";

  std::cout << "Rechazo primer pedido en: " << blockedRejectionTime << " ms\n";

  std::cout << "Pedidos restantes en OrderBook: " << remainingCount << '\n';

  if (!noBlocking || !countsCorrect) {

    std::cerr << "DP4 NO BLOCKING TEST: ERROR\n";
    return 1;
  }

  std::cout << "Otros pedidos no quedan bloqueados: OK\n";

  std::cout << "Capacidad y estados consistentes: OK\n";

  std::cout << "DP4 NO BLOCKING TEST: OK\n";

  return 0;
}
