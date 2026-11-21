#include "analytics/CoverageGpuDispatch06.hpp"
#include "analytics/CourierCoverageRuntime06.hpp"
#include "analytics/CourierLayoutRuntime04.hpp"
#include "analytics/CourierLayoutGpuDispatch04.hpp"
#include "analytics/CourierLoadLive10.hpp"
#include "analytics/CourierLoadGpuDispatch10.hpp"
#include "analytics/RestaurantDemandLive11.hpp"
#include "analytics/RestaurantDemandGpuDispatch11.hpp"
#include "analytics/OrderWaitLive12.hpp"
#include "analytics/OrderWaitGpuDispatch12.hpp"
#include "analytics/DeliveryHistogram.hpp"
#include "analytics/HeatmapRuntime05.hpp"
#include "analytics/HeatmapGpuDispatch05.hpp"
#ifdef EQUIPEGO_USE_CUDA
#include "analytics/DeliveryHistogramGpu.hpp"
#endif
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
#include <numeric>
#include <cstdint>
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
  std::unordered_map<std::string, std::size_t> createdPerRestaurant;
  std::size_t delivered = 0;
  std::size_t rejected = 0;
  std::size_t reassigned = 0;
  std::size_t breakdowns = 0;
  std::size_t stopping = 0;
  long long deliveryDurationSumMs = 0;
  long long maxDeliveryMs = 0;
  std::vector<long long> deliveryDurationsMs;
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
      ++audit.createdPerRestaurant[
          event.at("restaurant").get<std::string>()];
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
      audit.deliveryDurationsMs.push_back(duration);
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
#ifdef EQUIPEGO_USE_CUDA
    equipego::analytics::PropietarioGpuHistograma propietarioGpu;
#endif
    RoadNetwork roadNetwork(config);
    // Feature 03: GPU optativa. Nunca se usa en consultas de ETA en caliente:
    // solo precalcula la tabla ANTES de iniciar Fleet/Dispatcher y sus hilos.
    const char* flagGpu03 = std::getenv("EQUIPEGO_FLOYD03_GPU");
    const std::string valorGpu03 = flagGpu03 ? std::string(flagGpu03) : "0";
    const char* flagCpu03Previo = std::getenv("EQUIPEGO_FLOYD03_CPU");
    const std::string valorCpu03Previo = flagCpu03Previo
        ? std::string(flagCpu03Previo) : "0";
    if ((valorGpu03 != "0" && valorGpu03 != "1") ||
        (valorCpu03Previo != "0" && valorCpu03Previo != "1")) {
      throw std::invalid_argument("FLOYD03: flags CPU/GPU deben ser 0 o 1");
    }
    if (valorGpu03 == "1" && valorCpu03Previo == "1") {
      throw std::invalid_argument("FLOYD03: CPU=1 y GPU=1 son excluyentes");
    }
    if (valorGpu03 == "1") {
#ifdef EQUIPEGO_USE_CUDA
      try {
        const auto info03 = roadNetwork.habilitarFloyd03Gpu(config, 4096);
        std::cerr << "FLOYD03_GPU_CACHE,enabled=1,nodos=" << info03.nodos
                  << ",pares_verificados=" << info03.paresVerificados
                  << ",bytes=" << info03.bytesMatriz
                  << ",kernel_ms=" << info03.kernelMs
                  << ",total_ms=" << info03.totalMs << '\n';
      } catch (const std::exception& error03) {
        // No se publica una tabla parcial: todas las consultas usan Dijkstra.
        std::cerr << "FLOYD03_GPU_CACHE,enabled=0,fallback=Dijkstra,motivo="
                  << error03.what() << '\n';
      }
#else
      std::cerr << "FLOYD03_GPU_CACHE,enabled=0,fallback=Dijkstra,motivo="
                   "USE_CUDA_OFF\n";
#endif
    }
    // Opt-in para experimentar sin modificar el recorrido real de Dijkstra.
    // Solo inicializar antes de Fleet/Dispatcher y sus hilos.
    if (const char* flag03 = std::getenv("EQUIPEGO_FLOYD03_CPU")) {
      const std::string valor03(flag03);
      if (valor03 != "0" && valor03 != "1") {
        throw std::invalid_argument("EQUIPEGO_FLOYD03_CPU debe ser 0 o 1");
      }
      if (valor03 == "1") {
        try {
          roadNetwork.habilitarFloyd03Cpu(config, 16);
          std::cerr << "FLOYD03_CPU_CACHE,enabled=1,nodos="
                    << config.nodes.size() << ",bytes="
                    << roadNetwork.floyd03Bytes() << '\n';
        } catch (const std::exception &error03) {
          // Cualquier fallo de precalculo conserva Dijkstra sin tabla.
          std::cerr << "FLOYD03_CPU_CACHE,enabled=0,fallback=Dijkstra,motivo="
                    << error03.what() << '\n';
        }
      }
    }
    OrderBook orderBook(static_cast<std::size_t>(config.orders.maxPending));
    OrderProducer producer(config, orderBook, logger, clock, roadNetwork);
    Fleet fleet(config, roadNetwork, clock);
    Dispatcher dispatcher(config, roadNetwork, fleet);
#ifdef EQUIPEGO_USE_CUDA
    // El mismo propietario de CUDA atiende Histogram y el despacho ETA.
    // Umbral por defecto 128: evitar copias GPU en flotas diminutas.
    // EQUIPEGO_GPU08_MIN_QUOTES=1 permite verificar la ruta con flota pequena.
    std::size_t minimoGpu08 = 128;
    if (const char* variable = std::getenv("EQUIPEGO_GPU08_MIN_QUOTES")) {
      char* fin = nullptr;
      errno = 0;
      const unsigned long long valor = std::strtoull(variable, &fin, 10);
      if (errno != 0 || fin == variable || *fin != '\0' ||
          valor == 0 || valor > 100000000ULL) {
        throw std::invalid_argument("EQUIPEGO_GPU08_MIN_QUOTES invalido");
      }
      minimoGpu08 = static_cast<std::size_t>(valor);
    }
    dispatcher.setGpuEtaSubmitter(
        [&propietarioGpu](std::vector<equipego::analytics::EtaCandidato> datos) {
          return propietarioGpu.enviarEta(std::move(datos));
        },
        minimoGpu08);
    // Feature09: agrupacion observacional de pedidos por restaurante.
    // Por defecto solo envia lotes de al menos 16 pedidos a GPU;
    // EQUIPEGO_GPU09_MIN_ORDERS=1 habilita la ruta en pruebas con lotes pequenos.
    std::size_t minimoGpu09 = 16;
    if (const char* variable09 = std::getenv("EQUIPEGO_GPU09_MIN_ORDERS")) {
      char* fin09 = nullptr;
      errno = 0;
      const unsigned long long valor09 = std::strtoull(variable09, &fin09, 10);
      if (errno != 0 || fin09 == variable09 || *fin09 != '\0' ||
          valor09 == 0 || valor09 > 100000000ULL) {
        throw std::invalid_argument("EQUIPEGO_GPU09_MIN_ORDERS invalido");
      }
      minimoGpu09 = static_cast<std::size_t>(valor09);
    }
    dispatcher.setGpuScan09Submitter(
        [&propietarioGpu](std::vector<std::size_t> ids, std::size_t restaurantes) {
          return propietarioGpu.enviarScan09(std::move(ids), restaurantes);
        }, minimoGpu09);
#endif

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

    // Feature10 E4: muestra observacional al terminar Dispatcher, ANTES de
    // detener Fleet. Los couriers siguen vivos; snapshot individual por mutex.
    // Desactivado por defecto. No modifica pedidos, ETA ni Dispatcher.
    if (const char* flag10 = std::getenv("EQUIPEGO_LOAD10")) {
      const std::string modo10(flag10);
      if (modo10 != "0" && modo10 != "1") {
        std::cerr << "REDUCTION10_RUN,enabled=0,error=FLAG_INVALID_0_OR_1\n";
      } else if (modo10 == "1") {
        try {
          const char* flagGpu10 = std::getenv("EQUIPEGO_LOAD10_GPU");
          const std::string modoGpu10 = flagGpu10 ? std::string(flagGpu10) : "0";
          if (modoGpu10 != "0" && modoGpu10 != "1")
            throw std::invalid_argument("EQUIPEGO_LOAD10_GPU debe ser 0 o 1");
          const auto captura10 = equipego::analytics::capturarCargaFleet10(fleet);
#ifdef EQUIPEGO_USE_CUDA
          const auto seleccion10 = equipego::analytics::seleccionarCarga10(
              captura10.reservas, modoGpu10 == "1", true,
              [&propietarioGpu](std::vector<std::uint32_t> reservas10) {
                return propietarioGpu.enviarCarga10(std::move(reservas10));
              });
#else
          const auto seleccion10 = equipego::analytics::seleccionarCarga10(
              captura10.reservas, modoGpu10 == "1", false);
#endif
          const auto& r10 = seleccion10.resumen;
          const std::string idMax10 = r10.tieneMax
              ? captura10.courierIds.at(r10.indiceMax) : "NINGUNO";
          std::cerr << "REDUCTION10_RUN,enabled=1,backend="
                    << seleccion10.backend << ",couriers=" << r10.couriers
                    << ",con_pedidos=" << r10.conPedidos
                    << ",total_reservas=" << r10.totalReservas
                    << ",max_reservas=" << r10.maxReservas
                    << ",courier_max=" << idMax10
                    << ",promedio=" << r10.promedio()
                    << ",gpu_usada=" << (seleccion10.gpuUsada ? 1 : 0)
                    << ",fallback=" << seleccion10.fallback
                    << ",kernel_ms=" << seleccion10.kernelMs
                    << ",total_ms=" << seleccion10.totalMs << '\n';
        } catch (const std::exception& error10) {
          std::cerr << "REDUCTION10_RUN,enabled=0,error="
                    << error10.what() << '\n';
        }
      }
    }

    fleet.requestStop();
    fleet.join();

    // Feature04 E3: snapshot real del Fleet terminado (observacional).
    // La comparacion AoS/SoA NO sustituye el ETA vial del Dispatcher.
    if (const char* flag04 = std::getenv("EQUIPEGO_LAYOUT04")) {
      const std::string modo04(flag04);
      if (modo04 != "0" && modo04 != "1") {
        std::cerr << "LAYOUT04_RUNTIME,enabled=0,error=FLAG_INVALID_0_OR_1\n";
      } else if (modo04 == "1") {
        try {
          const auto registros04 = equipego::performance04::capturarFleet04(
              config, fleet);
          const auto diagnostico04 =
              equipego::performance04::compararLayoutRuntime04(config, registros04);
          std::cerr << "LAYOUT04_RUNTIME,enabled=1,source=Fleet_post_join"
                    << ",couriers=" << diagnostico04.couriers
                    << ",disponibles=" << diagnostico04.disponibles
                    << ",equivalencia_AoS_SoA=OK,eta_oficial=NO"
                    << ",modifica_dispatch=NO\n";
          // E4: GPU opcional en la misma cola de Histogram/Reduction/Scan.
          // El ETA oficial y los pedidos no dependen de este diagnostico.
          const char* flagGpu04 = std::getenv("EQUIPEGO_LAYOUT04_GPU");
          const std::string modoGpu04 = flagGpu04 ? flagGpu04 : "0";
          if (modoGpu04 != "0" && modoGpu04 != "1")
            throw std::invalid_argument("EQUIPEGO_LAYOUT04_GPU debe ser 0 o 1");
          if (modoGpu04 == "1") {
            const auto [lat04, lon04] = equipego::performance04::nodoLatLon04(
                config, config.restaurants.front().nodeId);
            const auto destino04 = equipego::performance04::proyectarMetros04(
                config, lat04, lon04);
#ifdef EQUIPEGO_USE_CUDA
            const auto gpu04 = equipego::performance04::seleccionarLayoutGpu04(
                registros04, destino04.x, destino04.y, 0.0f, 1, true, true,
                [&propietarioGpu](std::vector<equipego::performance04::CourierAoS04> v,
                                  float x, float y, float tramo, unsigned barridos) {
                  return propietarioGpu.enviarLayoutAoS04(
                      std::move(v), x, y, tramo, barridos);
                },
                [&propietarioGpu](equipego::performance04::CourierSoA04 v,
                                  float x, float y, float tramo, unsigned barridos) {
                  return propietarioGpu.enviarLayoutSoA04(
                      std::move(v), x, y, tramo, barridos);
                });
#else
            const auto gpu04 = equipego::performance04::seleccionarLayoutGpu04(
                registros04, destino04.x, destino04.y, 0.0f, 1, true, false);
#endif
            if (gpu04.usada) {
              std::cerr << "LAYOUT04_GPU_OWNER,backend=GPU,verified=1,couriers="
                        << gpu04.couriers << ",aos_kernel_ms="
                        << gpu04.aosKernelMs << ",aos_total_ms="
                        << gpu04.aosTotalMs << ",soa_kernel_ms="
                        << gpu04.soaKernelMs << ",soa_total_ms="
                        << gpu04.soaTotalMs << ",max_error="
                        << gpu04.maxError << ",eta_oficial=NO\n";
            } else {
              std::cerr << "LAYOUT04_GPU_FALLBACK,backend=CPU,reason="
                        << gpu04.fallback << ",modifica_dispatch=NO\n";
            }
          }
        } catch (const std::exception& error04) {
          std::cerr << "LAYOUT04_RUNTIME,enabled=0,fallback=sin_diagnostico,motivo="
                    << error04.what() << '\n';
        }
      }
    }

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

    // Feature11 E4: histograma 2D de orderCreated reales en el JSONL YA
    // auditado. Analisis observacional al terminar la simulacion; opt-in.
    if (const char* flag11 = std::getenv("EQUIPEGO_HIST11")) {
      const std::string modo11(flag11);
      if (modo11 != "0" && modo11 != "1") {
        std::cerr << "HISTOGRAM11_RUN,enabled=0,error=FLAG_INVALID_0_OR_1\n";
      } else if (modo11 == "1") {
        try {
          const char* flagGpu11 = std::getenv("EQUIPEGO_HIST11_GPU");
          const std::string modoGpu11 = flagGpu11 ? std::string(flagGpu11) : "0";
          if (modoGpu11 != "0" && modoGpu11 != "1")
            throw std::invalid_argument("EQUIPEGO_HIST11_GPU debe ser 0 o 1");
          std::vector<std::string> ids11;
          ids11.reserve(config.restaurants.size());
          for (const auto& r11 : config.restaurants) ids11.push_back(r11.id);
          constexpr std::uint64_t franjaMs11 = 60000;
          const auto capturado11 = equipego::analytics::capturarDemandaArchivo11(
              config.logFilePath, ids11, franjaMs11);
          if (capturado11.pedidos.size() != audit.created)
            throw std::logic_error("Feature11: total distinto de EventAudit");
#ifdef EQUIPEGO_USE_CUDA
          const auto seleccionado11 = equipego::analytics::seleccionarDemanda11(
              capturado11.pedidos, ids11.size(), capturado11.franjas,
              modoGpu11 == "1", true,
              [&propietarioGpu](std::vector<equipego::analytics::PedidoDemanda11> p,
                               std::size_t r, std::size_t f) {
                return propietarioGpu.enviarDemanda11(std::move(p), r, f);
              });
#else
          const auto seleccionado11 = equipego::analytics::seleccionarDemanda11(
              capturado11.pedidos, ids11.size(), capturado11.franjas,
              modoGpu11 == "1", false);
#endif
          const auto& h11 = seleccionado11.histograma;
          for (std::size_t i11 = 0; i11 < ids11.size(); ++i11) {
            const auto found11 = audit.createdPerRestaurant.find(ids11[i11]);
            const std::size_t auditCount11 = found11 == audit.createdPerRestaurant.end()
                ? 0u : found11->second;
            if (h11.porRestaurante[i11] != auditCount11)
              throw std::logic_error("Feature11: conteo distinto de EventAudit");
          }
          std::size_t mayor11 = 0;
          for (std::size_t i11 = 1; i11 < ids11.size(); ++i11)
            if (h11.porRestaurante[i11] > h11.porRestaurante[mayor11])
              mayor11 = i11;
          if (const char* rutaCsv11 = std::getenv("EQUIPEGO_HIST11_CSV")) {
            if (*rutaCsv11) {
              std::ofstream salida11(rutaCsv11);
              if (!salida11) throw std::runtime_error("Feature11: no se puede crear CSV");
              equipego::analytics::escribirDemandaCSV11(
                  salida11, h11, ids11, franjaMs11);
              salida11.flush();
              if (!salida11) throw std::runtime_error("Feature11: escritura CSV fallida");
            }
          }
          std::cerr << "HISTOGRAM11_RUN,enabled=1,backend=" << seleccionado11.backend
                    << ",pedidos=" << h11.totalPedidos
                    << ",restaurantes=" << h11.restaurantes
                    << ",franjas=" << h11.franjas << ",franja_ms=" << franjaMs11
                    << ",top_restaurante=" << ids11.at(mayor11)
                    << ",top_pedidos=" << h11.porRestaurante.at(mayor11)
                    << ",gpu_usada=" << (seleccionado11.gpuUsada ? 1 : 0)
                    << ",fallback=" << seleccionado11.fallback
                    << ",kernel_ms=" << seleccionado11.kernelMs
                    << ",total_ms=" << seleccionado11.totalMs << '\n';
        } catch (const std::exception& error11) {
          std::cerr << "HISTOGRAM11_RUN,enabled=0,error="
                    << error11.what() << '\n';
        }
      }
    }

    // Feature12 E4: filtro estable de esperas reales post-auditoria, opt-in.
    // Hasta primera asignacion, rechazo o cierre de simulacion si pendiente.
    if (const char* flag12 = std::getenv("EQUIPEGO_FILTER12")) {
      const std::string modo12(flag12);
      if (modo12 != "0" && modo12 != "1") {
        std::cerr << "FILTER12_RUN,enabled=0,error=FLAG_INVALID_0_OR_1\n";
      } else if (modo12 == "1") {
        try {
          const char* flagGpu12 = std::getenv("EQUIPEGO_FILTER12_GPU");
          const std::string modoGpu12 = flagGpu12 ? std::string(flagGpu12) : "0";
          if (modoGpu12 != "0" && modoGpu12 != "1")
            throw std::invalid_argument("EQUIPEGO_FILTER12_GPU debe ser 0 o 1");
          std::uint64_t umbral12 = 30000;
          if (const char* valor12 = std::getenv("EQUIPEGO_FILTER12_UMBRAL_MS")) {
            const std::string s12(valor12);
            if (s12.empty() || s12.find_first_not_of("0123456789") != std::string::npos)
              throw std::invalid_argument("Feature12: umbral no numerico");
            std::size_t consumidos12 = 0;
            umbral12 = std::stoull(s12, &consumidos12, 10);
            if (consumidos12 != s12.size())
              throw std::invalid_argument("Feature12: umbral invalido");
          }
          const auto capturado12 = equipego::analytics::capturarEsperasArchivo12(
              config.logFilePath);
          if (capturado12.pedidos.size() != audit.created)
            throw std::logic_error("Feature12: capturas distintas de EventAudit");
#ifdef EQUIPEGO_USE_CUDA
          const auto elegido12 = equipego::analytics::seleccionarFiltro12(
              capturado12.esperasMs, umbral12, modoGpu12 == "1", true,
              [&propietarioGpu](std::vector<std::uint64_t> esperas,
                               std::uint64_t umbral) {
                return propietarioGpu.enviarFiltro12(std::move(esperas), umbral);
              });
#else
          const auto elegido12 = equipego::analytics::seleccionarFiltro12(
              capturado12.esperasMs, umbral12, modoGpu12 == "1", false);
#endif
          if (elegido12.filtro.totalEntradas != capturado12.pedidos.size() ||
              elegido12.filtro.seleccionados() + elegido12.filtro.descartados() !=
                  capturado12.pedidos.size())
            throw std::logic_error("Feature12: balance del filtro invalido");
          if (const char* csv12 = std::getenv("EQUIPEGO_FILTER12_CSV")) {
            if (*csv12) {
              std::ofstream salida12(csv12);
              if (!salida12)
                throw std::runtime_error("Feature12: no se puede crear CSV");
              equipego::analytics::escribirFiltroCSV12(
                  salida12, capturado12, elegido12.filtro);
              salida12.flush();
              if (!salida12) throw std::runtime_error("Feature12: CSV incompleto");
            }
          }
          std::cerr << "FILTER12_RUN,enabled=1,backend=" << elegido12.backend
                    << ",pedidos=" << elegido12.filtro.totalEntradas
                    << ",seleccionados=" << elegido12.filtro.seleccionados()
                    << ",descartados=" << elegido12.filtro.descartados()
                    << ",asignados=" << capturado12.asignados
                    << ",rechazados=" << capturado12.rechazados
                    << ",pendientes=" << capturado12.pendientes
                    << ",umbral_ms=" << umbral12
                    << ",gpu_usada=" << (elegido12.gpuUsada ? 1 : 0)
                    << ",fallback=" << elegido12.fallback
                    << ",kernel_ms=" << elegido12.kernelMs
                    << ",total_ms=" << elegido12.totalMs << '\n';
        } catch (const std::exception& error12) {
          std::cerr << "FILTER12_RUN,enabled=0,error=" << error12.what() << '\n';
        }
      }
    }

    // Feature06 E4: eleccion observacional CPU/GPU con referencia CPU
    // y fallback seguro. Usa SOLO el propietario CUDA, despues de Fleet::join.
    if (const char* flagCoverage06 = std::getenv("EQUIPEGO_COVERAGE06")) {
      const std::string mode06(flagCoverage06);
      if (mode06 != "0" && mode06 != "1") {
        std::cerr << "STENCIL06_RUN,enabled=0,error=FLAG_INVALID_0_OR_1\n";
      } else if (mode06 == "1") {
        try {
          const char* flagGpu06 = std::getenv("EQUIPEGO_COVERAGE06_GPU");
          const std::string gpuMode06 = flagGpu06 ? std::string(flagGpu06) : "0";
          if (gpuMode06 != "0" && gpuMode06 != "1")
            throw std::invalid_argument("EQUIPEGO_COVERAGE06_GPU debe ser 0 o 1");
          const auto captured06 = equipego::stencil06::capturarFleet06(
              config, fleet, 512);
#ifdef EQUIPEGO_USE_CUDA
          const auto selected06 = equipego::stencil06::seleccionarCobertura06(
              captured06.campo, 20, gpuMode06 == "1", true,
              [&propietarioGpu](equipego::stencil06::Campo06 campo,
                               unsigned int barridos) {
                return propietarioGpu.enviarCobertura06(
                    std::move(campo), barridos,
                    equipego::stencil06::VarianteGpu06::BasicoGlobal);
              });
#else
          const auto selected06 = equipego::stencil06::seleccionarCobertura06(
              captured06.campo, 20, gpuMode06 == "1", false);
#endif
          if (selected06.gpuSolicitada && !selected06.gpuUsada)
            std::cerr << "STENCIL06_GPU_FALLBACK,reason="
                      << selected06.motivoFallback << '\n';
          if (selected06.gpuUsada)
            std::cerr << "STENCIL06_GPU_OWNER,verified=1,kernel_ms="
                      << selected06.kernelMs << ",total_ms="
                      << selected06.totalMs << ",max_error="
                      << selected06.errorMaximo << '\n';
          const char* out06 = std::getenv("EQUIPEGO_COVERAGE06_PREFIX");
          const std::string prefix06 = out06 ? std::string(out06) :
              "../Resultados_Feature06/Integracion06/coverage06_cpu_run";
          equipego::stencil06::guardarCobertura06(
              config, captured06, selected06.valores, prefix06, 20,
              selected06.backend);
          std::cerr << "STENCIL06_RUN,enabled=1,backend="
                    << selected06.backend << ",couriers="
                    << captured06.couriersTotales << ",used="
                    << captured06.couriersUsados << ",broken="
                    << captured06.couriersAveriados << ",sources="
                    << captured06.fuentesUnicas << ",grid=512x512,png="
                    << prefix06 << ".png\n";
        } catch (const std::exception& error06) {
          std::cerr << "STENCIL06_RUN,enabled=0,error="
                    << error06.what() << '\n';
        }
      }
    }

    // Feature05 E3: mapa de ORIGENES (restaurantes) de los pedidos
    // generados en esta ejecucion, solo al finalizar la simulacion.
    // No inventa destinos ni reemplaza el despacho.
    if (const char* flagHeatmap05 = std::getenv("EQUIPEGO_HEATMAP05")) {
      const std::string modeHeatmap05(flagHeatmap05);
      if (modeHeatmap05 != "0" && modeHeatmap05 != "1") {
        throw std::invalid_argument("EQUIPEGO_HEATMAP05 debe ser 0 o 1");
      }
      if (modeHeatmap05 == "1") {
        try {
          const auto projected05 =
              equipego::convolution05::projectRestaurantOrders05(
                  config, audit.createdPerRestaurant, 512);
          if (projected05.created != audit.created) {
            throw std::logic_error("HEATMAP05: created != auditoria de eventos");
          }
          // E4: opt-in CUDA solo a traves del propietario GPU existente.
          const char* flagGpu05 = std::getenv("EQUIPEGO_HEATMAP05_GPU");
          const std::string gpuMode05 = flagGpu05 ? std::string(flagGpu05) : "0";
          if (gpuMode05 != "0" && gpuMode05 != "1") {
            throw std::invalid_argument("EQUIPEGO_HEATMAP05_GPU debe ser 0 o 1");
          }
          const bool gpuHeatmap05 = gpuMode05 == "1";
#ifdef EQUIPEGO_USE_CUDA
          const auto selected05 = equipego::convolution05::selectHeatmap05(
              projected05.grid, gpuHeatmap05, true,
              [&propietarioGpu](equipego::convolution05::DemandGrid05 data) {
                return propietarioGpu.enviarHeatmap05(
                    std::move(data),
                    equipego::convolution05::GpuAlgorithm05::TiledSharedConstant);
              });
#else
          const auto selected05 = equipego::convolution05::selectHeatmap05(
              projected05.grid, gpuHeatmap05, false);
#endif
          if (selected05.gpuRequested && !selected05.usedGpu) {
            std::cerr << "HEATMAP05_GPU_FALLBACK,reason="
                      << selected05.fallbackReason << '\n';
          }
          if (selected05.usedGpu) {
            std::cerr << "HEATMAP05_GPU_OWNER,verified=1,kernel_ms="
                      << selected05.kernelMs << ",total_ms="
                      << selected05.totalMs << '\n';
          }
          const char* prefixEnv05 = std::getenv("EQUIPEGO_HEATMAP05_PREFIX");
          const std::string prefix05 = prefixEnv05
              ? std::string(prefixEnv05)
              : "../Resultados_Feature05/Integracion05/heatmap05_cpu_run";
          equipego::convolution05::saveRunHeatmap05(
              config, projected05, selected05.values, prefix05, selected05.backend);
          std::cerr << "HEATMAP05_RUN,enabled=1,backend="
                    << (selected05.usedGpu ? "GPU" : "CPU")
                    << ",created="
                    << projected05.created << ",restaurants="
                    << projected05.restaurantsWithOrders
                    << ",grid=512x512,png=" << prefix05 << ".png\n";
        } catch (const std::exception& heatmap05Error) {
          // El diagnostico no puede cambiar el balance ni el exit de una
          // simulacion correcta. El error siempre queda visible en stderr.
          std::cerr << "HEATMAP05_RUN,enabled=0,error="
                    << heatmap05Error.what() << '\n';
        }
      }
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

    auto histograma =
        equipego::analytics::histogramaEntregasCPU(audit.deliveryDurationsMs);
#ifdef EQUIPEGO_USE_CUDA
    // El hilo propietario serializa el acceso a CUDA; main recibe un future.
    auto futuroGpu = propietarioGpu.enviar(audit.deliveryDurationsMs);
    const auto resultadoGpu = futuroGpu.get();
    if (resultadoGpu.bins != histograma) {
      throw std::logic_error("Histograma GPU distinto de referencia CPU");
    }
    histograma = resultadoGpu.bins;
    std::cerr << "HISTOGRAMA_GPU_07,kernel_ms=" << resultadoGpu.kernelMs
              << ",total_ms=" << resultadoGpu.totalMs << '\n';
#endif
    const auto sumaBins = std::accumulate(histograma.begin(), histograma.end(),
                                          std::uint64_t{0});
    if (sumaBins != audit.delivered) {
      throw std::logic_error("El histograma no coincide con las entregas");
    }
    const nlohmann::json histogramJson = histograma;
    const nlohmann::json summary = {{"created", audit.created},
                                    {"delivered", audit.delivered},
                                    {"rejected", audit.rejected},
                                    {"pending", pendingFromEvents},
                                    {"reassigned", audit.reassigned},
                                    {"breakdowns", audit.breakdowns},
                                    {"avgDeliveryMs", avgDeliveryMs},
                                    {"maxDeliveryMs", audit.maxDeliveryMs},
                                    {"deliveredPerCourier", perCourier},
                                    {"deliveryHistogramMinutes", histogramJson}};
#ifdef EQUIPEGO_USE_CUDA
    std::cerr << "REDUCTION_GPU_08,intentos=" << dispatcher.getGpu08Intentos()
              << ",usos=" << dispatcher.getGpu08Usos()
              << ",timeouts=" << dispatcher.getGpu08Timeouts()
              << ",errores=" << dispatcher.getGpu08Errores()
              << ",diferencias=" << dispatcher.getGpu08Diferencias()
              << ",minimo_candidatos=" << minimoGpu08 << '\n';
#endif
    std::cout << summary.dump() << '\n' << std::flush;
    return 0;

  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }
}
