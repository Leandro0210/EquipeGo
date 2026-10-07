#pragma once

#include "Order.hpp"

#include <cstddef>
#include <deque>
#include <mutex>

class OrderBook {
private:
  std::size_t maxPending;
  std::deque<Order> orders;
  mutable std::mutex bookMutex;

public:
  explicit OrderBook(std::size_t maxPending);

  // Intenta agregar un pedido.
  // Retorna false si el libro ya alcanzó su capacidad máxima.
  bool tryAdd(Order &&order);

  std::size_t size() const;
  bool full() const;
};