#pragma once

#include "analytics/RestaurantOrderLive09.hpp"
#include "analytics/RestaurantOrderScanGpu.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace equipego::analytics {

// El dispatcher solo envia indices de restaurantes, NO objetos Order.
// El propietario CUDA realiza el calculo en su propio hilo.
using EnviarScanGpu09 = std::function<std::future<ResultadoScanGpu09>(
    std::vector<std::size_t>, std::size_t)>;

enum class EstadoScanGpu09 {
  SoloCpu, Usada, SinTiempo, FalloGpu, DiferenciaCpuGpu
};

struct ResultadoVistaGpu09 {
  VistaPendientesScan09 vista;
  EstadoScanGpu09 estado = EstadoScanGpu09::SoloCpu;
};

// OBSERVACIONAL: compara exactamente starts+indices con CPU y deja 'waiting'
// sin cambios. Si CUDA no responde antes del plazo real, se usa la vista CPU.
// La comparacion CPU deliberada permite detectar discrepancias sin perder datos.
template <class ContenedorPedidos>
ResultadoVistaGpu09 agruparPendientesConGpu09(
    const ContenedorPedidos& waiting,
    const std::unordered_map<std::string, std::size_t>& indiceRestaurantes,
    std::size_t numeroRestaurantes,
    const EnviarScanGpu09& enviar,
    std::chrono::steady_clock::time_point deadline,
    std::size_t minimoPedidos) {
  if (minimoPedidos == 0) {
    throw std::invalid_argument("Scan09: minimoPedidos debe ser mayor que 0");
  }

  ResultadoVistaGpu09 salida;
  salida.vista = agruparPendientesReales09(
      waiting, indiceRestaurantes, numeroRestaurantes);
  if (!enviar || waiting.size() < minimoPedidos || waiting.empty()) {
    return salida;
  }
  if (std::chrono::steady_clock::now() >= deadline) {
    salida.estado = EstadoScanGpu09::SinTiempo;
    return salida;
  }

  std::vector<std::size_t> ids;
  ids.reserve(waiting.size());
  for (const auto& pedido : waiting) {
    // Ya fue validado por agruparPendientesReales09().
    ids.push_back(indiceRestaurantes.at(pedido.restauranteId));
  }

  try {
    auto futuro = enviar(std::move(ids), numeroRestaurantes);
    if (!futuro.valid()) {
      throw std::runtime_error("Scan09: futuro CUDA invalido");
    }
    if (futuro.wait_until(deadline) != std::future_status::ready) {
      salida.estado = EstadoScanGpu09::SinTiempo;
      return salida;
    }

    auto gpu = futuro.get();
    if (gpu.agrupacion.starts != salida.vista.agrupacion.starts ||
        gpu.agrupacion.indices != salida.vista.agrupacion.indices) {
      salida.estado = EstadoScanGpu09::DiferenciaCpuGpu;
      return salida;
    }
    salida.vista.agrupacion = std::move(gpu.agrupacion);
    salida.estado = EstadoScanGpu09::Usada;
    return salida;
  } catch (...) {
    salida.estado = EstadoScanGpu09::FalloGpu;
    return salida;
  }
}

}  // namespace equipego::analytics
