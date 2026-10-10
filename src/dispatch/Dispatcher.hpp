
#pragma once

#include "config/Config.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <cstddef>
#include <string>
#include <vector>

// Cotizacion calculada para una moto candidata.
struct EtaQuote {
  std::size_t courierIndex;
  std::string courierId;
  double etaSimulatedSeconds;
};

// Error individual de un calculo.
struct QuoteFailure {
  std::string courierId;
  std::string reason;
};

// Resultados del conjunto de cotizaciones.
struct QuoteBatch {
  std::vector<EtaQuote> quotes;
  std::vector<QuoteFailure> failures;
};

class Dispatcher {
private:
  const SimulationConfig &config;
  const RoadNetwork &roadNetwork;
  Fleet &fleet;

public:
  Dispatcher(const SimulationConfig &config, const RoadNetwork &roadNetwork,
             Fleet &fleet);

  // Calcula las ETA de los candidatos en paralelo.
  // Por ahora espera todos los resultados.
  QuoteBatch quoteCandidates(const Order &order) const;
};
