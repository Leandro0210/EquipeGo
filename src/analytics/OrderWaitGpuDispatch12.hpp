#pragma once
// Feature12 E3: seleccion optativa CPU/GPU sobre copias de tiempos de espera.
// El backend CPU es la referencia: divergencia/error/timeout => CPU.
#include "analytics/OrderWaitFilter12.hpp"
#include "analytics/OrderWaitFilterGpu12.hpp"
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace equipego::analytics {
using EnviarFiltro12 = std::function<std::future<ResultadoFiltroGpu12>(
    std::vector<std::uint64_t>, std::uint64_t)>;

struct SeleccionFiltro12 {
  ResultadoFiltro12 filtro;
  bool gpuUsada = false;
  std::string backend = "CPU";
  std::string fallback;
  double h2dMs = 0.0;
  double kernelMs = 0.0;
  double d2hMs = 0.0;
  double totalMs = 0.0;
};

inline SeleccionFiltro12 seleccionarFiltro12(
    const std::vector<std::uint64_t>& esperasMs,
    std::uint64_t umbralMs, bool solicitarGpu, bool cudaDisponible,
    const EnviarFiltro12& enviar = {},
    std::chrono::milliseconds espera = std::chrono::seconds(30)) {
  SeleccionFiltro12 resultado;
  resultado.filtro = filtrarEsperasCPU12(esperasMs, umbralMs);
  if (!solicitarGpu) {
    resultado.fallback = "GPU_NO_SOLICITADA";
    return resultado;
  }
  if (esperasMs.empty()) {
    resultado.fallback = "SIN_PEDIDOS";
    return resultado;
  }
  if (!cudaDisponible) {
    resultado.fallback = "USE_CUDA_OFF";
    return resultado;
  }
  if (!enviar) {
    resultado.fallback = "SIN_PROPIETARIO_GPU";
    return resultado;
  }
  if (espera.count() <= 0) {
    resultado.fallback = "TIMEOUT";
    return resultado;
  }
  try {
    auto futuro = enviar(esperasMs, umbralMs); // cola recibe copia propia
    if (!futuro.valid() ||
        futuro.wait_for(espera) != std::future_status::ready) {
      resultado.fallback = "TIMEOUT_O_NO_LISTO";
      return resultado;
    }
    auto gpu = futuro.get();
    if (!filtrosIguales12(resultado.filtro, gpu.filtro)) {
      resultado.fallback = "DIFERENCIA_CPU_GPU";
      return resultado;
    }
    resultado.filtro = std::move(gpu.filtro);
    resultado.backend = "GPU";
    resultado.gpuUsada = true;
    resultado.h2dMs = gpu.h2dMs;
    resultado.kernelMs = gpu.kernelMs;
    resultado.d2hMs = gpu.d2hMs;
    resultado.totalMs = gpu.totalMs;
  } catch (const std::exception&) {
    resultado.fallback = "ERROR_GPU";
  }
  return resultado;
}
} // namespace equipego::analytics
