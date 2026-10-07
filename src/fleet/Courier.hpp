#pragma once

#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "map/RoadNetwork.hpp"

#include <condition_variable>
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

  std::optional<std::string> destination;
  bool stopRequested = false;

public:
  Courier(std::string id, const SimulationConfig &config,
          const RoadNetwork &roadNetwork, SimulationClock &clock);

  // Método que ejecutará el hilo del repartidor.
  void run();

  // Entrega un destino al repartidor si está disponible.
  bool assignDestination(const std::string &nodeId);

  // Solicita que el hilo termine.
  void requestStop();

  std::string getId() const;
  std::string getCurrentNode() const;
  CourierState getState() const;
};