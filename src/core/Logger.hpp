#pragma once

#include <cstddef>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

struct LiveEventStats {
  std::size_t created = 0;
  std::size_t delivered = 0;
  std::size_t rejected = 0;
  std::size_t breakdowns = 0;
  std::size_t reassigned = 0;

  std::size_t breaks = 0;

  std::size_t inProgress() const noexcept {
    return created >= delivered + rejected ? created - delivered - rejected : 0;
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

  LiveEventStats snapshotStats() const;
};
