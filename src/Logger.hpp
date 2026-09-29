#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>


class EventLogger {
private:
  std::ofstream fileStream;
  std::mutex logMutex; // El candado de exclusión mutua (Carpeta 06)

public:
  // Constructor y Destructor
  EventLogger(const std::string &filename);
  ~EventLogger();

  // Imprime la frase de inicio obligatoria
  void logStart();

  // Escribe un evento en el JSON de forma segura contra hilos
  void logEvent(nlohmann::json eventJson, long long simulatedTimeMs);
};

#endif // LOGGER_HPP