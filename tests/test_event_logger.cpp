
#include "core/Logger.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// =====================================================
// TEST DP7.1 - EVENT LOGGER
// =====================================================

int main() {

  constexpr int kThreads = 6;
  constexpr int kEventsPerThread = 200;

  const std::string filename =
      "equipego_dp7_logger_" +
      std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) +
      ".jsonl";

  const std::string filePath =
      (std::filesystem::temp_directory_path() / filename).string();

  bool startOk = false;
  bool invalidEventsRejected = false;
  bool validJsonl = true;
  bool structureOk = true;
  bool monotonicOk = true;
  bool countsOk = false;

  // =========================================
  // 1. CREAR LOGGER
  // =========================================

  {
    EventLogger logger(filePath);

    // =====================================
    // 2. VALIDAR LINEA DE INICIO
    // =====================================

    std::ostringstream captured;

    std::streambuf *original = std::cout.rdbuf(captured.rdbuf());

    logger.logStart();

    std::cout.rdbuf(original);

    startOk = captured.str() == "Simulation has started...\n";

    // =====================================
    // 3. PROBAR EVENTOS INVALIDOS
    // =====================================

    bool invalidObject = false;
    bool invalidName = false;
    bool invalidTime = false;
    bool invalidOrder = false;

    try {
      logger.logEvent(nlohmann::json::array(), 10);
    } catch (const std::invalid_argument &) {
      invalidObject = true;
    }

    try {
      logger.logEvent({{"event", ""}}, 10);
    } catch (const std::invalid_argument &) {
      invalidName = true;
    }

    try {
      logger.logEvent({{"event", "orderCreated"}}, -1);
    } catch (const std::invalid_argument &) {
      invalidTime = true;
    }

    try {
      logger.logEvent({{"event", "orderCreated"}, {"order", 123}}, 10);
    } catch (const std::invalid_argument &) {
      invalidOrder = true;
    }

    invalidEventsRejected =
        invalidObject && invalidName && invalidTime && invalidOrder;

    // =====================================
    // 4. GENERAR EVENTOS EN PARALELO
    // =====================================

    std::vector<std::thread> workers;

    workers.reserve(kThreads);

    for (int threadId = 0; threadId < kThreads; ++threadId) {

      workers.emplace_back([&logger, threadId]() {
        const std::string orderId = "test_order_" + std::to_string(threadId);

        const std::string courierId = "c" + std::to_string(threadId);

        for (int i = 0; i < kEventsPerThread; ++i) {

          const long long time = static_cast<long long>(i) * 10;

          logger.logEvent({{"event", "orderAssigned"},
                           {"order", orderId},
                           {"courier", courierId}},
                          time);
        }
      });
    }

    for (std::thread &worker : workers) {
      worker.join();
    }

    // =====================================
    // 5. COMPROBAR TIEMPO NO DECRECIENTE
    // =====================================

    logger.logEvent({{"event", "orderCreated"},
                     {"order", "timeline"},
                     {"restaurant", "r3"}},
                    100);

    // Simulamos un evento que llega tarde.
    // El archivo debe conservar t >= 100.
    logger.logEvent(
        {{"event", "orderAssigned"}, {"order", "timeline"}, {"courier", "c0"}},
        60);

    // Evento sin pedido.
    logger.logEvent({{"event", "courierBreakdown"}, {"courier", "c4"}}, 155);
  }

  // El logger ya cerro el archivo.
  // Ahora podemos leerlo sin concurrencia.

  // =========================================
  // 6. REVISAR TODO EL ARCHIVO
  // =========================================

  std::ifstream input(filePath);

  if (!input.is_open()) {
    std::cerr << "ERROR: no se pudo leer JSONL\n";
    return 1;
  }

  std::unordered_map<std::string, long long> lastTime;

  std::size_t totalLines = 0;
  std::size_t timelineEvents = 0;

  std::string line;

  while (std::getline(input, line)) {

    ++totalLines;

    if (line.empty()) {
      validJsonl = false;
      continue;
    }

    try {
      const nlohmann::json event = nlohmann::json::parse(line);

      // Cada linea tiene que ser un objeto.
      if (!event.is_object()) {
        structureOk = false;
        continue;
      }

      // Campos obligatorios.
      if (!event.contains("t") || !event["t"].is_number_integer() ||
          !event.contains("event") || !event["event"].is_string()) {

        structureOk = false;
        continue;
      }

      const long long timestamp = event["t"].get<long long>();

      if (timestamp < 0) {
        structureOk = false;
      }

      // Comprobar tiempos por pedido.
      if (event.contains("order")) {

        if (!event["order"].is_string()) {
          structureOk = false;
          continue;
        }

        const std::string orderId = event["order"].get<std::string>();

        const auto previous = lastTime.find(orderId);

        if (previous != lastTime.end() && timestamp < previous->second) {

          monotonicOk = false;
        }

        lastTime[orderId] = timestamp;

        if (orderId == "timeline") {
          ++timelineEvents;

          if (timestamp != 100) {
            monotonicOk = false;
          }
        }
      }

    } catch (const nlohmann::json::exception &) {
      validJsonl = false;
    }
  }

  // Seis hilos, 200 eventos cada uno,
  // mas los tres eventos de validacion.
  const std::size_t expectedCount = kThreads * kEventsPerThread + 3;

  countsOk = totalLines == expectedCount && timelineEvents == 2;

  input.close();

  std::remove(filePath.c_str());

  // =========================================
  // 7. MOSTRAR RESULTADOS
  // =========================================

  std::cout << "=== TEST DP7 EVENT LOGGER ===\n";

  std::cout << "Linea de inicio exacta: " << (startOk ? "OK" : "ERROR") << '\n';

  std::cout << "JSONL concurrente sin mezclas: "
            << (validJsonl && countsOk ? "OK" : "ERROR") << '\n';

  std::cout << "Campos obligatorios: " << (structureOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Tiempo monotono por pedido: " << (monotonicOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Eventos invalidos rechazados: "
            << (invalidEventsRejected ? "OK" : "ERROR") << '\n';

  const bool passed = startOk && invalidEventsRejected && validJsonl &&
                      structureOk && monotonicOk && countsOk;

  std::cout << "Eventos escritos: " << totalLines << "/" << expectedCount
            << '\n';

  std::cout << "DP7 EVENT LOGGER TEST: " << (passed ? "OK" : "ERROR") << '\n';

  return passed ? 0 : 1;
}
