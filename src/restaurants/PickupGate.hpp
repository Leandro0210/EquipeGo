
#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

class PickupGate {
private:
  const std::size_t capacity_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;

  std::size_t occupied_ = 0;
  std::size_t waiting_ = 0;
  std::size_t peak_ = 0;

  std::uint64_t issuedTicket_ = 0;
  std::uint64_t nextToEnter_ = 0;

  bool stopping_ = false;

  void releaseSlot() noexcept {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      --occupied_;
    }

    cv_.notify_all();
  }

public:

  class Permit {
    friend class PickupGate;

    PickupGate *gate_ = nullptr;

    explicit Permit(PickupGate *gate) noexcept : gate_(gate) {}

  public:
    Permit(const Permit &) = delete;
    Permit &operator=(const Permit &) = delete;

    Permit(Permit &&other) noexcept
        : gate_(std::exchange(other.gate_, nullptr)) {}

    Permit &operator=(Permit &&other) noexcept {
      if (this != &other) {
        release();

        gate_ = std::exchange(other.gate_, nullptr);
      }

      return *this;
    }

    ~Permit() { release(); }

    void release() noexcept {
      if (gate_ != nullptr) {
        PickupGate *gate = std::exchange(gate_, nullptr);

        gate->releaseSlot();
      }
    }
  };


  explicit PickupGate(std::size_t pickupSlots) : capacity_(pickupSlots) {

    if (pickupSlots == 0) {
      throw std::invalid_argument("pickupSlots debe ser mayor que cero");
    }
  }

  PickupGate(const PickupGate &) = delete;
  PickupGate &operator=(const PickupGate &) = delete;


  std::optional<Permit> acquire() {
    std::unique_lock<std::mutex> lock(mutex_);

    if (stopping_) {
      return std::nullopt;
    }

    const std::uint64_t myTicket = issuedTicket_++;

    ++waiting_;
    cv_.notify_all();

    cv_.wait(lock, [this, myTicket] {
      return stopping_ || (myTicket == nextToEnter_ && occupied_ < capacity_);
    });

    --waiting_;

    if (stopping_) {
      return std::nullopt;
    }

    ++nextToEnter_;
    ++occupied_;

    peak_ = std::max(peak_, occupied_);

    lock.unlock();
    cv_.notify_all();

    return Permit(this);
  }


  void requestStop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }

    cv_.notify_all();
  }


  std::size_t occupied() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return occupied_;
  }

  std::size_t peakUsage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return peak_;
  }

  std::size_t waitingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return waiting_;
  }

  bool waitForWaitingCount(std::size_t atLeast,
                           std::chrono::steady_clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex_);

    return cv_.wait_until(lock, deadline,
                          [this, atLeast] { return waiting_ >= atLeast; });
  }


  bool waitForOccupiedCount(std::size_t atLeast,
                            std::chrono::steady_clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex_);

    return cv_.wait_until(lock, deadline,
                          [this, atLeast] { return occupied_ >= atLeast; });
  }
};
