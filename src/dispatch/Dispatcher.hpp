
#pragma once

#include "config/Config.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// =====================================================
// COTIZACION ETA DE UN REPARTIDOR
// =====================================================

struct EtaQuote {
  std::size_t courierIndex;
  std::string courierId;
  double etaSimulatedSeconds;
};

// =====================================================
// ERROR INDIVIDUAL DE COTIZACION
// =====================================================

struct QuoteFailure {
  std::string courierId;
  std::string reason;
};

// =====================================================
// RESULTADO DE LAS COTIZACIONES
// =====================================================

struct QuoteBatch {
  std::vector<EtaQuote> quotes;
  std::vector<QuoteFailure> failures;
};

// =====================================================
// DISPATCHER - DESPACHADOR DE PEDIDOS
// =====================================================

class Dispatcher {
private:
  const SimulationConfig &config;
  const RoadNetwork &roadNetwork;
  Fleet &fleet;

public:
  Dispatcher(const SimulationConfig &config, const RoadNetwork &roadNetwork,
             Fleet &fleet);

  // Calcular cotizaciones ETA simultaneamente.
  QuoteBatch quoteCandidates(const Order &order) const;

  // Seleccionar el repartidor con menor ETA.
  // Si ninguno acepta, devuelve nullopt.
  std::optional<std::string> assignBestCourier(Order &order) const;
};
