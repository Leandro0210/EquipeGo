#pragma once
// Feature10 E3: seleccion segura CPU/GPU sobre snapshots inmutables.
// La GPU es opcional; cualquier discrepancia devuelve la referencia CPU.
#include "analytics/CourierLoadReduction10.hpp"
#include "analytics/CourierLoadReductionGpu10.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace equipego::analytics {

using EnviarCarga10 = std::function<std::future<ResultadoCargaGpu10>(
    std::vector<std::uint32_t>)>;

struct SeleccionCarga10 {
  ResumenCarga10 resumen;
  bool gpuUsada = false;
  std::string backend = "CPU";
  std::string fallback;
  double h2dMs = 0.0;
  double kernelMs = 0.0;
  double d2hMs = 0.0;
  double totalMs = 0.0;
};

inline bool resumenIgual10(const ResumenCarga10& a,
                           const ResumenCarga10& b) noexcept {
  return a.couriers == b.couriers &&
         a.conPedidos == b.conPedidos &&
         a.totalReservas == b.totalReservas &&
         a.maxReservas == b.maxReservas &&
         a.indiceMax == b.indiceMax &&
         a.tieneMax == b.tieneMax;
}

inline SeleccionCarga10 seleccionarCarga10(
    const std::vector<std::uint32_t>& reservas,
    bool solicitarGpu,
    bool cudaDisponible,
    const EnviarCarga10& enviar = {},
    std::chrono::milliseconds espera = std::chrono::seconds(30)) {
  SeleccionCarga10 resultado;
  resultado.resumen = reducirCargaCouriersCPU10(reservas);
  if (!solicitarGpu) {
    resultado.fallback = "GPU_NO_SOLICITADA";
    return resultado;
  }
  if (reservas.empty()) {
    resultado.fallback = "FLOTA_VACIA";
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
    auto futuro = enviar(reservas);  // La cola recibe su propia copia.
    if (!futuro.valid() ||
        futuro.wait_for(espera) != std::future_status::ready) {
      resultado.fallback = "TIMEOUT_O_NO_LISTO";
      return resultado;
    }
    const auto gpu = futuro.get();
    if (!resumenIgual10(resultado.resumen, gpu.resumen)) {
      resultado.fallback = "DIFERENCIA_CPU_GPU";
      return resultado;
    }
    resultado.resumen = gpu.resumen;
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
