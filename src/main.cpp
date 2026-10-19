#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "dispatch/Dispatcher.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/OrderBook.hpp"
#include "orders/OrderProducer.hpp"
#include "visualization/SimulationDashboard.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <climits>
#include <csignal>
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

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#define EQUIPEGO_HAS_POSIX_SIGNALS 1
#else
#define EQUIPEGO_HAS_POSIX_SIGNALS 0
#endif


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


namespace {

#ifdef EQUIPEGO_BENCHMARK_CLI
enum class DisplayMode { Auto, Gui, Headless };
#endif

struct RunOptions {
#ifdef EQUIPEGO_BENCHMARK_CLI
  DisplayMode display = DisplayMode::Auto;
  bool hasScale = false;
  double scale = 0.0;
  bool hasDuration = false;
  int duration = 0;
#endif
  bool hasLogPath = false;
  std::string logPath;
};

bool readOptions(int argc, char *argv[], RunOptions &options) {
  if (argc < 2) {
#ifdef EQUIPEGO_BENCHMARK_CLI
    std::cerr << "Uso: delivery_sim_bench config.json [--gui|--headless] "
                 "[--scale N] [--duration SEGUNDOS] [--log RUTA]\n";
#else
    std::cerr << "Uso: delivery_sim config.json [--log RUTA]\n";
#endif
    return false;
  }

  for (int i = 2; i < argc; ++i) {
    const std::string argument = argv[i];
#ifdef EQUIPEGO_BENCHMARK_CLI
    if (argument == "--gui") {
      options.display = DisplayMode::Gui;
    } else if (argument == "--headless") {
      options.display = DisplayMode::Headless;
    } else
#endif
    if (argument == "--log" && i + 1 < argc) {
      options.logPath = argv[++i];
      if (options.logPath.empty()) {
        std::cerr << "Ruta de log vacia\n";
        return false;
      }
      options.hasLogPath = true;
#ifdef EQUIPEGO_BENCHMARK_CLI
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
        if (consumed != value.size() || options.duration < 0 ||
            options.duration > 86400) {
          throw std::invalid_argument("duracion fuera de rango");
        }
        options.hasDuration = true;
      } catch (const std::exception &) {
        std::cerr << "Duracion invalida (0 a 86400 segundos simulados)\n";
        return false;
      }
#endif
    } else {
      std::cerr << "Opcion desconocida: " << argument << '\n';
      return false;
    }
  }
  return true;
}


static_assert(std::atomic<bool>::is_always_lock_free,
              "El indicador de apagado debe ser lock-free");
std::atomic<bool> gSignalStopRequested{false};

#if EQUIPEGO_HAS_POSIX_SIGNALS
volatile std::sig_atomic_t gSignalWakeWriteFd = -1;

extern "C" void handleStopSignal(int) noexcept {
  const int originalErrno = errno;
  gSignalStopRequested.store(true, std::memory_order_relaxed);
  const int descriptor = static_cast<int>(gSignalWakeWriteFd);
  if (descriptor >= 0) {
    const unsigned char wake = 1;
    const ssize_t ignored = ::write(descriptor, &wake, sizeof(wake));
    (void)ignored; 
  }
  errno = originalErrno;
}
#else
extern "C" void handleStopSignal(int) noexcept { gSignalStopRequested.store(true, std::memory_order_relaxed); }
#endif

class SignalStopController {
public:
  SignalStopController() {
    gSignalStopRequested.store(false, std::memory_order_relaxed);
#if EQUIPEGO_HAS_POSIX_SIGNALS
    int descriptors[2] = {-1, -1};
    if (::pipe(descriptors) != 0) {
      throw std::runtime_error("No se pudo crear canal de senales");
    }
    readFd = descriptors[0];
    writeFd = descriptors[1];

    const int flags = ::fcntl(writeFd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(writeFd, F_SETFL, flags | O_NONBLOCK) < 0) {
      closePipe();
      throw std::runtime_error("No se pudo configurar canal de senales");
    }
    gSignalWakeWriteFd = static_cast<std::sig_atomic_t>(writeFd);

    struct sigaction handlerAction {};
    handlerAction.sa_handler = handleStopSignal;
    ::sigemptyset(&handlerAction.sa_mask);
    handlerAction.sa_flags = 0;

    if (::sigaction(SIGINT, &handlerAction, &previousInt) != 0) {
      gSignalWakeWriteFd = -1;
      closePipe();
      throw std::runtime_error("No se pudo instalar SIGINT");
    }
    intInstalled = true;
    if (::sigaction(SIGTERM, &handlerAction, &previousTerm) != 0) {
      ::sigaction(SIGINT, &previousInt, nullptr);
      intInstalled = false;
      gSignalWakeWriteFd = -1;
      closePipe();
      throw std::runtime_error("No se pudo instalar SIGTERM");
    }
    termInstalled = true;
#else
    previousInt = std::signal(SIGINT, handleStopSignal);
    previousTerm = std::signal(SIGTERM, handleStopSignal);
#endif
  }

  SignalStopController(const SignalStopController &) = delete;
  SignalStopController &operator=(const SignalStopController &) = delete;

  ~SignalStopController() {
#if EQUIPEGO_HAS_POSIX_SIGNALS
    if (termInstalled) ::sigaction(SIGTERM, &previousTerm, nullptr);
    if (intInstalled) ::sigaction(SIGINT, &previousInt, nullptr);
    gSignalWakeWriteFd = -1;
    closePipe();
#else
    std::signal(SIGINT, previousInt);
    std::signal(SIGTERM, previousTerm);
#endif
  }

  bool stopRequested() const noexcept { return gSignalStopRequested.load(std::memory_order_relaxed); }

  void waitUntil(std::chrono::steady_clock::time_point deadline) const {
#if EQUIPEGO_HAS_POSIX_SIGNALS
    while (!stopRequested()) {
      int timeoutMs = -1;
      if (deadline != std::chrono::steady_clock::time_point::max()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return;
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
                .count();
        timeoutMs = static_cast<int>(remaining >= INT_MAX - 1
                                         ? INT_MAX
                                         : remaining + 1);
      }
      struct pollfd descriptor {};
      descriptor.fd = readFd;
      descriptor.events = POLLIN;
      const int result = ::poll(&descriptor, 1, timeoutMs);
      if (result < 0 && errno != EINTR) {
        throw std::runtime_error("Fallo esperando SIGINT/SIGTERM");
      }
      if (result > 0 && (descriptor.revents & (POLLERR | POLLNVAL))) {
        throw std::runtime_error("Canal de senales no disponible");
      }
    }
#else
    while (!stopRequested() &&
           std::chrono::steady_clock::now() < deadline) {
      const auto next = std::min(deadline, std::chrono::steady_clock::now() +
                                              std::chrono::milliseconds(50));
      std::this_thread::sleep_until(next);
    }
#endif
  }

private:
#if EQUIPEGO_HAS_POSIX_SIGNALS
  int readFd = -1;
  int writeFd = -1;
  bool intInstalled = false;
  bool termInstalled = false;
  struct sigaction previousInt {};
  struct sigaction previousTerm {};

  void closePipe() noexcept {
    if (readFd >= 0) ::close(readFd);
    if (writeFd >= 0) ::close(writeFd);
    readFd = -1;
    writeFd = -1;
  }
#else
  using Handler = void (*)(int);
  Handler previousInt = SIG_DFL;
  Handler previousTerm = SIG_DFL;
#endif
};

} 


int main(int argc, char *argv[]) {
  RunOptions options;
  if (!readOptions(argc, argv, options)) return 1;

  char *configArgs[] = {argv[0], argv[1]};
  SimulationConfig config;
  if (!loadAndValidateConfig(2, configArgs, config)) return 1;
  if (options.hasLogPath) config.logFilePath = options.logPath;

#ifdef EQUIPEGO_BENCHMARK_CLI
  const bool showGui = options.display != DisplayMode::Headless;
  if (options.hasScale) config.simulation.timeScale = options.scale;
  if (options.hasDuration) config.simulation.durationS = options.duration;
#else
  const bool showGui = true;
#endif

  try {
    SignalStopController signals;

    SimulationClock clock;
    EventLogger logger(config.logFilePath, true);
    RoadNetwork roadNetwork(config);
    OrderBook orderBook(static_cast<std::size_t>(config.orders.maxPending));
    OrderProducer producer(config, orderBook, logger, clock, roadNetwork);
    Fleet fleet(config, roadNetwork, clock);
    Dispatcher dispatcher(config, roadNetwork, fleet);

    std::unique_ptr<SimulationDashboard> dashboard;
    if (showGui) {
      dashboard = std::make_unique<SimulationDashboard>(config, roadNetwork);
      dashboard->open();
    }

    fleet.start();
    std::vector<std::string> courierIds;
    courierIds.reserve(fleet.size());
    for (std::size_t i = 0; i < fleet.size(); ++i) {
      Courier &courier = fleet.at(i);
      courier.setEventLogger(&logger);
      courierIds.push_back(courier.getId());
    }
    fleet.startRescue();

    const auto startTime = std::chrono::steady_clock::now();
    clock.start(config.simulation.timeScale);

    std::thread dispatcherThread(&Dispatcher::run, &dispatcher,
                                 std::ref(orderBook), std::ref(logger),
                                 std::ref(clock));
    logger.logStart();

    std::thread producerThread(&OrderProducer::run, &producer);

    const auto deadline = config.simulation.durationS > 0
        ? startTime + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::duration<double>(
                              static_cast<double>(config.simulation.durationS) /
                              config.simulation.timeScale))
        : std::chrono::steady_clock::time_point::max();

    if (dashboard) {
      while (!signals.stopRequested() &&
             std::chrono::steady_clock::now() < deadline) {
        bool keepOpen = false;
        try {
          keepOpen = dashboard->frame(
              fleet, logger.snapshotStats(), clock.getSimulatedTimeMs());
        } catch (const std::exception &error) {
          std::cerr << "Aviso DP9.1 (OpenCV): " << error.what() << '\n';
        }
        if (!keepOpen) break;
      }
      dashboard->close();
    } else {
      signals.waitUntil(deadline);
    }


    producer.requestStop();
    producerThread.join();
    const std::size_t createdCount = producer.getCreatedCount();
    const std::size_t producerRejected = producer.getRejectedCount();

    dispatcher.requestStop(orderBook);
    dispatcherThread.join();
    const std::size_t assignedCount = dispatcher.getAssignedCount();
    const std::size_t dispatcherRejected = dispatcher.getRejectedCount();
    const std::size_t dispatcherPending = dispatcher.getPendingCount();

    fleet.requestStop();
    fleet.join();

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

    std::vector<Order> remainingOrders = orderBook.takeRemaining();
    std::size_t bookPendingCount = 0;
    for (Order &order : remainingOrders) {
      if (!order.transitionTo(OrderState::Pending)) {
        throw std::logic_error("No se pudo finalizar: " + order.orderId);
      }
      ++bookPendingCount;
    }

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

    logger.logEvent({{"event", "simulationStopping"}},
                    clock.getSimulatedTimeMs());
    logger.flush();

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
