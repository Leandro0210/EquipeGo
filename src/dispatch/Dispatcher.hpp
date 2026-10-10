
#pragma once

#include "config/Config.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// =====================================================
// RESULTADOS DE LAS COTIZACIONES
// =====================================================

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

// Implementacion privada en Dispatcher.cpp.
class QuoteExecutor;

// Calculador alternativo para pruebas controladas.
// Recibe origen, restaurante y destino.
// Devuelve ETA en segundos simulados.
using QuoteCalculator = std::function<double(
    const std::string &, const std::string &, const std::string &)>;

// =====================================================
// DISPATCHER
// =====================================================

class Dispatcher {
private:
  const SimulationConfig &config;
  const RoadNetwork &roadNetwork;
  Fleet &fleet;

  QuoteCalculator calculator;

  // Trabajadores reutilizables para calcular ETA.
  std::unique_ptr<QuoteExecutor> executor;

public:
  Dispatcher(const SimulationConfig &config, const RoadNetwork &roadNetwork,
             Fleet &fleet, QuoteCalculator calculator = {});

  ~Dispatcher();

  Dispatcher(const Dispatcher &) = delete;
  Dispatcher &operator=(const Dispatcher &) = delete;

  // Calcula ETA en paralelo respetando
  // dispatch.quoteTimeoutMs (tiempo real).
  QuoteBatch quoteCandidates(const Order &order) const;

  // Intenta asignar a la moto con menor ETA.
  std::optional<std::string> assignBestCourier(Order &order) const;
};
