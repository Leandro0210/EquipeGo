#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "dispatch/Dispatcher.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/OrderBook.hpp"
#include "orders/OrderProducer.hpp"
#include "visualization/SimulationDashboard.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
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
// AUDITORIA ORIGINAL DP7 (SIN CAMBIAR EL CONTRATO)
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

    if (name == "orderCreated") {
      if (orderId.empty() || !event.contains("restaurant")) {
        throw std::runtime_error("orderCreated sin campos obligatorios");
      }
      if (!creationTimes.emplace(orderId, time).second) {
        throw std::runtime_error("Pedido creado dos veces: " + orderId);
      }
      ++audit.created;
    } else if (name == "orderAssigned") {
      if (creationTimes.find(orderId) == creationTimes.end() ||
          !event.contains("courier")) {
        throw std::runtime_error("orderAssigned invalido: " + orderId);
      }
    } else if (name == "pickupStarted" || name == "orderPickedUp") {
      if (creationTimes.find(orderId) == creationTimes.end() ||
          !event.contains("courier") || !event.contains("restaurant")) {
        throw std::runtime_error("Evento de recogida invalido: " + orderId);
      }
    } else if (name == "orderDelivered") {
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
    } else if (name == "orderRejected") {
      const std::string reason = event.at("reason").get<std::string>();
      const bool validReason = reason == "queueFull" ||
                               reason == "unreachable" ||
                               reason == "noCourier";
      if (!validReason || creationTimes.count(orderId) == 0 ||
          deliveredIds.count(orderId) != 0 ||
          !rejectedIds.insert(orderId).second) {
        throw std::runtime_error("Rechazo invalido: " + orderId);
      }
      ++audit.rejected;
    } else if (name == "courierBreakdown") {
      const std::string courier = event.at("courier").get<std::string>();
      if (validCouriers.count(courier) == 0) {
        throw std::runtime_error("Averia de repartidor desconocido");
      }
      ++audit.breakdowns;
    } else if (name == "orderReassigned") {
      const std::string from = event.at("from").get<std::string>();
      const std::string to = event.at("to").get<std::string>();
      if (creationTimes.count(orderId) == 0 || validCouriers.count(from) == 0 ||
          validCouriers.count(to) == 0 || from == to) {
        throw std::runtime_error("Reasignacion invalida: " + orderId);
      }
      ++audit.reassigned;
    } else if (name == "simulationStopping") {
      ++audit.stopping;
      stoppingSeen = true;
    } else {
      throw std::runtime_error("Evento no permitido: " + name);
    }
  }

  if (!input.eof() || audit.stopping != 1) {
    throw std::runtime_error("JSONL incompleto o sin simulationStopping");
  }
  return audit;
}

// =====================================================
// DP8.5: OPCIONES DEL SIMULADOR
// =====================================================

namespace {

enum class DisplayMode { Auto, Gui, Headless };

bool graphicsEnvironmentPresent() {
#if defined(__linux__)
  const char *x = std::getenv("DISPLAY");
  const char *wayland = std::getenv("WAYLAND_DISPLAY");
  return (x && *x) || (wayland && *wayland);
#else
  return true;
#endif
}

struct RunOptions {
  DisplayMode display = DisplayMode::Auto;
  bool hasScale = false;
  double scale = 0.0;
  bool hasDuration = false;
  int duration = 0;
};

bool readOptions(int argc, char *argv[], RunOptions &options) {
  if (argc < 2) {
    std::cerr << "Uso: delivery_sim config.json [--gui|--headless] "
                 "[--scale N] [--duration SEGUNDOS]\n";
    return false;
  }

  for (int i = 2; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--gui") {
      options.display = DisplayMode::Gui;
    } else if (argument == "--headless") {
      options.display = DisplayMode::Headless;
    } else if (argument == "--scale" && i + 1 < argc) {
      const std::string value = argv[++i];
      try {
        std::size_t consumed = 0;
        options.scale = std::stod(value, &consumed);
        if (consumed != value.size() || !std::isfinite(options.scale) ||
            options.scale < 0.25 || options.scale > 120.0) {
          throw std::invalid_argument("escala fuera de rango");
        }
        options.hasScale = true;
      } catch (const std::exception &) {
        std::cerr << "Escala invalida (0.25 a 120)\n";
        return false;
      }
    } else if (argument == "--duration" && i + 1 < argc) {
      const std::string value = argv[++i];
      try {
        std::size_t consumed = 0;
        options.duration = std::stoi(value, &consumed);
        if (consumed != value.size() || options.duration < 1 ||
            options.duration > 86400) {
          throw std::invalid_argument("duracion fuera de rango");
        }
        options.hasDuration = true;
      } catch (const std::exception &) {
        std::cerr << "Duracion invalida (1 a 86400 segundos simulados)\n";
        return false;
      }
    } else {
      std::cerr << "Opcion desconocida: " << argument << '\n';
      return false;
    }
  }
  return true;
}

} // namespace

// =====================================================
// MAIN - EQUIPEGO + OPEN CV REAL
// =====================================================

int main(int argc, char *argv[]) {
  RunOptions options;
  if (!readOptions(argc, argv, options)) return 1;

  // ConfigLoader recibe los dos argumentos que siempre ha aceptado.
  char *configArgs[] = {argv[0], argv[1]};
  SimulationConfig config;
  if (!loadAndValidateConfig(2, configArgs, config)) return 1;

  const bool showGui =
      options.display == DisplayMode::Gui ||
      (options.display == DisplayMode::Auto && graphicsEnvironmentPresent());

  if (options.hasScale) {
    config.simulation.timeScale = options.scale;
  } else if (showGui) {
    // La simulacion visual no debe correr a 60x por defecto.
    config.simulation.timeScale = 3.0;
  }
  if (options.hasDuration) {
    config.simulation.durationS = options.duration;
  }

  try {
    // =========================================
    // 1. CREAR COMPONENTES ORIGINALES
    // =========================================
    SimulationClock clock;
    clock.start(config.simulation.timeScale);
    EventLogger logger(config.logFilePath, true);
    RoadNetwork roadNetwork(config);
    OrderBook orderBook(static_cast<std::size_t>(config.orders.maxPending));
    OrderProducer producer(config, orderBook, logger, clock, roadNetwork);
    Fleet fleet(config, roadNetwork, clock);
    Dispatcher dispatcher(config, roadNetwork, fleet);

    // HighGUI solo corre en este hilo. Se abre ANTES de iniciar
    // los threads del productor y dispatcher para no dejarlos
    // vivos si falla la inicializacion de la ventana.
    std::unique_ptr<SimulationDashboard> dashboard;
    if (showGui) {
      dashboard = std::make_unique<SimulationDashboard>(config, roadNetwork);
      dashboard->open();
    }

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
    // 3. DESPACHADOR + MENSAJE INICIAL
    // =========================================
    std::thread dispatcherThread(&Dispatcher::run, &dispatcher,
                                 std::ref(orderBook), std::ref(logger),
                                 std::ref(clock));
    logger.logStart();

    // =========================================
    // 4. PRODUCTOR DE PEDIDOS REALES
    // =========================================
    std::thread producerThread(&OrderProducer::run, &producer);

    // =========================================
    // 5. VENTANA O ESPERA SIN INTERFAZ
    // =========================================
    if (config.simulation.durationS > 0) {
      const double realDurationSeconds =
          static_cast<double>(config.simulation.durationS) /
          config.simulation.timeScale;
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::duration<double>(realDurationSeconds);
      if (dashboard) {
        while (std::chrono::steady_clock::now() < deadline) {
          bool keepOpen = false;
          try {
            keepOpen = dashboard->frame(
                fleet, logger.snapshotStats(), clock.getSimulatedTimeMs());
          } catch (const std::exception &error) {
            // Incluso un error grafico termina mediante cierre normal.
            std::cerr << "Aviso DP8.5 (OpenCV): " << error.what() << '\n';
          }
          if (!keepOpen) break;
        }
        dashboard->close();
      } else {
        std::this_thread::sleep_for(
            std::chrono::duration<double>(realDurationSeconds));
      }
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
    // 9. RECUPERAR RESULTADOS
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
    // 10. PENDIENTES DE ORDERBOOK
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
    // 12. ULTIMO EVENTO JSONL
    // =========================================
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
    // 14. METRICAS
    // =========================================
    const double avgDeliveryMs =
        audit.delivered > 0
            ? static_cast<double>(audit.deliveryDurationSumMs) /
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
    // 15. RESUMEN FINAL - ULTIMA LINEA STDOUT
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
    std::cout << summary.dump() << '\n' << std::flush;
    return 0;

  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }
}
