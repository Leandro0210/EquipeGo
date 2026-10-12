#pragma once

#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

class EventLogger {
private:
  std::ofstream fileStream;
  std::mutex logMutex;
  std::unordered_map<std::string, long long> lastOrderTimeMs;

  // En pruebas se mantiene desactivado.
  // En delivery_sim se activa para cumplir DP7.
  bool emitToStdout = false;

public:
  explicit EventLogger(const std::string &filename, bool emitToStdout = false);

  ~EventLogger() = default;

  EventLogger(const EventLogger &) = delete;
  EventLogger &operator=(const EventLogger &) = delete;

  void logStart();

  void logEvent(nlohmann::json eventJson, long long simulatedTimeMs);

  // Esperar y volcar todos los eventos al disco.
  void flush();
};