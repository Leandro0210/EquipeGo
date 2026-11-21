#pragma once
// Feature 12: Filter/compaction estable de tiempos de espera de pedidos.
// Selecciona indices cuya espera supera estrictamente un umbral.
// Solo lee el vector de entrada; no cambia pedidos ni Dispatcher.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace equipego::analytics {

struct ResultadoFiltro12 {
  std::size_t totalEntradas = 0;
  std::uint64_t umbralMs = 0;
  std::vector<std::size_t> indices;

  std::size_t seleccionados() const noexcept { return indices.size(); }
  std::size_t descartados() const noexcept { return totalEntradas - indices.size(); }
};

inline bool filtrosIguales12(const ResultadoFiltro12& a,
                             const ResultadoFiltro12& b) noexcept {
  return a.totalEntradas == b.totalEntradas &&
         a.umbralMs == b.umbralMs && a.indices == b.indices;
}

inline ResultadoFiltro12 filtrarEsperasCPU12(
    const std::vector<std::uint64_t>& esperasMs, std::uint64_t umbralMs) {
  ResultadoFiltro12 resultado;
  resultado.totalEntradas = esperasMs.size();
  resultado.umbralMs = umbralMs;
  resultado.indices.reserve(esperasMs.size());
  for (std::size_t i = 0; i < esperasMs.size(); ++i) {
    if (esperasMs[i] > umbralMs) resultado.indices.push_back(i);
  }
  return resultado;
}

// Un vector de indices por particion. Concatenacion en orden de particion;
// por tanto el resultado es estable e identico a la referencia secuencial.
inline ResultadoFiltro12 filtrarEsperasCPUParalelo12(
    const std::vector<std::uint64_t>& esperasMs,
    std::uint64_t umbralMs, std::size_t hilos) {
  if (hilos == 0)
    throw std::invalid_argument("Feature12: numero de hilos invalido");
  if (esperasMs.empty() || hilos == 1)
    return filtrarEsperasCPU12(esperasMs, umbralMs);

  const std::size_t trabajadores = std::min(hilos, esperasMs.size());
  std::vector<std::vector<std::size_t>> parciales(trabajadores);
  std::vector<std::thread> pool;
  pool.reserve(trabajadores - 1);
  auto tarea = [&](std::size_t t) {
    const auto primero = esperasMs.size() / trabajadores * t +
        std::min(t, esperasMs.size() % trabajadores);
    const auto ultimo = esperasMs.size() / trabajadores * (t + 1) +
        std::min(t + 1, esperasMs.size() % trabajadores);
    auto& local = parciales[t];
    local.reserve(ultimo - primero);
    for (std::size_t i = primero; i < ultimo; ++i)
      if (esperasMs[i] > umbralMs) local.push_back(i);
  };

  for (std::size_t t = 1; t < trabajadores; ++t)
    pool.emplace_back(tarea, t);
  tarea(0);
  for (auto& hilo : pool) hilo.join();

  ResultadoFiltro12 resultado;
  resultado.totalEntradas = esperasMs.size();
  resultado.umbralMs = umbralMs;
  for (const auto& p : parciales)
    resultado.indices.insert(resultado.indices.end(), p.begin(), p.end());
  return resultado;
}

}  // namespace equipego::analytics
