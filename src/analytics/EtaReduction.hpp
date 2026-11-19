#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace equipego::analytics {

// Un candidato por courier: la disponibilidad corresponde a la instantanea
// anterior al calculo; la asignacion DEBE revalidarse en Courier::tryAssignOrder.
struct EtaCandidato {
  std::size_t courierIndex = 0;
  double etaSimulatedSeconds = 0.0;
  bool disponible = true;
};

inline bool etaValida(const EtaCandidato& candidato) noexcept {
  return candidato.disponible &&
         std::isfinite(candidato.etaSimulatedSeconds) &&
         candidato.etaSimulatedSeconds >= 0.0;
}

// Regla determinista: ETA ascendente; empate -> menor indice de courier.
inline bool mejorEta(const EtaCandidato& nuevo,
                     const EtaCandidato& actual) noexcept {
  return nuevo.etaSimulatedSeconds < actual.etaSimulatedSeconds ||
         (nuevo.etaSimulatedSeconds == actual.etaSimulatedSeconds &&
          nuevo.courierIndex < actual.courierIndex);
}

inline std::optional<EtaCandidato> reducirMejorEtaCPU(
    const std::vector<EtaCandidato>& candidatos) {
  std::optional<EtaCandidato> mejor;
  for (const auto& candidato : candidatos) {
    if (!etaValida(candidato)) continue;
    if (!mejor || mejorEta(candidato, *mejor)) mejor = candidato;
  }
  return mejor;
}

// Referencia multihilo para comparar con la reduccion CUDA. Las posiciones
// se reparten sin escrituras compartidas; cada hilo publica un ganador local.
inline std::optional<EtaCandidato> reducirMejorEtaCPUParalelo(
    const std::vector<EtaCandidato>& candidatos, std::size_t hilos) {
  if (hilos == 0) {
    throw std::invalid_argument("La cantidad de hilos debe ser positiva");
  }
  if (candidatos.empty()) return std::nullopt;

  const std::size_t trabajadores = std::min(hilos, candidatos.size());
  std::vector<std::optional<EtaCandidato>> mejores(trabajadores);
  std::vector<std::thread> threads;
  threads.reserve(trabajadores);
  for (std::size_t t = 0; t < trabajadores; ++t) {
    threads.emplace_back([&, t] {
      const std::size_t inicio = candidatos.size() * t / trabajadores;
      const std::size_t fin = candidatos.size() * (t + 1) / trabajadores;
      std::optional<EtaCandidato> local;
      for (std::size_t i = inicio; i < fin; ++i) {
        const auto& c = candidatos[i];
        if (!etaValida(c)) continue;
        if (!local || mejorEta(c, *local)) local = c;
      }
      mejores[t] = local;
    });
  }
  for (auto& hilo : threads) hilo.join();

  std::optional<EtaCandidato> mejor;
  for (const auto& local : mejores) {
    if (local && (!mejor || mejorEta(*local, *mejor))) mejor = *local;
  }
  return mejor;
}

}  // namespace equipego::analytics
