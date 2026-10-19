
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

using RealClock = std::chrono::steady_clock;

bool waitForCourierState(Courier &courier, CourierState expected,
                         const std::shared_ptr<CapacitySignal> &signal) {
  std::atomic<bool> cancelled{false};

  const auto deadline = RealClock::now() + std::chrono::seconds(5);

  while (RealClock::now() < deadline) {
    const auto version = signal->version();

    if (courier.getState() == expected) {
      return true;
    }

    signal->waitUntilChange(version, deadline, cancelled);
  }

  return courier.getState() == expected;
}

bool waitForDelivery(Courier &courier, const std::string &orderId,
                     const std::shared_ptr<CapacitySignal> &signal) {
  std::atomic<bool> cancelled{false};

  const auto deadline = RealClock::now() + std::chrono::seconds(5);

  while (RealClock::now() < deadline) {
    const auto version = signal->version();

    const auto state = courier.getAssignedOrderState(orderId);

    if (state && *state == OrderState::Delivered) {
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
  config.fleet.bagCapacity = 2;
  config.fleet.startNode = "n8";
  config.simulation.timeScale = 120.0;

  for (Restaurant &restaurant : config.restaurants) {
    if (restaurant.id == "r3") {
      restaurant.prepTimeMinMs = 0;
      restaurant.prepTimeMaxMs = 0;
    }
  }

  RoadNetwork network(config);

  bool breakdownOk = false;
  bool cargoPreservedOk = false;
  bool shutdownOk = false;
  bool normalTravelOk = false;


  {
    config.incidents.breakdownProbability = 1.0;

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);
    fleet.start();

    auto signal = fleet.getCapacitySignal();

    const bool firstAccepted = fleet.at(0).tryAssignOrder(
        Order("broken_active", "r3", "n23", clock.getSimulatedTimeMs()));

    const bool secondAccepted = fleet.at(0).tryAssignOrder(
        Order("broken_queued", "r3", "n23", clock.getSimulatedTimeMs()));

    const bool broken =
        waitForCourierState(fleet.at(0), CourierState::Broken, signal);

    const auto activeState = fleet.at(0).getAssignedOrderState("broken_active");

    const auto queuedState = fleet.at(0).getAssignedOrderState("broken_queued");

    breakdownOk = firstAccepted && secondAccepted && broken;

    cargoPreservedOk = fleet.at(0).getReservedCount() == 2 && activeState &&
                       *activeState == OrderState::PickedUp && queuedState &&
                       *queuedState == OrderState::Assigned;

    const bool rejectedNewOrder = !fleet.at(0).tryAssignOrder(
        Order("broken_extra", "r3", "n23", clock.getSimulatedTimeMs()));

    cargoPreservedOk = cargoPreservedOk && rejectedNewOrder;

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

    shutdownOk = results.size() == 2 && ids.size() == 2 && pending == 2 &&
                 fleet.at(0).getReservedCount() == 0;
  }


  {
    config.incidents.breakdownProbability = 0.0;

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);
    fleet.start();

    const bool accepted = fleet.at(0).tryAssignOrder(
        Order("safe_delivery", "r3", "n23", clock.getSimulatedTimeMs()));

    const bool delivered = waitForDelivery(fleet.at(0), "safe_delivery",
                                           fleet.getCapacitySignal());

    const bool neverBroken = fleet.at(0).getState() != CourierState::Broken;

    fleet.requestStop();
    fleet.join();

    const auto results = fleet.takeFinishedOrders();

    normalTravelOk = accepted && delivered && neverBroken &&
                     results.size() == 1 &&
                     results[0].order.getState() == OrderState::Delivered;
  }


  std::cout << "=== TEST DP6 BREAKDOWN COURIER ===\n";

  std::cout << "Averia durante un recorrido: " << (breakdownOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Carga conservada al averiarse: "
            << (cargoPreservedOk ? "OK" : "ERROR") << '\n';

  std::cout << "Apagado sin pedidos perdidos: " << (shutdownOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Viaje normal sin averias: " << (normalTravelOk ? "OK" : "ERROR")
            << '\n';

  const bool passed =
      breakdownOk && cargoPreservedOk && shutdownOk && normalTravelOk;

  std::cout << "DP6 BREAKDOWN COURIER TEST: " << (passed ? "OK" : "ERROR")
            << '\n';

  return passed ? 0 : 1;
}
