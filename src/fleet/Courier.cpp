
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

    // Despertar por un destino, un pedido o una parada.
    workCv.wait(lock, [this] {
      return stopRequested || destination.has_value() ||
             !assignedOrders.empty();
    });

    if (stopRequested) {
      finishPendingLocked();
      state = CourierState::Idle;
      break;
    }

    // Mantener compatibilidad con las pruebas
    // anteriores de navegacion manual.
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

    // Tomar el pedido mas antiguo.
    // Se mantiene reservado mientras esta activo.
    activeOrder.emplace(std::move(assignedOrders.front()));

    assignedOrders.pop_front();
    state = CourierState::Moving;

    // No mantener el mutex durante el recorrido.
    lock.unlock();

    const bool delivered = processActiveOrder();

    lock.lock();

    if (stopRequested) {
      finishPendingLocked();
      state = CourierState::Idle;
      break;
    }

    if (!delivered && activeOrder) {
      // Conservar el pedido para futura recuperacion.
      // DP.4 gestionara su reasignacion.
      blockedOrders.push_back(std::move(*activeOrder));

      activeOrder.reset();
    }

    state = CourierState::Idle;
  }
}

// =====================================================
// PROCESAR UN PEDIDO
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

  // Buscar el restaurante del pedido.
  const auto restaurant = std::find_if(
      config.restaurants.begin(), config.restaurants.end(),
      [&restaurantId](const Restaurant &r) { return r.id == restaurantId; });

  if (restaurant == config.restaurants.end()) {
    return false;
  }

  // ETAPA 1: viajar al restaurante.
  if (!travelTo(restaurant->nodeId)) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(stateMutex);

    if (stopRequested || !activeOrder ||
        !activeOrder->transitionTo(OrderState::PickedUp)) {
      return false;
    }
  }

  // En DP.5 se agregaran los tiempos de preparacion
  // y los puestos de recogida de los restaurantes.

  // ETAPA 2: viajar al cliente.
  if (!travelTo(deliveryNode)) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(stateMutex);

    if (stopRequested || !activeOrder ||
        !activeOrder->transitionTo(OrderState::Delivered)) {
      return false;
    }

    // Pedido terminado: libera el espacio reservado.
    finishedOrders.push_back(std::move(*activeOrder));

    activeOrder.reset();
  }

  return true;
}

// =====================================================
// MOVIMIENTO TEMPORIZADO POR CALLES
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
// DESTINO MANUAL PARA PRUEBAS DE NAVEGACION
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
// VERIFICAR IDENTIFICADORES DUPLICADOS
// Requiere stateMutex adquirido.
// =====================================================

bool Courier::hasOrderIdLocked(const std::string &orderId) const {

  if (activeOrder && activeOrder->orderId == orderId) {
    return true;
  }

  const auto containsId = [&orderId](const std::deque<Order> &list) {
    return std::any_of(list.begin(), list.end(), [&orderId](const Order &o) {
      return o.orderId == orderId;
    });
  };

  return containsId(assignedOrders) || containsId(blockedOrders) ||
         containsId(finishedOrders);
}

// =====================================================
// ASIGNAR PEDIDO CON CAPACIDAD PROTEGIDA
// =====================================================

bool Courier::tryAssignOrder(Order &&order) {
  if (order.getState() != OrderState::Created) {
    return false;
  }

  const auto restaurant = std::find_if(
      config.restaurants.begin(), config.restaurants.end(),
      [&order](const Restaurant &r) { return r.id == order.restauranteId; });

  // Comprobar que el restaurante y el destino
  // forman un recorrido valido.
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

    // La capacidad incluye el pedido en movimiento
    // y los pedidos que esperan o requieren recuperacion.
    if (assignedOrders.size() + blockedOrders.size() +
                (activeOrder ? 1u : 0u) >=
            capacity ||
        hasOrderIdLocked(order.orderId)) {
      return false;
    }

    assignedOrders.push_back(std::move(order));

    assignedOrders.back().transitionTo(OrderState::Assigned);
  }

  // Ahora el hilo de Courier tambien despierta
  // cuando tiene pedidos reales.
  workCv.notify_one();

  return true;
}

// =====================================================
// MARCAR PEDIDOS SIN FINALIZAR COMO PENDING
// Requiere stateMutex adquirido.
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
// CONSULTAS SEGURAS ENTRE HILOS
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
// INFORMACION DEL REPARTIDOR
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
