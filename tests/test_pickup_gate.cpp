
#include "restaurants/PickupGate.hpp"

#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

int main() {

  bool fifoOk = false;
  bool capacityOk = false;
  bool shutdownOk = false;


  {
    PickupGate gate(1);

    auto first = gate.acquire();

    std::mutex resultMutex;
    std::vector<int> granted;

    auto worker = [&](int id) {
      auto permit = gate.acquire();

      if (permit) {
        std::lock_guard<std::mutex> lock(resultMutex);

        granted.push_back(id);
      }
    };

    std::thread t1(worker, 1);

    const bool queued1 =
        gate.waitForWaitingCount(1, Clock::now() + std::chrono::seconds(3));

    std::thread t2(worker, 2);

    const bool queued2 =
        gate.waitForWaitingCount(2, Clock::now() + std::chrono::seconds(3));

    first.reset();

    t1.join();
    t2.join();

    fifoOk = queued1 && queued2 && granted == std::vector<int>({1, 2}) &&
             gate.peakUsage() == 1 && gate.occupied() == 0;
  }


  {
    PickupGate gate(2);

    auto first = gate.acquire();
    auto second = gate.acquire();

    int cancelled = 0;
    std::mutex resultMutex;

    auto waiter = [&] {
      auto permit = gate.acquire();

      if (!permit) {
        std::lock_guard<std::mutex> lock(resultMutex);

        ++cancelled;
      }
    };

    std::thread t1(waiter);
    std::thread t2(waiter);

    const bool bothQueued =
        gate.waitForWaitingCount(2, Clock::now() + std::chrono::seconds(3));

    capacityOk = bothQueued && gate.occupied() == 2 && gate.peakUsage() == 2;

    gate.requestStop();

    t1.join();
    t2.join();

    first.reset();
    second.reset();

    shutdownOk = cancelled == 2 && gate.occupied() == 0 &&
                 gate.waitingCount() == 0 && !gate.acquire().has_value();
  }


  std::cout << "=== TEST DP5 PICKUP SLOTS ===\n";

  std::cout << "Turnos FIFO: " << (fifoOk ? "OK" : "ERROR") << '\n';

  std::cout << "Maximo de 2 cupos: " << (capacityOk ? "OK" : "ERROR") << '\n';

  std::cout << "Apagado desbloquea fila: " << (shutdownOk ? "OK" : "ERROR")
            << '\n';

  const bool passed = fifoOk && capacityOk && shutdownOk;

  std::cout << "DP5 PICKUP GATE TEST: " << (passed ? "OK" : "ERROR") << '\n';

  return passed ? 0 : 1;
}
