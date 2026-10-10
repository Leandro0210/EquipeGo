
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "dispatch/Dispatcher.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <cmath>
#include <iostream>
#include <set>
#include <string>

int main(int argc, char *argv[]) {
  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  config.fleet.couriers = 8;

  SimulationClock clock;
  clock.start(config.simulation.timeScale);

  RoadNetwork network(config);
  Fleet fleet(config, network, clock);

  fleet.start();

  Dispatcher dispatcher(config, network, fleet);

  Order order("dp4_o1", config.restaurants.at(0).id, "n23",
              clock.getSimulatedTimeMs());

  std::cout << "=== TEST DP4 - ETA PARALELAS ===\n";

  const QuoteBatch result = dispatcher.quoteCandidates(order);

  std::cout << "Cotizaciones: " << result.quotes.size() << '\n';

  std::cout << "Errores: " << result.failures.size() << '\n';

  bool valid = result.quotes.size() == 8 && result.failures.empty();

  std::set<std::string> ids;

  const auto toRestaurant = network.shortestRoute(
      config.fleet.startNode, config.restaurants.at(0).nodeId);

  const auto toCustomer =
      network.shortestRoute(config.restaurants.at(0).nodeId, "n23");

  if (!toRestaurant || !toCustomer) {
    valid = false;
  }

  if (toRestaurant && toCustomer) {
    const double expected =
        (toRestaurant->distanceMeters + toCustomer->distanceMeters) /
        (config.fleet.speedKmh / 3.6);

    for (const EtaQuote &quote : result.quotes) {
      std::cout << quote.courierId << " -> ETA " << quote.etaSimulatedSeconds
                << " segundos simulados\n";

      ids.insert(quote.courierId);

      if (!std::isfinite(quote.etaSimulatedSeconds) ||
          std::fabs(quote.etaSimulatedSeconds - expected) > 0.001) {
        valid = false;
      }
    }
  }

  if (ids.size() != 8) {
    valid = false;
  }

  // Probar errores independientes en las tareas.
  Order invalid("dp4_invalid", config.restaurants.at(0).id, "nodo_inexistente",
                0);

  const QuoteBatch failed = dispatcher.quoteCandidates(invalid);

  if (!failed.quotes.empty() || failed.failures.size() != 8) {
    valid = false;
  }

  fleet.requestStop();
  fleet.join();

  if (!valid) {
    std::cerr << "ERROR: cotizaciones incorrectas\n";
    return 1;
  }

  std::cout << "Ocho ETA calculadas: OK\n";
  std::cout << "Errores individuales capturados: OK\n";
  std::cout << "DP4 QUOTES TEST: OK\n";

  return 0;
}
