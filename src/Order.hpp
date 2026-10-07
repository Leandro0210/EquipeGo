#pragma once

#include <string>
#include <utility>

enum class OrderState {
  Created,
  Assigned,
  PickedUp,
  Delivered,
  Rejected,
  Pending
};

struct Order {
  std::string orderId;
  std::string restauranteId;
  std::string deliveryNodeId;
  long long time;

private:
  OrderState state = OrderState::Created;

public:
  Order(std::string id, std::string rest, std::string node, long long t)
      : orderId(std::move(id)), restauranteId(std::move(rest)),
        deliveryNodeId(std::move(node)), time(t) {}

  // Un pedido no puede copiarse.
  Order(const Order &other) = delete;
  Order &operator=(const Order &other) = delete;

  // Un pedido sí puede transferirse.
  Order(Order &&other) noexcept = default;
  Order &operator=(Order &&other) noexcept = default;

  OrderState getState() const noexcept { return state; }

  bool isFinal() const noexcept {
    return state == OrderState::Delivered || state == OrderState::Rejected ||
           state == OrderState::Pending;
  }

  bool transitionTo(OrderState newState) noexcept {
    bool validTransition = false;

    switch (state) {
    case OrderState::Created:
      validTransition = newState == OrderState::Assigned ||
                        newState == OrderState::Rejected ||
                        newState == OrderState::Pending;
      break;

    case OrderState::Assigned:
      validTransition =
          newState == OrderState::PickedUp || newState == OrderState::Pending;
      break;

    case OrderState::PickedUp:
      validTransition =
          newState == OrderState::Delivered || newState == OrderState::Pending;
      break;

    case OrderState::Delivered:
    case OrderState::Rejected:
    case OrderState::Pending:
      validTransition = false;
      break;
    }

    if (!validTransition) {
      return false;
    }

    state = newState;
    return true;
  }
};