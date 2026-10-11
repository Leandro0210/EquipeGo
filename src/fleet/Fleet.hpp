
#pragma once

#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/CapacitySignal.hpp"
#include "fleet/Courier.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"
#include "restaurants/RestaurantManager.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

  // Administrador unico de restaurantes.
  std::shared_ptr<RestaurantManager> restaurantManager;

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

  // Para pruebas y futura visualizacion.
  std::shared_ptr<RestaurantManager> getRestaurantManager() const;

  std::vector<CourierOrderResult> takeFinishedOrders();
};
