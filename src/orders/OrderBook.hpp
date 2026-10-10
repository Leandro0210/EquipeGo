
#pragma once

#include "orders/Order.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

class OrderBook {
private:
  std::size_t maxPending;
  std::deque<Order> orders;

  // Protege el acceso a la cola compartida.
  mutable std::mutex bookMutex;

  // Despierta a un consumidor cuando llega un pedido.
  std::condition_variable ordersCv;

  bool stopRequested = false;

public:
  explicit OrderBook(std::size_t maxPending);

  // Productor: intenta insertar un pedido.
  bool tryAdd(Order &&order);

  // Consumidor: espera un pedido y lo extrae en orden FIFO.
  // Devuelve nullopt cuando se solicita detener el libro.
  std::optional<Order> waitAndTake();

  // Detiene nuevas inserciones y despierta a los consumidores.
  // Conserva los pedidos que siguen pendientes.
  void requestStop();

  std::size_t size() const;
  bool full() const;
};
