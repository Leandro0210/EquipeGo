#include "OrderProducer.hpp"

#include "Order.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

OrderProducer::OrderProducer(const SimulationConfig &config,
                             OrderBook &orderBook, EventLogger &logger,
                             SimulationClock &clock,
                             const RoadNetwork &roadNetwork)
    : config(config), orderBook(orderBook), logger(logger), clock(clock),
      roadNetwork(roadNetwork), generator(config.orders) {}

void OrderProducer::run() {
  while (true) {
    const int simulatedWaitMs = generator.nextIntervalMs();

    const double realWaitMs =
        static_cast<double>(simulatedWaitMs) / config.simulation.timeScale;

    {
      std::unique_lock<std::mutex> lock(stopMutex);

      const bool stopped = stopCv.wait_for(
          lock, std::chrono::duration<double, std::milli>(realWaitMs),
          [this]() { return stopRequested; });

      if (stopped) {
        break;
      }
    }

    const long long creationTime = clock.getSimulatedTimeMs();
    const int burstSize = generator.nextBurstSize();

    for (int i = 0; i < burstSize; ++i) {
      const std::size_t restaurantIndex =
          generator.nextRestaurantIndex(config.restaurants.size());

      const std::size_t deliveryNodeIndex =
          generator.nextDeliveryNodeIndex(config.nodes.size());

      const Restaurant &restaurant = config.restaurants[restaurantIndex];

      const Node &deliveryNode = config.nodes[deliveryNodeIndex];

      const std::string orderId = "o" + std::to_string(nextOrderNumber++);

      // Todo pedido primero se registra como creado.
      logger.logEvent({{"event", "orderCreated"},
                       {"order", orderId},
                       {"restaurant", restaurant.id}},
                      creationTime);

      // Si desde el restaurante no existe ruta válida hacia el destino,
      // el pedido termina inmediatamente como unreachable.
      if (!roadNetwork.isReachable(restaurant.nodeId, deliveryNode.id)) {

        logger.logEvent({{"event", "orderRejected"},
                         {"order", orderId},
                         {"reason", "unreachable"}},
                        creationTime);

        continue;
      }

      Order order(orderId, restaurant.id, deliveryNode.id, creationTime);

      // Si el libro ya está lleno, el pedido se rechaza por queueFull.
      if (!orderBook.tryAdd(std::move(order))) {
        logger.logEvent({{"event", "orderRejected"},
                         {"order", orderId},
                         {"reason", "queueFull"}},
                        creationTime);
      }
    }
  }
}

void OrderProducer::requestStop() {
  {
    std::lock_guard<std::mutex> lock(stopMutex);
    stopRequested = true;
  }

  stopCv.notify_all();
}
