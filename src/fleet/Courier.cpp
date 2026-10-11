
#include "fleet/Courier.hpp"
#include "restaurants/PreparationTime.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace {

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
      return stopRequested || state == CourierState::Broken ||
             destination.has_value() || !assignedOrders.empty();
    });

    if (stopRequested) {
      finishPendingLocked();
      state = CourierState::Idle;
      break;
    }

    // La moto averiada no procesa mas pedidos.
    // Conserva su carga mientras espera rescate.
    if (state == CourierState::Broken) {
      workCv.wait(lock, [this] { return stopRequested; });

      finishPendingLocked();
      state = CourierState::Idle;
      break;
    }

    // =========================================
    // MOVIMIENTO MANUAL DE PRUEBAS
    // =========================================

    if (destination) {
      const std::string target = *destination;
      destination.reset();

      state = CourierState::Moving;
      lock.unlock();

      // El movimiento manual no genera averias.
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

    // =========================================
    // PROCESAR SIGUIENTE PEDIDO
    // =========================================

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

    // NUEVO: mantener todos los pedidos
    // bajo custodia si la moto se averio.
    if (state == CourierState::Broken) {
      continue;
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

  const auto restaurantBreakdown = incidents::makeBreakdownPlan(
      config.orders.seed, id, orderId, "toRestaurant",
      config.incidents.breakdownProbability);

  if (!travelTo(restaurant->nodeId, &restaurantBreakdown)) {
    return false;
  }

  // =============================================
  // ETAPA 2: ESPERAR PREPARACION
  // =============================================

  const long long preparationMs =
      preparationDurationMs(*restaurant, orderId, config.orders.seed);

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
  // ETAPA 4: ENTREGA EN RESTAURANTE
  // =============================================

  {
    std::unique_lock<std::mutex> lock(stateMutex);

    if (stopRequested || !activeOrder) {
      return false;
    }

    const double realHandoffMs =
        static_cast<double>(kPickupHandoffMs) / config.simulation.timeScale;

    const bool interrupted = workCv.wait_for(
        lock, std::chrono::duration<double, std::milli>(realHandoffMs),
        [this] { return stopRequested; });

    if (interrupted || stopRequested || !activeOrder) {
      return false;
    }

    if (!activeOrder->transitionTo(OrderState::PickedUp)) {
      return false;
    }
  }

  pickupPermit.reset();

  // =============================================
  // ETAPA 5: VIAJAR AL CLIENTE
  // =============================================

  const auto customerBreakdown = incidents::makeBreakdownPlan(
      config.orders.seed, id, orderId, "toCustomer",
      config.incidents.breakdownProbability);

  if (!travelTo(deliveryNode, &customerBreakdown)) {
    return false;
  }

  // =============================================
  // ETAPA 6: ENTREGAR PEDIDO
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

  if (capacitySignal) {
    capacitySignal->notifyChange();
  }

  return true;
}

// =====================================================
// MOVIMIENTO Y DETECCION DE AVERIAS
// =====================================================

bool Courier::travelTo(const std::string &targetNode,
                       const incidents::BreakdownPlan *breakdown) {
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

  const bool shouldBreak =
      breakdown != nullptr && breakdown->occurs && route->distanceMeters > 0.0;

  const double breakdownDistance =
      shouldBreak ? route->distanceMeters * breakdown->atFraction : 0.0;

  double traveledMeters = 0.0;

  for (const RouteSegment &segment : route->segments) {

    // Determinar si la averia ocurre
    // dentro de este segmento.
    const bool breakdownHere =
        shouldBreak &&
        traveledMeters + segment.distanceMeters >= breakdownDistance;

    const double distanceToTravel =
        breakdownHere ? std::max(0.0, breakdownDistance - traveledMeters)
                      : segment.distanceMeters;

    const double simulatedSeconds = distanceToTravel / speedMetersPerSecond;

    const double realSeconds = simulatedSeconds / config.simulation.timeScale;

    {
      std::unique_lock<std::mutex> lock(stateMutex);

      const bool interrupted =
          workCv.wait_for(lock, std::chrono::duration<double>(realSeconds),
                          [this] { return stopRequested; });

      if (interrupted || stopRequested) {
        return false;
      }

      // =====================================
      // AVERIA DURANTE EL VIAJE
      // =====================================

      if (breakdownHere) {
        state = CourierState::Broken;

        // No avanzar al nodo final:
        // la moto quedo detenida en
        // el segmento actual.
      } else {
        currentNode = segment.to;
      }
    }

    if (breakdownHere) {
      // Avisar fuera del mutex del Courier.
      if (capacitySignal) {
        capacitySignal->notifyChange();
      }

      return false;
    }

    traveledMeters += segment.distanceMeters;
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
// FINALIZAR PEDIDOS PENDIENTES
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
// INFORMACION
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
