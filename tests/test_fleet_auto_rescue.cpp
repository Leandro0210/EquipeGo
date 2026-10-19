
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/Fleet.hpp"
#include "incidents/BreakdownPolicy.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using RealClock = std::chrono::steady_clock;

template <typename Predicate>
bool waitFor(const std::shared_ptr<CapacitySignal> &signal,
             Predicate predicate) {
  std::atomic<bool> cancelled{false};

  const auto deadline = RealClock::now() + std::chrono::seconds(10);

  while (RealClock::now() < deadline) {
    const auto version = signal->version();

    if (predicate()) {
      return true;
    }

    signal->waitUntilChange(version, deadline, cancelled);
  }

  return predicate();
}

std::string findRescueOrder(unsigned int seed, double probability) {
  for (int i = 0; i < 10000; ++i) {
    const std::string id = "auto_rescue_" + std::to_string(i);

    const auto source =
        incidents::makeBreakdownPlan(seed, "c0", id, "toCustomer", probability);

    const auto recipient =
        incidents::makeBreakdownPlan(seed, "c1", id, "toCustomer", probability);

    if (source.occurs && !recipient.occurs) {
      return id;
    }
  }

  return "";
}

int main(int argc, char *argv[]) {
  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  config.fleet.couriers = 2;
  config.fleet.bagCapacity = 1;
  config.fleet.startNode = "n8";
  config.simulation.timeScale = 120.0;

  for (Restaurant &restaurant : config.restaurants) {
    if (restaurant.id == "r3") {
      restaurant.nodeId = "n8";
      restaurant.prepTimeMinMs = 0;
      restaurant.prepTimeMaxMs = 0;
    }
  }

  RoadNetwork network(config);

  bool automaticRescueOk = false;
  bool deliveredOk = false;
  bool simultaneousOk = false;
  bool noDeadlockOk = false;


  {
    config.incidents.breakdownProbability = 0.5;

    const std::string orderId = findRescueOrder(
        config.orders.seed, config.incidents.breakdownProbability);

    if (orderId.empty()) {
      std::cerr << "ERROR: no se encontro pedido de prueba\n";
      return 1;
    }

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);
    fleet.start();
    fleet.startRescue();

    const bool accepted = fleet.at(0).tryAssignOrder(
        Order(orderId, "r3", "n23", clock.getSimulatedTimeMs()));

    const bool delivered = waitFor(fleet.getCapacitySignal(), [&] {
      const auto state = fleet.at(1).getAssignedOrderState(orderId);

      return state && *state == OrderState::Delivered;
    });

    const bool sourceBroken = fleet.at(0).getState() == CourierState::Broken;

    const bool sourceEmpty = fleet.at(0).getReservedCount() == 0;

    fleet.requestStop();
    fleet.join();

    const auto results = fleet.takeFinishedOrders();

    automaticRescueOk = accepted && sourceBroken && sourceEmpty && delivered;

    deliveredOk = results.size() == 1 && results[0].courierId == "c1" &&
                  results[0].order.orderId == orderId &&
                  results[0].order.getState() == OrderState::Delivered;
  }


  {
    config.incidents.breakdownProbability = 1.0;

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);
    fleet.start();
    fleet.startRescue();

    const bool acceptedA = fleet.at(0).tryAssignOrder(
        Order("simultaneous_a", "r3", "n23", clock.getSimulatedTimeMs()));

    const bool acceptedB = fleet.at(1).tryAssignOrder(
        Order("simultaneous_b", "r3", "n23", clock.getSimulatedTimeMs()));

    const bool bothBroken = waitFor(fleet.getCapacitySignal(), [&] {
      return fleet.at(0).getState() == CourierState::Broken &&
             fleet.at(1).getState() == CourierState::Broken;
    });

    simultaneousOk = acceptedA && acceptedB && bothBroken &&
                     fleet.at(0).getReservedCount() == 1 &&
                     fleet.at(1).getReservedCount() == 1;

    const auto stopStart = RealClock::now();

    fleet.requestStop();
    fleet.join();

    const auto stopElapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(RealClock::now() -
                                                              stopStart)
            .count();

    const auto results = fleet.takeFinishedOrders();

    std::set<std::string> ids;
    std::size_t pending = 0;

    for (const auto &result : results) {
      ids.insert(result.order.orderId);

      if (result.order.getState() == OrderState::Pending) {
        ++pending;
      }
    }

    noDeadlockOk = stopElapsed < 3000 && results.size() == 2 &&
                   ids.size() == 2 && pending == 2;
  }


  std::cout << "=== TEST DP6 FLEET AUTO RESCUE ===\n";

  std::cout << "Rescate automatico: " << (automaticRescueOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Pedido rescatado entregado: " << (deliveredOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Averias simultaneas: " << (simultaneousOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Sin deadlocks ni perdidas: " << (noDeadlockOk ? "OK" : "ERROR")
            << '\n';

  const bool passed =
      automaticRescueOk && deliveredOk && simultaneousOk && noDeadlockOk;

  std::cout << "DP6 FLEET AUTO RESCUE TEST: " << (passed ? "OK" : "ERROR")
            << '\n';

  return passed ? 0 : 1;
}
