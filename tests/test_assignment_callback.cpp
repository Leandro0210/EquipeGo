
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "fleet/Courier.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

// =====================================================
// TEST DP7.2 - TRANSACCION DE ASIGNACION
// =====================================================

int main(int argc, char *argv[]) {

  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  config.fleet.bagCapacity = 1;
  config.incidents.breakdownProbability = 0.0;

  RoadNetwork network(config);

  SimulationClock clock;
  clock.start(config.simulation.timeScale);

  Courier courier("c0", config, network, clock);

  const std::string orderId = "dp7_callback_test";

  Order order(orderId, "r3", "n23", clock.getSimulatedTimeMs());

  // =========================================
  // 1. SIMULAR ERROR DEL REGISTRO
  // =========================================

  bool exceptionCaught = false;

  try {
    courier.tryAssignOrder(std::move(order), []() {
      throw std::runtime_error("Fallo simulado del logger");
    });

  } catch (const std::runtime_error &) {
    exceptionCaught = true;
  }

  const bool rollbackOk = exceptionCaught && courier.getReservedCount() == 0 &&
                          order.orderId == orderId &&
                          order.getState() == OrderState::Created;

  // =========================================
  // 2. REGISTRAR ASIGNACION CORRECTA
  // =========================================

  const std::string filename =
      "equipego_dp7_callback_" +
      std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) +
      ".jsonl";

  const auto path = std::filesystem::temp_directory_path() / filename;

  bool accepted = false;
  bool callbackCalled = false;
  bool rejectedWithoutCallback = false;

  {
    EventLogger logger(path.string());

    accepted = courier.tryAssignOrder(std::move(order), [&]() {
      callbackCalled = true;

      logger.logEvent(
          {{"event", "orderAssigned"}, {"order", orderId}, {"courier", "c0"}},
          clock.getSimulatedTimeMs());
    });

    const auto state = courier.getAssignedOrderState(orderId);

    accepted = accepted && callbackCalled && courier.getReservedCount() == 1 &&
               state.has_value() && *state == OrderState::Assigned;

    // =====================================
    // 3. RECHAZAR CUANDO NO HAY CAPACIDAD
    // =====================================

    bool unexpectedCallback = false;

    Order secondOrder("dp7_second", "r3", "n23", clock.getSimulatedTimeMs());

    const bool secondAccepted = courier.tryAssignOrder(
        std::move(secondOrder), [&]() { unexpectedCallback = true; });

    rejectedWithoutCallback = !secondAccepted && !unexpectedCallback &&
                              courier.getReservedCount() == 1;
  }

  // =========================================
  // 4. VERIFICAR EL JSONL
  // =========================================

  std::ifstream input(path);

  bool logOk = false;

  if (input) {
    std::string firstLine;
    std::string secondLine;

    if (std::getline(input, firstLine) && !std::getline(input, secondLine)) {

      try {
        const auto event = nlohmann::json::parse(firstLine);

        logOk = event.at("event") == "orderAssigned" &&
                event.at("order") == orderId && event.at("courier") == "c0" &&
                event.at("t").is_number_integer();

      } catch (const nlohmann::json::exception &) {
        logOk = false;
      }
    }
  }

  input.close();
  std::filesystem::remove(path);

  // =========================================
  // 5. RESULTADOS
  // =========================================

  std::cout << "=== TEST DP7 ASSIGNMENT CALLBACK ===\n";

  std::cout << "Rollback ante error: " << (rollbackOk ? "OK" : "ERROR") << '\n';

  std::cout << "Asignacion confirmada: " << (accepted ? "OK" : "ERROR") << '\n';

  std::cout << "Rechazo sin callback: "
            << (rejectedWithoutCallback ? "OK" : "ERROR") << '\n';

  std::cout << "Evento JSONL unico: " << (logOk ? "OK" : "ERROR") << '\n';

  const bool passed =
      rollbackOk && accepted && rejectedWithoutCallback && logOk;

  std::cout << "DP7 ASSIGNMENT CALLBACK TEST: " << (passed ? "OK" : "ERROR")
            << '\n';

  return passed ? 0 : 1;
}
