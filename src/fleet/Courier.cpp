#include "fleet/Courier.hpp"

#include <utility>

Courier::Courier(std::string id, const SimulationConfig &config,
                 const RoadNetwork &roadNetwork, SimulationClock &clock)
    : id(std::move(id)), config(config), roadNetwork(roadNetwork), clock(clock),
      currentNode(config.fleet.startNode) {}

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

    const std::string origin = currentNode;

    lock.unlock();

    const auto route = roadNetwork.shortestRoute(origin, target);

    lock.lock();

    if (route.has_value()) {
      currentNode = target;
    }

    state = CourierState::Idle;
  }
}

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

void Courier::requestStop() {
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    stopRequested = true;
  }

  workCv.notify_all();
}

std::string Courier::getId() const { return id; }

std::string Courier::getCurrentNode() const {
  std::lock_guard<std::mutex> lock(stateMutex);
  return currentNode;
}

CourierState Courier::getState() const {
  std::lock_guard<std::mutex> lock(stateMutex);
  return state;
}