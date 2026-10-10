
#pragma once

#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/Courier.hpp"
#include "map/RoadNetwork.hpp"

#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

class Fleet {
private:
  const SimulationConfig &config;
  const RoadNetwork &roadNetwork;
  SimulationClock &clock;

  // Objetos repartidores.
  std::vector<std::unique_ptr<Courier>> couriers;

  // Un hilo independiente por repartidor.
  std::vector<std::thread> threads;

  bool started = false;

public:
  Fleet(const SimulationConfig &config, const RoadNetwork &roadNetwork,
        SimulationClock &clock);

  ~Fleet();

  // Evitar copias de una flota que posee hilos.
  Fleet(const Fleet &) = delete;
  Fleet &operator=(const Fleet &) = delete;

  // Iniciar todos los repartidores.
  void start();

  // Solicitar detener todos los hilos.
  void requestStop();

  // Esperar la finalizacion de todos los hilos.
  void join();

  std::size_t size() const noexcept;

  Courier &at(std::size_t index);
};
