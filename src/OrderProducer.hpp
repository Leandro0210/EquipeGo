#pragma once

#include "Clock.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "OrderBook.hpp"
#include "OrderGenerator.hpp"
#include "RoadNetwork.hpp"

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
