#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "dispatch/Dispatcher.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/OrderBook.hpp"
#include "orders/OrderProducer.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// =====================================================
// DP7.3 - ESTADISTICAS DESDE EL REGISTRO REAL
// =====================================================

struct EventAudit {
  std::size_t created = 0;
  std::size_t delivered = 0;
  std::size_t rejected = 0;
  std::size_t reassigned = 0;
  std::size_t breakdowns = 0;
  std::size_t stopping = 0;

  long long deliveryDurationSumMs = 0;
  long long maxDeliveryMs = 0;

  std::unordered_map<std::string, std::size_t> deliveredPerCourier;
};

// =====================================================
// VALIDAR EL ARCHIVO JSONL
// =====================================================

EventAudit auditEventLog(const std::string &filename,
                         const std::vector<std::string> &courierIds) {

  std::ifstream input(filename);

  if (!input) {
    throw std::runtime_error("No se pudo leer el JSONL: " + filename);
  }

  EventAudit audit;

  std::unordered_set<std::string> validCouriers;

  for (const std::string &courier : courierIds) {
    validCouriers.insert(courier);
    audit.deliveredPerCourier[courier] = 0;
  }

  std::unordered_map<std::string, long long> creationTimes;
  std::unordered_map<std::string, long long> lastTimes;

  std::unordered_set<std::string> deliveredIds;
  std::unordered_set<std::string> rejectedIds;

  std::string line;
  std::size_t lineNumber = 0;
  bool stoppingSeen = false;

  while (std::getline(input, line)) {

    ++lineNumber;

    if (line.empty()) {
      throw std::runtime_error("Linea vacia en JSONL");
    }

    const nlohmann::json event = nlohmann::json::parse(line);

    if (!event.is_object() || !event.contains("t") ||
        !event["t"].is_number_integer() || !event.contains("event") ||
        !event["event"].is_string()) {
      throw std::runtime_error("Evento JSONL invalido en linea " +
                               std::to_string(lineNumber));
    }

    const long long time = event.at("t").get<long long>();

    const std::string name = event.at("event").get<std::string>();

    if (time < 0) {
      throw std::runtime_error("Tiempo negativo en JSONL");
    }

    if (stoppingSeen) {
      throw std::runtime_error("Evento despues de simulationStopping");
    }

    std::string orderId;

    if (event.contains("order")) {
      orderId = event.at("order").get<std::string>();

      if (orderId.empty()) {
        throw std::runtime_error("Pedido con ID vacio");
      }

      const auto previous = lastTimes.find(orderId);

      if (previous != lastTimes.end() && time < previous->second) {
        throw std::runtime_error("Tiempo no monotono: " + orderId);
      }

      lastTimes[orderId] = time;
    }

    // =========================================
    // PEDIDO CREADO
    // =========================================

    if (name == "orderCreated") {

      if (orderId.empty() || !event.contains("restaurant")) {
        throw std::runtime_error("orderCreated sin campos obligatorios");
      }

      if (!creationTimes.emplace(orderId, time).second) {
        throw std::runtime_error("Pedido creado dos veces: " + orderId);
      }

      ++audit.created;
    }

    // =========================================
    // PEDIDO ASIGNADO
    // =========================================

    else if (name == "orderAssigned") {

      if (creationTimes.find(orderId) == creationTimes.end() ||
          !event.contains("courier")) {
        throw std::runtime_error("orderAssigned invalido: " + orderId);
      }
    }

    // =========================================
    // COMIENZO DE RECOGIDA
    // =========================================

    else if (name == "pickupStarted" || name == "orderPickedUp") {

      if (creationTimes.find(orderId) == creationTimes.end() ||
          !event.contains("courier") || !event.contains("restaurant")) {
        throw std::runtime_error("Evento de recogida invalido: " + orderId);
      }
    }

    // =========================================
    // PEDIDO ENTREGADO
    // =========================================

    else if (name == "orderDelivered") {

      const auto created = creationTimes.find(orderId);

      if (created == creationTimes.end() || rejectedIds.count(orderId) != 0 ||
          !deliveredIds.insert(orderId).second) {
        throw std::runtime_error("Entrega invalida o duplicada: " + orderId);
      }

      const std::string courier = event.at("courier").get<std::string>();

      if (validCouriers.count(courier) == 0) {
        throw std::runtime_error("Repartidor desconocido: " + courier);
      }

      const long long duration = time - created->second;

      if (duration < 0) {
        throw std::runtime_error("Duracion negativa: " + orderId);
      }

      ++audit.delivered;
      ++audit.deliveredPerCourier[courier];

      audit.deliveryDurationSumMs += duration;

      if (duration > audit.maxDeliveryMs) {
        audit.maxDeliveryMs = duration;
      }
    }

    // =========================================
    // PEDIDO RECHAZADO
    // =========================================

    else if (name == "orderRejected") {

      const std::string reason = event.at("reason").get<std::string>();

      const bool validReason = reason == "queueFull" ||
                               reason == "unreachable" || reason == "noCourier";

      if (!validReason || creationTimes.count(orderId) == 0 ||
          deliveredIds.count(orderId) != 0 ||
          !rejectedIds.insert(orderId).second) {
        throw std::runtime_error("Rechazo invalido: " + orderId);
      }

      ++audit.rejected;
    }

    // =========================================
    // AVERIA DE REPARTIDOR
    // =========================================

    else if (name == "courierBreakdown") {

      const std::string courier = event.at("courier").get<std::string>();

      if (validCouriers.count(courier) == 0) {
        throw std::runtime_error("Averia de repartidor desconocido");
      }

      ++audit.breakdowns;
    }

    // =========================================
    // REASIGNACION DE PEDIDO
    // =========================================

    else if (name == "orderReassigned") {

      const std::string from = event.at("from").get<std::string>();

      const std::string to = event.at("to").get<std::string>();

      if (creationTimes.count(orderId) == 0 || validCouriers.count(from) == 0 ||
          validCouriers.count(to) == 0 || from == to) {
        throw std::runtime_error("Reasignacion invalida: " + orderId);
      }

      ++audit.reassigned;
    }

    // =========================================
    // FIN DE SIMULACION
    // =========================================

    else if (name == "simulationStopping") {
      ++audit.stopping;
      stoppingSeen = true;
    }

    else {
      throw std::runtime_error("Evento no permitido: " + name);
    }
  }

  if (!input.eof() || audit.stopping != 1) {
    throw std::runtime_error("JSONL incompleto o sin simulationStopping");
  }

  return audit;
}

// =====================================================
// MAIN - EQUIPEGO
// =====================================================

int main(int argc, char *argv[]) {

  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  try {

    // =========================================
    // 1. CREAR COMPONENTES
    // =========================================

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    // true: emitir JSONL tambien a stdout.
    EventLogger logger(config.logFilePath, true);

    RoadNetwork roadNetwork(config);

    OrderBook orderBook(static_cast<std::size_t>(config.orders.maxPending));

    OrderProducer producer(config, orderBook, logger, clock, roadNetwork);

    Fleet fleet(config, roadNetwork, clock);

    Dispatcher dispatcher(config, roadNetwork, fleet);

    // =========================================
    // 2. INICIAR FLOTA
    // =========================================

    fleet.start();

    std::vector<std::string> courierIds;
    courierIds.reserve(fleet.size());

    for (std::size_t i = 0; i < fleet.size(); ++i) {

      Courier &courier = fleet.at(i);

      courier.setEventLogger(&logger);
      courierIds.push_back(courier.getId());
    }

    fleet.startRescue();

    // =========================================
    // 3. INICIAR DISPATCHER
    // =========================================

    std::thread dispatcherThread(&Dispatcher::run, &dispatcher,
                                 std::ref(orderBook), std::ref(logger),
                                 std::ref(clock));

    // El Dispatcher ya esta iniciado y
    // la flota esta preparada.
    //
    // La linea se imprime ANTES de crear
    // el primer pedido.
    logger.logStart();

    // =========================================
    // 4. INICIAR PRODUCTOR
    // =========================================

    std::thread producerThread(&OrderProducer::run, &producer);

    // =========================================
    // 5. DURACION DE SIMULACION
    // =========================================

    if (config.simulation.durationS > 0) {

      const double realDurationSeconds =
          static_cast<double>(config.simulation.durationS) /
          config.simulation.timeScale;

      std::this_thread::sleep_for(
          std::chrono::duration<double>(realDurationSeconds));
    }

    // =========================================
    // 6. DETENER PRODUCTOR
    // =========================================

    producer.requestStop();
    producerThread.join();

    const std::size_t createdCount = producer.getCreatedCount();

    const std::size_t producerRejected = producer.getRejectedCount();

    // =========================================
    // 7. DETENER DISPATCHER
    // =========================================

    dispatcher.requestStop(orderBook);
    dispatcherThread.join();

    const std::size_t assignedCount = dispatcher.getAssignedCount();

    const std::size_t dispatcherRejected = dispatcher.getRejectedCount();

    const std::size_t dispatcherPending = dispatcher.getPendingCount();

    // =========================================
    // 8. DETENER FLOTA
    // =========================================

    fleet.requestStop();
    fleet.join();

    // =========================================
    // 9. RECUPERAR RESULTADOS DE LA FLOTA
    // =========================================

    std::vector<CourierOrderResult> fleetResults = fleet.takeFinishedOrders();

    const std::size_t fleetFinalizedCount = fleetResults.size();

    std::size_t deliveredCount = 0;
    std::size_t fleetPendingCount = 0;

    std::unordered_set<std::string> fleetOrderIds;

    for (const CourierOrderResult &result : fleetResults) {

      const Order &order = result.order;

      if (!fleetOrderIds.insert(order.orderId).second) {
        throw std::logic_error("Pedido duplicado en Fleet: " + order.orderId);
      }

      if (order.getState() == OrderState::Delivered) {
        ++deliveredCount;

      } else if (order.getState() == OrderState::Pending) {
        ++fleetPendingCount;

      } else {
        throw std::logic_error("Pedido sin estado final: " + order.orderId);
      }
    }

    // =========================================
    // 10. RECUPERAR PEDIDOS DE ORDERBOOK
    // =========================================

    std::vector<Order> remainingOrders = orderBook.takeRemaining();

    std::size_t bookPendingCount = 0;

    for (Order &order : remainingOrders) {

      if (!order.transitionTo(OrderState::Pending)) {
        throw std::logic_error("No se pudo finalizar: " + order.orderId);
      }

      ++bookPendingCount;
    }

    // =========================================
    // 11. CONTABILIDAD DEL SIMULADOR
    // =========================================

    const std::size_t rejectedCount = producerRejected + dispatcherRejected;

    const std::size_t pendingCount =
        fleetPendingCount + dispatcherPending + bookPendingCount;

    const std::size_t totalFinalized =
        deliveredCount + rejectedCount + pendingCount;

    if (fleetFinalizedCount != assignedCount) {
      throw std::logic_error("Los pedidos asignados no coinciden "
                             "con los resultados de Fleet");
    }

    if (totalFinalized != createdCount) {
      throw std::logic_error("Balance global de pedidos incorrecto");
    }

    // =========================================
    // 12. ULTIMO EVENTO DEL JSONL
    // =========================================

    // Todos los hilos ya terminaron.
    // No apareceran eventos nuevos despues.
    logger.logEvent({{"event", "simulationStopping"}},
                    clock.getSimulatedTimeMs());

    logger.flush();

    // =========================================
    // 13. AUDITORIA DEL ARCHIVO
    // =========================================

    const EventAudit audit = auditEventLog(config.logFilePath, courierIds);

    if (audit.created != createdCount || audit.delivered != deliveredCount ||
        audit.rejected != rejectedCount ||
        audit.reassigned != fleet.getRescuedOrderCount() ||
        audit.breakdowns != fleet.getBreakdownCount()) {

      throw std::logic_error("Los eventos JSONL no coinciden "
                             "con los contadores del simulador");
    }

    if (audit.created < audit.delivered + audit.rejected) {

      throw std::logic_error("Mas entregas y rechazos que creaciones");
    }

    const std::size_t pendingFromEvents =
        audit.created - audit.delivered - audit.rejected;

    if (pendingFromEvents != pendingCount) {
      throw std::logic_error("Los pedidos pendientes no coinciden");
    }

    // =========================================
    // 14. CALCULAR METRICAS DE ENTREGA
    // =========================================

    const double avgDeliveryMs =
        audit.delivered > 0 ? static_cast<double>(audit.deliveryDurationSumMs) /
                                  static_cast<double>(audit.delivered)
                            : 0.0;

    nlohmann::json perCourier = nlohmann::json::object();

    std::size_t sumDeliveredPerCourier = 0;

    for (const std::string &courier : courierIds) {

      const std::size_t count = audit.deliveredPerCourier.at(courier);

      perCourier[courier] = count;
      sumDeliveredPerCourier += count;
    }

    if (sumDeliveredPerCourier != audit.delivered) {
      throw std::logic_error("La suma por repartidor no coincide");
    }

    // =========================================
    // 15. RESUMEN FINAL OBLIGATORIO
    // =========================================

    const nlohmann::json summary = {{"created", audit.created},
                                    {"delivered", audit.delivered},
                                    {"rejected", audit.rejected},
                                    {"pending", pendingFromEvents},
                                    {"reassigned", audit.reassigned},
                                    {"breakdowns", audit.breakdowns},
                                    {"avgDeliveryMs", avgDeliveryMs},
                                    {"maxDeliveryMs", audit.maxDeliveryMs},
                                    {"deliveredPerCourier", perCourier}};

    // Debe ser la ultima linea de stdout.
    std::cout << summary.dump() << '\n' << std::flush;

    return 0;

  } catch (const std::exception &error) {

    std::cerr << "Error: " << error.what() << '\n';

    return 1;
  }
}