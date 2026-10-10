
#pragma once

#include "orders/Order.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

class OrderBook {
private:
  std::size_t maxPending;
  std::deque<Order> orders;

  // Protege el acceso concurrente a la cola.
  mutable std::mutex bookMutex;

  // Despierta al Dispatcher cuando hay pedidos.
  std::condition_variable ordersCv;

  bool stopRequested = false;

public:
  explicit OrderBook(std::size_t maxPending);

  // Productor: insertar un nuevo pedido.
  bool tryAdd(Order &&order);

  // Consumidor: esperar y extraer en orden FIFO.
  std::optional<Order> waitAndTake();

  // Detener nuevas inserciones y despertar consumidores.
  void requestStop();

  // Recuperar todos los pedidos restantes.
  // Utilizar despues de detener el OrderBook
  // y finalizar el hilo consumidor.
  std::vector<Order> takeRemaining();

  std::size_t size() const;
  bool full() const;
};
