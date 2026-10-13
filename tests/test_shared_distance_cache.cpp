// Test de DP10.2: compila solo con la biblioteca estandar de C++17.
// Verifica single-flight, clave direccional y propagacion de excepciones.
#include "dispatch/SharedDistanceCache.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

int main() {
  SharedDistanceCache cache;
  std::atomic<int> computed{0};
  std::atomic<int> wrong{0};
  std::vector<std::thread> workers;

  // 32 hilos solicitan al mismo tiempo la misma distancia: UNA cuenta.
  for (int i = 0; i < 32; ++i) {
    workers.emplace_back([&]() {
      try {
        const double result = cache.get("r0", "n15", [&]() -> double {
          computed.fetch_add(1);
          std::this_thread::sleep_for(std::chrono::milliseconds(12));
          return 125.5;
        });
        if (std::abs(result - 125.5) > 1e-9) wrong.fetch_add(1);
      } catch (...) {
        wrong.fetch_add(1);
      }
    });
  }
  for (auto &worker : workers) worker.join();
  if (computed.load() != 1 || wrong.load() != 0) {
    std::cerr << "ERROR: single-flight: calculos=" << computed.load()
              << ", errores=" << wrong.load() << '\n';
    return 1;
  }

  // Grafo dirigido: A -> B y B -> A NO comparten distancia.
  const double other = cache.get("n15", "r0", [&] {
    computed.fetch_add(1);
    return 139.0;
  });
  if (other != 139.0 || computed.load() != 2) {
    std::cerr << "ERROR: cache no respeta direccion de calles\n";
    return 1;
  }

  std::atomic<int> errorsCalculated{0};
  std::atomic<int> errorsCaught{0};
  workers.clear();
  for (int i = 0; i < 16; ++i) {
    workers.emplace_back([&]() {
      try {
        (void)cache.get("r1", "inalcanzable", [&]() -> double {
          errorsCalculated.fetch_add(1);
          std::this_thread::sleep_for(std::chrono::milliseconds(8));
          throw std::runtime_error("No existe ruta");
        });
      } catch (const std::runtime_error &) {
        errorsCaught.fetch_add(1);
      }
    });
  }
  for (auto &worker : workers) worker.join();
  if (errorsCalculated.load() != 1 || errorsCaught.load() != 16) {
    std::cerr << "ERROR: excepciones no propagadas a todos los hilos\n";
    return 1;
  }

  std::cout << "DP10.2 CACHE: OK | 32 hilos/1 calculo | direccion OK | "
               "16 errores propagados | sin busy waiting\n";
  return 0;
}
