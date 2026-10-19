
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

class CapacitySignal {
private:
  mutable std::mutex signalMutex;
  std::condition_variable signalCv;
  std::uint64_t generation = 0;

public:
  std::uint64_t version() const {
    std::lock_guard<std::mutex> lock(signalMutex);
    return generation;
  }

  void notifyChange() {
    {
      std::lock_guard<std::mutex> lock(signalMutex);
      ++generation;
    }

    signalCv.notify_all();
  }

  bool waitUntilChange(std::uint64_t previousVersion,
                       std::chrono::steady_clock::time_point deadline,
                       const std::atomic<bool> &stopRequested) {
    std::unique_lock<std::mutex> lock(signalMutex);

    signalCv.wait_until(
        lock, deadline, [this, previousVersion, &stopRequested] {
          return generation != previousVersion || stopRequested.load();
        });

    return generation != previousVersion && !stopRequested.load();
  }

  bool waitForChange(std::uint64_t previousVersion,
                     const std::atomic<bool> &stopRequested) {
    std::unique_lock<std::mutex> lock(signalMutex);

    signalCv.wait(lock, [this, previousVersion, &stopRequested] {
      return generation != previousVersion || stopRequested.load();
    });

    return generation != previousVersion && !stopRequested.load();
  }
};
