
#include "dispatch/Dispatcher.hpp"

#include <algorithm>
#include <future>
#include <stdexcept>
#include <utility>
#include <vector>

// =====================================================
// CONSTRUCTOR
// =====================================================

Dispatcher::Dispatcher(const SimulationConfig &config,
                       const RoadNetwork &roadNetwork, Fleet &fleet)
    : config(config), roadNetwork(roadNetwork), fleet(fleet) {}

// =====================================================
// COTIZACIONES ETA CONCURRENTES
// =====================================================

QuoteBatch Dispatcher::quoteCandidates(const Order &order) const {

  QuoteBatch batch;

  // 1. Encontrar el restaurante del pedido.
  const auto restaurant = std::find_if(
      config.restaurants.begin(), config.restaurants.end(),
      [&order](const Restaurant &r) { return r.id == order.restauranteId; });

  if (restaurant == config.restaurants.end()) {
    throw std::invalid_argument("Restaurante inexistente: " +
                                order.restauranteId);
  }

  const std::string restaurantNode = restaurant->nodeId;
  const std::string deliveryNode = order.deliveryNodeId;

  // Cada future almacenara el resultado de una tarea.
  struct PendingQuote {
    std::string courierId;
    std::future<EtaQuote> future;
  };

  std::vector<PendingQuote> pending;
  pending.reserve(fleet.size());

  // ================================================
  // 2. LANZAR CALCULOS CONCURRENTES
  // ================================================

  for (std::size_t i = 0; i < fleet.size(); ++i) {
    Courier &courier = fleet.at(i);

    // Consultas seguras: cada una usa el mutex
    // interno del repartidor.
    if (courier.getState() == CourierState::Broken) {
      continue;
    }

    if (courier.getReservedCount() >=
        static_cast<std::size_t>(config.fleet.bagCapacity)) {
      continue;
    }

    // Captura de la posicion actual de la moto.
    const std::string startNode = courier.getCurrentNode();

    const std::string courierId = courier.getId();

    // La politica async fuerza ejecucion concurrente.
    auto future = std::async(
        std::launch::async,
        [this, i, courierId, startNode, restaurantNode,
         deliveryNode]() -> EtaQuote {
          // Ruta de moto al restaurante.
          const auto toRestaurant =
              roadNetwork.shortestRoute(startNode, restaurantNode);

          // Ruta de restaurante al cliente.
          const auto toCustomer =
              roadNetwork.shortestRoute(restaurantNode, deliveryNode);

          if (!toRestaurant || !toCustomer) {
            throw std::runtime_error("No existe una ruta valida");
          }

          const double totalMeters =
              toRestaurant->distanceMeters + toCustomer->distanceMeters;

          const double speedMetersPerSecond = config.fleet.speedKmh / 3.6;

          return EtaQuote{i, courierId, totalMeters / speedMetersPerSecond};
        });

    pending.push_back(PendingQuote{courierId, std::move(future)});
  }

  // ================================================
  // 3. RECOLECTAR RESULTADOS
  // ================================================

  for (auto &task : pending) {
    try {
      // get obtiene la cotizacion o propaga
      // la excepcion de esta tarea.
      batch.quotes.push_back(task.future.get());

    } catch (const std::exception &e) {
      // Fallo individual: no perder las demas ETA.
      batch.failures.push_back(QuoteFailure{task.courierId, e.what()});
    }
  }

  return batch;
}
