
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

using RealClock = std::chrono::steady_clock;

// =====================================================
// ESPERAR UNA ENTREGA SIN BUSY WAITING
// =====================================================

bool waitForDelivery(Courier &courier, const std::string &orderId,
                     const std::shared_ptr<CapacitySignal> &signal) {

  std::atomic<bool> cancelled{false};

  const auto deadline = RealClock::now() + std::chrono::seconds(15);

  while (RealClock::now() < deadline) {

    const auto version = signal->version();

    const auto state = courier.getAssignedOrderState(orderId);

    if (state && *state == OrderState::Delivered) {
      return true;
    }

    signal->waitUntilChange(version, deadline, cancelled);
  }

  const auto finalState = courier.getAssignedOrderState(orderId);

  return finalState && *finalState == OrderState::Delivered;
}

// =====================================================
// TEST PRINCIPAL
// =====================================================

int main(int argc, char *argv[]) {

  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  config.fleet.startNode = "n8";
  config.fleet.bagCapacity = 2;
  config.simulation.timeScale = 120.0;
  config.incidents.breakdownProbability = 0.0;

  // El restaurante de prueba esta en el
  // nodo inicial, con preparacion inmediata.
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
    std::cerr << "ERROR: falta el restaurante r3\n";
    return 1;
  }

  const std::string orderId = "dp7_lifecycle_order";

  const std::string filename =
      "equipego_dp7_lifecycle_" +
      std::to_string(RealClock::now().time_since_epoch().count()) + ".jsonl";

  const auto path = std::filesystem::temp_directory_path() / filename;

  bool assigned = false;
  bool delivered = false;

  // =========================================
  // 1. EJECUTAR EL REPARTIDOR
  // =========================================

  {
    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    RoadNetwork network(config);

    EventLogger logger(path.string());

    auto signal = std::make_shared<CapacitySignal>();

    auto restaurants = std::make_shared<RestaurantManager>(config.restaurants);

    Courier courier("c0", config, network, clock, signal, restaurants);

    courier.setEventLogger(&logger);

    std::thread worker(&Courier::run, &courier);

    // La creacion precede a la asignacion.
    logger.logEvent(
        {{"event", "orderCreated"}, {"order", orderId}, {"restaurant", "r3"}},
        clock.getSimulatedTimeMs());

    assigned = courier.tryAssignOrder(
        Order(orderId, "r3", "n23", clock.getSimulatedTimeMs()), [&]() {
          logger.logEvent({{"event", "orderAssigned"},
                           {"order", orderId},
                           {"courier", "c0"}},
                          clock.getSimulatedTimeMs());
        });

    delivered = waitForDelivery(courier, orderId, signal);

    courier.requestStop();
    restaurants->requestStop();
    worker.join();
  }

  // =========================================
  // 2. ANALIZAR ARCHIVO JSONL
  // =========================================

  const std::vector<std::string> expected = {"orderCreated", "orderAssigned",
                                             "pickupStarted", "orderPickedUp",
                                             "orderDelivered"};

  std::vector<nlohmann::json> events;

  bool validJson = true;

  {
    std::ifstream input(path);

    std::string line;

    if (!input) {
      validJson = false;
    }

    while (std::getline(input, line)) {
      try {
        events.push_back(nlohmann::json::parse(line));
      } catch (const nlohmann::json::exception &) {
        validJson = false;
      }
    }
  }

  std::filesystem::remove(path);

  // =========================================
  // 3. VERIFICAR ORDEN Y CAMPOS
  // =========================================

  bool correctSequence = events.size() == expected.size();

  bool correctFields = true;
  bool monotonicTime = true;

  long long previousTime = -1;

  for (std::size_t i = 0; i < events.size(); ++i) {

    const auto &event = events[i];

    if (!event.is_object() || !event.contains("event") ||
        !event.contains("order") || !event.contains("t")) {

      correctFields = false;
      correctSequence = false;
      continue;
    }

    if (i >= expected.size() || event["event"] != expected[i]) {
      correctSequence = false;
    }

    if (event["order"] != orderId || !event["t"].is_number_integer()) {
      correctFields = false;
      continue;
    }

    const long long time = event["t"].get<long long>();

    if (time < previousTime) {
      monotonicTime = false;
    }

    previousTime = time;

    if (i >= 2 && i <= 3) {
      if (!event.contains("restaurant") || !event.contains("courier") ||
          event["restaurant"] != "r3" || event["courier"] != "c0") {
        correctFields = false;
      }
    }

    if (i == 4) {
      if (!event.contains("courier") || event["courier"] != "c0") {
        correctFields = false;
      }
    }
  }

  // =========================================
  // 4. MOSTRAR RESULTADOS
  // =========================================

  std::cout << "=== TEST DP7 COURIER LIFECYCLE ===\n";

  std::cout << "Pedido asignado: " << (assigned ? "OK" : "ERROR") << '\n';

  std::cout << "Pedido entregado: " << (delivered ? "OK" : "ERROR") << '\n';

  std::cout << "JSONL valido: " << (validJson ? "OK" : "ERROR") << '\n';

  std::cout << "Cinco eventos en orden: " << (correctSequence ? "OK" : "ERROR")
            << '\n';

  std::cout << "Campos obligatorios: " << (correctFields ? "OK" : "ERROR")
            << '\n';

  std::cout << "Tiempo no decreciente: " << (monotonicTime ? "OK" : "ERROR")
            << '\n';

  const bool passed = assigned && delivered && validJson && correctSequence &&
                      correctFields && monotonicTime;

  std::cout << "DP7 COURIER LIFECYCLE TEST: " << (passed ? "OK" : "ERROR")
            << '\n';

  return passed ? 0 : 1;
}
