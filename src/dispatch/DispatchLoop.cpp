#include "dispatch/Dispatcher.hpp"

#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "fleet/CapacitySignal.hpp"
#include "orders/OrderBook.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>


void Dispatcher::run(OrderBook &orderBook, EventLogger &logger,
                     SimulationClock &clock) {

  auto signal = fleet.getCapacitySignal();

  if (!signal) {
    throw std::runtime_error("Dispatcher sin CapacitySignal");
  }

  orderBook.setNotificationSignal(signal);

  std::deque<Order> waiting;

  while (!stopRequested.load()) {

    const std::uint64_t observedVersion = signal->version();


    if (waiting.empty()) {
      std::optional<Order> first = orderBook.waitAndTake();

      if (!first) {
        break;
      }

      waiting.push_back(std::move(*first));
    }


    while (!stopRequested.load()) {
      std::optional<Order> next = orderBook.tryTake();

      if (!next) {
        break;
      }

      waiting.push_back(std::move(*next));
    }


    for (auto it = waiting.begin();
         it != waiting.end() && !stopRequested.load();) {

      Order &order = *it;
      const std::string orderId = order.orderId;

      const long long expirationTime =
          order.time + static_cast<long long>(config.dispatch.acceptTimeoutMs);

      const long long now = clock.getSimulatedTimeMs();


      if (now >= expirationTime) {

        if (!order.transitionTo(OrderState::Rejected)) {
          throw std::logic_error("No se pudo rechazar: " + orderId);
        }

        logger.logEvent({{"event", "orderRejected"},
                         {"order", orderId},
                         {"reason", "noCourier"}},
                        now);

        ++rejectedCount;

        orderBook.completeTaken();

        it = waiting.erase(it);
        continue;
      }


      bool assigned = false;

      try {

        QuoteBatch batch = quoteCandidates(order);

        std::sort(batch.quotes.begin(), batch.quotes.end(),
                  [](const EtaQuote &a, const EtaQuote &b) {
                    if (a.etaSimulatedSeconds != b.etaSimulatedSeconds) {
                      return a.etaSimulatedSeconds < b.etaSimulatedSeconds;
                    }

                    return a.courierIndex < b.courierIndex;
                  });

        for (const EtaQuote &quote : batch.quotes) {

          if (stopRequested.load() ||
              clock.getSimulatedTimeMs() >= expirationTime) {
            break;
          }

          Courier &courier = fleet.at(quote.courierIndex);

          const auto onAssigned = [&logger, &clock, &orderId, &quote]() {
            logger.logEvent({{"event", "orderAssigned"},
                             {"order", orderId},
                             {"courier", quote.courierId}},
                            clock.getSimulatedTimeMs());
          };

          if (courier.tryAssignOrder(std::move(order), onAssigned)) {

            ++assignedCount;
            assigned = true;
            break;
          }
        }

      } catch (const std::exception &error) {

        std::cerr << "Error despachando " << orderId << ": " << error.what()
                  << '\n';
      }

      if (assigned) {

        orderBook.completeTaken();
        it = waiting.erase(it);
        continue;
      }

      ++it;
    }

    if (stopRequested.load()) {
      break;
    }

    if (waiting.empty()) {
      continue;
    }


    long long earliestExpiration =
        waiting.front().time +
        static_cast<long long>(config.dispatch.acceptTimeoutMs);

    for (const Order &order : waiting) {

      const long long expiration =
          order.time + static_cast<long long>(config.dispatch.acceptTimeoutMs);

      earliestExpiration = std::min(earliestExpiration, expiration);
    }

    const long long remainingSimulatedMs =
        earliestExpiration - clock.getSimulatedTimeMs();

    if (remainingSimulatedMs <= 0) {
      continue;
    }

    const double remainingRealMs =
        static_cast<double>(remainingSimulatedMs) / config.simulation.timeScale;

    const auto realWait =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double, std::milli>(remainingRealMs));

    const auto deadline = std::chrono::steady_clock::now() + realWait;

    signal->waitUntilChange(observedVersion, deadline, stopRequested);
  }


  for (Order &order : waiting) {

    if (!order.transitionTo(OrderState::Pending)) {
      throw std::logic_error("No se pudo finalizar pedido: " + order.orderId);
    }

    ++pendingCount;
    orderBook.completeTaken();
  }
}


void Dispatcher::requestStop(OrderBook &orderBook) {

  stopRequested.store(true);

  orderBook.requestStop();

  auto signal = fleet.getCapacitySignal();

  if (signal) {
    signal->notifyChange();
  }
}