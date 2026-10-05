#include "OrderBook.hpp"

#include <utility>

OrderBook::OrderBook(std::size_t maxPending) : maxPending(maxPending) {}

bool OrderBook::tryAdd(Order &&order) {
  std::lock_guard<std::mutex> lock(bookMutex);

  if (orders.size() >= maxPending) {
    return false;
  }

  orders.push_back(std::move(order));
  return true;
}

std::size_t OrderBook::size() const {
  std::lock_guard<std::mutex> lock(bookMutex);
  return orders.size();
}

bool OrderBook::full() const {
  std::lock_guard<std::mutex> lock(bookMutex);
  return orders.size() >= maxPending;
}