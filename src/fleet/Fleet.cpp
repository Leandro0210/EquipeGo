
#include "fleet/Fleet.hpp"

#include <stdexcept>
#include <string>

Fleet::Fleet(const SimulationConfig &config, const RoadNetwork &roadNetwork,
             SimulationClock &clock)
    : config(config), roadNetwork(roadNetwork), clock(clock) {}

// Asegura que ningun hilo quede activo al destruir Fleet.
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

    // 1. Crear todos los repartidores.
    for (std::size_t i = 0; i < count; ++i) {
      const std::string id = "c" + std::to_string(i);

      couriers.push_back(
          std::make_unique<Courier>(id, config, roadNetwork, clock));
    }

    // 2. Crear un hilo por repartidor.
    for (const auto &courier : couriers) {
      threads.emplace_back(&Courier::run, courier.get());
    }

  } catch (...) {
    // Si falla la creacion de algun hilo,
    // detener y unir los que ya fueron iniciados.
    requestStop();
    join();
    throw;
  }
}

// =====================================================
// DETENER TODOS LOS REPARTIDORES
// =====================================================

void Fleet::requestStop() {
  for (const auto &courier : couriers) {
    courier->requestStop();
  }
}

// =====================================================
// ESPERAR FINALIZACION DE HILOS
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
