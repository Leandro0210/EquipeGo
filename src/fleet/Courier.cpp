
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

    if (state == CourierState::Broken) {
      workCv.wait(lock, [this] { return stopRequested; });

      finishPendingLocked();
      state = CourierState::Idle;
      break;
    }

    // =========================================
    // MOVIMIENTO MANUAL
    // =========================================

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

    // =========================================
    // PROCESAR PEDIDO
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
  OrderState initialState = OrderState::Created;

  {
    std::lock_guard<std::mutex> lock(stateMutex);

    if (!activeOrder) {
      return false;
    }

    restaurantId = activeOrder->restauranteId;
    deliveryNode = activeOrder->deliveryNodeId;
    orderId = activeOrder->orderId;
    creationTime = activeOrder->time;
    initialState = activeOrder->getState();
  }

  if (initialState != OrderState::Assigned &&
      initialState != OrderState::PickedUp) {
    return false;
  }

  // Si el pedido ya fue recogido por una moto
  // que posteriormente se averio, no debemos
  // regresar al restaurante ni repetir pickup.
  if (initialState == OrderState::Assigned) {

    const auto restaurant = std::find_if(
        config.restaurants.begin(), config.restaurants.end(),
        [&restaurantId](const Restaurant &r) { return r.id == restaurantId; });

    if (restaurant == config.restaurants.end()) {
      return false;
    }

    // =========================================
    // ETAPA 1: VIAJAR AL RESTAURANTE
    // =========================================

    const auto restaurantBreakdown = incidents::makeBreakdownPlan(
        config.orders.seed, id, orderId, "toRestaurant",
        config.incidents.breakdownProbability);

    if (!travelTo(restaurant->nodeId, &restaurantBreakdown)) {
      return false;
    }

    // =========================================
    // ETAPA 2: ESPERAR PREPARACION
    // =========================================

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

    // =========================================
    // ETAPA 3: CUPO FIFO
    // =========================================

    std::optional<PickupGate::Permit> pickupPermit;

    if (restaurantManager) {
      const auto gate = restaurantManager->gateFor(restaurantId);

      pickupPermit = gate->acquire();

      if (!pickupPermit) {
        return false;
      }
    }

    // =========================================
    // ETAPA 4: RECOGER COMIDA
    // =========================================

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
  }

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
  // ETAPA 6: ENTREGAR
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

      if (breakdownHere) {
        state = CourierState::Broken;
        everBroken = true;
      } else {
        currentNode = segment.to;
      }
    }

    if (breakdownHere) {
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
// DP6: TRANSFERENCIA ATOMICA
// =====================================================

bool Courier::transferAllTo(Courier &recipient) {

  // Una moto nunca se transfiere a si misma.
  if (this == &recipient) {
    return false;
  }

  bool transferred = false;

  {
    // Bloquear ambos mutex sin depender del
    // orden de las llamadas. Evita deadlocks.
    std::scoped_lock lock(stateMutex, recipient.stateMutex);

    // Solo transferir desde motos averiadas.
    if (stopRequested || recipient.stopRequested ||
        state != CourierState::Broken ||
        recipient.state == CourierState::Broken) {
      return false;
    }

    const std::size_t outgoing =
        assignedOrders.size() + blockedOrders.size() + (activeOrder ? 1u : 0u);

    if (outgoing == 0) {
      return false;
    }

    const std::size_t recipientReserved = recipient.assignedOrders.size() +
                                          recipient.blockedOrders.size() +
                                          (recipient.activeOrder ? 1u : 0u);

    const std::size_t capacity =
        static_cast<std::size_t>(recipient.config.fleet.bagCapacity);

    // No se acepta un traslado parcial.
    if (recipientReserved > capacity ||
        outgoing > capacity - recipientReserved) {
      return false;
    }

    // Evitar IDs duplicados en el destino.
    const auto duplicate = [&recipient](const Order &order) {
      return recipient.hasOrderIdLocked(order.orderId);
    };

    if (activeOrder && duplicate(*activeOrder)) {
      return false;
    }

    for (const Order &order : assignedOrders) {
      if (duplicate(order)) {
        return false;
      }
    }

    for (const Order &order : blockedOrders) {
      if (duplicate(order)) {
        return false;
      }
    }

    // =========================================
    // PREPARAR TODA LA MEMORIA ANTES DE MOVER
    // =========================================

    // Order no admite copias. Preconstruimos
    // posiciones temporales antes de tocar
    // cualquier pedido real.
    //
    // Si una asignacion de memoria falla aqui,
    // ambas motos permanecen sin cambios.
    std::deque<Order> staged;

    const std::size_t totalQueued = recipient.assignedOrders.size() + outgoing;

    for (std::size_t i = 0; i < totalQueued; ++i) {
      staged.emplace_back("", "", "", 0);
    }

    // =========================================
    // MOVIMIENTO BAJO AMBOS MUTEX
    // =========================================

    auto position = staged.begin();

    // Conservar primero los pedidos que
    // ya estaban esperando en el receptor.
    for (Order &order : recipient.assignedOrders) {
      *position = std::move(order);
      ++position;
    }

    // Prioridad al pedido activo averiado.
    if (activeOrder) {
      *position = std::move(*activeOrder);
      ++position;
    }

    for (Order &order : assignedOrders) {
      *position = std::move(order);
      ++position;
    }

    for (Order &order : blockedOrders) {
      *position = std::move(order);
      ++position;
    }

    // Publicar toda la cola nueva con un
    // solo intercambio, bajo ambos mutex.
    recipient.assignedOrders.swap(staged);

    // Quitar todas las referencias originales.
    activeOrder.reset();
    assignedOrders.clear();
    blockedOrders.clear();

    // La moto fuente sigue Broken, pero vacia.
    transferred = true;
  }

  // Despertar al receptor y avisar al despachador
  // DESPUES de liberar los mutex.
  if (transferred) {
    recipient.workCv.notify_one();

    if (recipient.capacitySignal) {
      recipient.capacitySignal->notifyChange();
    }
  }

  return transferred;
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
// VERIFICAR DUPLICADOS
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
// ASIGNACION DE PEDIDOS
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
// CIERRE DE PEDIDOS
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

bool Courier::hasBrokenDown() const {
  std::lock_guard<std::mutex> lock(stateMutex);
  return everBroken;
}