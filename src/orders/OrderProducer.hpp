
#pragma once

#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/OrderBook.hpp"
#include "orders/OrderGenerator.hpp"

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

  std::size_t rejectedCount = 0;

public:
  OrderProducer(const SimulationConfig &config, OrderBook &orderBook,
                EventLogger &logger, SimulationClock &clock,
                const RoadNetwork &roadNetwork);

  void run();
  void requestStop();

  std::size_t getCreatedCount() const { return nextOrderNumber; }

  std::size_t getRejectedCount() const { return rejectedCount; }
};
