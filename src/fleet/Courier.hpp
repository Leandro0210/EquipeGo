
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

  // Protege la posicion, el estado y los pedidos.
  mutable std::mutex stateMutex;
  std::condition_variable workCv;

  std::string currentNode;
  CourierState state = CourierState::Idle;

  std::optional<std::string> destination;
  bool stopRequested = false;

  // Pedidos aceptados con capacidad reservada.
  // Se procesaran en un siguiente miniavance.
  std::deque<Order> assignedOrders;

  bool travelTo(const std::string &targetNode);

public:
  Courier(std::string id, const SimulationConfig &config,
          const RoadNetwork &roadNetwork, SimulationClock &clock);

  void run();

  // Comando temporal de navegacion utilizado en pruebas.
  bool assignDestination(const std::string &nodeId);

  // Reservar capacidad y aceptar un pedido.
  // Devuelve false si no hay espacio o el pedido es invalido.
  bool tryAssignOrder(Order &&order);

  // Consultas seguras desde otros hilos.
  std::size_t getReservedCount() const;

  std::optional<OrderState>
  getAssignedOrderState(const std::string &orderId) const;

  void requestStop();

  std::string getId() const;
  std::string getCurrentNode() const;
  CourierState getState() const;
};
