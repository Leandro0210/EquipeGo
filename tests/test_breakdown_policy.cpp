
#include "incidents/BreakdownPolicy.hpp"

#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main() {
  bool probabilityOk = true;
  bool deterministicOk = true;
  bool positionOk = true;
  bool variationOk = false;
  bool concurrencyOk = false;

  constexpr unsigned int seed = 42;


  for (int i = 0; i < 100; ++i) {
    const std::string orderId = "order_" + std::to_string(i);

    const auto never =
        incidents::makeBreakdownPlan(seed, "c0", orderId, "toCustomer", 0.0);

    const auto always =
        incidents::makeBreakdownPlan(seed, "c0", orderId, "toCustomer", 1.0);

    if (never.occurs || !always.occurs) {
      probabilityOk = false;
    }
  }


  const auto first =
      incidents::makeBreakdownPlan(seed, "c3", "order_17", "toRestaurant", 0.5);

  const auto second =
      incidents::makeBreakdownPlan(seed, "c3", "order_17", "toRestaurant", 0.5);

  deterministicOk =
      first.occurs == second.occurs && first.atFraction == second.atFraction;


  int breakdowns = 0;

  for (int i = 0; i < 1000; ++i) {
    const auto plan = incidents::makeBreakdownPlan(
        seed, "c1", "order_" + std::to_string(i), "toCustomer", 0.5);

    if (plan.atFraction < 0.10 || plan.atFraction >= 0.90) {
      positionOk = false;
    }

    if (plan.occurs) {
      ++breakdowns;
    }
  }

  variationOk = breakdowns > 0 && breakdowns < 1000;


  std::atomic<int> errors{0};
  std::vector<std::thread> threads;

  for (int worker = 0; worker < 8; ++worker) {
    threads.emplace_back([&] {
      for (int i = 0; i < 500; ++i) {
        const auto result = incidents::makeBreakdownPlan(seed, "c3", "order_17",
                                                         "toRestaurant", 0.5);

        if (result.occurs != first.occurs ||
            result.atFraction != first.atFraction) {
          ++errors;
        }
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  concurrencyOk = errors.load() == 0;


  std::cout << "=== TEST DP6 BREAKDOWN POLICY ===\n";

  std::cout << "Probabilidades 0 y 1: " << (probabilityOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Decisiones reproducibles: "
            << (deterministicOk ? "OK" : "ERROR") << '\n';

  std::cout << "Posicion de averia valida: " << (positionOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Variacion entre pedidos: " << (variationOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Consultas concurrentes: " << (concurrencyOk ? "OK" : "ERROR")
            << '\n';

  const bool passed = probabilityOk && deterministicOk && positionOk &&
                      variationOk && concurrencyOk;

  std::cout << "DP6 BREAKDOWN POLICY TEST: " << (passed ? "OK" : "ERROR")
            << '\n';

  return passed ? 0 : 1;
}
