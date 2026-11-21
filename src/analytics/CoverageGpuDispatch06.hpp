#pragma once
// Feature06 E4: diagnostic-only GPU route via existing CUDA owner.
// Preserves CPU reference on error, disagreement or missing CUDA.
#include "analytics/CourierCoverageGpu06.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <future>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace equipego::stencil06 {

struct RutaCobertura06 {
  std::vector<float> valores;
  std::string backend = "CPU";
  std::string motivoFallback;
  double kernelMs = 0.0;
  double totalMs = 0.0;
  bool gpuSolicitada = false;
  bool gpuUsada = false;
  float errorMaximo = 0.0f;
};

using EnvioCobertura06 = std::function<std::future<ResultadoGpu06>(Campo06, unsigned int)>;

inline RutaCobertura06 seleccionarCobertura06(
    const Campo06& campo, unsigned int barridos, bool solicitarGpu,
    bool cudaCompilado, const EnvioCobertura06& enviar = {},
    float tolerancia = 1e-5f) {
  if (!(std::isfinite(tolerancia) && tolerancia >= 0.0f && tolerancia <= 1e-3f))
    throw std::invalid_argument("STENCIL06_OWNER: tolerancia invalida");
  RutaCobertura06 resultado;
  resultado.gpuSolicitada = solicitarGpu;
  resultado.valores = evolucionar(campo, barridos, 1);
  if (!solicitarGpu) return resultado;
  if (!cudaCompilado) {
    resultado.motivoFallback = "USE_CUDA_OFF";
    return resultado;
  }
  if (!enviar) {
    resultado.motivoFallback = "OWNER_UNAVAILABLE";
    return resultado;
  }
  try {
    auto gpu = enviar(campo, barridos).get();
    if (gpu.valores.size() != resultado.valores.size()) {
      resultado.motivoFallback = "SIZE_MISMATCH";
      return resultado;
    }
    float error = 0.0f;
    for (std::size_t i = 0; i < gpu.valores.size(); ++i) {
      const float valor = gpu.valores[i];
      if (!std::isfinite(valor)) {
        resultado.motivoFallback = "NONFINITE_GPU";
        return resultado;
      }
      // Las fuentes y los bordes deben ser inmutables, aun con tolerancia.
      if (campo.fijas[i] && valor != campo.valores[i]) {
        resultado.motivoFallback = "FIXED_CELL_MISMATCH";
        return resultado;
      }
      error = std::max(error, std::fabs(valor - resultado.valores[i]));
    }
    resultado.errorMaximo = error;
    if (error > tolerancia) {
      resultado.motivoFallback = "MISMATCH_CPU_GPU";
      return resultado;
    }
    if (!(std::isfinite(gpu.kernelMs) && gpu.kernelMs >= 0.0 &&
          std::isfinite(gpu.totalMs) && gpu.totalMs >= 0.0)) {
      resultado.motivoFallback = "INVALID_TIMINGS";
      return resultado;
    }
    resultado.valores = std::move(gpu.valores);
    resultado.backend = "GPU";
    resultado.gpuUsada = true;
    resultado.kernelMs = gpu.kernelMs;
    resultado.totalMs = gpu.totalMs;
    return resultado;
  } catch (const std::exception& ex) {
    resultado.motivoFallback = std::string("GPU_EXCEPTION:") + ex.what();
    return resultado;
  } catch (...) {
    resultado.motivoFallback = "GPU_EXCEPTION:unknown";
    return resultado;
  }
}
} // namespace equipego::stencil06
