
#include "fleet/Courier.hpp"
#include "restaurants/PreparationTime.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace {

// Tiempo fijo de entrega en restaurante.
// Son milisegundos SIMULADOS.
constexpr long long kPickupHandoffMs = 2000;

} // namespace

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
// HILO PRINCIPAL
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

    // Movimiento manual.
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

    // Procesar siguiente pedido.
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
  std::string orderId;
  long long creationTime = 0;

  {
    std::lock_guard<std::mutex> lock(stateMutex);

    if (!activeOrder) {
      return false;
    }

    restaurantId = activeOrder->restauranteId;
    deliveryNode = activeOrder->deliveryNodeId;
    orderId = activeOrder->orderId;
    creationTime = activeOrder->time;
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
  // ETAPA 2: ESPERAR PREPARACION
  // =============================================

  const long long preparationMs =
      preparationDurationMs(*restaurant, orderId, config.orders.seed);

  // La comida comienza a prepararse
  // desde la creacion del pedido.
  const long long readyAt = creationTime + preparationMs;

  {
    std::unique_lock<std::mutex> lock(stateMutex);

    while (!stopRequested) {
      const long long remainingSimulatedMs =
          readyAt - clock.getSimulatedTimeMs();

      if (remainingSimulatedMs <= 0) {
        break;
      }

      const double realWaitMs = static_cast<double>(remainingSimulatedMs) /
                                config.simulation.timeScale;

      // Espera interrumpible sin busy waiting.
      workCv.wait_for(lock,
                      std::chrono::duration<double, std::milli>(realWaitMs),
                      [this] { return stopRequested; });
    }

    if (stopRequested) {
      return false;
    }
  }

  // =============================================
  // ETAPA 3: OBTENER CUPO FIFO
  // =============================================

  std::optional<PickupGate::Permit> pickupPermit;

  if (restaurantManager) {
    const auto gate = restaurantManager->gateFor(restaurantId);

    pickupPermit = gate->acquire();

    if (!pickupPermit) {
      return false;
    }
  }

  // =============================================
  // ETAPA 4: ENTREGA DE COMIDA EN RESTAURANTE
  // =============================================

  {
    // Mientras el permiso exista, el restaurante
    // tiene un cupo ocupado por esta moto.
    std::unique_lock<std::mutex> lock(stateMutex);

    if (stopRequested || !activeOrder) {
      return false;
    }

    const double realHandoffMs =
        static_cast<double>(kPickupHandoffMs) / config.simulation.timeScale;

    // Simular el tiempo de entrega de comida.
    // workCv libera temporalmente stateMutex.
    // pickupPermit permanece adquirido.
    const bool interrupted = workCv.wait_for(
        lock, std::chrono::duration<double, std::milli>(realHandoffMs),
        [this] { return stopRequested; });

    if (interrupted || stopRequested || !activeOrder) {
      return false;
    }

    // Solo ahora se completa la recogida.
    if (!activeOrder->transitionTo(OrderState::PickedUp)) {
      return false;
    }
  }

  // =============================================
  // ETAPA 5: LIBERAR CUPO DEL RESTAURANTE
  // =============================================

  // RAII libera el cupo incluso si se retorna
  // anticipadamente durante la espera.
  pickupPermit.reset();

  // =============================================
  // ETAPA 6: VIAJAR AL CLIENTE
  // =============================================

  if (!travelTo(deliveryNode)) {
    return false;
  }

  // =============================================
  // ETAPA 7: ENTREGAR PEDIDO
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

  // Avisar al Dispatcher que hay capacidad.
  if (capacitySignal) {
    capacitySignal->notifyChange();
  }

  return true;
}

// =====================================================
// MOVIMIENTO SOBRE LA RED DE CALLES
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
