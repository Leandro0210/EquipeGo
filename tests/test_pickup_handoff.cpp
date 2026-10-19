
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>

using RealClock = std::chrono::steady_clock;


bool waitForDeliveries(Fleet &fleet) {
  auto signal = fleet.getCapacitySignal();

  std::atomic<bool> cancelled{false};

  const auto deadline = RealClock::now() + std::chrono::seconds(5);

  while (RealClock::now() < deadline) {
    const auto version = signal->version();

    const auto a = fleet.at(0).getAssignedOrderState("handoff_a");

    const auto b = fleet.at(1).getAssignedOrderState("handoff_b");

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

  bool found = false;

  for (Restaurant &restaurant : config.restaurants) {
    if (restaurant.id == "r3") {
      restaurant.pickupSlots = 1;
      restaurant.prepTimeMinMs = 0;
      restaurant.prepTimeMaxMs = 0;
      found = true;
    }
  }

  if (!found) {
    std::cerr << "ERROR: falta restaurante r3\n";
    return 1;
  }

  RoadNetwork network(config);

  bool occupationOk = false;
  bool queueOk = false;
  bool shutdownOk = false;
  bool deliveriesOk = false;


  {
    config.simulation.timeScale = 1.0;

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);

    auto gate = fleet.getRestaurantManager()->gateFor("r3");

    fleet.start();

    const bool acceptedA = fleet.at(0).tryAssignOrder(
        Order("handoff_stop_a", "r3", "n8", clock.getSimulatedTimeMs()));

    const bool occupied = gate->waitForOccupiedCount(
        1, RealClock::now() + std::chrono::seconds(3));

    const auto stateA = fleet.at(0).getAssignedOrderState("handoff_stop_a");

    occupationOk = acceptedA && occupied && gate->occupied() == 1 && stateA &&
                   *stateA == OrderState::Assigned;

    const bool acceptedB = fleet.at(1).tryAssignOrder(
        Order("handoff_stop_b", "r3", "n8", clock.getSimulatedTimeMs()));

    const bool queued = gate->waitForWaitingCount(
        1, RealClock::now() + std::chrono::seconds(3));

    const auto stateB = fleet.at(1).getAssignedOrderState("handoff_stop_b");

    queueOk = acceptedB && queued && gate->occupied() == 1 &&
              gate->peakUsage() == 1 && stateB &&
              *stateB == OrderState::Assigned;

    const auto stopStart = RealClock::now();

    fleet.requestStop();
    fleet.join();

    const auto stopElapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(RealClock::now() -
                                                              stopStart)
            .count();

    const auto results = fleet.takeFinishedOrders();

    std::size_t pending = 0;

    for (const auto &result : results) {
      if (result.order.getState() == OrderState::Pending) {
        ++pending;
      }
    }

    shutdownOk = stopElapsedMs < 1000 && results.size() == 2 && pending == 2 &&
                 gate->occupied() == 0 && gate->waitingCount() == 0;
  }


  {
    config.simulation.timeScale = 60.0;

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);

    auto gate = fleet.getRestaurantManager()->gateFor("r3");

    fleet.start();

    const bool acceptedA = fleet.at(0).tryAssignOrder(
        Order("handoff_a", "r3", "n8", clock.getSimulatedTimeMs()));

    const bool acceptedB = fleet.at(1).tryAssignOrder(
        Order("handoff_b", "r3", "n8", clock.getSimulatedTimeMs()));

    const bool delivered = waitForDeliveries(fleet);

    fleet.requestStop();
    fleet.join();

    const auto results = fleet.takeFinishedOrders();

    std::size_t deliveredCount = 0;

    for (const auto &result : results) {
      if (result.order.getState() == OrderState::Delivered) {
        ++deliveredCount;
      }
    }

    deliveriesOk = acceptedA && acceptedB && delivered && results.size() == 2 &&
                   deliveredCount == 2 && gate->peakUsage() == 1 &&
                   gate->occupied() == 0;
  }


  std::cout << "=== TEST DP5 PICKUP HANDOFF ===\n";

  std::cout << "Cupo ocupado durante handoff: "
            << (occupationOk ? "OK" : "ERROR") << '\n';

  std::cout << "Segunda moto espera en fila: " << (queueOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Apagado interrumpe handoff: " << (shutdownOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Dos entregas con un cupo: " << (deliveriesOk ? "OK" : "ERROR")
            << '\n';

  const bool passed = occupationOk && queueOk && shutdownOk && deliveriesOk;

  std::cout << "DP5 PICKUP HANDOFF TEST: " << (passed ? "OK" : "ERROR") << '\n';

  return passed ? 0 : 1;
}
