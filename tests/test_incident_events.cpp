
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "fleet/Fleet.hpp"
#include "incidents/BreakdownPolicy.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

using RealClock = std::chrono::steady_clock;


template <typename Predicate>
bool waitFor(const std::shared_ptr<CapacitySignal> &signal,
             Predicate predicate) {

  std::atomic<bool> cancelled{false};

  const auto deadline = RealClock::now() + std::chrono::seconds(10);

  while (RealClock::now() < deadline) {

    const auto version = signal->version();

    if (predicate()) {
      return true;
    }

    signal->waitUntilChange(version, deadline, cancelled);
  }

  return predicate();
}


std::string findRescueOrder(unsigned int seed) {

  constexpr double probability = 0.5;

  for (int i = 0; i < 10000; ++i) {

    const std::string orderId = "incident_test_" + std::to_string(i);

    const auto source = incidents::makeBreakdownPlan(seed, "c0", orderId,
                                                     "toCustomer", probability);

    const auto recipient = incidents::makeBreakdownPlan(
        seed, "c1", orderId, "toCustomer", probability);

    if (source.occurs && !recipient.occurs) {
      return orderId;
    }
  }

  return "";
}


std::vector<nlohmann::json> readEvents(const std::filesystem::path &path) {

  std::ifstream input(path);
  std::vector<nlohmann::json> events;
  std::string line;

  if (!input) {
    throw std::runtime_error("No se pudo abrir el registro de eventos");
  }

  while (std::getline(input, line)) {
    events.push_back(nlohmann::json::parse(line));
  }

  return events;
}


std::size_t countEvent(const std::vector<nlohmann::json> &events,
                       const std::string &name) {

  std::size_t total = 0;

  for (const auto &event : events) {
    if (event.value("event", "") == name) {
      ++total;
    }
  }

  return total;
}


bool testSuccessfulRescue(SimulationConfig config,
                          const std::filesystem::path &logPath) {

  config.fleet.couriers = 2;
  config.fleet.bagCapacity = 1;
  config.fleet.startNode = "n8";
  config.simulation.timeScale = 120.0;
  config.incidents.breakdownProbability = 0.5;

  const std::string orderId = findRescueOrder(config.orders.seed);

  if (orderId.empty()) {
    return false;
  }

  bool restaurantFound = false;

  for (Restaurant &restaurant : config.restaurants) {
    if (restaurant.id == "r3") {
      restaurant.nodeId = "n8";
      restaurant.prepTimeMinMs = 0;
      restaurant.prepTimeMaxMs = 0;
      restaurantFound = true;
    }
  }

  if (!restaurantFound) {
    return false;
  }

  std::size_t breakdowns = 0;
  std::size_t reassignments = 0;

  bool accepted = false;
  bool delivered = false;
  bool resultOk = false;

  {
    RoadNetwork network(config);

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    EventLogger logger(logPath.string());

    Fleet fleet(config, network, clock);
    fleet.start();

    for (std::size_t i = 0; i < fleet.size(); ++i) {
      fleet.at(i).setEventLogger(&logger);
    }

    fleet.startRescue();

    logger.logEvent(
        {{"event", "orderCreated"}, {"order", orderId}, {"restaurant", "r3"}},
        clock.getSimulatedTimeMs());

    accepted = fleet.at(0).tryAssignOrder(
        Order(orderId, "r3", "n23", clock.getSimulatedTimeMs()), [&]() {
          logger.logEvent({{"event", "orderAssigned"},
                           {"order", orderId},
                           {"courier", "c0"}},
                          clock.getSimulatedTimeMs());
        });

    delivered = waitFor(fleet.getCapacitySignal(), [&]() {
      const auto state = fleet.at(1).getAssignedOrderState(orderId);

      return state && *state == OrderState::Delivered;
    });

    fleet.requestStop();
    fleet.join();

    breakdowns = fleet.getBreakdownCount();
    reassignments = fleet.getRescuedOrderCount();

    const auto results = fleet.takeFinishedOrders();

    resultOk = results.size() == 1 && results[0].courierId == "c1" &&
               results[0].order.orderId == orderId &&
               results[0].order.getState() == OrderState::Delivered &&
               fleet.getRescueCount() == 1;
  }

  const auto events = readEvents(logPath);


  bool fieldsOk = false;
  bool correctOrder = false;

  std::size_t breakdownIndex = events.size();
  std::size_t reassignmentIndex = events.size();
  std::size_t deliveryIndex = events.size();

  long long reassignmentTime = -1;
  long long deliveryTime = -1;

  for (std::size_t i = 0; i < events.size(); ++i) {

    const auto &event = events[i];
    const std::string name = event.value("event", "");

    if (name == "courierBreakdown") {
      breakdownIndex = i;
      fieldsOk = event.value("courier", "") == "c0";
    }

    if (name == "orderReassigned") {
      reassignmentIndex = i;
      reassignmentTime = event.at("t").get<long long>();

      fieldsOk = fieldsOk && event.value("order", "") == orderId &&
                 event.value("from", "") == "c0" &&
                 event.value("to", "") == "c1";
    }

    if (name == "orderDelivered") {
      deliveryIndex = i;
      deliveryTime = event.at("t").get<long long>();

      fieldsOk = fieldsOk && event.value("order", "") == orderId &&
                 event.value("courier", "") == "c1";
    }
  }

  correctOrder = breakdownIndex < reassignmentIndex &&
                 reassignmentIndex < deliveryIndex && reassignmentTime >= 0 &&
                 deliveryTime >= reassignmentTime;

  const bool countsOk =
      countEvent(events, "courierBreakdown") == breakdowns &&
      countEvent(events, "orderReassigned") == reassignments &&
      breakdowns == 1 && reassignments == 1;

  return accepted && delivered && resultOk && fieldsOk && correctOrder &&
         countsOk;
}


bool testSimultaneousBreakdowns(SimulationConfig config,
                                const std::filesystem::path &logPath) {

  config.fleet.couriers = 2;
  config.fleet.bagCapacity = 1;
  config.fleet.startNode = "n8";
  config.simulation.timeScale = 120.0;
  config.incidents.breakdownProbability = 1.0;

  bool restaurantFound = false;

  for (Restaurant &restaurant : config.restaurants) {
    if (restaurant.id == "r3") {
      restaurant.nodeId = "n8";
      restaurant.prepTimeMinMs = 0;
      restaurant.prepTimeMaxMs = 0;
      restaurantFound = true;
    }
  }

  if (!restaurantFound) {
    return false;
  }

  bool acceptedA = false;
  bool acceptedB = false;
  bool bothBroken = false;
  bool noDeadlock = false;

  std::size_t breakdowns = 0;
  std::size_t rescues = 0;

  {
    RoadNetwork network(config);

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    EventLogger logger(logPath.string());

    Fleet fleet(config, network, clock);
    fleet.start();

    for (std::size_t i = 0; i < fleet.size(); ++i) {
      fleet.at(i).setEventLogger(&logger);
    }

    fleet.startRescue();

    const auto assign = [&](std::size_t index, const std::string &orderId) {
      logger.logEvent(
          {{"event", "orderCreated"}, {"order", orderId}, {"restaurant", "r3"}},
          clock.getSimulatedTimeMs());

      return fleet.at(index).tryAssignOrder(
          Order(orderId, "r3", "n23", clock.getSimulatedTimeMs()), [&]() {
            logger.logEvent({{"event", "orderAssigned"},
                             {"order", orderId},
                             {"courier", "c" + std::to_string(index)}},
                            clock.getSimulatedTimeMs());
          });
    };

    acceptedA = assign(0, "simultaneous_a");
    acceptedB = assign(1, "simultaneous_b");

    bothBroken = waitFor(fleet.getCapacitySignal(), [&]() {
      return fleet.at(0).getState() == CourierState::Broken &&
             fleet.at(1).getState() == CourierState::Broken;
    });

    const auto stopStart = RealClock::now();

    fleet.requestStop();
    fleet.join();

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             RealClock::now() - stopStart)
                             .count();

    breakdowns = fleet.getBreakdownCount();
    rescues = fleet.getRescuedOrderCount();

    const auto results = fleet.takeFinishedOrders();

    std::set<std::string> ids;
    std::size_t pending = 0;

    for (const auto &result : results) {
      ids.insert(result.order.orderId);

      if (result.order.getState() == OrderState::Pending) {
        ++pending;
      }
    }

    noDeadlock = elapsed < 3000 && results.size() == 2 && ids.size() == 2 &&
                 pending == 2 && fleet.getRescueCount() == 0;
  }

  const auto events = readEvents(logPath);

  std::set<std::string> brokenCouriers;

  for (const auto &event : events) {
    if (event.value("event", "") == "courierBreakdown") {
      brokenCouriers.insert(event.value("courier", ""));
    }
  }

  const bool countsOk = breakdowns == 2 && rescues == 0 &&
                        brokenCouriers == std::set<std::string>({"c0", "c1"}) &&
                        countEvent(events, "courierBreakdown") == 2 &&
                        countEvent(events, "orderReassigned") == 0;

  return acceptedA && acceptedB && bothBroken && noDeadlock && countsOk;
}


int main(int argc, char *argv[]) {

  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  const auto directory = std::filesystem::temp_directory_path();

  const std::string suffix =
      std::to_string(RealClock::now().time_since_epoch().count());

  const auto rescueLog = directory / ("dp7_rescue_" + suffix + ".jsonl");

  const auto simultaneousLog =
      directory / ("dp7_simultaneous_" + suffix + ".jsonl");

  bool rescueOk = false;
  bool simultaneousOk = false;

  try {
    rescueOk = testSuccessfulRescue(config, rescueLog);
    simultaneousOk = testSimultaneousBreakdowns(config, simultaneousLog);

  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
  }

  std::filesystem::remove(rescueLog);
  std::filesystem::remove(simultaneousLog);

  std::cout << "=== TEST DP7 INCIDENT EVENTS ===\n";

  std::cout << "Averia y rescate registrado: " << (rescueOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Reasignacion antes de entrega: " << (rescueOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Averias simultaneas sin rescate falso: "
            << (simultaneousOk ? "OK" : "ERROR") << '\n';

  const bool passed = rescueOk && simultaneousOk;

  std::cout << "DP7 INCIDENT EVENTS TEST: " << (passed ? "OK" : "ERROR")
            << '\n';

  return passed ? 0 : 1;
}
