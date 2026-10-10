
#include "fleet/Fleet.hpp"

#include <stdexcept>
#include <string>

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
// INICIAR REPARTIDORES
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

    // Crear todos los actores compartiendo
    // el mismo canal de notificaciones.
    for (std::size_t i = 0; i < count; ++i) {

      const std::string id = "c" + std::to_string(i);

      couriers.push_back(std::make_unique<Courier>(id, config, roadNetwork,
                                                   clock, capacitySignal));
    }

    // Iniciar un hilo independiente por actor.
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
// ESPERAR FINALIZACION
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
