
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"
#include "restaurants/PreparationTime.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

using RealClock = std::chrono::steady_clock;

bool waitForDelivery(Fleet &fleet, const std::string &orderId) {
  auto signal = fleet.getCapacitySignal();
  std::atomic<bool> cancelled{false};

  const auto deadline = RealClock::now() + std::chrono::seconds(5);

  while (RealClock::now() < deadline) {
    const auto version = signal->version();

    const auto state = fleet.at(0).getAssignedOrderState(orderId);

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

  config.fleet.couriers = 1;
  config.fleet.bagCapacity = 1;
  config.fleet.startNode = "n8";
  config.simulation.timeScale = 20.0;

  Restaurant *restaurant = nullptr;

  for (Restaurant &r : config.restaurants) {
    if (r.id == "r3") {
      restaurant = &r;
      break;
    }
  }

  if (!restaurant) {
    std::cerr << "ERROR: falta r3\n";
    return 1;
  }

  // =============================================
  // TEST 1: DURACION REPRODUCIBLE
  // =============================================

  const auto durationA =
      preparationDurationMs(*restaurant, "o17", config.orders.seed);

  const auto durationB =
      preparationDurationMs(*restaurant, "o17", config.orders.seed);

  const bool deterministic = durationA == durationB &&
                             durationA >= restaurant->prepTimeMinMs &&
                             durationA <= restaurant->prepTimeMaxMs;

  RoadNetwork network(config);

  bool preparationOk = false;
  bool fromCreationOk = false;
  bool shutdownOk = false;

  // =============================================
  // TEST 2: NO RECOGER ANTES DE TIEMPO
  // =============================================

  {
    restaurant->prepTimeMinMs = 12000;
    restaurant->prepTimeMaxMs = 12000;

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);
    fleet.start();

    const long long createdAt = clock.getSimulatedTimeMs();

    const bool accepted = fleet.at(0).tryAssignOrder(
        Order("dp5_prep_wait", "r3", "n8", createdAt));

    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const auto earlyState = fleet.at(0).getAssignedOrderState("dp5_prep_wait");

    const bool notPickedUpEarly =
        earlyState && *earlyState == OrderState::Assigned;

    const bool delivered = waitForDelivery(fleet, "dp5_prep_wait");

    const long long elapsedSimMs = clock.getSimulatedTimeMs() - createdAt;

    fleet.requestStop();
    fleet.join();

    auto results = fleet.takeFinishedOrders();

    preparationOk = accepted && notPickedUpEarly && delivered &&
                    elapsedSimMs >= 12000 && results.size() == 1 &&
                    results[0].order.getState() == OrderState::Delivered;
  }

  // =============================================
  // TEST 3: COCINA DESDE LA CREACION
  // =============================================

  {
    restaurant->prepTimeMinMs = 16000;
    restaurant->prepTimeMaxMs = 16000;

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);
    fleet.start();

    // Crear pedido antes de asignarlo.
    const long long createdAt = clock.getSimulatedTimeMs();

    // 900 ms reales = 18000 ms simulados.
    // La comida ya debe estar preparada.
    std::this_thread::sleep_for(std::chrono::milliseconds(900));

    const auto assignmentStart = RealClock::now();

    const bool accepted = fleet.at(0).tryAssignOrder(
        Order("dp5_prep_early", "r3", "n8", createdAt));

    const bool delivered = waitForDelivery(fleet, "dp5_prep_early");

    const auto elapsedAfterAssignment =
        std::chrono::duration_cast<std::chrono::milliseconds>(RealClock::now() -
                                                              assignmentStart)
            .count();

    fleet.requestStop();
    fleet.join();

    auto results = fleet.takeFinishedOrders();

    fromCreationOk = accepted && delivered && elapsedAfterAssignment < 450 &&
                     results.size() == 1 &&
                     results[0].order.getState() == OrderState::Delivered;
  }

  // =============================================
  // TEST 4: APAGADO DURANTE COCINA
  // =============================================

  {
    // Preparacion de 20 segundos reales.
    // No debemos esperar todo ese tiempo
    // para poder apagar la flota.
    restaurant->prepTimeMinMs = 400000;
    restaurant->prepTimeMaxMs = 400000;

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);
    fleet.start();

    const bool accepted = fleet.at(0).tryAssignOrder(
        Order("dp5_prep_stop", "r3", "n8", clock.getSimulatedTimeMs()));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto state = fleet.at(0).getAssignedOrderState("dp5_prep_stop");

    const bool waiting = state && *state == OrderState::Assigned;

    const auto stopStart = RealClock::now();

    fleet.requestStop();
    fleet.join();

    const auto stopElapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(RealClock::now() -
                                                              stopStart)
            .count();

    auto results = fleet.takeFinishedOrders();

    shutdownOk = accepted && waiting && stopElapsedMs < 1000 &&
                 results.size() == 1 &&
                 results[0].order.getState() == OrderState::Pending;
  }

  // =============================================
  // RESULTADOS
  // =============================================

  std::cout << "=== TEST DP5 PREPARATION TIME ===\n";

  std::cout << "Duracion reproducible: " << (deterministic ? "OK" : "ERROR")
            << '\n';

  std::cout << "Espera antes de recoger: " << (preparationOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Preparacion desde creacion: "
            << (fromCreationOk ? "OK" : "ERROR") << '\n';

  std::cout << "Apagado durante cocina: " << (shutdownOk ? "OK" : "ERROR")
            << '\n';

  const bool passed =
      deterministic && preparationOk && fromCreationOk && shutdownOk;

  std::cout << "DP5 PREPARATION TEST: " << (passed ? "OK" : "ERROR") << '\n';

  return passed ? 0 : 1;
}
