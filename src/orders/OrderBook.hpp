
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

  // Pedidos extraidos pero todavia no resueltos.
  std::size_t inFlight = 0;

  // Notifica al Dispatcher cuando llega un pedido.
  std::shared_ptr<CapacitySignal> wakeSignal;

public:
  explicit OrderBook(std::size_t maxPending);

  // Vincular el canal de notificaciones.
  void setNotificationSignal(std::shared_ptr<CapacitySignal> signal);

  // Productor: agregar un pedido.
  bool tryAdd(Order &&order);

  // Consumidor: esperar un pedido (bloqueante).
  std::optional<Order> waitAndTake();

  // Consumidor: intentar sacar un pedido sin esperar.
  std::optional<Order> tryTake();

  // Avisar que un pedido extraido ya fue resuelto:
  // Assigned, Rejected o Pending.
  void completeTaken();

  // Detener el libro.
  void requestStop();

  // Recuperar pedidos al finalizar la simulacion.
  std::vector<Order> takeRemaining();

  std::size_t size() const;
  bool full() const;
};
