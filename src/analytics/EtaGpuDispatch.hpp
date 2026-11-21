#pragma once

#include "analytics/EtaDispatchOrder.hpp"
#include "analytics/EtaReductionGpu.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <future>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace equipego::analytics {

// No requiere CUDA para compilar: el callback se conecta desde main solamente
// cuando EQUIPEGO_USE_CUDA esta habilitado. La cola siempre posee las entradas.
using EnviarReduccionGpu08 = std::function<std::future<ResultadoReduccionGpu>(
    std::vector<EtaCandidato>)>;

enum class EstadoEleccionGpu08 {
  SoloCpu, Usada, SinTiempo, FalloGpu, DiferenciaCpuGpu
};

// Se preserva la ordenacion de respaldo: si el primero no acepta la orden,
// el dispatcher intenta los otros por (ETA, courierId).
// Una GPU lenta nunca obliga a bloquear pasado el deadline real indicado.
template <class Quote>
EstadoEleccionGpu08 ordenarConGpu08Ocpu(
    std::vector<Quote>& cotizaciones,
    const EnviarReduccionGpu08& enviar,
    std::chrono::steady_clock::time_point limite,
    std::size_t minimoCandidatos) {
  cotizaciones.erase(
      std::remove_if(cotizaciones.begin(), cotizaciones.end(), [](const Quote& q) {
        return !std::isfinite(q.etaSimulatedSeconds) || q.etaSimulatedSeconds < 0.0;
      }),
      cotizaciones.end());

  if (!enviar || cotizaciones.size() < minimoCandidatos ||
      cotizaciones.empty()) {
    ordenarCotizacionesPorReduccion(cotizaciones);
    return EstadoEleccionGpu08::SoloCpu;
  }

  if (std::chrono::steady_clock::now() >= limite) {
    ordenarCotizacionesPorReduccion(cotizaciones);
    return EstadoEleccionGpu08::SinTiempo;
  }

  // Cada courierId obtiene un rango lexicografico; la GPU compara
  // (ETA, rango) y recuperamos la cotizacion real al recibir el ganador.
  auto entradas = prepararEtaPorCourierId(cotizaciones);
  const auto referencia = reducirMejorEtaCPU(entradas.candidatos);

  try {
    auto futuro = enviar(std::move(entradas.candidatos));
    if (!futuro.valid()) {
      throw std::runtime_error("Futuro GPU no valido");
    }
    if (futuro.wait_until(limite) != std::future_status::ready) {
      // El hilo propietario puede seguir trabajando. El futuro se abandona
      // sin liberar entradas que el propietario ya tiene en su propia tarea.
      ordenarCotizacionesPorReduccion(cotizaciones);
      return EstadoEleccionGpu08::SinTiempo;
    }

    const ResultadoReduccionGpu gpu = futuro.get();
    const bool coincide = referencia.has_value() == gpu.mejor.has_value() &&
        (!referencia ||
         (referencia->courierIndex == gpu.mejor->courierIndex &&
          referencia->etaSimulatedSeconds == gpu.mejor->etaSimulatedSeconds));
    if (!coincide || !gpu.mejor) {
      ordenarCotizacionesPorReduccion(cotizaciones);
      return EstadoEleccionGpu08::DiferenciaCpuGpu;
    }

    // El indice retornado por GPU es un RANGO DE ID, no un indice de flota.
    if (gpu.mejor->courierIndex >= entradas.posicionPorRango.size()) {
      ordenarCotizacionesPorReduccion(cotizaciones);
      return EstadoEleccionGpu08::DiferenciaCpuGpu;
    }
    const auto posicion = entradas.posicionPorRango[gpu.mejor->courierIndex];
    std::iter_swap(cotizaciones.begin(), cotizaciones.begin() + posicion);
    std::sort(cotizaciones.begin() + 1, cotizaciones.end(),
              etaMenorPorCourierId<Quote>);
    return EstadoEleccionGpu08::Usada;
  } catch (...) {
    ordenarCotizacionesPorReduccion(cotizaciones);
    return EstadoEleccionGpu08::FalloGpu;
  }
}

}  // namespace equipego::analytics
