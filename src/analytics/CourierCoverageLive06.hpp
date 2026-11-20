#pragma once

// Feature 06, Etapa 3: proyeccion de snapshot del Fleet a Campo06.
// El algoritmo CPU/GPU se mantiene independiente de Fleet y Dispatcher.
#include "analytics/CourierCoverage06.hpp"

#include <cstddef>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace equipego::stencil06 {

struct Muestra06 {
  float x = 0.0f;               // posicion normalizada sobre la imagen OSM
  float y = 0.0f;
  bool averiado = false;        // courier roto no ofrece cobertura
  bool enMovimiento = false;    // posicion interpolada dentro de un segmento
};

struct InformeCobertura06 {
  Campo06 campo;
  std::size_t couriersTotales = 0;
  std::size_t couriersUsados = 0;
  std::size_t couriersAveriados = 0;
  std::size_t muestrasMovimiento = 0;
  std::size_t fuentesUnicas = 0;
};

inline InformeCobertura06 proyectarCobertura06(
    const std::vector<Muestra06>& muestras, std::size_t lado = 512) {
  InformeCobertura06 resultado;
  resultado.couriersTotales = muestras.size();
  std::vector<PuntoNormalizado06> activas;
  activas.reserve(muestras.size());
  for (const auto& m : muestras) {
    if (m.averiado) {
      ++resultado.couriersAveriados;
      continue;
    }
    if (!std::isfinite(m.x) || !std::isfinite(m.y) ||
        m.x < 0.0f || m.y < 0.0f || m.x > 1.0f || m.y > 1.0f) {
      throw std::invalid_argument("STENCIL06_RUNTIME: coordenada normalizada invalida");
    }
    activas.push_back({m.x, m.y});
    ++resultado.couriersUsados;
    if (m.enMovimiento) ++resultado.muestrasMovimiento;
  }
  resultado.campo = construirCampo(activas, lado);
  resultado.fuentesUnicas = contarFuentes(resultado.campo);
  if (resultado.couriersUsados + resultado.couriersAveriados !=
      resultado.couriersTotales) {
    throw std::logic_error("STENCIL06_RUNTIME: conteo inconsistente");
  }
  if (resultado.fuentesUnicas > resultado.couriersUsados) {
    throw std::logic_error("STENCIL06_RUNTIME: fuentes superan couriers");
  }
  return resultado;
}

} // namespace equipego::stencil06
