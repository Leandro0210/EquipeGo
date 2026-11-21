#pragma once

#include "analytics/EtaReduction.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace equipego::analytics {

// Regla de la rubrica: (etaSimulatedSeconds, courierId). Si dos entradas
// tienen el mismo ID (defensa), se desempata por courierIndex.
// El orden por ID se transforma en un rango numerico para que el argmin CPU
// y el kernel CUDA, que comparan (ETA, indice), usen EXACTAMENTE la misma regla.
template <class Quote>
struct EntradasEtaOrdenadasPorId {
  std::vector<EtaCandidato> candidatos;
  // posicionPorRango[rangoLex] -> posicion original dentro de las quotes.
  std::vector<std::size_t> posicionPorRango;
};

template <class Quote>
EntradasEtaOrdenadasPorId<Quote> prepararEtaPorCourierId(
    const std::vector<Quote>& quotes) {
  EntradasEtaOrdenadasPorId<Quote> salida;
  salida.posicionPorRango.resize(quotes.size());
  std::iota(salida.posicionPorRango.begin(),
            salida.posicionPorRango.end(), std::size_t{0});
  std::sort(salida.posicionPorRango.begin(), salida.posicionPorRango.end(),
            [&quotes](std::size_t i, std::size_t j) {
              if (quotes[i].courierId != quotes[j].courierId) {
                return quotes[i].courierId < quotes[j].courierId;
              }
              if (quotes[i].courierIndex != quotes[j].courierIndex) {
                return quotes[i].courierIndex < quotes[j].courierIndex;
              }
              return i < j;
            });
  salida.candidatos.reserve(quotes.size());
  for (std::size_t rango = 0; rango < quotes.size(); ++rango) {
    const auto& q = quotes[salida.posicionPorRango[rango]];
    salida.candidatos.push_back({rango, q.etaSimulatedSeconds, true});
  }
  return salida;
}

template <class Quote>
bool etaMenorPorCourierId(const Quote& a, const Quote& b) {
  if (a.etaSimulatedSeconds != b.etaSimulatedSeconds) {
    return a.etaSimulatedSeconds < b.etaSimulatedSeconds;
  }
  if (a.courierId != b.courierId) {
    return a.courierId < b.courierId;
  }
  return a.courierIndex < b.courierIndex;
}

// El primer courier se selecciona con argmin CPU; los demas permanecen
// ordenados para reintentar si Courier::tryAssignOrder rechaza la asignacion.
template <class Quote>
void ordenarCotizacionesPorReduccion(std::vector<Quote>& quotes) {
  quotes.erase(std::remove_if(quotes.begin(), quotes.end(), [](const Quote& q) {
    return !std::isfinite(q.etaSimulatedSeconds) || q.etaSimulatedSeconds < 0.0;
  }), quotes.end());
  if (quotes.empty()) return;

  const auto entradas = prepararEtaPorCourierId(quotes);
  const auto ganador = reducirMejorEtaCPU(entradas.candidatos);
  if (!ganador || ganador->courierIndex >= entradas.posicionPorRango.size()) {
    throw std::logic_error("Argmin sin cotizacion asociada");
  }
  const std::size_t posicion = entradas.posicionPorRango[ganador->courierIndex];
  std::iter_swap(quotes.begin(), quotes.begin() + posicion);
  std::sort(quotes.begin() + 1, quotes.end(), etaMenorPorCourierId<Quote>);
}

}  // namespace equipego::analytics
