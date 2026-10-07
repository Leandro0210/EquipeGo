#pragma once

#include "core/Clock.hpp"
#include "config/Config.hpp"
#include "core/Logger.hpp"
#include "orders/OrderBook.hpp"
#include "orders/OrderGenerator.hpp"
#include "map/RoadNetwork.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>

class OrderProducer {
private:
  const SimulationConfig &config;
  OrderBook &orderBook;
  EventLogger &logger;
  SimulationClock &clock;
  const RoadNetwork &roadNetwork;

  OrderGenerator generator;

  std::mutex stopMutex;
  std::condition_variable stopCv;
  bool stopRequested = false;

  std::size_t nextOrderNumber = 0;

public:
  OrderProducer(const SimulationConfig &config, OrderBook &orderBook,
                EventLogger &logger, SimulationClock &clock,
                const RoadNetwork &roadNetwork);

  void run();

  void requestStop();
};
