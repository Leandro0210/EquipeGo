#pragma once

#include <cstddef>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

// DP8.5: copia coherente de contadores publicados mediante JSONL.
// 'pending' en vivo se calcula como creados - entregados - rechazados:
// incluye pedidos en cola, asignados o en viaje, no solo OrderBook.
struct LiveEventStats {
  std::size_t created = 0;
  std::size_t delivered = 0;
  std::size_t rejected = 0;
  std::size_t breakdowns = 0;
  std::size_t reassigned = 0;

  std::size_t inProgress() const noexcept {
    return created >= delivered + rejected
               ? created - delivered - rejected
               : 0;
  }
};

class EventLogger {
private:
  std::ofstream fileStream;
  mutable std::mutex logMutex;
  std::unordered_map<std::string, long long> lastOrderTimeMs;
  bool emitToStdout = false;
  LiveEventStats liveStats;

public:
  explicit EventLogger(const std::string &filename, bool emitToStdout = false);
  ~EventLogger() = default;

  EventLogger(const EventLogger &) = delete;
  EventLogger &operator=(const EventLogger &) = delete;

  void logStart();
  void logEvent(nlohmann::json eventJson, long long simulatedTimeMs);
  void flush();

  // La interfaz grafica nunca accede directamente al stream ni a los mapas.
  LiveEventStats snapshotStats() const;
};
