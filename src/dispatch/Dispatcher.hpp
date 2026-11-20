
#pragma once

#include "analytics/EtaGpuDispatch.hpp"
#include "analytics/RestaurantOrderGpuDispatch09.hpp"
#include "config/Config.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <string>
#include <vector>

class OrderBook;
class EventLogger;
class SimulationClock;
class QuoteExecutor;

struct EtaQuote {
  std::size_t courierIndex;
  std::string courierId;
  double etaSimulatedSeconds;
};

struct QuoteFailure {
  std::string courierId;
  std::string reason;
};

struct QuoteBatch {
  std::vector<EtaQuote> quotes;
  std::vector<QuoteFailure> failures;
};

using QuoteCalculator = std::function<double(
    const std::string &, const std::string &, const std::string &)>;

class Dispatcher {
private:
  const SimulationConfig &config;
  const RoadNetwork &roadNetwork;
  Fleet &fleet;

  QuoteCalculator calculator;
  std::unique_ptr<QuoteExecutor> executor;
  equipego::analytics::EnviarReduccionGpu08 gpuEtaSubmitter;
  std::size_t gpuMinQuotes = 128;
  equipego::analytics::EnviarScanGpu09 gpuScanSubmitter;
  std::size_t gpuScanMinOrders = 16;  // umbral configurable, no GPU para lotes pequenos
  std::atomic<std::size_t> gpu09Intentos{0};
  std::atomic<std::size_t> gpu09Usos{0};
  std::atomic<std::size_t> gpu09Timeouts{0};
  std::atomic<std::size_t> gpu09Errores{0};
  std::atomic<std::size_t> gpu09Diferencias{0};

  std::atomic<bool> stopRequested{false};

  std::atomic<std::size_t> assignedCount{0};
  std::atomic<std::size_t> rejectedCount{0};
  std::atomic<std::size_t> pendingCount{0};
  std::atomic<std::size_t> gpu08Intentos{0};
  std::atomic<std::size_t> gpu08Usos{0};
  std::atomic<std::size_t> gpu08Timeouts{0};
  std::atomic<std::size_t> gpu08Errores{0};
  std::atomic<std::size_t> gpu08Diferencias{0};

public:
  Dispatcher(const SimulationConfig &config, const RoadNetwork &roadNetwork,
             Fleet &fleet, QuoteCalculator calculator = {});

  ~Dispatcher();

  // Configurar antes de iniciar el hilo Dispatcher::run.
  void setGpuEtaSubmitter(
      equipego::analytics::EnviarReduccionGpu08 submitter,
      std::size_t minimoCandidatos);
  std::size_t getGpu08Intentos() const { return gpu08Intentos.load(); }
  std::size_t getGpu08Usos() const { return gpu08Usos.load(); }
  std::size_t getGpu08Timeouts() const { return gpu08Timeouts.load(); }
  std::size_t getGpu08Errores() const { return gpu08Errores.load(); }
  std::size_t getGpu08Diferencias() const { return gpu08Diferencias.load(); }

  // Configurar antes de iniciar Dispatcher::run. No se conecta en builds CPU.
  void setGpuScan09Submitter(equipego::analytics::EnviarScanGpu09 submitter,
                             std::size_t minimoPedidos) {
    if (minimoPedidos == 0) {
      throw std::invalid_argument("Scan09: minimoPedidos debe ser > 0");
    }
    gpuScanSubmitter = std::move(submitter);
    gpuScanMinOrders = minimoPedidos;
  }
  std::size_t getGpu09Intentos() const { return gpu09Intentos.load(); }
  std::size_t getGpu09Usos() const { return gpu09Usos.load(); }
  std::size_t getGpu09Timeouts() const { return gpu09Timeouts.load(); }
  std::size_t getGpu09Errores() const { return gpu09Errores.load(); }
  std::size_t getGpu09Diferencias() const { return gpu09Diferencias.load(); }

  Dispatcher(const Dispatcher &) = delete;
  Dispatcher &operator=(const Dispatcher &) = delete;

  QuoteBatch quoteCandidates(const Order &order) const;

  std::optional<std::string> assignBestCourier(Order &order) const;

  void run(OrderBook &orderBook, EventLogger &logger, SimulationClock &clock);

  void requestStop(OrderBook &orderBook);

  std::size_t getAssignedCount() const { return assignedCount.load(); }

  std::size_t getRejectedCount() const { return rejectedCount.load(); }

  std::size_t getPendingCount() const { return pendingCount.load(); }
};
