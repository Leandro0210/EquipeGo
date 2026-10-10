
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
#include <vector>

int main(int argc, char *argv[]) {

  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  config.fleet.couriers = 2;
  config.fleet.bagCapacity = 1;
  config.fleet.speedKmh = 1.0;
  config.simulation.timeScale = 1.0;

  SimulationClock clock;
  clock.start(config.simulation.timeScale);

  RoadNetwork network(config);
  Fleet fleet(config, network, clock);

  fleet.start();

  auto signal = fleet.getCapacitySignal();

  const auto beforeDelivery = signal->version();

  // Pedido rapido: restaurante y entrega
  // estan en el mismo nodo n8.
  Order fastOrder("dp4_delivered", "r3", "n8", clock.getSimulatedTimeMs());

  // Pedido lento: el repartidor debe
  // recorrer una distancia considerable.
  Order slowOrder("dp4_pending", "r3", "n23", clock.getSimulatedTimeMs());

  const bool firstAccepted = fleet.at(0).tryAssignOrder(std::move(fastOrder));

  const bool secondAccepted = fleet.at(1).tryAssignOrder(std::move(slowOrder));

  if (!firstAccepted || !secondAccepted) {

    fleet.requestStop();
    fleet.join();

    std::cerr << "ERROR: no se pudieron asignar pedidos\n";

    return 1;
  }

  std::atomic<bool> stopRequested{false};

  // Esperar a que el primer repartidor
  // libere capacidad tras la entrega.
  const bool notified = signal->waitUntilChange(
      beforeDelivery,
      std::chrono::steady_clock::now() + std::chrono::seconds(5),
      stopRequested);

  const auto firstState = fleet.at(0).getAssignedOrderState("dp4_delivered");

  const bool deliveredBeforeStop =
      firstState && *firstState == OrderState::Delivered;

  // Interrumpir la entrega lenta.
  fleet.requestStop();
  fleet.join();

  // Recuperar los pedidos ya finalizados.
  std::vector<CourierOrderResult> results = fleet.takeFinishedOrders();

  std::size_t delivered = 0;
  std::size_t pending = 0;

  std::set<std::string> ids;

  for (const auto &result : results) {

    ids.insert(result.order.orderId);

    if (result.order.getState() == OrderState::Delivered) {
      ++delivered;
    }

    if (result.order.getState() == OrderState::Pending) {
      ++pending;
    }
  }

  // Una segunda lectura no debe volver
  // a devolver los mismos pedidos.
  const auto secondCollection = fleet.takeFinishedOrders();

  const bool correct = notified && deliveredBeforeStop && results.size() == 2 &&
                       delivered == 1 && pending == 1 && ids.size() == 2 &&
                       secondCollection.empty();

  std::cout << "=== TEST CONTABILIDAD FINAL FLEET ===\n";

  std::cout << "Pedidos recuperados: " << results.size() << '\n';

  std::cout << "Entregados: " << delivered << '\n';

  std::cout << "Pendientes: " << pending << '\n';

  std::cout << "Sin duplicados: " << (ids.size() == 2 ? "SI" : "NO") << '\n';

  std::cout << "Segunda recoleccion vacia: "
            << (secondCollection.empty() ? "SI" : "NO") << '\n';

  if (!correct) {

    std::cerr << "FLEET FINAL ACCOUNTING TEST: ERROR\n";

    return 1;
  }

  std::cout << "FLEET FINAL ACCOUNTING TEST: OK\n";

  return 0;
}
