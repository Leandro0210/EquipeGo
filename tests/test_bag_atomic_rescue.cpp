
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

using RealClock = std::chrono::steady_clock;


template <typename Predicate>
bool waitFor(const std::shared_ptr<CapacitySignal> &signal,
             Predicate predicate) {
  std::atomic<bool> cancelled{false};

  const auto deadline = RealClock::now() + std::chrono::seconds(14);

  while (RealClock::now() < deadline) {
    const auto version = signal->version();

    if (predicate()) {
      return true;
    }

    signal->waitUntilChange(version, deadline, cancelled);
  }

  return predicate();
}

bool remainsUntransferred(Fleet &fleet,
                          const std::shared_ptr<CapacitySignal> &signal) {
  std::atomic<bool> cancelled{false};

  const auto deadline = RealClock::now() + std::chrono::milliseconds(500);

  const auto intact = [&]() {
    return fleet.getRescueCount() == 0 && fleet.getRescuedOrderCount() == 0 &&
           fleet.at(0).getReservedCount() == 2 &&
           fleet.at(1).getReservedCount() == 1 &&
           fleet.at(2).getReservedCount() == 1;
  };

  while (RealClock::now() < deadline) {
    const auto version = signal->version();

    if (!intact()) {
      return false;
    }

    signal->waitUntilChange(version, deadline, cancelled);
  }

  return intact();
}


std::string findSafeOrder(unsigned int seed, double probability,
                          bool sourceMustBreak, const std::string &excluded) {
  for (int i = 0; i < 10000; ++i) {
    const std::string id = "whole_bag_" + std::to_string(i);

    if (id == excluded) {
      continue;
    }

    const auto source =
        incidents::makeBreakdownPlan(seed, "c0", id, "toCustomer", probability);

    const auto recipient =
        incidents::makeBreakdownPlan(seed, "c1", id, "toCustomer", probability);

    if (!recipient.occurs && (!sourceMustBreak || source.occurs)) {
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

  config.fleet.bagCapacity = 2;
  config.fleet.startNode = "n8";
  config.simulation.timeScale = 120.0;

  for (Restaurant &restaurant : config.restaurants) {

    if (restaurant.id == "r3") {
      restaurant.nodeId = "n8";
      restaurant.prepTimeMinMs = 0;
      restaurant.prepTimeMaxMs = 0;
    }

    if (restaurant.id == "r1") {
      restaurant.nodeId = "n8";
      restaurant.prepTimeMinMs = 3600000;
      restaurant.prepTimeMaxMs = 3600000;
    }
  }

  RoadNetwork network(config);

  bool rejectedPartialOk = false;
  bool preservedOk = false;
  bool fullTransferOk = false;
  bool deliveredOk = false;
  bool accountingOk = false;


  {
    config.fleet.couriers = 3;
    config.incidents.breakdownProbability = 1.0;

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);
    fleet.start();

    const bool busyB = fleet.at(1).tryAssignOrder(
        Order("busy_b", "r1", "n23", clock.getSimulatedTimeMs()));

    const bool busyC = fleet.at(2).tryAssignOrder(
        Order("busy_c", "r1", "n23", clock.getSimulatedTimeMs()));

    const bool first = fleet.at(0).tryAssignOrder(
        Order("bag_first", "r3", "n23", clock.getSimulatedTimeMs()));

    const bool second = fleet.at(0).tryAssignOrder(
        Order("bag_second", "r3", "n23", clock.getSimulatedTimeMs()));

    const bool broken = waitFor(fleet.getCapacitySignal(), [&]() {
      return fleet.at(0).getState() == CourierState::Broken;
    });

    fleet.startRescue();

    const bool noPartial =
        remainsUntransferred(fleet, fleet.getCapacitySignal());

    rejectedPartialOk =
        busyB && busyC && first && second && broken && noPartial;

    const auto firstState = fleet.at(0).getAssignedOrderState("bag_first");

    const auto secondState = fleet.at(0).getAssignedOrderState("bag_second");

    preservedOk = firstState.has_value() && secondState.has_value() &&
                  *firstState == OrderState::PickedUp &&
                  *secondState == OrderState::Assigned &&
                  fleet.at(0).getReservedCount() == 2;

    fleet.requestStop();
    fleet.join();

    const auto results = fleet.takeFinishedOrders();

    std::set<std::string> ids;
    std::size_t pending = 0;

    for (const auto &result : results) {
      ids.insert(result.order.orderId);

      if (result.order.getState() == OrderState::Pending) {
        ++pending;
      }
    }

    accountingOk = results.size() == 4 && ids.size() == 4 && pending == 4 &&
                   fleet.getRescueCount() == 0 &&
                   fleet.getRescuedOrderCount() == 0;
  }


  {
    config.fleet.couriers = 2;
    config.incidents.breakdownProbability = 0.5;

    const auto firstId = findSafeOrder(
        config.orders.seed, config.incidents.breakdownProbability, true, "");

    const auto secondId =
        findSafeOrder(config.orders.seed, config.incidents.breakdownProbability,
                      false, firstId);

    if (firstId.empty() || secondId.empty()) {
      std::cerr << "No se encontraron pedidos "
                << "de prueba apropiados\n";
      return 1;
    }

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);
    fleet.start();

    const bool first = fleet.at(0).tryAssignOrder(
        Order(firstId, "r3", "n23", clock.getSimulatedTimeMs()));

    const bool second = fleet.at(0).tryAssignOrder(
        Order(secondId, "r3", "n23", clock.getSimulatedTimeMs()));

    const bool broken = waitFor(fleet.getCapacitySignal(), [&]() {
      return fleet.at(0).getState() == CourierState::Broken;
    });

    fleet.startRescue();

    const bool rescued = waitFor(fleet.getCapacitySignal(), [&]() {
      return fleet.getRescueCount() == 1 && fleet.getRescuedOrderCount() == 2 &&
             fleet.at(0).getReservedCount() == 0;
    });

    fullTransferOk =
        first && second && broken && rescued && fleet.at(0).hasBrokenDown();

    const bool bothDelivered = waitFor(fleet.getCapacitySignal(), [&]() {
      const auto a = fleet.at(1).getAssignedOrderState(firstId);

      const auto b = fleet.at(1).getAssignedOrderState(secondId);

      return a && b && *a == OrderState::Delivered &&
             *b == OrderState::Delivered;
    });

    fleet.requestStop();
    fleet.join();

    const auto results = fleet.takeFinishedOrders();

    std::set<std::string> ids;
    std::size_t delivered = 0;

    for (const auto &result : results) {
      ids.insert(result.order.orderId);

      if (result.order.getState() == OrderState::Delivered) {
        ++delivered;
      }
    }

    deliveredOk = bothDelivered && results.size() == 2 && ids.size() == 2 &&
                  delivered == 2 && fleet.getRescueCount() == 1 &&
                  fleet.getRescuedOrderCount() == 2;
  }


  std::cout << "=== TEST DP6 WHOLE BAG RESCUE ===\n";

  std::cout << "Sin transferencia parcial: "
            << (rejectedPartialOk ? "OK" : "ERROR") << '\n';

  std::cout << "Bolsa intacta al rechazar: " << (preservedOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Rescate de bolsa completa: "
            << (fullTransferOk ? "OK" : "ERROR") << '\n';

  std::cout << "Ambos pedidos entregados: " << (deliveredOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Contabilidad sin duplicados: "
            << (accountingOk ? "OK" : "ERROR") << '\n';

  const bool passed = rejectedPartialOk && preservedOk && fullTransferOk &&
                      deliveredOk && accountingOk;

  std::cout << "DP6 WHOLE BAG RESCUE TEST: " << (passed ? "OK" : "ERROR")
            << '\n';

  return passed ? 0 : 1;
}
