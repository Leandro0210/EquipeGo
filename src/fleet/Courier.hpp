
#pragma once

#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

enum class CourierState { Idle, Moving, Broken };

class Courier {
private:
  std::string id;

  const SimulationConfig &config;
  const RoadNetwork &roadNetwork;
  SimulationClock &clock;

  mutable std::mutex stateMutex;
  std::condition_variable workCv;

  std::string currentNode;
  CourierState state = CourierState::Idle;

  // Comando temporal de navegacion manual.
  std::optional<std::string> destination;
  bool stopRequested = false;

  // Pedidos aceptados con espacio reservado.
  std::deque<Order> assignedOrders;

  // Pedido que se esta procesando.
  std::optional<Order> activeOrder;

  // Pedidos cuyo recorrido no pudo completarse.
  std::deque<Order> blockedOrders;

  // Pedidos entregados o pendientes al finalizar.
  std::deque<Order> finishedOrders;

  bool travelTo(const std::string &targetNode);
  bool processActiveOrder();

  // Estas funciones requieren stateMutex adquirido.
  bool hasOrderIdLocked(const std::string &orderId) const;
  void finishPendingLocked();

public:
  Courier(std::string id, const SimulationConfig &config,
          const RoadNetwork &roadNetwork, SimulationClock &clock);

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
};
