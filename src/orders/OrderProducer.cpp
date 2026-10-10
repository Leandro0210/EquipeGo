
#include "orders/OrderProducer.hpp"

#include "orders/Order.hpp"

#include <chrono>
#include <string>
#include <utility>

// =====================================================
// CONSTRUCTOR
// =====================================================

OrderProducer::OrderProducer(const SimulationConfig &config,
                             OrderBook &orderBook, EventLogger &logger,
                             SimulationClock &clock,
                             const RoadNetwork &roadNetwork)
    : config(config), orderBook(orderBook), logger(logger), clock(clock),
      roadNetwork(roadNetwork), generator(config.orders) {}

// =====================================================
// HILO PRODUCTOR
// =====================================================

void OrderProducer::run() {

  while (true) {

    const int simulatedWaitMs = generator.nextIntervalMs();

    const double realWaitMs =
        static_cast<double>(simulatedWaitMs) / config.simulation.timeScale;

    {
      std::unique_lock<std::mutex> lock(stopMutex);

      const bool stopped = stopCv.wait_for(
          lock, std::chrono::duration<double, std::milli>(realWaitMs),
          [this] { return stopRequested; });

      if (stopped) {
        break;
      }
    }

    const long long creationTime = clock.getSimulatedTimeMs();

    const int burstSize = generator.nextBurstSize();

    // ============================================
    // CREAR PEDIDOS DE LA RAFAGA
    // ============================================

    for (int i = 0; i < burstSize; ++i) {

      const std::size_t restaurantIndex =
          generator.nextRestaurantIndex(config.restaurants.size());

      const std::size_t deliveryNodeIndex =
          generator.nextDeliveryNodeIndex(config.nodes.size());

      const Restaurant &restaurant = config.restaurants[restaurantIndex];

      const Node &deliveryNode = config.nodes[deliveryNodeIndex];

      const std::string orderId = "o" + std::to_string(nextOrderNumber++);

      // Registrar creacion.
      logger.logEvent({{"event", "orderCreated"},
                       {"order", orderId},
                       {"restaurant", restaurant.id}},
                      creationTime);

      // ========================================
      // RECHAZO: RUTA INALCANZABLE
      // ========================================

      if (!roadNetwork.isReachable(restaurant.nodeId, deliveryNode.id)) {

        logger.logEvent({{"event", "orderRejected"},
                         {"order", orderId},
                         {"reason", "unreachable"}},
                        creationTime);

        ++rejectedCount;
        continue;
      }

      // ========================================
      // ENVIAR A ORDERBOOK
      // ========================================

      Order order(orderId, restaurant.id, deliveryNode.id, creationTime);

      if (!orderBook.tryAdd(std::move(order))) {

        logger.logEvent({{"event", "orderRejected"},
                         {"order", orderId},
                         {"reason", "queueFull"}},
                        creationTime);

        ++rejectedCount;
      }
    }
  }
}

// =====================================================
// APAGADO DEL PRODUCTOR
// =====================================================

void OrderProducer::requestStop() {

  {
    std::lock_guard<std::mutex> lock(stopMutex);
    stopRequested = true;
  }

  stopCv.notify_all();
}
