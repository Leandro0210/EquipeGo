
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

int main(int argc, char *argv[]) {

  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  config.fleet.couriers = 1;
  config.fleet.bagCapacity = 1;

  SimulationClock clock;
  clock.start(config.simulation.timeScale);

  RoadNetwork network(config);
  Fleet fleet(config, network, clock);

  auto signal = fleet.getCapacitySignal();

  if (!signal) {
    std::cerr << "ERROR: señal inexistente\n";
    return 1;
  }

  std::atomic<bool> stopRequested{false};

  fleet.start();

  // ---------------------------------------------
  // COMPROBAR QUE NO HAYA NOTIFICACION ESPURIA
  // ---------------------------------------------

  const auto initialVersion = signal->version();

  const bool unexpectedChange = signal->waitUntilChange(
      initialVersion,
      std::chrono::steady_clock::now() + std::chrono::milliseconds(100),
      stopRequested);

  if (unexpectedChange) {
    fleet.requestStop();
    fleet.join();

    std::cerr << "ERROR: notificacion inesperada\n";
    return 1;
  }

  // ---------------------------------------------
  // ASIGNAR UN PEDIDO REAL
  // ---------------------------------------------

  Order order("dp4_signal_1", "r3", "n23", clock.getSimulatedTimeMs());

  const auto beforeDelivery = signal->version();

  if (!fleet.at(0).tryAssignOrder(std::move(order))) {

    fleet.requestStop();
    fleet.join();

    std::cerr << "ERROR: pedido rechazado\n";
    return 1;
  }

  // ---------------------------------------------
  // ESPERAR NOTIFICACION DE ENTREGA
  // ---------------------------------------------

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);

  const bool notified =
      signal->waitUntilChange(beforeDelivery, deadline, stopRequested);

  const auto state = fleet.at(0).getAssignedOrderState("dp4_signal_1");

  const bool delivered = state && *state == OrderState::Delivered;

  const bool capacityReleased = fleet.at(0).getReservedCount() == 0;

  fleet.requestStop();
  fleet.join();

  std::cout << "=== TEST NOTIFICACION DE CAPACIDAD ===\n";

  std::cout << "Notificacion recibida: " << (notified ? "SI" : "NO") << '\n';

  std::cout << "Pedido entregado: " << (delivered ? "SI" : "NO") << '\n';

  std::cout << "Capacidad liberada: " << (capacityReleased ? "SI" : "NO")
            << '\n';

  if (!notified || !delivered || !capacityReleased) {

    std::cerr << "ERROR: prueba de capacidad fallida\n";
    return 1;
  }

  std::cout << "CAPACITY SIGNAL TEST: OK\n";

  return 0;
}
