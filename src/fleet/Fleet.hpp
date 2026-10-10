
#pragma once

#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/CapacitySignal.hpp"
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

  // Canal compartido de notificaciones.
  std::shared_ptr<CapacitySignal> capacitySignal;

  std::vector<std::unique_ptr<Courier>> couriers;
  std::vector<std::thread> threads;

  bool started = false;

public:
  Fleet(const SimulationConfig &config, const RoadNetwork &roadNetwork,
        SimulationClock &clock);

  ~Fleet();

  Fleet(const Fleet &) = delete;
  Fleet &operator=(const Fleet &) = delete;

  void start();
  void requestStop();
  void join();

  std::size_t size() const noexcept;

  Courier &at(std::size_t index);

  // Lo utilizara el Dispatcher para esperar
  // a que alguna moto libere capacidad.
  std::shared_ptr<CapacitySignal> getCapacitySignal() const;
};
