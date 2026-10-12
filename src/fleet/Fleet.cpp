
#include "fleet/Fleet.hpp"

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
// DP6: COORDINADOR DE RESCATE
// =====================================================

void Fleet::rescueLoop() {

  // Solo el hilo rescatista accede a este
  // vector, por eso no necesita otro mutex.
  //
  // Cada moto averiada se contabiliza una vez
  // cuando consigue su primer rescate.
  std::vector<bool> rescueRecorded(couriers.size(), false);

  while (!rescueStopRequested.load()) {

    // Capturar la version ANTES de examinar
    // las motos evita perder notificaciones.
    const auto previousVersion = capacitySignal->version();

    // =========================================
    // 1. BUSCAR MOTOS AVERIADAS
    // =========================================

    for (std::size_t sourceIndex = 0; sourceIndex < couriers.size();
         ++sourceIndex) {

      if (rescueStopRequested.load()) {
        break;
      }

      Courier &source = *couriers[sourceIndex];

      if (source.getState() != CourierState::Broken) {
        continue;
      }

      if (source.getReservedCount() == 0) {
        continue;
      }

      // =====================================
      // 2. BUSCAR MOTOS RECEPTORAS
      // =====================================

      for (std::size_t recipientIndex = 0; recipientIndex < couriers.size();
           ++recipientIndex) {

        if (rescueStopRequested.load()) {
          break;
        }

        // Una moto no se rescata a si misma.
        if (sourceIndex == recipientIndex) {
          continue;
        }

        Courier &recipient = *couriers[recipientIndex];

        // =================================
        // 3. TRANSFERIR PEDIDOS
        // =================================

        // Cada operacion exitosa mueve
        // exactamente un pedido y reduce
        // la carga pendiente de la averiada.
        //
        // El bucle termina cuando:
        // - la fuente se queda sin pedidos;
        // - el destino se llena;
        // - el destino deja de ser valido;
        // - se solicita apagar.
        //
        // No es busy waiting: cada vuelta
        // debe completar una transferencia.
        while (!rescueStopRequested.load() && source.transferOneTo(recipient)) {

          // Contar una sola vez la moto
          // cuyo rescate fue iniciado.
          if (!rescueRecorded[sourceIndex]) {
            rescueRecorded[sourceIndex] = true;

            successfulRescues.fetch_add(1, std::memory_order_relaxed);
          }

          // Cada pedido transferido cuenta
          // una vez, aunque las transferencias
          // sean hacia motos diferentes.
          rescuedOrderCount.fetch_add(1, std::memory_order_relaxed);

          // Publicar actualizacion para
          // dispatcher y pruebas.
          capacitySignal->notifyChange();
        }
      }
    }

    if (rescueStopRequested.load()) {
      break;
    }

    // =========================================
    // 4. ESPERAR CAMBIOS SIN BUSY WAITING
    // =========================================

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

  // Despertar las colas FIFO de restaurantes.
  restaurantManager->requestStop();

  // Interrumpir recorridos y esperas.
  for (const auto &courier : couriers) {
    courier->requestStop();
  }
}

// =====================================================
// ESPERAR TERMINACION DE LOS HILOS
// =====================================================

void Fleet::join() {

  // Primero finalizar el hilo de rescate.
  if (rescueThread.joinable()) {
    rescueThread.join();
  }

  // Luego finalizar todos los repartidores.
  for (std::thread &worker : threads) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

// =====================================================
// CONSULTAS DE FLOTA
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
// DP6: CONTADOR DE MOTOS AVERIADAS
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
// DP6: CONTADOR DE RESCATES EXITOSOS
// =====================================================

std::size_t Fleet::getRescueCount() const noexcept {
  return successfulRescues.load();
}

// =====================================================
// DP6: CONTADOR DE PEDIDOS RESCATADOS
// =====================================================

std::size_t Fleet::getRescuedOrderCount() const noexcept {
  return rescuedOrderCount.load();
}
