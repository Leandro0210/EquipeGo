
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using RealClock = std::chrono::steady_clock;

bool waitForTwoDeliveries(Fleet &fleet, const std::string &firstId,
                          const std::string &secondId) {
  auto signal = fleet.getCapacitySignal();
  std::atomic<bool> cancelled{false};

  const auto deadline = RealClock::now() + std::chrono::seconds(5);

  while (RealClock::now() < deadline) {
    const auto version = signal->version();

    const auto a = fleet.at(0).getAssignedOrderState(firstId);

    const auto b = fleet.at(1).getAssignedOrderState(secondId);

    if (a && b && *a == OrderState::Delivered && *b == OrderState::Delivered) {
      return true;
    }

    signal->waitUntilChange(version, deadline, cancelled);
  }

  return false;
}

int main(int argc, char *argv[]) {
  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  config.fleet.couriers = 2;
  config.fleet.bagCapacity = 1;
  config.fleet.startNode = "n8";
  config.simulation.timeScale = 60.0;

  bool foundRestaurant = false;

  for (Restaurant &restaurant : config.restaurants) {
    if (restaurant.id == "r3") {
      restaurant.pickupSlots = 1;

      // Este test evalua cupos, no cocina.
      restaurant.prepTimeMinMs = 0;
      restaurant.prepTimeMaxMs = 0;

      foundRestaurant = true;
    }
  }

  if (!foundRestaurant) {
    std::cerr << "ERROR: falta restaurante r3\n";
    return 1;
  }

  SimulationClock clock;
  clock.start(config.simulation.timeScale);

  RoadNetwork network(config);

  bool integrationOk = false;
  bool shutdownOk = false;

  // =============================================
  // TEST 1: CUPOS COMPARTIDOS
  // =============================================

  {
    Fleet fleet(config, network, clock);

    auto manager = fleet.getRestaurantManager();
    auto gate = manager->gateFor("r3");

    const bool sharedManager = manager->size() == config.restaurants.size() &&
                               gate == manager->gateFor("r3");

    auto blocker = gate->acquire();

    fleet.start();

    const bool a = fleet.at(0).tryAssignOrder(
        Order("dp5_a", "r3", "n8", clock.getSimulatedTimeMs()));

    const bool b = fleet.at(1).tryAssignOrder(
        Order("dp5_b", "r3", "n8", clock.getSimulatedTimeMs()));

    const bool bothWaiting = gate->waitForWaitingCount(
        2, RealClock::now() + std::chrono::seconds(5));

    const auto stateA = fleet.at(0).getAssignedOrderState("dp5_a");

    const auto stateB = fleet.at(1).getAssignedOrderState("dp5_b");

    const bool correctlyBlocked = stateA && stateB &&
                                  *stateA == OrderState::Assigned &&
                                  *stateB == OrderState::Assigned;

    blocker.reset();

    const bool bothDelivered = waitForTwoDeliveries(fleet, "dp5_a", "dp5_b");

    fleet.requestStop();
    fleet.join();

    auto results = fleet.takeFinishedOrders();

    std::size_t delivered = 0;

    for (const auto &result : results) {
      if (result.order.getState() == OrderState::Delivered) {
        ++delivered;
      }
    }

    integrationOk = sharedManager && a && b && bothWaiting &&
                    correctlyBlocked && bothDelivered && results.size() == 2 &&
                    delivered == 2 && gate->peakUsage() == 1 &&
                    gate->occupied() == 0;
  }

  // =============================================
  // TEST 2: APAGADO EN FILA
  // =============================================

  {
    Fleet fleet(config, network, clock);

    auto gate = fleet.getRestaurantManager()->gateFor("r3");

    auto blocker = gate->acquire();

    fleet.start();

    const bool a = fleet.at(0).tryAssignOrder(
        Order("dp5_stop_a", "r3", "n8", clock.getSimulatedTimeMs()));

    const bool b = fleet.at(1).tryAssignOrder(
        Order("dp5_stop_b", "r3", "n8", clock.getSimulatedTimeMs()));

    const bool bothWaiting = gate->waitForWaitingCount(
        2, RealClock::now() + std::chrono::seconds(5));

    fleet.requestStop();
    fleet.join();

    auto results = fleet.takeFinishedOrders();

    std::size_t pending = 0;

    for (const auto &result : results) {
      if (result.order.getState() == OrderState::Pending) {
        ++pending;
      }
    }

    blocker.reset();

    shutdownOk = a && b && bothWaiting && results.size() == 2 && pending == 2 &&
                 gate->waitingCount() == 0 && gate->occupied() == 0;
  }

  std::cout << "=== TEST DP5 RESTAURANT FLEET ===\n";

  std::cout << "Cupos compartidos entre motos: "
            << (integrationOk ? "OK" : "ERROR") << '\n';

  std::cout << "Apagado con motos esperando: " << (shutdownOk ? "OK" : "ERROR")
            << '\n';

  const bool passed = integrationOk && shutdownOk;

  std::cout << "DP5 RESTAURANT FLEET TEST: " << (passed ? "OK" : "ERROR")
            << '\n';

  return passed ? 0 : 1;
}
