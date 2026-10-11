
#include "fleet/Courier.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

// =====================================================
// CONSTRUCTOR
// =====================================================

Courier::Courier(std::string id, const SimulationConfig &config,
                 const RoadNetwork &roadNetwork, SimulationClock &clock,
                 std::shared_ptr<CapacitySignal> capacitySignal,
                 std::shared_ptr<RestaurantManager> restaurantManager)
    : id(std::move(id)), config(config), roadNetwork(roadNetwork), clock(clock),
      capacitySignal(std::move(capacitySignal)),
      restaurantManager(std::move(restaurantManager)),
      currentNode(config.fleet.startNode) {}

// =====================================================
// HILO DEL REPARTIDOR
// =====================================================

void Courier::run() {
  while (true) {
    std::unique_lock<std::mutex> lock(stateMutex);

    workCv.wait(lock, [this] {
      return stopRequested || destination.has_value() ||
             !assignedOrders.empty();
    });

    if (stopRequested) {
      finishPendingLocked();
      state = CourierState::Idle;
      break;
    }

    // Movimiento manual de pruebas anteriores.
    if (destination) {
      const std::string target = *destination;
      destination.reset();

      state = CourierState::Moving;
      lock.unlock();

      travelTo(target);

      lock.lock();

      if (stopRequested) {
        finishPendingLocked();
        state = CourierState::Idle;
        break;
      }

      state = CourierState::Idle;
      continue;
    }

    // Tomar el siguiente pedido reservado.
    activeOrder.emplace(std::move(assignedOrders.front()));

    assignedOrders.pop_front();
    state = CourierState::Moving;

    lock.unlock();

    const bool delivered = processActiveOrder();

    lock.lock();

    if (stopRequested) {
      finishPendingLocked();
      state = CourierState::Idle;
      break;
    }

    if (!delivered && activeOrder) {
      blockedOrders.push_back(std::move(*activeOrder));
      activeOrder.reset();
    }

    state = CourierState::Idle;
  }
}

// =====================================================
// RECOGIDA Y ENTREGA
// =====================================================

bool Courier::processActiveOrder() {
  std::string restaurantId;
  std::string deliveryNode;

  {
    std::lock_guard<std::mutex> lock(stateMutex);

    if (!activeOrder) {
      return false;
    }

    restaurantId = activeOrder->restauranteId;
    deliveryNode = activeOrder->deliveryNodeId;
  }

  const auto restaurant = std::find_if(
      config.restaurants.begin(), config.restaurants.end(),
      [&restaurantId](const Restaurant &r) { return r.id == restaurantId; });

  if (restaurant == config.restaurants.end()) {
    return false;
  }

  // =============================================
  // ETAPA 1: VIAJAR AL RESTAURANTE
  // =============================================

  if (!travelTo(restaurant->nodeId)) {
    return false;
  }

  // =============================================
  // ETAPA 2: OBTENER CUPO FIFO
  // =============================================

  std::optional<PickupGate::Permit> pickupPermit;

  if (restaurantManager) {
    const auto gate = restaurantManager->gateFor(restaurantId);

    // Espera bloqueante hasta que:
    // - haya un cupo y sea nuestro turno
    // - o la flota solicite apagado
    pickupPermit = gate->acquire();

    if (!pickupPermit) {
      return false;
    }
  }

  // =============================================
  // ETAPA 3: RECOGER PEDIDO
  // =============================================

  {
    std::lock_guard<std::mutex> lock(stateMutex);

    if (stopRequested || !activeOrder ||
        !activeOrder->transitionTo(OrderState::PickedUp)) {
      return false;
    }
  }

  // El cupo se libera al completar la recogida.
  // En el siguiente miniavance agregaremos
  // el tiempo simulado de preparacion.
  pickupPermit.reset();

  // =============================================
  // ETAPA 4: VIAJAR AL CLIENTE
  // =============================================

  if (!travelTo(deliveryNode)) {
    return false;
  }

  // =============================================
  // ETAPA 5: ENTREGAR PEDIDO
  // =============================================

  {
    std::lock_guard<std::mutex> lock(stateMutex);

    if (stopRequested || !activeOrder ||
        !activeOrder->transitionTo(OrderState::Delivered)) {
      return false;
    }

    finishedOrders.push_back(std::move(*activeOrder));

    activeOrder.reset();
  }

  // Notificar capacidad liberada al Dispatcher.
  if (capacitySignal) {
    capacitySignal->notifyChange();
  }

  return true;
}

// =====================================================
// MOVIMIENTO SOBRE LAS CALLES
// =====================================================

bool Courier::travelTo(const std::string &targetNode) {
  std::string origin;

  {
    std::lock_guard<std::mutex> lock(stateMutex);
    origin = currentNode;
  }

  const auto route = roadNetwork.shortestRoute(origin, targetNode);

  if (!route) {
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
                        [this] { return stopRequested; });

    if (interrupted) {
      return false;
    }

    currentNode = segment.to;
  }

  return true;
}

// =====================================================
// DESTINO MANUAL
// =====================================================

bool Courier::assignDestination(const std::string &nodeId) {
  {
    std::lock_guard<std::mutex> lock(stateMutex);

    if (stopRequested || state != CourierState::Idle || destination ||
        !assignedOrders.empty()) {
      return false;
    }

    destination = nodeId;
  }

  workCv.notify_one();
  return true;
}

// =====================================================
// VERIFICAR PEDIDO DUPLICADO
// =====================================================

bool Courier::hasOrderIdLocked(const std::string &orderId) const {
  if (activeOrder && activeOrder->orderId == orderId) {
    return true;
  }

  const auto containsId = [&orderId](const std::deque<Order> &list) {
    return std::any_of(
        list.begin(), list.end(),
        [&orderId](const Order &order) { return order.orderId == orderId; });
  };

  return containsId(assignedOrders) || containsId(blockedOrders) ||
         containsId(finishedOrders);
}

// =====================================================
// ASIGNAR PEDIDO
// =====================================================

bool Courier::tryAssignOrder(Order &&order) {
  if (order.getState() != OrderState::Created) {
    return false;
  }

  const auto restaurant = std::find_if(
      config.restaurants.begin(), config.restaurants.end(),
      [&order](const Restaurant &r) { return r.id == order.restauranteId; });

  if (restaurant == config.restaurants.end() ||
      !roadNetwork.isReachable(restaurant->nodeId, order.deliveryNodeId)) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(stateMutex);

    if (stopRequested || state == CourierState::Broken) {
      return false;
    }

    const std::size_t capacity =
        static_cast<std::size_t>(config.fleet.bagCapacity);

    const std::size_t reserved =
        assignedOrders.size() + blockedOrders.size() + (activeOrder ? 1u : 0u);

    if (reserved >= capacity || hasOrderIdLocked(order.orderId)) {
      return false;
    }

    assignedOrders.push_back(std::move(order));

    assignedOrders.back().transitionTo(OrderState::Assigned);
  }

  workCv.notify_one();
  return true;
}

// =====================================================
// CIERRE DE PEDIDOS PENDIENTES
// =====================================================

void Courier::finishPendingLocked() {
  if (activeOrder) {
    activeOrder->transitionTo(OrderState::Pending);

    finishedOrders.push_back(std::move(*activeOrder));

    activeOrder.reset();
  }

  while (!assignedOrders.empty()) {
    assignedOrders.front().transitionTo(OrderState::Pending);

    finishedOrders.push_back(std::move(assignedOrders.front()));

    assignedOrders.pop_front();
  }

  while (!blockedOrders.empty()) {
    blockedOrders.front().transitionTo(OrderState::Pending);

    finishedOrders.push_back(std::move(blockedOrders.front()));

    blockedOrders.pop_front();
  }
}

// =====================================================
// CONSULTAS SEGURAS
// =====================================================

std::size_t Courier::getReservedCount() const {
  std::lock_guard<std::mutex> lock(stateMutex);

  return assignedOrders.size() + blockedOrders.size() + (activeOrder ? 1u : 0u);
}

std::size_t Courier::getFinishedCount() const {
  std::lock_guard<std::mutex> lock(stateMutex);
  return finishedOrders.size();
}

std::optional<OrderState>
Courier::getAssignedOrderState(const std::string &orderId) const {
  std::lock_guard<std::mutex> lock(stateMutex);

  if (activeOrder && activeOrder->orderId == orderId) {
    return activeOrder->getState();
  }

  for (const auto *list : {&assignedOrders, &blockedOrders, &finishedOrders}) {
    for (const Order &order : *list) {
      if (order.orderId == orderId) {
        return order.getState();
      }
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
// DATOS DEL REPARTIDOR
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
