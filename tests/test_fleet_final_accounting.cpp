
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

int main(int argc, char *argv[]) {
  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  config.fleet.couriers = 2;
  config.fleet.bagCapacity = 1;
  config.fleet.speedKmh = 1.0;
  config.simulation.timeScale = 1.0;

  for (Restaurant &restaurant : config.restaurants) {
    if (restaurant.id == "r3") {
      restaurant.prepTimeMinMs = 0;
      restaurant.prepTimeMaxMs = 0;
    }
  }

  SimulationClock clock;
  clock.start(config.simulation.timeScale);

  RoadNetwork network(config);
  Fleet fleet(config, network, clock);

  fleet.start();

  auto signal = fleet.getCapacitySignal();
  const auto beforeDelivery = signal->version();

  Order fastOrder("dp4_delivered", "r3", "n8", clock.getSimulatedTimeMs());

  Order slowOrder("dp4_pending", "r3", "n23", clock.getSimulatedTimeMs());

  const bool firstAccepted = fleet.at(0).tryAssignOrder(std::move(fastOrder));

  const bool secondAccepted = fleet.at(1).tryAssignOrder(std::move(slowOrder));

  if (!firstAccepted || !secondAccepted) {
    fleet.requestStop();
    fleet.join();

    std::cerr << "ERROR: no se pudieron asignar pedidos\n";

    return 1;
  }

  std::atomic<bool> stopRequested{false};

  const bool notified = signal->waitUntilChange(
      beforeDelivery,
      std::chrono::steady_clock::now() + std::chrono::seconds(5),
      stopRequested);

  const auto firstState = fleet.at(0).getAssignedOrderState("dp4_delivered");

  const bool deliveredBeforeStop =
      firstState && *firstState == OrderState::Delivered;

  fleet.requestStop();
  fleet.join();

  auto results = fleet.takeFinishedOrders();

  std::size_t delivered = 0;
  std::size_t pending = 0;
  std::set<std::string> ids;

  for (const auto &result : results) {
    ids.insert(result.order.orderId);

    if (result.order.getState() == OrderState::Delivered) {
      ++delivered;
    }

    if (result.order.getState() == OrderState::Pending) {
      ++pending;
    }
  }

  const auto secondCollection = fleet.takeFinishedOrders();

  const bool correct = notified && deliveredBeforeStop && results.size() == 2 &&
                       delivered == 1 && pending == 1 && ids.size() == 2 &&
                       secondCollection.empty();

  std::cout << "=== TEST CONTABILIDAD FINAL FLEET ===\n";

  std::cout << "Pedidos recuperados: " << results.size() << '\n';

  std::cout << "Entregados: " << delivered << '\n';

  std::cout << "Pendientes: " << pending << '\n';

  std::cout << "Sin duplicados: " << (ids.size() == 2 ? "SI" : "NO") << '\n';

  std::cout << "Segunda recoleccion vacia: "
            << (secondCollection.empty() ? "SI" : "NO") << '\n';

  std::cout << "FLEET FINAL ACCOUNTING TEST: " << (correct ? "OK" : "ERROR")
            << '\n';

  return correct ? 0 : 1;
}
