
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

  // -------------------------------------------------
  // 1. BUSCAR EL RESTAURANTE
  // -------------------------------------------------

  const auto restaurant = std::find_if(
      config.restaurants.begin(), config.restaurants.end(),
      [&order](const Restaurant &r) { return r.id == order.restauranteId; });

  if (restaurant == config.restaurants.end()) {
    throw std::invalid_argument("Restaurante inexistente: " +
                                order.restauranteId);
  }

  const std::string restaurantNode = restaurant->nodeId;

  const std::string deliveryNode = order.deliveryNodeId;

  // -------------------------------------------------
  // 2. ESTRUCTURA PARA FUTURES
  // -------------------------------------------------

  struct PendingQuote {
    std::string courierId;
    std::future<EtaQuote> future;
  };

  std::vector<PendingQuote> pending;

  pending.reserve(fleet.size());

  // -------------------------------------------------
  // 3. LANZAR CALCULOS EN PARALELO
  // -------------------------------------------------

  for (std::size_t i = 0; i < fleet.size(); ++i) {

    Courier &courier = fleet.at(i);

    // Ignorar repartidores averiados.
    if (courier.getState() == CourierState::Broken) {
      continue;
    }

    // Ignorar repartidores sin capacidad.
    if (courier.getReservedCount() >=
        static_cast<std::size_t>(config.fleet.bagCapacity)) {
      continue;
    }

    // Obtener posicion actual de forma segura.
    const std::string startNode = courier.getCurrentNode();

    const std::string courierId = courier.getId();

    // Cada candidato calcula su ETA
    // mediante una tarea concurrente.
    auto future = std::async(
        std::launch::async,

        [this, i, courierId, startNode, restaurantNode,
         deliveryNode]() -> EtaQuote {
          // Ruta desde la moto al restaurante.
          const auto toRestaurant =
              roadNetwork.shortestRoute(startNode, restaurantNode);

          // Ruta desde el restaurante al cliente.
          const auto toCustomer =
              roadNetwork.shortestRoute(restaurantNode, deliveryNode);

          // Comprobar que ambas rutas existen.
          if (!toRestaurant || !toCustomer) {
            throw std::runtime_error("No existe una ruta valida");
          }

          // Distancia total en metros.
          const double totalMeters =
              toRestaurant->distanceMeters + toCustomer->distanceMeters;

          // Convertir km/h a m/s.
          const double speedMetersPerSecond = config.fleet.speedKmh / 3.6;

          // ETA en segundos simulados.
          const double eta = totalMeters / speedMetersPerSecond;

          return EtaQuote{i, courierId, eta};
        });

    // Guardar el future sin esperar todavia.
    pending.push_back(PendingQuote{courierId, std::move(future)});
  }

  // -------------------------------------------------
  // 4. RECOLECTAR RESULTADOS
  // -------------------------------------------------

  for (auto &task : pending) {

    try {
      // Obtener resultado de la tarea.
      EtaQuote quote = task.future.get();

      batch.quotes.push_back(std::move(quote));

    } catch (const std::exception &e) {

      // Un error individual no debe detener
      // las cotizaciones de las otras motos.
      batch.failures.push_back(QuoteFailure{task.courierId, e.what()});
    }
  }

  return batch;
}

// =====================================================
// SELECCIONAR EL MEJOR REPARTIDOR
// =====================================================

std::optional<std::string> Dispatcher::assignBestCourier(Order &order) const {

  // -------------------------------------------------
  // 1. CALCULAR ETA DE LOS CANDIDATOS
  // -------------------------------------------------

  QuoteBatch batch = quoteCandidates(order);

  // -------------------------------------------------
  // 2. ORDENAR POR MENOR ETA
  // -------------------------------------------------

  std::sort(batch.quotes.begin(), batch.quotes.end(),

            [](const EtaQuote &a, const EtaQuote &b) {
              // Prioridad: menor tiempo de entrega.
              if (a.etaSimulatedSeconds != b.etaSimulatedSeconds) {

                return a.etaSimulatedSeconds < b.etaSimulatedSeconds;
              }

              // Desempate: menor indice.
              return a.courierIndex < b.courierIndex;
            });

  // -------------------------------------------------
  // 3. INTENTAR ASIGNAR AL MEJOR
  // -------------------------------------------------

  for (const EtaQuote &quote : batch.quotes) {

    Courier &courier = fleet.at(quote.courierIndex);

    // La comprobacion final de capacidad
    // ocurre dentro de Courier::tryAssignOrder().
    //
    // Si rechaza antes de moverlo, el pedido
    // sigue disponible para otro candidato.

    if (courier.tryAssignOrder(std::move(order))) {

      // Asignacion realizada correctamente.
      return quote.courierId;
    }
  }

  // -------------------------------------------------
  // 4. NINGUN REPARTIDOR DISPONIBLE
  // -------------------------------------------------

  // El pedido sigue perteneciendo al llamador.
  // Posteriormente implementaremos reintentos
  // hasta acceptTimeoutMs.

  return std::nullopt;
}
