#pragma once

#include <string>
#include <utility>

struct Order {
  std::string orderId;
  std::string restauranteId;
  std::string deliveryNodeId;
  long long time;

  Order(std::string id, std::string rest, std::string node, long long t)
      : orderId(std::move(id)), restauranteId(std::move(rest)),
        deliveryNodeId(std::move(node)), time(t) {}

  // Un pedido no puede copiarse.
  Order(const Order &other) = delete;
  Order &operator=(const Order &other) = delete;

  // Un pedido sí puede transferirse a otro propietario.
  Order(Order &&other) noexcept = default;
  Order &operator=(Order &&other) noexcept = default;
};
