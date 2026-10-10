
#include "orders/OrderBook.hpp"

#include <stdexcept>
#include <utility>

// =====================================================
// CONSTRUCTOR
// =====================================================

OrderBook::OrderBook(std::size_t maxPending) : maxPending(maxPending) {}

// =====================================================
// INSERTAR PEDIDO
// =====================================================

bool OrderBook::tryAdd(Order &&order) {
  {
    std::lock_guard<std::mutex> lock(bookMutex);

    if (stopRequested || orders.size() >= maxPending) {
      return false;
    }

    orders.push_back(std::move(order));
  }

  ordersCv.notify_one();
  return true;
}

// =====================================================
// ESPERAR Y EXTRAER PEDIDO
// =====================================================

std::optional<Order> OrderBook::waitAndTake() {
  std::unique_lock<std::mutex> lock(bookMutex);

  ordersCv.wait(lock, [this] { return !orders.empty() || stopRequested; });

  // Conservar los pedidos que siguen en cola
  // cuando la simulacion termina.
  if (stopRequested) {
    return std::nullopt;
  }

  Order order = std::move(orders.front());
  orders.pop_front();

  return std::optional<Order>(std::move(order));
}

// =====================================================
// DETENER ORDERBOOK
// =====================================================

void OrderBook::requestStop() {
  {
    std::lock_guard<std::mutex> lock(bookMutex);
    stopRequested = true;
  }

  ordersCv.notify_all();
}

// =====================================================
// RECUPERAR PEDIDOS AL FINALIZAR
// =====================================================

std::vector<Order> OrderBook::takeRemaining() {

  std::vector<Order> remaining;

  {
    std::lock_guard<std::mutex> lock(bookMutex);

    if (!stopRequested) {
      throw std::logic_error(
          "Debe detenerse OrderBook antes de recuperar pedidos");
    }

    remaining.reserve(orders.size());

    // Transferir cada pedido sin copiarlo.
    while (!orders.empty()) {
      remaining.push_back(std::move(orders.front()));

      orders.pop_front();
    }
  }

  return remaining;
}

// =====================================================
// CONSULTAS SEGURAS
// =====================================================

std::size_t OrderBook::size() const {
  std::lock_guard<std::mutex> lock(bookMutex);
  return orders.size();
}

bool OrderBook::full() const {
  std::lock_guard<std::mutex> lock(bookMutex);
  return orders.size() >= maxPending;
}
