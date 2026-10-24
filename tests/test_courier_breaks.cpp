
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "fleet/CapacitySignal.hpp"
#include "fleet/Courier.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"
#include "restaurants/RestaurantManager.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using Steady = std::chrono::steady_clock;

bool waitState(Courier &courier, CourierState desired,
               Steady::time_point deadline) {

  while (Steady::now() < deadline) {
    if (courier.getState() == desired)
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return courier.getState() == desired;
}

int main(int argc, char **argv) {
  SimulationConfig cfg;
  if (!loadAndValidateConfig(argc, argv, cfg))
    return 1;
  cfg.fleet.startNode = "n8";
  cfg.fleet.bagCapacity = 2;
  cfg.fleet.breakAfterDeliveries = 1;
  cfg.fleet.breakMs = 120000;
  cfg.simulation.timeScale = 120.0;
  cfg.incidents.breakdownProbability = 0.0;
  bool restaurantFound = false;
  for (Restaurant &restaurant : cfg.restaurants) {
    if (restaurant.id == "r3") {
      restaurant.nodeId = "n8";
      restaurant.prepTimeMinMs = 0;
      restaurant.prepTimeMaxMs = 0;
      restaurantFound = true;
    }
  }
  if (!restaurantFound)
    return 1;

  const auto tmp =
      std::filesystem::temp_directory_path() /
      ("equipego_break_test_" +
       std::to_string(Steady::now().time_since_epoch().count()) + ".jsonl");
  bool firstAssigned = false;
  bool resting = false;
  bool rejectedInBreak = false;
  bool signaledOnReturn = false;
  bool secondAssigned = false;
  bool closedQuickly = false;

  {
    SimulationClock clock;
    clock.start(cfg.simulation.timeScale);
    RoadNetwork network(cfg);
    EventLogger logger(tmp.string());
    auto signal = std::make_shared<CapacitySignal>();
    auto restaurants = std::make_shared<RestaurantManager>(cfg.restaurants);
    Courier courier("c0", cfg, network, clock, signal, restaurants);
    courier.setEventLogger(&logger);
    std::thread thread(&Courier::run, &courier);

    const std::string firstId = "break_test_1";
    logger.logEvent(
        {{"event", "orderCreated"}, {"order", firstId}, {"restaurant", "r3"}},
        clock.getSimulatedTimeMs());
    firstAssigned = courier.tryAssignOrder(
        Order(firstId, "r3", "n23", clock.getSimulatedTimeMs()), [&] {
          logger.logEvent({{"event", "orderAssigned"},
                           {"order", firstId},
                           {"courier", "c0"}},
                          clock.getSimulatedTimeMs());
        });
    resting = waitState(courier, CourierState::Resting,
                        Steady::now() + std::chrono::seconds(15));
    if (resting) {
      bool callbackCalled = false;
      const Order attempt("break_rejected", "r3", "n23",
                          clock.getSimulatedTimeMs());

      rejectedInBreak = !courier.tryAssignOrder(
                            Order("break_rejected", "r3", "n23", attempt.time),
                            [&] { callbackCalled = true; }) &&
                        !callbackCalled;

      std::atomic<bool> cancelled{false};
      const auto version = signal->version();
      const auto deadline = Steady::now() + std::chrono::seconds(4);
      signaledOnReturn =
          signal->waitUntilChange(version, deadline, cancelled) &&
          waitState(courier, CourierState::Idle, deadline);
      const std::string secondId = "break_test_2";
      logger.logEvent({{"event", "orderCreated"},
                       {"order", secondId},
                       {"restaurant", "r3"}},
                      clock.getSimulatedTimeMs());
      secondAssigned = courier.tryAssignOrder(
          Order(secondId, "r3", "n23", clock.getSimulatedTimeMs()), [&] {
            logger.logEvent({{"event", "orderAssigned"},
                             {"order", secondId},
                             {"courier", "c0"}},
                            clock.getSimulatedTimeMs());
          });
    }
    const auto startedStopping = Steady::now();
    courier.requestStop();
    restaurants->requestStop();
    thread.join();
    closedQuickly = Steady::now() - startedStopping < std::chrono::seconds(5);
    logger.flush();
  }

  std::ifstream input(tmp);
  bool eventsValid = true;
  int starts = 0, ends = 0;
  bool inBreak = false;
  std::string line;
  while (std::getline(input, line)) {
    const nlohmann::json ev = nlohmann::json::parse(line);
    const std::string name = ev.at("event").get<std::string>();
    if (name == "courierBreakStarted") {
      eventsValid = eventsValid && !inBreak;
      inBreak = true;
      ++starts;
    } else if (name == "courierBreakEnded") {
      eventsValid = eventsValid && inBreak;
      inBreak = false;
      ++ends;
    } else if (name == "orderAssigned" && inBreak) {
      eventsValid = false;
    }
  }
  input.close();
  std::filesystem::remove(tmp);
  eventsValid = eventsValid && !inBreak && starts >= 1 && starts == ends;
  const bool pass = firstAssigned && resting && rejectedInBreak &&
                    signaledOnReturn && secondAssigned && closedQuickly &&
                    eventsValid;
  std::cout << "Variante C | first=" << firstAssigned << " resting=" << resting
            << " rejected=" << rejectedInBreak << " woke=" << signaledOnReturn
            << " returned=" << secondAssigned << " stop=" << closedQuickly
            << " events=" << eventsValid << '\n';
  return pass ? 0 : 1;
}
