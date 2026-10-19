
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/OrderBook.hpp"
#include "restaurants/PickupGate.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

int main(int argc, char *argv[]) {
  bool bookOk = false;
  bool bagOk = false;
  bool pickupOk = false;


  {
    OrderBook book(10);
    std::atomic<int> accepted{0};
    std::vector<std::thread> workers;

    for (int i = 0; i < 32; ++i) {
      workers.emplace_back([&, i] {
        Order order("book_" + std::to_string(i), "r3", "n8", 0);

        if (book.tryAdd(std::move(order))) {
          ++accepted;
        }
      });
    }

    for (auto &worker : workers) {
      worker.join();
    }

    const bool initialLimit =
        accepted.load() == 10 && book.size() == 10 && book.full();

    auto taken = book.tryTake();

    const bool inflightProtected =
        taken.has_value() && book.size() == 9 && book.full() &&
        !book.tryAdd(Order("overflow", "r3", "n8", 0));

    book.completeTaken();

    const bool capacityRecovered =
        book.tryAdd(Order("replacement", "r3", "n8", 0)) && book.size() == 10;

    book.requestStop();
    const auto remaining = book.takeRemaining();

    bookOk = initialLimit && inflightProtected && capacityRecovered &&
             remaining.size() == 10;
  }


  {
    PickupGate gate(2);

    auto slot1 = gate.acquire();
    auto slot2 = gate.acquire();

    std::atomic<int> acquired{0};
    std::vector<std::thread> workers;

    for (int i = 0; i < 10; ++i) {
      workers.emplace_back([&] {
        auto permit = gate.acquire();

        if (permit) {
          ++acquired;
        }
      });
    }

    const bool queued = gate.waitForWaitingCount(
        10, std::chrono::steady_clock::now() + std::chrono::seconds(5));

    const bool limitRespected = gate.occupied() == 2 && gate.peakUsage() == 2;

    slot1.reset();
    slot2.reset();

    for (auto &worker : workers) {
      worker.join();
    }

    pickupOk = queued && limitRespected && acquired.load() == 10 &&
               gate.peakUsage() == 2 && gate.occupied() == 0 &&
               gate.waitingCount() == 0;
  }


  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  config.fleet.couriers = 8;
  config.fleet.bagCapacity = 2;
  config.fleet.startNode = "n8";
  config.simulation.timeScale = 60.0;

  for (Restaurant &restaurant : config.restaurants) {
    if (restaurant.id == "r3") {
      restaurant.prepTimeMinMs = 600000;
      restaurant.prepTimeMaxMs = 600000;
    }
  }

  SimulationClock clock;
  clock.start(config.simulation.timeScale);

  RoadNetwork network(config);
  Fleet fleet(config, network, clock);

  fleet.start();

  std::atomic<int> accepted{0};
  std::atomic<bool> capacityValid{true};

  std::vector<std::thread> workers;

  for (int worker = 0; worker < 8; ++worker) {
    workers.emplace_back([&, worker] {
      for (int j = 0; j < 16; ++j) {
        const std::size_t courierIndex =
            static_cast<std::size_t>((worker + j) % 8);

        Order order("bag_" + std::to_string(worker) + "_" + std::to_string(j),
                    "r3", "n8", clock.getSimulatedTimeMs());

        Courier &courier = fleet.at(courierIndex);

        if (courier.tryAssignOrder(std::move(order))) {
          ++accepted;
        }

        if (courier.getReservedCount() > 2) {
          capacityValid.store(false);
        }
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  bool everyCourierFull = true;

  for (std::size_t i = 0; i < fleet.size(); ++i) {
    if (fleet.at(i).getReservedCount() != 2) {
      everyCourierFull = false;
    }
  }

  fleet.requestStop();
  fleet.join();

  auto results = fleet.takeFinishedOrders();

  bool allPending = true;
  bool allReleased = true;

  for (const auto &result : results) {
    if (result.order.getState() != OrderState::Pending) {
      allPending = false;
    }
  }

  for (std::size_t i = 0; i < fleet.size(); ++i) {
    if (fleet.at(i).getReservedCount() != 0) {
      allReleased = false;
    }
  }

  bagOk = capacityValid.load() && accepted.load() == 16 && everyCourierFull &&
          results.size() == 16 && allPending && allReleased;


  std::cout << "=== TEST DP5 RESOURCE LIMITS ===\n";

  std::cout << "maxPending concurrente: " << (bookOk ? "OK" : "ERROR") << '\n';

  std::cout << "pickupSlots concurrente: " << (pickupOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "bagCapacity concurrente: " << (bagOk ? "OK" : "ERROR") << '\n';

  const bool passed = bookOk && pickupOk && bagOk;

  std::cout << "DP5 RESOURCE LIMITS: " << (passed ? "OK" : "ERROR") << '\n';

  return passed ? 0 : 1;
}
