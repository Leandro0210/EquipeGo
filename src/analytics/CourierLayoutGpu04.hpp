#pragma once
#include "analytics/CourierLayout04.hpp"
#include <vector>
namespace equipego::performance04 {
struct ResultadoGpu04 {
  std::vector<float> values;
  double kernelMs = 0.0;
  double h2dMs = 0.0;
  double d2hMs = 0.0;
  double totalMs = 0.0; // incluye cudaMalloc, H2D, kernel, D2H, cudaFree
};
ResultadoGpu04 calcularAoSGpu04(const std::vector<CourierAoS04>& input,
    float rx, float ry, float tramo, unsigned int barridos);
ResultadoGpu04 calcularSoAGpu04(const CourierSoA04& input,
    float rx, float ry, float tramo, unsigned int barridos);
} // namespace equipego::performance04
