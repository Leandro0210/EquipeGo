
#include "fleet/Fleet.hpp"

#include <stdexcept>
#include <string>
#include <utility>

// =====================================================
// CONSTRUCTOR
// =====================================================

Fleet::Fleet(const SimulationConfig &config, const RoadNetwork &roadNetwork,
             SimulationClock &clock)
    : config(config), roadNetwork(roadNetwork), clock(clock),
      capacitySignal(std::make_shared<CapacitySignal>()) {}

// =====================================================
// DESTRUCTOR
// =====================================================

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

      couriers.push_back(std::make_unique<Courier>(id, config, roadNetwork,
                                                   clock, capacitySignal));
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
// DETENER FLOTA
// =====================================================

void Fleet::requestStop() {

  for (const auto &courier : couriers) {
    courier->requestStop();
  }
}

// =====================================================
// ESPERAR HILOS
// =====================================================

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

// =====================================================
// NUEVO: RECOLECTAR ESTADOS FINALES
// =====================================================

std::vector<CourierOrderResult> Fleet::takeFinishedOrders() {

  // No debemos recuperar resultados mientras
  // los trabajadores continuan ejecutandose.
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

      // Transferir propiedad y conservar
      // el identificador de la moto.
      results.push_back(CourierOrderResult{courierId, std::move(order)});
    }
  }

  return results;
}
