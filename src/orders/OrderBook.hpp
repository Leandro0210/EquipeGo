
#pragma once

#include "orders/Order.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

class CapacitySignal;

class OrderBook {
private:
  std::size_t maxPending;
  std::deque<Order> orders;

  mutable std::mutex bookMutex;
  std::condition_variable ordersCv;

  bool stopRequested = false;

  std::size_t inFlight = 0;

  std::shared_ptr<CapacitySignal> wakeSignal;

public:
  explicit OrderBook(std::size_t maxPending);

  void setNotificationSignal(std::shared_ptr<CapacitySignal> signal);

  bool tryAdd(Order &&order);

  std::optional<Order> waitAndTake();

  std::optional<Order> tryTake();

  void completeTaken();

  void requestStop();

  std::vector<Order> takeRemaining();

  std::size_t size() const;
  bool full() const;
};
