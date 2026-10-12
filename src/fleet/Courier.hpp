#pragma once

#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/CapacitySignal.hpp"
#include "incidents/BreakdownPolicy.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"
#include "restaurants/RestaurantManager.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class EventLogger;

enum class CourierState { Idle, Moving, Broken };

// DP8.4: solo valores copiados; no exponemos los pedidos mutables.
struct CourierOrderSnapshot {
  std::string orderId;
  std::string restauranteId;
  std::string deliveryNodeId;
  OrderState state = OrderState::Created;
};

struct CourierSnapshot {
  std::string id;
  std::string currentNode;
  CourierState state = CourierState::Idle;
  std::optional<CourierOrderSnapshot> active;
  std::optional<CourierOrderSnapshot> nextOrder;
  std::size_t reservedCount = 0;
};

class Courier {
private:
  std::string id;

  const SimulationConfig &config;
  const RoadNetwork &roadNetwork;
  SimulationClock &clock;

  std::shared_ptr<CapacitySignal> capacitySignal;
  std::shared_ptr<RestaurantManager> restaurantManager;

  // DP7: referencia no propietaria al logger.
  // El logger debe vivir mas que el repartidor.
  EventLogger *eventLogger = nullptr;

  mutable std::mutex stateMutex;
  std::condition_variable workCv;

  std::string currentNode;
  CourierState state = CourierState::Idle;
  bool everBroken = false;

  std::optional<std::string> destination;
  bool stopRequested = false;

  std::deque<Order> assignedOrders;
  std::optional<Order> activeOrder;
  std::deque<Order> blockedOrders;
  std::deque<Order> finishedOrders;

  bool travelTo(const std::string &targetNode,
                const incidents::BreakdownPlan *breakdown = nullptr);

  bool processActiveOrder();

  bool hasOrderIdLocked(const std::string &orderId) const;

  void finishPendingLocked();

public:
  Courier(std::string id, const SimulationConfig &config,
          const RoadNetwork &roadNetwork, SimulationClock &clock,
          std::shared_ptr<CapacitySignal> capacitySignal = nullptr,
          std::shared_ptr<RestaurantManager> restaurantManager = nullptr);

  void setEventLogger(EventLogger *logger);
  void run();

  bool assignDestination(const std::string &nodeId);

  bool tryAssignOrder(Order &&order,
                      const std::function<void()> &onAssigned = {});

  bool transferAllTo(Courier &recipient);

  std::size_t getReservedCount() const;
  std::size_t getFinishedCount() const;

  std::optional<OrderState>
  getAssignedOrderState(const std::string &orderId) const;

  void requestStop();

  std::string getId() const;
  std::string getCurrentNode() const;

  CourierState getState() const;
  bool hasBrokenDown() const;

  // DP8.4: leer identificador, nodo, fase y pedidos bajo UN mutex.
  // Evita mezclar informacion de instantes diferentes dentro
  // de la misma moto y nunca devuelve referencias a Order.
  CourierSnapshot getSnapshot() const {
    std::lock_guard<std::mutex> lock(stateMutex);

    CourierSnapshot snapshot;
    snapshot.id = id;
    snapshot.currentNode = currentNode;
    snapshot.state = state;
    snapshot.reservedCount =
        assignedOrders.size() + blockedOrders.size() +
        (activeOrder ? 1u : 0u);

    const auto copyOrder = [](const Order &order) {
      CourierOrderSnapshot data;
      data.orderId = order.orderId;
      data.restauranteId = order.restauranteId;
      data.deliveryNodeId = order.deliveryNodeId;
      data.state = order.getState();
      return data;
    };

    if (activeOrder) {
      snapshot.active = copyOrder(*activeOrder);
    }
    if (!assignedOrders.empty()) {
      snapshot.nextOrder = copyOrder(assignedOrders.front());
    } else if (!blockedOrders.empty()) {
      snapshot.nextOrder = copyOrder(blockedOrders.front());
    }
    return snapshot;
  }

  std::vector<Order> takeFinishedOrders() {
    std::lock_guard<std::mutex> lock(stateMutex);

    std::vector<Order> results;
    results.reserve(finishedOrders.size());

    while (!finishedOrders.empty()) {
      results.push_back(std::move(finishedOrders.front()));
      finishedOrders.pop_front();
    }
    return results;
  }
};
