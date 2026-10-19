#include "dispatch/Dispatcher.hpp"
#include "dispatch/SharedDistanceCache.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

using RealClock = std::chrono::steady_clock;


struct TimedQuote {
  std::optional<EtaQuote> quote;
  std::exception_ptr error;
  RealClock::time_point finishedAt;
};

} 


class QuoteExecutor {
private:
  std::mutex queueMutex;
  std::condition_variable queueCv;

  std::deque<std::function<void()>> jobs;
  std::vector<std::thread> workers;

  bool stopping = false;
  const std::size_t maxQueued;

  void workerLoop() {
    while (true) {
      std::function<void()> job;

      {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCv.wait(lock, [this] { return stopping || !jobs.empty(); });
        if (stopping) {
          return;
        }
        job = std::move(jobs.front());
        jobs.pop_front();
      }

      job();
    }
  }

public:
  explicit QuoteExecutor(std::size_t workerCount)
      : maxQueued(std::max<std::size_t>(64, workerCount * 16)) {
    workers.reserve(workerCount);
    try {
      for (std::size_t i = 0; i < workerCount; ++i) {
        workers.emplace_back(&QuoteExecutor::workerLoop, this);
      }
    } catch (...) {
      {
        std::lock_guard<std::mutex> lock(queueMutex);
        stopping = true;
      }
      queueCv.notify_all();
      for (auto &worker : workers) {
        if (worker.joinable()) {
          worker.join();
        }
      }
      throw;
    }
  }

  ~QuoteExecutor() {
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      stopping = true;
      jobs.clear();
    }
    queueCv.notify_all();
    for (auto &worker : workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  std::future<TimedQuote> submit(std::function<TimedQuote()> work) {
    auto task =
        std::make_shared<std::packaged_task<TimedQuote()>>(std::move(work));
    std::future<TimedQuote> result = task->get_future();

    {
      std::lock_guard<std::mutex> lock(queueMutex);
      if (stopping) {
        throw std::runtime_error("Ejecutor detenido");
      }
      if (jobs.size() >= maxQueued) {
        throw std::runtime_error("Cola de cotizaciones llena");
      }
      jobs.emplace_back([task] { (*task)(); });
    }
    queueCv.notify_one();
    return result;
  }
};


Dispatcher::Dispatcher(const SimulationConfig &config,
                       const RoadNetwork &roadNetwork, Fleet &fleet,
                       QuoteCalculator customCalculator)
    : config(config), roadNetwork(roadNetwork), fleet(fleet),
      calculator(std::move(customCalculator)) {


  if (!calculator) {
    auto sharedLegCache = std::make_shared<SharedDistanceCache>();

    calculator = [this, sharedLegCache](const std::string &startNode,
                                       const std::string &restaurantNode,
                                       const std::string &deliveryNode) -> double {
      const auto toRestaurant =
          this->roadNetwork.shortestRoute(startNode, restaurantNode);
      if (!toRestaurant) {
        throw std::runtime_error("No existe una ruta valida");
      }

      const double toCustomerMeters = sharedLegCache->get(
          restaurantNode, deliveryNode, [this, &restaurantNode,
                                         &deliveryNode]() -> double {
            const auto route =
                this->roadNetwork.shortestRoute(restaurantNode, deliveryNode);
            if (!route) {
              throw std::runtime_error("No existe una ruta valida");
            }
            return route->distanceMeters;
          });

      const double distanceMeters =
          toRestaurant->distanceMeters + toCustomerMeters;
      const double speedMetersPerSecond = this->config.fleet.speedKmh / 3.6;
      return distanceMeters / speedMetersPerSecond;
    };
  }

  const std::size_t hardware =
      std::max<std::size_t>(2, std::thread::hardware_concurrency());
  const std::size_t workerCount = std::max<std::size_t>(
      1, std::min<std::size_t>(fleet.size(), std::min<std::size_t>(hardware, 8)));
  executor = std::make_unique<QuoteExecutor>(workerCount);
}

Dispatcher::~Dispatcher() = default;


QuoteBatch Dispatcher::quoteCandidates(const Order &order) const {
  QuoteBatch batch;

  const auto restaurant = std::find_if(
      config.restaurants.begin(), config.restaurants.end(),
      [&order](const Restaurant &r) { return r.id == order.restauranteId; });

  if (restaurant == config.restaurants.end()) {
    throw std::invalid_argument("Restaurante inexistente: " +
                                order.restauranteId);
  }

  const std::string restaurantNode = restaurant->nodeId;
  const std::string deliveryNode = order.deliveryNodeId;

  const auto deadline = RealClock::now() +
                        std::chrono::milliseconds(config.dispatch.quoteTimeoutMs);

  struct PendingQuote {
    std::string courierId;
    std::future<TimedQuote> future;
  };
  std::vector<PendingQuote> pending;
  pending.reserve(fleet.size());

  for (std::size_t i = 0; i < fleet.size(); ++i) {
    Courier &courier = fleet.at(i);
    if (courier.getState() == CourierState::Broken) {
      continue;
    }
    if (courier.getReservedCount() >=
        static_cast<std::size_t>(config.fleet.bagCapacity)) {
      continue;
    }

    const std::string startNode = courier.getCurrentNode();
    const std::string courierId = courier.getId();
    const QuoteCalculator calculate = calculator;

    try {
      auto future = executor->submit([calculate, i, courierId, startNode,
                                      restaurantNode,
                                      deliveryNode]() -> TimedQuote {
        TimedQuote result;
        try {
          const double eta = calculate(startNode, restaurantNode, deliveryNode);
          if (!std::isfinite(eta) || eta < 0.0) {
            throw std::runtime_error("ETA invalida");
          }
          result.quote = EtaQuote{i, courierId, eta};
        } catch (...) {
          result.error = std::current_exception();
        }
        result.finishedAt = RealClock::now();
        return result;
      });

      pending.push_back(PendingQuote{courierId, std::move(future)});
    } catch (const std::exception &e) {
      batch.failures.push_back(QuoteFailure{courierId, e.what()});
    }
  }

  for (auto &task : pending) {
    const auto status = task.future.wait_until(deadline);
    if (status != std::future_status::ready) {
      batch.failures.push_back(QuoteFailure{task.courierId, "quoteTimeout"});
      continue;
    }

    try {
      TimedQuote result = task.future.get();
      if (result.finishedAt > deadline) {
        batch.failures.push_back(QuoteFailure{task.courierId, "quoteTimeout"});
        continue;
      }
      if (result.error) {
        std::rethrow_exception(result.error);
      }
      if (result.quote) {
        batch.quotes.push_back(std::move(*result.quote));
      }
    } catch (const std::exception &e) {
      batch.failures.push_back(QuoteFailure{task.courierId, e.what()});
    }
  }
  return batch;
}


std::optional<std::string> Dispatcher::assignBestCourier(Order &order) const {
  QuoteBatch batch = quoteCandidates(order);

  std::sort(batch.quotes.begin(), batch.quotes.end(),
            [](const EtaQuote &a, const EtaQuote &b) {
              if (a.etaSimulatedSeconds != b.etaSimulatedSeconds) {
                return a.etaSimulatedSeconds < b.etaSimulatedSeconds;
              }
              return a.courierIndex < b.courierIndex;
            });

  for (const EtaQuote &quote : batch.quotes) {
    Courier &courier = fleet.at(quote.courierIndex);
    if (courier.tryAssignOrder(std::move(order))) {
      return quote.courierId;
    }
  }
  return std::nullopt;
}
