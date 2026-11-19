#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace equipego::analytics {

// Un contador por minuto (0..59), y el ultimo agrupa >= 60 minutos.
// Los tiempos son milisegundos SIMULADOS, no de reloj real.
inline constexpr std::size_t kHistogramaBins = 61;
inline constexpr long long kMsPorMinuto = 60000;
using HistogramaEntregas = std::array<std::uint64_t, kHistogramaBins>;

inline std::size_t indiceMinuto(long long duracionMs) {
  if (duracionMs < 0) {
    throw std::invalid_argument("Duracion de entrega negativa");
  }
  return static_cast<std::size_t>(
      std::min<long long>(duracionMs / kMsPorMinuto,
                          static_cast<long long>(kHistogramaBins - 1)));
}

inline HistogramaEntregas histogramaEntregasCPU(
    const std::vector<long long>& duracionesMs) {
  HistogramaEntregas bins{};
  for (const long long duracion : duracionesMs) {
    ++bins[indiceMinuto(duracion)];
  }
  return bins;
}

// CPU multihilo: privados por hilo, sin atomics ni carreras, y merge final.
// Se usa como baseline optimizado en las pruebas de benchmarking.
inline HistogramaEntregas histogramaEntregasCPUParalelo(
    const std::vector<long long>& duracionesMs, std::size_t hilos) {
  if (hilos == 0) {
    throw std::invalid_argument("La cantidad de hilos debe ser positiva");
  }
  HistogramaEntregas resultado{};
  if (duracionesMs.empty()) return resultado;

  // Validar antes de arrancar threads: no lanzar excepciones dentro de hilos.
  for (const long long duracion : duracionesMs) {
    if (duracion < 0) {
      throw std::invalid_argument("Duracion de entrega negativa");
    }
  }

  const std::size_t trabajadores = std::min(hilos, duracionesMs.size());
  std::vector<HistogramaEntregas> privados(trabajadores);
  std::vector<std::thread> threads;
  threads.reserve(trabajadores);

  for (std::size_t t = 0; t < trabajadores; ++t) {
    threads.emplace_back([&, t] {
      const std::size_t inicio = duracionesMs.size() * t / trabajadores;
      const std::size_t fin = duracionesMs.size() * (t + 1) / trabajadores;
      auto& local = privados[t];
      for (std::size_t i = inicio; i < fin; ++i) {
        const std::size_t bin = static_cast<std::size_t>(
            std::min<long long>(duracionesMs[i] / kMsPorMinuto,
                                static_cast<long long>(kHistogramaBins - 1)));
        ++local[bin];
      }
    });
  }
  for (auto& thread : threads) thread.join();
  for (const auto& local : privados) {
    for (std::size_t bin = 0; bin < kHistogramaBins; ++bin) {
      resultado[bin] += local[bin];
    }
  }
  return resultado;
}

}  // namespace equipego::analytics
