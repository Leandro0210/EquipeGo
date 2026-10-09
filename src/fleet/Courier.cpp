
#include "fleet/Courier.hpp"

#include <chrono>
#include <utility>

Courier::Courier(std::string id, const SimulationConfig &config,
                 const RoadNetwork &roadNetwork, SimulationClock &clock)
    : id(std::move(id)), config(config), roadNetwork(roadNetwork), clock(clock),
      currentNode(config.fleet.startNode) {}

// =====================================================
// HILO PRINCIPAL DEL REPARTIDOR
// =====================================================

void Courier::run() {
  while (true) {
    std::unique_lock<std::mutex> lock(stateMutex);

    // Esperar hasta recibir un destino o una orden de parada.
    workCv.wait(lock,
                [this]() { return destination.has_value() || stopRequested; });

    if (stopRequested) {
      break;
    }

    const std::string target = *destination;
    destination.reset();

    state = CourierState::Moving;

    // Liberar mutex mientras realizamos el recorrido.
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
// MOVIMIENTO POR LOS SEGMENTOS DE LA RUTA
// =====================================================

bool Courier::travelTo(const std::string &targetNode) {
  std::string origin;

  {
    std::lock_guard<std::mutex> lock(stateMutex);
    origin = currentNode;
  }

  // Calcular ruta minima con Dijkstra.
  const auto route = roadNetwork.shortestRoute(origin, targetNode);

  if (!route.has_value()) {
    return false;
  }

  // Convertir velocidad de km/h a m/s.
  const double speedMetersPerSecond = config.fleet.speedKmh / 3.6;

  // Recorrer las calles de una en una.
  for (const RouteSegment &segment : route->segments) {

    // Tiempo que tardaria en la simulacion.
    const double simulatedSeconds =
        segment.distanceMeters / speedMetersPerSecond;

    // Convertir a tiempo real segun timeScale.
    const double realSeconds = simulatedSeconds / config.simulation.timeScale;

    std::unique_lock<std::mutex> lock(stateMutex);

    // Esperar el tiempo de recorrido o despertar si
    // solicitan detener la simulacion.
    const bool interrupted =
        workCv.wait_for(lock, std::chrono::duration<double>(realSeconds),
                        [this]() { return stopRequested; });

    if (interrupted) {
      return false;
    }

    // Actualizar posicion al llegar a la interseccion.
    currentNode = segment.to;
  }

  return true;
}

// =====================================================
// ASIGNAR DESTINO
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

  // Despertar al hilo de esta moto.
  workCv.notify_one();

  return true;
}

// =====================================================
// DETENER REPARTIDOR
// =====================================================

void Courier::requestStop() {
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    stopRequested = true;
  }

  // Despertar incluso si esta viajando.
  workCv.notify_all();
}

// =====================================================
// CONSULTAS PROTEGIDAS POR MUTEX
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
