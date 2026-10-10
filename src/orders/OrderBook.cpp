
#include "orders/OrderBook.hpp"

#include <utility>

OrderBook::OrderBook(std::size_t maxPending) : maxPending(maxPending) {}

// =====================================================
// PRODUCTOR: INSERTAR PEDIDO
// =====================================================

bool OrderBook::tryAdd(Order &&order) {
  {
    std::lock_guard<std::mutex> lock(bookMutex);

    if (stopRequested || orders.size() >= maxPending) {
      return false;
    }

    orders.push_back(std::move(order));
  }

  // Avisar a un consumidor que hay un nuevo pedido.
  ordersCv.notify_one();

  return true;
}

// =====================================================
// CONSUMIDOR: ESPERAR Y EXTRAER PEDIDO
// =====================================================

std::optional<Order> OrderBook::waitAndTake() {
  std::unique_lock<std::mutex> lock(bookMutex);

  ordersCv.wait(lock, [this]() { return !orders.empty() || stopRequested; });

  // Al detener la simulacion, conservar los
  // pedidos restantes para contabilizarlos despues.
  if (stopRequested) {
    return std::nullopt;
  }

  // FIFO: extraer el pedido mas antiguo.
  Order order = std::move(orders.front());
  orders.pop_front();

  return std::optional<Order>(std::move(order));
}

// =====================================================
// DETENER LIBRO DE PEDIDOS
// =====================================================

void OrderBook::requestStop() {
  {
    std::lock_guard<std::mutex> lock(bookMutex);
    stopRequested = true;
  }

  // Despertar a todos los consumidores bloqueados.
  ordersCv.notify_all();
}

// =====================================================
// CONSULTAS PROTEGIDAS POR MUTEX
// =====================================================

std::size_t OrderBook::size() const {
  std::lock_guard<std::mutex> lock(bookMutex);
  return orders.size();
}

bool OrderBook::full() const {
  std::lock_guard<std::mutex> lock(bookMutex);
  return orders.size() >= maxPending;
}
