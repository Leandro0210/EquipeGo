
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <string>

using RealClock = std::chrono::steady_clock;

// =====================================================
// ESPERAR CAMBIO SIN BUSY WAITING
// =====================================================

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

// =====================================================
// TEST PRINCIPAL
// =====================================================

int main(int argc, char *argv[]) {

  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  // ============================================
  // CONFIGURACION
  // ============================================

  config.fleet.couriers = 3;
  config.fleet.bagCapacity = 2;
  config.fleet.startNode = "n8";

  config.simulation.timeScale = 120.0;

  // Forzar averia de c0 en el recorrido.
  config.incidents.breakdownProbability = 1.0;

  for (Restaurant &restaurant : config.restaurants) {

    // Restaurante de los pedidos a rescatar.
    if (restaurant.id == "r3") {
      restaurant.nodeId = "n8";
      restaurant.prepTimeMinMs = 0;
      restaurant.prepTimeMaxMs = 0;
    }

    // Restaurante que mantendra ocupadas
    // las otras dos motos durante el test.
    if (restaurant.id == "r1") {
      restaurant.nodeId = "n8";
      restaurant.prepTimeMinMs = 3600000;
      restaurant.prepTimeMaxMs = 3600000;
    }
  }

  RoadNetwork network(config);

  SimulationClock clock;
  clock.start(config.simulation.timeScale);

  Fleet fleet(config, network, clock);

  fleet.start();
  fleet.startRescue();

  // ============================================
  // TEST 1: OCUPAR UN CUPO DE C1 Y C2
  // ============================================

  const bool busyB = fleet.at(1).tryAssignOrder(
      Order("busy_b", "r1", "n23", clock.getSimulatedTimeMs()));

  const bool busyC = fleet.at(2).tryAssignOrder(
      Order("busy_c", "r1", "n23", clock.getSimulatedTimeMs()));

  const bool initialCapacityOk = busyB && busyC &&
                                 fleet.at(1).getReservedCount() == 1 &&
                                 fleet.at(2).getReservedCount() == 1;

  // ============================================
  // TEST 2: ASIGNAR DOS PEDIDOS A C0
  // ============================================

  const bool firstAccepted = fleet.at(0).tryAssignOrder(
      Order("rescue_first", "r3", "n23", clock.getSimulatedTimeMs()));

  const bool secondAccepted = fleet.at(0).tryAssignOrder(
      Order("rescue_second", "r3", "n23", clock.getSimulatedTimeMs()));

  // ============================================
  // TEST 3: ESPERAR RESCATE DISTRIBUIDO
  // ============================================

  const bool rescued = waitFor(fleet.getCapacitySignal(), [&] {
    return fleet.getRescuedOrderCount() == 2 && fleet.getRescueCount() == 1 &&
           fleet.at(0).getReservedCount() == 0;
  });

  const bool brokenOk = fleet.at(0).hasBrokenDown() &&
                        fleet.at(0).getState() == CourierState::Broken;

  // ============================================
  // TEST 4: VERIFICAR CAPACIDAD
  // ============================================

  const bool capacityOk = initialCapacityOk &&
                          fleet.at(1).getReservedCount() == 2 &&
                          fleet.at(2).getReservedCount() == 2;

  // ============================================
  // TEST 5: COMPROBAR DISTRIBUCION
  // ============================================

  const bool firstAtB =
      fleet.at(1).getAssignedOrderState("rescue_first").has_value();

  const bool secondAtB =
      fleet.at(1).getAssignedOrderState("rescue_second").has_value();

  const bool firstAtC =
      fleet.at(2).getAssignedOrderState("rescue_first").has_value();

  const bool secondAtC =
      fleet.at(2).getAssignedOrderState("rescue_second").has_value();

  // Un pedido en B y otro en C.
  const bool distributionOk =
      (firstAtB != secondAtB) && (firstAtC != secondAtC) &&
      (firstAtB != firstAtC) && (secondAtB != secondAtC);

  // ============================================
  // TEST 6: APAGADO Y CONTABILIDAD
  // ============================================

  fleet.requestStop();
  fleet.join();

  const auto results = fleet.takeFinishedOrders();

  std::set<std::string> ids;
  std::size_t pending = 0;

  bool firstFinal = false;
  bool secondFinal = false;

  for (const auto &result : results) {

    ids.insert(result.order.orderId);

    if (result.order.getState() == OrderState::Pending) {
      ++pending;
    }

    if (result.order.orderId == "rescue_first") {
      firstFinal = true;
    }

    if (result.order.orderId == "rescue_second") {
      secondFinal = true;
    }
  }

  const bool accountingOk = results.size() == 4 && ids.size() == 4 &&
                            pending == 4 && firstFinal && secondFinal;

  // ============================================
  // MOSTRAR RESULTADOS
  // ============================================

  std::cout << "=== TEST DP6 DISTRIBUTED RESCUE ===\n";

  std::cout << "Averia detectada: " << (brokenOk ? "OK" : "ERROR") << '\n';

  std::cout << "Rescate de dos pedidos: " << (rescued ? "OK" : "ERROR") << '\n';

  std::cout << "Capacidad respetada: " << (capacityOk ? "OK" : "ERROR") << '\n';

  std::cout << "Distribucion B y C: " << (distributionOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Contabilidad sin duplicados: "
            << (accountingOk ? "OK" : "ERROR") << '\n';

  const bool passed = firstAccepted && secondAccepted && brokenOk && rescued &&
                      capacityOk && distributionOk && accountingOk;

  std::cout << "DP6 DISTRIBUTED RESCUE TEST: " << (passed ? "OK" : "ERROR")
            << '\n';

  return passed ? 0 : 1;
}
