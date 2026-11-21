#pragma once

#include "analytics/RestaurantOrderScan.hpp"

#include <cstddef>
#include <vector>

namespace equipego::analytics {

// Los indices agrupan establemente posiciones de entrada (no objetos Order).
struct ResultadoScanGpu09 {
  AgrupacionPedidosRestaurante agrupacion;
  double h2dMs = 0.0;
  double kernelMs = 0.0;
  double d2hMs = 0.0;
  double totalMs = 0.0;
};

// Unicamente desde el propietario compartido CUDA (no Dispatcher).
ResultadoScanGpu09 ejecutarAgrupacionScanGpu09(
    const std::vector<std::size_t>& restaurantIds,
    std::size_t restaurantCount);

}  // namespace equipego::analytics
