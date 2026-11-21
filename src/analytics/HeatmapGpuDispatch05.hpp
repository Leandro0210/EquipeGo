#pragma once
// Feature 05 E4: ejecutar mapa GPU solo en el propietario CUDA y
// usar referencia CPU ante error, resultado diferente o USE_CUDA=OFF.
// No altera el dispatcher ni el estado de la simulacion.
#include "analytics/OrderHeatmapGpu05.hpp"
#include <exception>
#include <future>
#include <stdexcept>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace equipego::convolution05 {

struct HeatmapRoute05 {
  std::vector<float> values;
  std::string backend = "cpu_separable";
  std::string fallbackReason;
  double kernelMs = 0.0;
  double totalMs = 0.0;
  bool gpuRequested = false;
  bool usedGpu = false;
};

using HeatmapSubmit05 = std::function<std::future<GpuResult05>(DemandGrid05)>;

inline HeatmapRoute05 selectHeatmap05(const DemandGrid05& grid,
                                      bool gpuRequested,
                                      bool cudaCompiled,
                                      const HeatmapSubmit05& submit = {}) {
  HeatmapRoute05 answer;
  answer.gpuRequested = gpuRequested;
  answer.values = gaussianSeparable05(grid, 2);
  if (!gpuRequested) return answer;
  if (!cudaCompiled) {
    answer.fallbackReason = "USE_CUDA_OFF";
    return answer;
  }
  if (!submit) {
    answer.fallbackReason = "OWNER_UNAVAILABLE";
    return answer;
  }
  try {
    // La unica via CUDA es el future del hilo propietario; nunca se ejecuta
    // gaussianGpu05 directamente en un hilo de Fleet o del Dispatcher.
    GpuResult05 gpu = submit(grid).get();
    if (gpu.values != answer.values) {
      answer.fallbackReason = "MISMATCH_CPU_GPU";
      return answer;
    }
    answer.values = std::move(gpu.values);
    answer.backend = "gpu_tiled";
    answer.usedGpu = true;
    answer.kernelMs = gpu.kernelMs;
    answer.totalMs = gpu.totalMs;
    return answer;
  } catch (const std::exception& error) {
    answer.fallbackReason = std::string("GPU_EXCEPTION:") + error.what();
    return answer;
  } catch (...) {
    answer.fallbackReason = "GPU_EXCEPTION:unknown";
    return answer;
  }
}

} // namespace equipego::convolution05
