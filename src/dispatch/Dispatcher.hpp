
#pragma once

#include "config/Config.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class OrderBook;
class EventLogger;
class SimulationClock;
class QuoteExecutor;

struct EtaQuote {
  std::size_t courierIndex;
  std::string courierId;
  double etaSimulatedSeconds;
};

struct QuoteFailure {
  std::string courierId;
  std::string reason;
};

struct QuoteBatch {
  std::vector<EtaQuote> quotes;
  std::vector<QuoteFailure> failures;
};

using QuoteCalculator = std::function<double(
    const std::string &, const std::string &, const std::string &)>;

class Dispatcher {
private:
  const SimulationConfig &config;
  const RoadNetwork &roadNetwork;
  Fleet &fleet;

  QuoteCalculator calculator;
  std::unique_ptr<QuoteExecutor> executor;

  std::atomic<bool> stopRequested{false};

  std::atomic<std::size_t> assignedCount{0};
  std::atomic<std::size_t> rejectedCount{0};
  std::atomic<std::size_t> pendingCount{0};

public:
  Dispatcher(const SimulationConfig &config, const RoadNetwork &roadNetwork,
             Fleet &fleet, QuoteCalculator calculator = {});

  ~Dispatcher();

  Dispatcher(const Dispatcher &) = delete;
  Dispatcher &operator=(const Dispatcher &) = delete;

  QuoteBatch quoteCandidates(const Order &order) const;

  std::optional<std::string> assignBestCourier(Order &order) const;

  void run(OrderBook &orderBook, EventLogger &logger, SimulationClock &clock);

  void requestStop(OrderBook &orderBook);

  std::size_t getAssignedCount() const { return assignedCount.load(); }

  std::size_t getRejectedCount() const { return rejectedCount.load(); }

  std::size_t getPendingCount() const { return pendingCount.load(); }
};
