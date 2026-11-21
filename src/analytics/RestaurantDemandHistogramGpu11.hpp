#pragma once
// Feature 11 E2: histograma bidimensional restaurante x franja en CUDA.
// Solo para snapshots inmutables. El propietario CUDA se integra en E3.
#include "analytics/RestaurantDemandHistogram11.hpp"
#include <vector>

namespace equipego::analytics {

struct ResultadoDemandaGpu11 {
  HistogramaDemanda11 histograma;
  double h2dMs = 0.0;
  double kernelMs = 0.0;
  double d2hMs = 0.0;
  double totalMs = 0.0;
};

// Soporta 1..1024 celdas. Para dimensiones mayores, usar referencia CPU.
// El llamador debe usar el hilo propietario CUDA cuando se integre en E3.
ResultadoDemandaGpu11 ejecutarHistogramaDemandaGpu11(
    const std::vector<PedidoDemanda11>& pedidos,
    std::size_t restaurantes, std::size_t franjas);

} // namespace equipego::analytics
