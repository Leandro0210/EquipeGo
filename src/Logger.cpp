#include "Logger.hpp"
#include <iostream>

EventLogger::EventLogger(const std::string &filename) {
  // std::ios::trunc asegura que si corres el programa 2 veces, el log viejo se
  // borra
  fileStream.open(filename, std::ios::out | std::ios::trunc);
  if (!fileStream.is_open()) {
    std::cerr << "Error: No se pudo abrir el archivo de log " << filename
              << "\n";
  }
}

EventLogger::~EventLogger() {
  if (fileStream.is_open()) {
    fileStream.close();
  }
}

void EventLogger::logStart() {
  // La rúbrica DP.7 y 6.1 exigen exactamente esta línea en stdout, sin
  // traducir[cite: 1]
  std::cout << "Simulation has started...\n";
}

void EventLogger::logEvent(nlohmann::json eventJson,
                           long long simulatedTimeMs) {
  // 1. Bloqueo de ámbito: El hilo toma la llave. Si otro hilo intenta entrar,
  // se queda esperando.
  std::lock_guard<std::mutex> lock(logMutex);

  // 2. Todo evento debe tener la propiedad "t"[cite: 1]
  eventJson["t"] = simulatedTimeMs;

  // 3. Escribimos en el archivo: .dump() convierte el JSON a texto y agregamos
  // el salto de línea
  if (fileStream.is_open()) {
    fileStream << eventJson.dump() << "\n";
    fileStream.flush(); // Fuerza a guardar en el disco duro de inmediato
  }

  // 4. ¡Magia de C++! Al llegar al final de las llaves, 'lock' se destruye
  // y el mutex se libera automáticamente para el siguiente hilo.
}