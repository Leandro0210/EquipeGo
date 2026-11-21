#pragma once

// Feature04 E4: comparacion GPU AoS/SoA sobre una instantanea inmutable.
// El despacho oficial nunca usa estos tiempos euclidianos aproximados.
// Solo el propietario CUDA ejecuta los backends; esta capa espera futures.
#include "analytics/CourierLayout04.hpp"
#include "analytics/CourierLayoutGpu04.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace equipego::performance04 {

using EnviarLayoutAoS04 = std::function<std::future<ResultadoGpu04>(
    std::vector<CourierAoS04>, float, float, float, unsigned int)>;
using EnviarLayoutSoA04 = std::function<std::future<ResultadoGpu04>(
    CourierSoA04, float, float, float, unsigned int)>;

struct InformeLayoutGpu04 {
  bool solicitada = false;
  bool usada = false;
  std::string backend = "CPU";
  std::string fallback;
  std::size_t couriers = 0;
  double maxError = 0.0;
  double aosKernelMs = 0.0;
  double aosTotalMs = 0.0;
  double soaKernelMs = 0.0;
  double soaTotalMs = 0.0;
};

inline void compararReferencia04(const std::vector<float>& esperado,
                                  const std::vector<float>& observado,
                                  double& maxError) {
  if (esperado.size() != observado.size()) {
    throw std::runtime_error("LAYOUT04_GPU: longitud diferente");
  }
  for (std::size_t i = 0; i < esperado.size(); ++i) {
    const double a = esperado[i];
    const double b = observado[i];
    if (a == -1.0) {
      if (b != -1.0) {
        throw std::runtime_error("LAYOUT04_GPU: courier no disponible incluido");
      }
      continue;
    }
    if (!std::isfinite(a) || !std::isfinite(b) || a < 0.0 || b < 0.0) {
      throw std::runtime_error("LAYOUT04_GPU: resultado no finito o negativo");
    }
    const double delta = std::abs(a - b);
    const double limite = 0.001 + 0.0001 * std::max(std::abs(a), std::abs(b));
    if (delta > limite) {
      throw std::runtime_error("LAYOUT04_GPU: equivalencia CPU/GPU fuera de tolerancia");
    }
    maxError = std::max(maxError, delta);
  }
}

inline InformeLayoutGpu04 seleccionarLayoutGpu04(
    const std::vector<CourierAoS04>& aos,
    float rx, float ry, float tramo, unsigned int barridos,
    bool solicitarGpu, bool cudaDisponible,
    EnviarLayoutAoS04 enviarAoS = {}, EnviarLayoutSoA04 enviarSoA = {},
    std::chrono::milliseconds plazo = std::chrono::milliseconds(10000)) {
  InformeLayoutGpu04 informe;
  informe.solicitada = solicitarGpu;
  informe.couriers = aos.size();
  if (!solicitarGpu) return informe;
  if (!cudaDisponible) {
    informe.fallback = "USE_CUDA_OFF";
    return informe;
  }
  if (!enviarAoS || !enviarSoA) {
    informe.fallback = "SIN_PROPIETARIO_GPU";
    return informe;
  }
  if (plazo.count() <= 0) {
    informe.fallback = "PLAZO_INVALIDO";
    return informe;
  }
  try {
    // Mismos campos, orden, restaurante, barridos y condiciones para ambas.
    const auto soa = convertirSoA(aos);
    const auto cpuAoS = calcularAoS(aos, rx, ry, tramo, 1, barridos);
    const auto cpuSoA = calcularSoA(soa, rx, ry, tramo, 1, barridos);
    if (cpuAoS != cpuSoA) {
      throw std::runtime_error("LAYOUT04_GPU: divergencia AoS/SoA en CPU");
    }
    if (aos.empty()) {
      informe.fallback = "FLOTA_VACIA";
      return informe;
    }
    const auto deadline = std::chrono::steady_clock::now() + plazo;
    auto futuroAoS = enviarAoS(aos, rx, ry, tramo, barridos);
    auto futuroSoA = enviarSoA(soa, rx, ry, tramo, barridos);
    if (!futuroAoS.valid() || !futuroSoA.valid()) {
      throw std::runtime_error("LAYOUT04_GPU: futuro invalido");
    }
    if (futuroAoS.wait_until(deadline) != std::future_status::ready ||
        futuroSoA.wait_until(deadline) != std::future_status::ready) {
      informe.fallback = "TIMEOUT";
      return informe;
    }
    const auto gpuAoS = futuroAoS.get();
    const auto gpuSoA = futuroSoA.get();
    compararReferencia04(cpuAoS, gpuAoS.values, informe.maxError);
    compararReferencia04(cpuSoA, gpuSoA.values, informe.maxError);
    for (const double ms : {gpuAoS.kernelMs, gpuAoS.totalMs,
                            gpuSoA.kernelMs, gpuSoA.totalMs}) {
      if (!std::isfinite(ms) || ms < 0.0) {
        throw std::runtime_error("LAYOUT04_GPU: tiempos invalidos");
      }
    }
    informe.aosKernelMs = gpuAoS.kernelMs;
    informe.aosTotalMs = gpuAoS.totalMs;
    informe.soaKernelMs = gpuSoA.kernelMs;
    informe.soaTotalMs = gpuSoA.totalMs;
    informe.backend = "GPU";
    informe.usada = true;
  } catch (const std::exception& e) {
    informe.fallback = "ERROR_O_DIFERENCIA";
    // Evitar emitir excepciones de backends externos con rutas/commas/lineas.
    (void)e;
    informe.maxError = 0.0;
  }
  return informe;
}

} // namespace equipego::performance04
