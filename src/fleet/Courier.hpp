
#pragma once

#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/CapacitySignal.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"
#include "restaurants/RestaurantManager.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

enum class CourierState { Idle, Moving, Broken };

class Courier {
private:
  std::string id;

  const SimulationConfig &config;
  const RoadNetwork &roadNetwork;
  SimulationClock &clock;

  std::shared_ptr<CapacitySignal> capacitySignal;

  // Compartido con todas las motos de Fleet.
  std::shared_ptr<RestaurantManager> restaurantManager;

  mutable std::mutex stateMutex;
  std::condition_variable workCv;

  std::string currentNode;
  CourierState state = CourierState::Idle;

  std::optional<std::string> destination;
  bool stopRequested = false;

  std::deque<Order> assignedOrders;
  std::optional<Order> activeOrder;
  std::deque<Order> blockedOrders;
  std::deque<Order> finishedOrders;

  bool travelTo(const std::string &targetNode);
  bool processActiveOrder();

  bool hasOrderIdLocked(const std::string &orderId) const;

  void finishPendingLocked();

public:
  // Parametros opcionales para conservar
  // compatibilidad con pruebas anteriores.
  Courier(std::string id, const SimulationConfig &config,
          const RoadNetwork &roadNetwork, SimulationClock &clock,
          std::shared_ptr<CapacitySignal> capacitySignal = nullptr,
          std::shared_ptr<RestaurantManager> restaurantManager = nullptr);

  void run();

  bool assignDestination(const std::string &nodeId);
  bool tryAssignOrder(Order &&order);

  std::size_t getReservedCount() const;
  std::size_t getFinishedCount() const;

  std::optional<OrderState>
  getAssignedOrderState(const std::string &orderId) const;

  void requestStop();

  std::string getId() const;
  std::string getCurrentNode() const;
  CourierState getState() const;

  // Recuperar pedidos despues de Fleet::join().
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
