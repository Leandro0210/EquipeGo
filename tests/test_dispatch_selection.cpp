
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "dispatch/Dispatcher.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char *argv[]) {
  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  config.fleet.couriers = 8;
  config.fleet.bagCapacity = 1;

  SimulationClock clock;
  clock.start(config.simulation.timeScale);

  RoadNetwork network(config);
  Fleet fleet(config, network, clock);

  fleet.start();

  Dispatcher dispatcher(config, network, fleet);

  // Mover solamente c0 a otra zona.
  if (!fleet.at(0).assignDestination("n23")) {
    std::cerr << "ERROR: no se pudo mover c0\n";
    return 1;
  }

  // Esperar a que c0 complete el desplazamiento.
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);

  bool arrived = false;

  while (std::chrono::steady_clock::now() < deadline) {
    if (fleet.at(0).getCurrentNode() == "n23" &&
        fleet.at(0).getState() == CourierState::Idle) {
      arrived = true;
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (!arrived) {
    std::cerr << "ERROR: c0 no llego a n23\n";
    return 1;
  }

  // r3 esta en n8, el nodo inicial de c1 a c7.
  Order firstOrder("dp4_best_1", "r3", "n23", clock.getSimulatedTimeMs());

  const auto first = dispatcher.assignBestCourier(firstOrder);

  std::cout << "=== TEST SELECCION DISPATCHER ===\n";
  std::cout << "Primera moto elegida: " << first.value_or("ninguna") << '\n';

  // Con c1 ocupada y capacidad 1,
  // la siguiente moto esperada es c2.
  Order secondOrder("dp4_best_2", "r3", "n23", clock.getSimulatedTimeMs());

  const auto second = dispatcher.assignBestCourier(secondOrder);

  std::cout << "Segunda moto elegida: " << second.value_or("ninguna") << '\n';

  bool correct = first && *first == "c1" && second && *second == "c2";

  // Esperar a que ambas entreguen sus pedidos.
  const auto deliveryDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);

  bool delivered = false;

  while (std::chrono::steady_clock::now() < deliveryDeadline) {

    const auto state1 = fleet.at(1).getAssignedOrderState("dp4_best_1");

    const auto state2 = fleet.at(2).getAssignedOrderState("dp4_best_2");

    if (state1 && state2 && *state1 == OrderState::Delivered &&
        *state2 == OrderState::Delivered) {
      delivered = true;
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  fleet.requestStop();
  fleet.join();

  correct = correct && delivered && fleet.at(1).getReservedCount() == 0 &&
            fleet.at(2).getReservedCount() == 0;

  if (!correct) {
    std::cerr << "ERROR: seleccion o entrega incorrecta\n";
    return 1;
  }

  std::cout << "Seleccion por menor ETA: OK\n";
  std::cout << "Respeto de capacidad: OK\n";
  std::cout << "Ambos pedidos entregados: OK\n";
  std::cout << "DP4 SELECTION TEST: OK\n";

  return 0;
}
