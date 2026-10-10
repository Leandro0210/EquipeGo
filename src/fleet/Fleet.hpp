
#pragma once

#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/CapacitySignal.hpp"
#include "fleet/Courier.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Pedido finalizado junto con su repartidor.
struct CourierOrderResult {
  std::string courierId;
  Order order;
};

class Fleet {
private:
  const SimulationConfig &config;
  const RoadNetwork &roadNetwork;
  SimulationClock &clock;

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

  std::shared_ptr<CapacitySignal> getCapacitySignal() const;

  // NUEVO:
  // Recuperar los pedidos finalizados de cada
  // repartidor, despues de detener la flota.
  std::vector<CourierOrderResult> takeFinishedOrders();
};
