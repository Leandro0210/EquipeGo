
#pragma once

#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

class EventLogger {
private:
  // Archivo de eventos JSONL.
  std::ofstream fileStream;

  // Protege la escritura y el historial
  // de marcas de tiempo.
  std::mutex logMutex;

  // Ultimo tiempo registrado por pedido.
  std::unordered_map<std::string, long long> lastOrderTimeMs;

public:
  explicit EventLogger(const std::string &filename);

  ~EventLogger() = default;

  EventLogger(const EventLogger &) = delete;
  EventLogger &operator=(const EventLogger &) = delete;

  // Imprime exactamente la linea obligatoria.
  void logStart();

  // Registra un objeto JSON completo por linea.
  // Es seguro para llamadas concurrentes.
  //
  // Valida:
  // - objeto JSON;
  // - nombre de evento no vacio;
  // - tiempo simulado no negativo;
  // - ID de pedido valido, cuando existe.
  //
  // Mantiene tiempos no decrecientes por pedido.
  void logEvent(nlohmann::json eventJson, long long simulatedTimeMs);
};
