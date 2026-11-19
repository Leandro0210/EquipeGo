#pragma once

#include "analytics/EtaReduction.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace equipego::analytics {

// Aplica argmin a las cotizaciones del despacho REAL. El primer elemento
// determina el primer intento; los demas se ordenan para reintentar si
// tryAssignOrder detecta un cambio concurrente de capacidad o estado.
// Quote requiere courierIndex y etaSimulatedSeconds; no conoce a Courier.
template <class Quote>
void ordenarCotizacionesPorReduccion(std::vector<Quote>& quotes) {
  // La calculadora rechaza valores invalidos, pero una barrera defensiva
  // evita enviar a Courier resultados no finitos si cambia el productor.
  quotes.erase(std::remove_if(quotes.begin(), quotes.end(), [](const Quote& q) {
    return !std::isfinite(q.etaSimulatedSeconds) || q.etaSimulatedSeconds < 0.0;
  }), quotes.end());
  if (quotes.empty()) return;

  std::vector<EtaCandidato> candidatos;
  candidatos.reserve(quotes.size());
  for (const auto& q : quotes) {
    candidatos.push_back({q.courierIndex, q.etaSimulatedSeconds, true});
  }
  const auto ganador = reducirMejorEtaCPU(candidatos);
  if (!ganador) return;  // Defensa, todos los candidatos estaban filtrados.

  const auto it = std::find_if(quotes.begin(), quotes.end(), [&ganador](const Quote& q) {
    return q.courierIndex == ganador->courierIndex &&
           q.etaSimulatedSeconds == ganador->etaSimulatedSeconds;
  });
  if (it == quotes.end()) throw std::logic_error("Argmin sin cotizacion asociada");

  // Primero el ganador DE LA REDUCCION, no el resultado de un sort.
  std::iter_swap(quotes.begin(), it);
  std::sort(quotes.begin() + 1, quotes.end(), [](const Quote& a, const Quote& b) {
    if (a.etaSimulatedSeconds != b.etaSimulatedSeconds) {
      return a.etaSimulatedSeconds < b.etaSimulatedSeconds;
    }
    return a.courierIndex < b.courierIndex;
  });
}

}  // namespace equipego::analytics
