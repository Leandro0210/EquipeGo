
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
// INICIAR RESCATE AUTOMATICO
// =====================================================

void Fleet::startRescue() {
  if (!started || rescueStarted || rescueStopRequested.load()) {
    throw std::logic_error("No se puede iniciar el rescate");
  }

  rescueThread = std::thread(&Fleet::rescueLoop, this);

  rescueStarted = true;
}

// =====================================================
// HILO RESCATISTA
// =====================================================

void Fleet::rescueLoop() {
  while (!rescueStopRequested.load()) {

    // Capturamos la version ANTES de
    // examinar la flota. Si ocurre un cambio
    // durante el examen, no perderemos
    // esa notificacion.
    const auto previousVersion = capacitySignal->version();

    // Buscar motos averiadas.
    for (const auto &source : couriers) {

      if (rescueStopRequested.load()) {
        break;
      }

      if (source->getState() != CourierState::Broken) {
        continue;
      }

      const std::size_t outgoing = source->getReservedCount();

      if (outgoing == 0) {
        continue;
      }

      // Buscar una receptora.
      for (const auto &recipient : couriers) {

        if (rescueStopRequested.load()) {
          break;
        }

        if (source.get() == recipient.get()) {
          continue;
        }

        // transferAllTo utiliza scoped_lock
        // sobre ambas motos y comprueba
        // nuevamente estado y capacidad.
        if (source->transferAllTo(*recipient)) {

          successfulRescues.fetch_add(1, std::memory_order_relaxed);

          rescuedOrderCount.fetch_add(outgoing, std::memory_order_relaxed);

          break;
        }
      }
    }

    if (rescueStopRequested.load()) {
      break;
    }

    // Sin busy waiting. Dormir hasta
    // una averia, liberacion de capacidad,
    // transferencia o apagado.
    capacitySignal->waitForChange(previousVersion, rescueStopRequested);
  }
}

// =====================================================
// APAGADO ORDENADO
// =====================================================

void Fleet::requestStop() {
  rescueStopRequested.store(true);

  // Despertar al rescatista.
  capacitySignal->notifyChange();

  // Despertar las colas de restaurantes.
  restaurantManager->requestStop();

  // Interrumpir recorridos y esperas.
  for (const auto &courier : couriers) {
    courier->requestStop();
  }
}

void Fleet::join() {

  // Primero finalizar el rescate:
  // asi no hay nuevas transferencias
  // mientras recolectamos resultados.
  if (rescueThread.joinable()) {
    rescueThread.join();
  }

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
// RESULTADOS FINALES
// =====================================================

std::vector<CourierOrderResult> Fleet::takeFinishedOrders() {

  if (rescueThread.joinable()) {
    throw std::logic_error("Debes finalizar el hilo de rescate "
                           "antes de recuperar pedidos");
  }

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

std::size_t Fleet::getBreakdownCount() const {
  std::size_t total = 0;

  for (const auto &courier : couriers) {
    if (courier->hasBrokenDown()) {
      ++total;
    }
  }

  return total;
}

std::size_t Fleet::getRescueCount() const noexcept {
  return successfulRescues.load();
}

std::size_t Fleet::getRescuedOrderCount() const noexcept {
  return rescuedOrderCount.load();
}