
#include "orders/OrderBook.hpp"

#include "fleet/CapacitySignal.hpp"

#include <stdexcept>
#include <utility>


OrderBook::OrderBook(std::size_t maxPending) : maxPending(maxPending) {}


void OrderBook::setNotificationSignal(std::shared_ptr<CapacitySignal> signal) {
  std::lock_guard<std::mutex> lock(bookMutex);
  wakeSignal = std::move(signal);
}


bool OrderBook::tryAdd(Order &&order) {

  std::shared_ptr<CapacitySignal> signal;

  {
    std::lock_guard<std::mutex> lock(bookMutex);

    if (stopRequested || orders.size() + inFlight >= maxPending) {
      return false;
    }

    orders.push_back(std::move(order));

    signal = wakeSignal;
  }

  ordersCv.notify_one();

  if (signal) {
    signal->notifyChange();
  }

  return true;
}


std::optional<Order> OrderBook::waitAndTake() {

  std::unique_lock<std::mutex> lock(bookMutex);

  ordersCv.wait(lock, [this] { return stopRequested || !orders.empty(); });

  if (stopRequested) {
    return std::nullopt;
  }

  Order order = std::move(orders.front());
  orders.pop_front();

  ++inFlight;

  return std::optional<Order>(std::move(order));
}


std::optional<Order> OrderBook::tryTake() {

  std::lock_guard<std::mutex> lock(bookMutex);

  if (stopRequested || orders.empty()) {
    return std::nullopt;
  }

  Order order = std::move(orders.front());
  orders.pop_front();

  ++inFlight;

  return std::optional<Order>(std::move(order));
}


void OrderBook::completeTaken() {

  std::lock_guard<std::mutex> lock(bookMutex);

  if (inFlight == 0) {
    throw std::logic_error("No hay pedidos extraidos para finalizar");
  }

  --inFlight;
}


void OrderBook::requestStop() {

  std::shared_ptr<CapacitySignal> signal;

  {
    std::lock_guard<std::mutex> lock(bookMutex);

    stopRequested = true;
    signal = wakeSignal;
  }

  ordersCv.notify_all();

  if (signal) {
    signal->notifyChange();
  }
}


std::vector<Order> OrderBook::takeRemaining() {

  std::vector<Order> remaining;

  {
    std::lock_guard<std::mutex> lock(bookMutex);

    if (!stopRequested) {
      throw std::logic_error(
          "Debe detenerse OrderBook antes de recuperar pedidos");
    }

    if (inFlight != 0) {
      throw std::logic_error("Existen pedidos todavia en manos del Dispatcher");
    }

    remaining.reserve(orders.size());

    while (!orders.empty()) {
      remaining.push_back(std::move(orders.front()));

      orders.pop_front();
    }
  }

  return remaining;
}


std::size_t OrderBook::size() const {

  std::lock_guard<std::mutex> lock(bookMutex);

  return orders.size();
}

bool OrderBook::full() const {

  std::lock_guard<std::mutex> lock(bookMutex);

  return orders.size() + inFlight >= maxPending;
}
