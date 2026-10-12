
#pragma once

#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/CapacitySignal.hpp"
#include "fleet/Courier.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"
#include "restaurants/RestaurantManager.hpp"

#include <atomic>
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
  std::shared_ptr<RestaurantManager> restaurantManager;

  std::vector<std::unique_ptr<Courier>> couriers;
  std::vector<std::thread> threads;

  // DP6: coordinador de rescate.
  std::thread rescueThread;
  std::atomic<bool> rescueStopRequested{false};

  std::atomic<std::size_t> successfulRescues{0};
  std::atomic<std::size_t> rescuedOrderCount{0};

  bool started = false;
  bool rescueStarted = false;

  void rescueLoop();

public:
  Fleet(const SimulationConfig &config, const RoadNetwork &roadNetwork,
        SimulationClock &clock);

  ~Fleet();

  Fleet(const Fleet &) = delete;
  Fleet &operator=(const Fleet &) = delete;

  void start();

  // Iniciar el rescate automatico.
  // Separado de start() para conservar
  // los tests manuales de transferencia.
  void startRescue();

  void requestStop();
  void join();

  std::size_t size() const noexcept;
  Courier &at(std::size_t index);

  std::shared_ptr<CapacitySignal> getCapacitySignal() const;

  std::shared_ptr<RestaurantManager> getRestaurantManager() const;

  std::vector<CourierOrderResult> takeFinishedOrders();

  std::size_t getBreakdownCount() const;
  std::size_t getRescueCount() const noexcept;
  std::size_t getRescuedOrderCount() const noexcept;
};
