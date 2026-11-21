#pragma once
// Feature11 E3: seleccion segura CPU/GPU sobre pedidos inmutables.
// La GPU es opt-in y el CPU es autoridad: discrepancia/error => fallback.
#include "analytics/RestaurantDemandHistogram11.hpp"
#include "analytics/RestaurantDemandHistogramGpu11.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace equipego::analytics {
using EnviarDemanda11 = std::function<std::future<ResultadoDemandaGpu11>(
    std::vector<PedidoDemanda11>, std::size_t, std::size_t)>;

struct SeleccionDemanda11 {
  HistogramaDemanda11 histograma;
  bool gpuUsada = false;
  std::string backend = "CPU";
  std::string fallback;
  double h2dMs = 0.0;
  double kernelMs = 0.0;
  double d2hMs = 0.0;
  double totalMs = 0.0;
};

inline SeleccionDemanda11 seleccionarDemanda11(
    const std::vector<PedidoDemanda11>& pedidos,
    std::size_t restaurantes, std::size_t franjas,
    bool solicitarGpu, bool cudaDisponible,
    const EnviarDemanda11& enviar = {},
    std::chrono::milliseconds espera = std::chrono::seconds(30)) {
  SeleccionDemanda11 resultado;
  resultado.histograma = histogramaDemandaCPU11(pedidos, restaurantes, franjas);
  if (!solicitarGpu) {
    resultado.fallback = "GPU_NO_SOLICITADA";
    return resultado;
  }
  if (pedidos.empty()) {
    resultado.fallback = "SIN_PEDIDOS";
    return resultado;
  }
  if (!cudaDisponible) {
    resultado.fallback = "USE_CUDA_OFF";
    return resultado;
  }
  // El kernel de E2 utiliza un maximo de 1024 celdas por bloque.
  if (restaurantes > 1024u / franjas) {
    resultado.fallback = "GPU_DIMENSIONES_NO_SOPORTADAS";
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
    auto futuro = enviar(pedidos, restaurantes, franjas);
    if (!futuro.valid() ||
        futuro.wait_for(espera) != std::future_status::ready) {
      resultado.fallback = "TIMEOUT_O_NO_LISTO";
      return resultado;
    }
    const auto gpu = futuro.get();
    if (!histogramasIguales11(resultado.histograma, gpu.histograma)) {
      resultado.fallback = "DIFERENCIA_CPU_GPU";
      return resultado;
    }
    resultado.histograma = gpu.histograma;
    resultado.gpuUsada = true;
    resultado.backend = "GPU";
    resultado.h2dMs = gpu.h2dMs;
    resultado.kernelMs = gpu.kernelMs;
    resultado.d2hMs = gpu.d2hMs;
    resultado.totalMs = gpu.totalMs;
    return resultado;
  } catch (const std::exception&) {
    resultado.fallback = "ERROR_GPU";
    return resultado;
  }
}
} // namespace equipego::analytics
