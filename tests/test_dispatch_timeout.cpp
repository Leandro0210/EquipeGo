
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "dispatch/Dispatcher.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <set>
#include <string>
#include <thread>

int main(int argc, char *argv[]) {
  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  config.fleet.couriers = 8;
  config.dispatch.quoteTimeoutMs = 200;

  SimulationClock clock;
  clock.start(config.simulation.timeScale);

  RoadNetwork network(config);
  Fleet fleet(config, network, clock);

  fleet.start();

  std::atomic<int> quoteNumber{0};
  bool passed = false;

  {
    // Calculador artificial solo para esta prueba.
    // Una tarea tarda 700 ms.
    // Las demas devuelven inmediatamente una ETA.
    QuoteCalculator testCalculator =
        [&quoteNumber](const std::string &, const std::string &,
                       const std::string &) -> double {
      const int number = quoteNumber.fetch_add(1);

      if (number == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
      }

      return 120.0;
    };

    Dispatcher dispatcher(config, network, fleet, testCalculator);

    Order order("dp4_timeout_1", config.restaurants.at(0).id, "n23",
                clock.getSimulatedTimeMs());

    const auto start = std::chrono::steady_clock::now();

    const QuoteBatch batch = dispatcher.quoteCandidates(order);

    const double elapsedMs = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();

    std::set<std::string> checkedIds;
    bool timeoutFound = false;
    bool correctEtas = true;

    for (const EtaQuote &quote : batch.quotes) {
      checkedIds.insert(quote.courierId);

      if (quote.etaSimulatedSeconds != 120.0) {
        correctEtas = false;
      }
    }

    for (const QuoteFailure &failure : batch.failures) {
      checkedIds.insert(failure.courierId);

      if (failure.reason == "quoteTimeout") {
        timeoutFound = true;
      }
    }

    std::cout << "=== TEST DP4 QUOTE TIMEOUT ===\n";
    std::cout << "Limite configurado: " << config.dispatch.quoteTimeoutMs
              << " ms\n";

    std::cout << "Tiempo de respuesta: " << elapsedMs << " ms\n";

    std::cout << "Cotizaciones a tiempo: " << batch.quotes.size() << '\n';

    std::cout << "Cotizaciones fallidas: " << batch.failures.size() << '\n';

    // Hay 7 respuestas rapidas y 1 tardia.
    // El margen de 500 ms contempla la carga
    // del sistema de pruebas, no cambia el
    // plazo interno del Dispatcher (200 ms).
    passed = batch.quotes.size() == 7 && batch.failures.size() == 1 &&
             checkedIds.size() == 8 && timeoutFound && correctEtas &&
             elapsedMs < 500.0;

    // Al salir, el destructor del Dispatcher
    // espera al trabajador lento y libera
    // sus recursos de forma ordenada.
  }

  fleet.requestStop();
  fleet.join();

  if (!passed) {
    std::cerr << "ERROR: quoteTimeoutMs no respetado\n";
    return 1;
  }

  std::cout << "Cotizaciones tardias ignoradas: OK\n";
  std::cout << "Dispatcher no espera 700 ms: OK\n";
  std::cout << "DP4 TIMEOUT TEST: OK\n";

  return 0;
}
