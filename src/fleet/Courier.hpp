
#pragma once

#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/CapacitySignal.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

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

  mutable std::mutex stateMutex;
  std::condition_variable workCv;

  std::string currentNode;
  CourierState state = CourierState::Idle;

  std::optional<std::string> destination;
  bool stopRequested = false;

  // Pedidos esperando procesamiento.
  std::deque<Order> assignedOrders;

  // Pedido actualmente en movimiento.
  std::optional<Order> activeOrder;

  // Pedidos que no pudieron completar su ruta.
  std::deque<Order> blockedOrders;

  // Pedidos entregados o pendientes al detener.
  std::deque<Order> finishedOrders;

  bool travelTo(const std::string &targetNode);
  bool processActiveOrder();

  bool hasOrderIdLocked(const std::string &orderId) const;

  void finishPendingLocked();

public:
  Courier(std::string id, const SimulationConfig &config,
          const RoadNetwork &roadNetwork, SimulationClock &clock,
          std::shared_ptr<CapacitySignal> capacitySignal = nullptr);

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

  // =================================================
  // NUEVO: RECUPERAR RESULTADOS FINALES
  // =================================================

  // Fleet lo llamara despues de join().
  // Cada pedido se mueve una sola vez.
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
