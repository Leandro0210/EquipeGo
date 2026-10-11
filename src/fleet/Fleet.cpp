
#include "fleet/Fleet.hpp"

#include <stdexcept>
#include <string>
#include <utility>

Fleet::Fleet(const SimulationConfig &config, const RoadNetwork &roadNetwork,
             SimulationClock &clock)
    : config(config), roadNetwork(roadNetwork), clock(clock),
      capacitySignal(std::make_shared<CapacitySignal>()),
      restaurantManager(
          std::make_shared<RestaurantManager>(config.restaurants)) {}

Fleet::~Fleet() {
  requestStop();
  join();
}

// =====================================================
// INICIAR FLOTA
// =====================================================

void Fleet::start() {
  if (started) {
    throw std::logic_error("La flota ya fue iniciada");
  }

  started = true;

  try {
    const std::size_t count = static_cast<std::size_t>(config.fleet.couriers);

    couriers.reserve(count);
    threads.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
      const std::string id = "c" + std::to_string(i);

      // Todas las motos reciben el MISMO
      // administrador de restaurantes.
      couriers.push_back(std::make_unique<Courier>(
          id, config, roadNetwork, clock, capacitySignal, restaurantManager));
    }

    for (const auto &courier : couriers) {
      threads.emplace_back(&Courier::run, courier.get());
    }

  } catch (...) {
    requestStop();
    join();
    throw;
  }
}

// =====================================================
// APAGADO ORDENADO
// =====================================================

void Fleet::requestStop() {
  // Primero despertar filas de restaurantes.
  restaurantManager->requestStop();

  // Despues interrumpir recorridos y esperas
  // de los repartidores.
  for (const auto &courier : couriers) {
    courier->requestStop();
  }
}

void Fleet::join() {
  for (std::thread &worker : threads) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

// =====================================================
// CONSULTAS
// =====================================================

std::size_t Fleet::size() const noexcept { return couriers.size(); }

Courier &Fleet::at(std::size_t index) { return *couriers.at(index); }

std::shared_ptr<CapacitySignal> Fleet::getCapacitySignal() const {
  return capacitySignal;
}

std::shared_ptr<RestaurantManager> Fleet::getRestaurantManager() const {
  return restaurantManager;
}

// =====================================================
// RESULTADOS FINALES DE LA FLOTA
// =====================================================

std::vector<CourierOrderResult> Fleet::takeFinishedOrders() {

  for (const std::thread &worker : threads) {
    if (worker.joinable()) {
      throw std::logic_error("Debes ejecutar Fleet::join() "
                             "antes de recuperar pedidos");
    }
  }

  std::vector<CourierOrderResult> results;

  for (const auto &courier : couriers) {
    const std::string courierId = courier->getId();

    std::vector<Order> finished = courier->takeFinishedOrders();

    for (Order &order : finished) {
      results.push_back(CourierOrderResult{courierId, std::move(order)});
    }
  }

  return results;
}
