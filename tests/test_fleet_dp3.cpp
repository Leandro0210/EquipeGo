
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

int main(int argc, char *argv[]) {
  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  // Prueba con 8 actores independientes.
  config.fleet.couriers = 8;
  config.fleet.bagCapacity = 1;

  SimulationClock clock;
  clock.start(config.simulation.timeScale);

  RoadNetwork network(config);

  // Usamos el restaurante ubicado en el nodo
  // inicial de las motos (r3 en nuestro JSON).
  const auto restaurant =
      std::find_if(config.restaurants.begin(), config.restaurants.end(),
                   [&config](const Restaurant &r) {
                     return r.nodeId == config.fleet.startNode;
                   });

  if (restaurant == config.restaurants.end()) {
    std::cerr << "ERROR: restaurante inicial no encontrado\n";
    return 1;
  }

  const std::string target = "n23";

  const auto route = network.shortestRoute(restaurant->nodeId, target);

  if (!route) {
    std::cerr << "ERROR: no existe ruta de entrega\n";
    return 1;
  }

  const double expectedSeconds = route->distanceMeters /
                                 (config.fleet.speedKmh / 3.6) /
                                 config.simulation.timeScale;

  Fleet fleet(config, network, clock);

  // ===========================================
  // INICIAR OCHO REPARTIDORES
  // ===========================================

  fleet.start();

  if (fleet.size() != 8) {
    std::cerr << "ERROR: cantidad de motos\n";
    return 1;
  }

  const auto start = std::chrono::steady_clock::now();

  // Asignacion manual solo para probar DP.3.
  // La seleccion automatica sera trabajo de DP.4.
  for (std::size_t i = 0; i < fleet.size(); ++i) {
    const std::string orderId = "dp3_" + std::to_string(i);

    Order order(orderId, restaurant->id, target, clock.getSimulatedTimeMs());

    if (!fleet.at(i).tryAssignOrder(std::move(order))) {
      std::cerr << "ERROR: asignacion " << orderId << '\n';
      return 1;
    }
  }

  // ===========================================
  // ESPERAR ENTREGAS
  // ===========================================

  const auto deadline =
      start + std::chrono::duration<double>(expectedSeconds + 5.0);

  bool allDelivered = false;

  while (std::chrono::steady_clock::now() < deadline) {
    allDelivered = true;

    for (std::size_t i = 0; i < fleet.size(); ++i) {
      const std::string orderId = "dp3_" + std::to_string(i);

      const auto state = fleet.at(i).getAssignedOrderState(orderId);

      if (!state || *state != OrderState::Delivered) {
        allDelivered = false;
        break;
      }
    }

    if (allDelivered) {
      break;
    }

    // Espera solo del codigo de prueba.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();

  // ===========================================
  // APAGADO DE TODA LA FLOTA
  // ===========================================

  fleet.requestStop();
  fleet.join();

  std::size_t deliveredCount = 0;
  bool positionsValid = true;
  bool capacityReleased = true;
  bool idleAfterStop = true;

  for (std::size_t i = 0; i < fleet.size(); ++i) {
    const Courier &courier = fleet.at(i);

    if (courier.getFinishedCount() == 1 &&
        courier.getAssignedOrderState("dp3_" + std::to_string(i)) ==
            OrderState::Delivered) {
      ++deliveredCount;
    }

    if (courier.getCurrentNode() != target) {
      positionsValid = false;
    }

    if (courier.getReservedCount() != 0) {
      capacityReleased = false;
    }

    if (courier.getState() != CourierState::Idle) {
      idleAfterStop = false;
    }
  }

  // ===========================================
  // RESULTADOS
  // ===========================================

  std::cout << "\n=== PRUEBA FINAL DP.3 ===\n";
  std::cout << "Repartidores: " << fleet.size() << '\n';
  std::cout << "Pedidos entregados: " << deliveredCount << '\n';
  std::cout << "Tiempo estimado por moto: " << expectedSeconds << " s\n";
  std::cout << "Tiempo medido: " << elapsed << " s\n";
  std::cout << "Tiempo secuencial estimado: " << expectedSeconds * fleet.size()
            << " s\n";

  const bool concurrent = elapsed < expectedSeconds * 4.0;

  if (!allDelivered || deliveredCount != fleet.size() || !positionsValid ||
      !capacityReleased || !idleAfterStop || !concurrent) {
    std::cerr << "ERROR: DP.3 no supero la prueba.\n";
    return 1;
  }

  std::cout << "Movimiento simultaneo: OK\n";
  std::cout << "Entregas completas: OK\n";
  std::cout << "Capacidad liberada: OK\n";
  std::cout << "Apagado de hilos: OK\n";
  std::cout << "DP3 FLEET TEST: OK\n";

  return 0;
}
