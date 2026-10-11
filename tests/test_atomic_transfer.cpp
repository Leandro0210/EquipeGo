
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

using RealClock = std::chrono::steady_clock;

bool waitForBroken(Courier &courier,
                   const std::shared_ptr<CapacitySignal> &signal) {
  std::atomic<bool> cancelled{false};

  const auto deadline = RealClock::now() + std::chrono::seconds(8);

  while (RealClock::now() < deadline) {
    const auto version = signal->version();

    if (courier.getState() == CourierState::Broken) {
      return true;
    }

    signal->waitUntilChange(version, deadline, cancelled);
  }

  return courier.getState() == CourierState::Broken;
}

bool waitForTwoDeliveries(Courier &courier, const std::string &firstId,
                          const std::string &secondId,
                          const std::shared_ptr<CapacitySignal> &signal) {
  std::atomic<bool> cancelled{false};

  const auto deadline = RealClock::now() + std::chrono::seconds(12);

  while (RealClock::now() < deadline) {
    const auto version = signal->version();

    const auto first = courier.getAssignedOrderState(firstId);

    const auto second = courier.getAssignedOrderState(secondId);

    if (first && second && *first == OrderState::Delivered &&
        *second == OrderState::Delivered) {
      return true;
    }

    signal->waitUntilChange(version, deadline, cancelled);
  }

  return false;
}

std::string findSafeTransferOrder(unsigned int seed, double probability,
                                  bool requireSourceBreak,
                                  const std::string &excluded) {
  for (int i = 0; i < 10000; ++i) {
    const std::string id = "transfer_" + std::to_string(i);

    if (id == excluded) {
      continue;
    }

    const auto source =
        incidents::makeBreakdownPlan(seed, "c0", id, "toCustomer", probability);

    const auto recipient =
        incidents::makeBreakdownPlan(seed, "c1", id, "toCustomer", probability);

    if (!recipient.occurs && (!requireSourceBreak || source.occurs)) {
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
  config.fleet.bagCapacity = 2;
  config.fleet.startNode = "n8";
  config.simulation.timeScale = 120.0;

  // Restaurante inmediato para pedidos del test.
  // Otro restaurante cocina durante mucho tiempo
  // para conservar ocupada la moto receptora
  // en la prueba de capacidad insuficiente.
  for (Restaurant &restaurant : config.restaurants) {
    if (restaurant.id == "r3") {
      restaurant.nodeId = "n8";
      restaurant.prepTimeMinMs = 0;
      restaurant.prepTimeMaxMs = 0;
    }

    if (restaurant.id == "r1") {
      restaurant.nodeId = "n8";
      restaurant.prepTimeMinMs = 600000;
      restaurant.prepTimeMaxMs = 600000;
    }
  }

  RoadNetwork network(config);

  bool capacityOk = false;
  bool sourcePreservedOk = false;
  bool transferOk = false;
  bool deliveredOk = false;

  // ============================================
  // TEST 1: CAPACIDAD INSUFICIENTE
  // ============================================

  {
    config.incidents.breakdownProbability = 1.0;

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);
    fleet.start();

    const bool a = fleet.at(0).tryAssignOrder(
        Order("failed_active", "r3", "n23", clock.getSimulatedTimeMs()));

    const bool b = fleet.at(0).tryAssignOrder(
        Order("failed_queued", "r3", "n23", clock.getSimulatedTimeMs()));

    const bool c = fleet.at(1).tryAssignOrder(
        Order("recipient_busy", "r1", "n23", clock.getSimulatedTimeMs()));

    const bool broken = waitForBroken(fleet.at(0), fleet.getCapacitySignal());

    // Origen tiene 2 pedidos; destino tiene 1.
    // bagCapacity = 2. No caben los tres.
    const bool rejected = !fleet.at(0).transferAllTo(fleet.at(1));

    const auto first = fleet.at(0).getAssignedOrderState("failed_active");

    const auto second = fleet.at(0).getAssignedOrderState("failed_queued");

    capacityOk = a && b && c && broken && rejected &&
                 fleet.at(0).getReservedCount() == 2 &&
                 fleet.at(1).getReservedCount() == 1;

    sourcePreservedOk = first && second && *first == OrderState::PickedUp &&
                        *second == OrderState::Assigned;

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

    sourcePreservedOk = sourcePreservedOk && results.size() == 3 &&
                        ids.size() == 3 && pending == 3;
  }

  // ============================================
  // TEST 2: TRANSFERENCIA Y ENTREGA EXITOSA
  // ============================================

  {
    config.incidents.breakdownProbability = 0.5;

    const std::string firstId = findSafeTransferOrder(
        config.orders.seed, config.incidents.breakdownProbability, true, "");

    const std::string secondId = findSafeTransferOrder(
        config.orders.seed, config.incidents.breakdownProbability, false,
        firstId);

    if (firstId.empty() || secondId.empty()) {
      std::cerr << "ERROR: no se encontraron pedidos "
                << "apropiados para la prueba\n";
      return 1;
    }

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);
    fleet.start();

    const bool a = fleet.at(0).tryAssignOrder(
        Order(firstId, "r3", "n23", clock.getSimulatedTimeMs()));

    const bool b = fleet.at(0).tryAssignOrder(
        Order(secondId, "r3", "n23", clock.getSimulatedTimeMs()));

    const bool broken = waitForBroken(fleet.at(0), fleet.getCapacitySignal());

    const auto firstBefore = fleet.at(0).getAssignedOrderState(firstId);

    const auto secondBefore = fleet.at(0).getAssignedOrderState(secondId);

    const bool correctInitialStates = firstBefore && secondBefore &&
                                      *firstBefore == OrderState::PickedUp &&
                                      *secondBefore == OrderState::Assigned;

    const bool transferred = fleet.at(0).transferAllTo(fleet.at(1));

    const bool sourceEmpty = fleet.at(0).getReservedCount() == 0;

    const bool sourceStillBroken =
        fleet.at(0).getState() == CourierState::Broken;

    transferOk = a && b && broken && correctInitialStates && transferred &&
                 sourceEmpty && sourceStillBroken;

    const bool delivered = waitForTwoDeliveries(fleet.at(1), firstId, secondId,
                                                fleet.getCapacitySignal());

    fleet.requestStop();
    fleet.join();

    const auto results = fleet.takeFinishedOrders();

    std::set<std::string> ids;
    std::size_t deliveredCount = 0;

    for (const auto &result : results) {
      ids.insert(result.order.orderId);

      if (result.order.getState() == OrderState::Delivered) {
        ++deliveredCount;
      }
    }

    deliveredOk = transferred && delivered && results.size() == 2 &&
                  ids.size() == 2 && deliveredCount == 2;
  }

  // ============================================
  // RESULTADOS
  // ============================================

  std::cout << "=== TEST DP6 ATOMIC TRANSFER ===\n";

  std::cout << "Rechazo por capacidad: " << (capacityOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Origen intacto tras rechazo: "
            << (sourcePreservedOk ? "OK" : "ERROR") << '\n';

  std::cout << "Transferencia completa: " << (transferOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Pedidos transferidos entregados: "
            << (deliveredOk ? "OK" : "ERROR") << '\n';

  const bool passed =
      capacityOk && sourcePreservedOk && transferOk && deliveredOk;

  std::cout << "DP6 ATOMIC TRANSFER TEST: " << (passed ? "OK" : "ERROR")
            << '\n';

  return passed ? 0 : 1;
}
