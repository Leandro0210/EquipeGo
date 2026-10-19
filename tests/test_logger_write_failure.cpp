
#include "core/Logger.hpp"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>


int main() {

#if defined(__linux__)

  constexpr int kThreads = 4;
  constexpr int kEventsPerThread = 1500;

  std::atomic<int> workerExceptions{0};

  bool flushDetectedFailure = false;
  bool allWorkersFinished = false;

  try {

    EventLogger logger("/dev/full");

    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    const std::string payload(64, 'X');


    for (int i = 0; i < kThreads; ++i) {

      workers.emplace_back([&logger, &workerExceptions, &payload, i]() {
        const std::string orderId = "write_failure_" + std::to_string(i);

        const std::string courierId = "c" + std::to_string(i);

        try {

          for (int eventNumber = 0; eventNumber < kEventsPerThread;
               ++eventNumber) {

            logger.logEvent({{"event", "orderAssigned"},
                             {"order", orderId},
                             {"courier", courierId},
                             {"payload", payload}},
                            static_cast<long long>(eventNumber * 10));
          }

        } catch (...) {

          workerExceptions.fetch_add(1);
        }
      });
    }

    for (std::thread &worker : workers) {
      worker.join();
    }

    allWorkersFinished = true;


    try {

      logger.flush();

    } catch (const std::runtime_error &) {

      flushDetectedFailure = true;
    }

  } catch (const std::exception &error) {

    std::cerr << "ERROR preparando el test: " << error.what() << '\n';

    return 1;
  }


  const bool workersOk = allWorkersFinished && workerExceptions.load() == 0;

  const bool passed = workersOk && flushDetectedFailure;

  std::cout << "=== TEST DP7 LOGGER WRITE FAILURE ===\n";

  std::cout << "Hilos terminados: " << (workersOk ? "OK" : "ERROR") << '\n';

  std::cout << "Sin excepciones de I/O en workers: "
            << (workerExceptions.load() == 0 ? "OK" : "ERROR") << '\n';

  std::cout << "Fallo detectado en flush: "
            << (flushDetectedFailure ? "OK" : "ERROR") << '\n';

  std::cout << "DP7 LOGGER WRITE FAILURE TEST: " << (passed ? "OK" : "ERROR")
            << '\n';

  return passed ? 0 : 1;

#else

  std::cout << "Test de /dev/full disponible solo en Linux\n";

  return 0;

#endif
}
