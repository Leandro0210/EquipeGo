
#include "fleet/Fleet.hpp"

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

// =====================================================
// CONSTRUCTOR
// =====================================================

Fleet::Fleet(const SimulationConfig &config, const RoadNetwork &roadNetwork,
             SimulationClock &clock)
    : config(config), roadNetwork(roadNetwork), clock(clock),
      capacitySignal(std::make_shared<CapacitySignal>()),
      restaurantManager(
          std::make_shared<RestaurantManager>(config.restaurants)) {}

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
// DP6: RESCATE ATOMICO DE LA BOLSA COMPLETA
// =====================================================

void Fleet::rescueLoop() {

  while (!rescueStopRequested.load()) {

    // Capturamos la version antes de revisar
    // la flota para no perder notificaciones.
    const auto previousVersion = capacitySignal->version();

    // =========================================
    // 1. BUSCAR MOTOS AVERIADAS
    // =========================================

    for (const auto &source : couriers) {

      if (rescueStopRequested.load()) {
        break;
      }

      if (source->getState() != CourierState::Broken) {
        continue;
      }

      // Una moto averiada puede conservar
      // pedidos activos, asignados o bloqueados.
      const std::size_t outgoing = source->getReservedCount();

      if (outgoing == 0) {
        continue;
      }

      // =====================================
      // 2. BUSCAR UNA RECEPTORA CAPAZ
      // =====================================

      for (const auto &recipient : couriers) {

        if (rescueStopRequested.load()) {
          break;
        }

        if (source.get() == recipient.get()) {
          continue;
        }

        // =================================
        // 3. TRANSFERENCIA TODO O NADA
        // =================================

        // transferAllTo() bloquea ambos
        // mutex y verifica de nuevo:
        //
        // - fuente averiada;
        // - receptora operativa;
        // - ausencia de apagado;
        // - capacidad para TODA la bolsa;
        // - ausencia de IDs duplicados.
        //
        // Si no cabe la carga completa,
        // no se transfiere ningun pedido.
        //
        // No dividimos la bolsa entre
        // distintas motos.
        if (source->transferAllTo(*recipient)) {

          // Contabilizar una operacion
          // de rescate de bolsa completa.
          successfulRescues.fetch_add(1, std::memory_order_relaxed);

          // Contabilizar todos los pedidos
          // incluidos en esa transferencia.
          rescuedOrderCount.fetch_add(outgoing, std::memory_order_relaxed);

          // El receptor fue notificado por
          // transferAllTo(). Avisamos tambien
          // del cambio a los observadores.
          capacitySignal->notifyChange();

          // Bolsa totalmente rescatada.
          break;
        }
      }
    }

    if (rescueStopRequested.load()) {
      break;
    }

    // =========================================
    // 4. ESPERA SIN BUSY WAITING
    // =========================================

    // Si no hay espacio para toda la bolsa,
    // esperamos hasta que alguna moto libere
    // capacidad, ocurra otra averia o
    // comience el apagado.
    capacitySignal->waitForChange(previousVersion, rescueStopRequested);
  }
}

// =====================================================
// APAGADO ORDENADO
// =====================================================

void Fleet::requestStop() {

  rescueStopRequested.store(true);

  // Despertar al coordinador.
  capacitySignal->notifyChange();

  // Interrumpir las colas de restaurantes.
  restaurantManager->requestStop();

  // Interrumpir repartidores.
  for (const auto &courier : couriers) {
    courier->requestStop();
  }
}

// =====================================================
// ESPERAR TODOS LOS HILOS
// =====================================================

void Fleet::join() {

  // El coordinador debe finalizar antes de
  // recolectar los pedidos de la flota.
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
// RECUPERAR RESULTADOS FINALES
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

// =====================================================
// DP6: MOTOS AVERIADAS
// =====================================================

std::size_t Fleet::getBreakdownCount() const {

  std::size_t total = 0;

  for (const auto &courier : couriers) {
    if (courier->hasBrokenDown()) {
      ++total;
    }
  }

  return total;
}

// =====================================================
// DP6: RESCATES EXITOSOS
// =====================================================

std::size_t Fleet::getRescueCount() const noexcept {
  return successfulRescues.load();
}

// =====================================================
// DP6: PEDIDOS RESCATADOS
// =====================================================

std::size_t Fleet::getRescuedOrderCount() const noexcept {
  return rescuedOrderCount.load();
}
