
#include "fleet/Courier.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

Courier::Courier(std::string id, const SimulationConfig &config,
                 const RoadNetwork &roadNetwork, SimulationClock &clock)
    : id(std::move(id)), config(config), roadNetwork(roadNetwork), clock(clock),
      currentNode(config.fleet.startNode) {}

// =====================================================
// HILO DEL REPARTIDOR
// =====================================================

void Courier::run() {
  while (true) {
    std::unique_lock<std::mutex> lock(stateMutex);

    workCv.wait(lock,
                [this]() { return destination.has_value() || stopRequested; });

    if (stopRequested) {
      break;
    }

    const std::string target = *destination;
    destination.reset();

    state = CourierState::Moving;

    lock.unlock();

    travelTo(target);

    lock.lock();

    if (stopRequested) {
      break;
    }

    state = CourierState::Idle;
  }
}

// =====================================================
// MOVIMIENTO POR CALLES
// =====================================================

bool Courier::travelTo(const std::string &targetNode) {
  std::string origin;

  {
    std::lock_guard<std::mutex> lock(stateMutex);
    origin = currentNode;
  }

  const auto route = roadNetwork.shortestRoute(origin, targetNode);

  if (!route.has_value()) {
    return false;
  }

  const double speedMetersPerSecond = config.fleet.speedKmh / 3.6;

  for (const RouteSegment &segment : route->segments) {
    const double simulatedSeconds =
        segment.distanceMeters / speedMetersPerSecond;

    const double realSeconds = simulatedSeconds / config.simulation.timeScale;

    std::unique_lock<std::mutex> lock(stateMutex);

    const bool interrupted =
        workCv.wait_for(lock, std::chrono::duration<double>(realSeconds),
                        [this]() { return stopRequested; });

    if (interrupted) {
      return false;
    }

    currentNode = segment.to;
  }

  return true;
}

// =====================================================
// DESTINO DE NAVEGACION
// =====================================================

bool Courier::assignDestination(const std::string &nodeId) {
  {
    std::lock_guard<std::mutex> lock(stateMutex);

    if (stopRequested || state != CourierState::Idle ||
        destination.has_value()) {
      return false;
    }

    destination = nodeId;
  }

  workCv.notify_one();
  return true;
}

// =====================================================
// ASIGNACION CONCURRENTE DE PEDIDOS
// =====================================================

bool Courier::tryAssignOrder(Order &&order) {
  std::lock_guard<std::mutex> lock(stateMutex);

  // No aceptar pedidos durante una parada o averia.
  if (stopRequested || state == CourierState::Broken) {
    return false;
  }

  const std::size_t capacity =
      static_cast<std::size_t>(config.fleet.bagCapacity);

  // Capacidad reservada: nunca exceder bagCapacity.
  if (assignedOrders.size() >= capacity) {
    return false;
  }

  // Solo se pueden asignar pedidos recien creados.
  if (order.getState() != OrderState::Created) {
    return false;
  }

  // Evitar el mismo identificador dos veces en esta moto.
  const bool duplicate =
      std::any_of(assignedOrders.begin(), assignedOrders.end(),
                  [&order](const Order &existing) {
                    return existing.orderId == order.orderId;
                  });

  if (duplicate) {
    return false;
  }

  // Validar transicion antes de transferir el pedido.
  if (!order.transitionTo(OrderState::Assigned)) {
    return false;
  }

  // La propiedad del pedido pasa a esta moto.
  assignedOrders.push_back(std::move(order));

  return true;
}

// =====================================================
// CONSULTAR CAPACIDAD RESERVADA
// =====================================================

std::size_t Courier::getReservedCount() const {
  std::lock_guard<std::mutex> lock(stateMutex);
  return assignedOrders.size();
}

// =====================================================
// CONSULTAR ESTADO DE UN PEDIDO
// =====================================================

std::optional<OrderState>
Courier::getAssignedOrderState(const std::string &orderId) const {

  std::lock_guard<std::mutex> lock(stateMutex);

  for (const Order &order : assignedOrders) {
    if (order.orderId == orderId) {
      return order.getState();
    }
  }

  return std::nullopt;
}

// =====================================================
// APAGADO
// =====================================================

void Courier::requestStop() {
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    stopRequested = true;
  }

  workCv.notify_all();
}

// =====================================================
// CONSULTAS
// =====================================================

std::string Courier::getId() const { return id; }

std::string Courier::getCurrentNode() const {
  std::lock_guard<std::mutex> lock(stateMutex);
  return currentNode;
}

CourierState Courier::getState() const {
  std::lock_guard<std::mutex> lock(stateMutex);
  return state;
}
