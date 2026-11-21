#pragma once
// Feature12 E2: stable GPU stream compaction for immutable order wait snapshots.
// CPU and GPU output identical original indices in ascending order.
#include "analytics/OrderWaitFilter12.hpp"
#include <cstdint>
#include <vector>
namespace equipego::analytics {
struct ResultadoFiltroGpu12 {
  ResultadoFiltro12 filtro;
  double h2dMs = 0.0;
  double kernelMs = 0.0;
  double d2hMs = 0.0;
  double totalMs = 0.0;
};
// Call on the CUDA-owner thread when integrated with live simulator (E3).
ResultadoFiltroGpu12 ejecutarFiltroEsperasGpu12(
    const std::vector<std::uint64_t>& esperasMs, std::uint64_t umbralMs);
}  // namespace equipego::analytics
