#pragma once

#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>

class EventLogger {
private:
  std::ofstream fileStream;
  std::mutex logMutex;

public:
  explicit EventLogger(const std::string &filename);
  ~EventLogger() = default;

  // Mensaje obligatorio al iniciar la simulación.
  void logStart();

  // Escribe un evento JSON de forma segura entre múltiples hilos.
  void logEvent(nlohmann::json eventJson, long long simulatedTimeMs);
};
