
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

  // Protege el estado compartido del repartidor.
  mutable std::mutex stateMutex;

  // Permite esperar trabajo sin consumir CPU.
  std::condition_variable workCv;

  std::string currentNode;
  CourierState state = CourierState::Idle;

  std::optional<std::string> destination;
  bool stopRequested = false;

  // Movimiento por las calles de la ruta.
  bool travelTo(const std::string &targetNode);

public:
  Courier(std::string id, const SimulationConfig &config,
          const RoadNetwork &roadNetwork, SimulationClock &clock);

  // Metodo ejecutado por el hilo del repartidor.
  void run();

  // Asignar un destino si el repartidor esta disponible.
  bool assignDestination(const std::string &nodeId);

  // Solicitar la finalizacion del hilo.
  void requestStop();

  std::string getId() const;
  std::string getCurrentNode() const;
  CourierState getState() const;
};
