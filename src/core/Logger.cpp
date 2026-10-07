#include "core/Logger.hpp"

#include <iostream>
#include <stdexcept>

EventLogger::EventLogger(const std::string &filename)
    : fileStream(filename, std::ios::out | std::ios::trunc) {

  if (!fileStream.is_open()) {
    throw std::runtime_error("No se pudo abrir el archivo de log: " + filename);
  }
}

void EventLogger::logStart() { std::cout << "Simulation has started...\n"; }

void EventLogger::logEvent(nlohmann::json eventJson,
                           long long simulatedTimeMs) {
  std::lock_guard<std::mutex> lock(logMutex);

  eventJson["t"] = simulatedTimeMs;
  fileStream << eventJson.dump() << '\n';
}
