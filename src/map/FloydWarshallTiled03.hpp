#pragma once

#include "config/Config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace equipego::analytics {

// Matriz dirigida de distancias de la red vial, expresadas en metros.
// No son distancias rectas: usa EXACTAMENTE street.lengthMeters y oneWay.
// Inalcanzable: +inf. Nodo desconocido: nullopt, como RoadNetwork.
struct TablaRutas03 {
  std::vector<std::string> nodos;
  std::unordered_map<std::string, std::size_t> indicePorNodo;
  std::vector<double> metros;  // Matriz densa, n*n, por filas.

  std::size_t dimension() const noexcept { return nodos.size(); }
  std::size_t bytesMatriz() const noexcept { return metros.size() * sizeof(double); }

  std::optional<double> distanciaMetros(const std::string& origen,
                                       const std::string& destino) const {
    const auto desde = indicePorNodo.find(origen);
    const auto hasta = indicePorNodo.find(destino);
    if (desde == indicePorNodo.end() || hasta == indicePorNodo.end()) {
      return std::nullopt;
    }
    const double d = metros[desde->second * dimension() + hasta->second];
    if (!std::isfinite(d)) return std::nullopt;
    return d;
  }
};

inline TablaRutas03 construirMatrizRutas03(const SimulationConfig& config,
                                         std::size_t maxNodos = 512) {
  if (maxNodos == 0 || config.nodes.empty() || config.nodes.size() > maxNodos) {
    throw std::invalid_argument("Floyd03: numero de nodos fuera del limite");
  }
  const std::size_t n = config.nodes.size();
  // Limite conservador para evitar costos O(n^2) / O(n^3) accidentales.
  if (n > std::numeric_limits<std::size_t>::max() / n) {
    throw std::length_error("Floyd03: overflow n*n");
  }
  TablaRutas03 tabla;
  tabla.nodos.reserve(n);
  tabla.indicePorNodo.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const std::string& id = config.nodes[i].id;
    if (id.empty() || !tabla.indicePorNodo.emplace(id, i).second) {
      throw std::invalid_argument("Floyd03: ID de nodo vacio o duplicado");
    }
    tabla.nodos.push_back(id);
  }

  const double inf = std::numeric_limits<double>::infinity();
  tabla.metros.assign(n * n, inf);
  for (std::size_t i = 0; i < n; ++i) tabla.metros[i * n + i] = 0.0;

  for (const Street& street : config.streets) {
    const auto from = tabla.indicePorNodo.find(street.from);
    const auto to = tabla.indicePorNodo.find(street.to);
    if (from == tabla.indicePorNodo.end() || to == tabla.indicePorNodo.end()) {
      throw std::invalid_argument("Floyd03: calle con extremo inexistente");
    }
    if (!std::isfinite(street.lengthMeters) || street.lengthMeters < 0.0) {
      throw std::invalid_argument("Floyd03: longitud de calle invalida");
    }
    const auto guardarMinimo = [&](std::size_t i, std::size_t j) {
      double& d = tabla.metros[i * n + j];
      d = std::min(d, street.lengthMeters);
    };
    guardarMinimo(from->second, to->second);
    if (!street.oneWay) guardarMinimo(to->second, from->second);
  }
  return tabla;
}

// Referencia O(n^3): equivalente a Dijkstra para aristas no negativas.
inline TablaRutas03 floydWarshallReferenciaCpu03(
    const SimulationConfig& config, std::size_t maxNodos = 512) {
  auto t = construirMatrizRutas03(config, maxNodos);
  const std::size_t n = t.dimension();
  for (std::size_t k = 0; k < n; ++k) {
    for (std::size_t i = 0; i < n; ++i) {
      const double dik = t.metros[i * n + k];
      if (!std::isfinite(dik)) continue;
      for (std::size_t j = 0; j < n; ++j) {
        const double dkj = t.metros[k * n + j];
        if (!std::isfinite(dkj)) continue;
        const double alternativa = dik + dkj;
        if (alternativa < t.metros[i * n + j]) {
          t.metros[i * n + j] = alternativa;
        }
      }
    }
  }
  return t;
}

// Floyd-Warshall POR BLOQUES, en tres fases por pivote:
//   1) diagonal (pivote,pivote)
//   2) fila/columna del pivote
//   3) bloques restantes
// Prepara las dependencias correctas del posterior kernel CUDA con shared memory.
// Esta es una version CPU; aqui no se afirma reutilizacion en memoria compartida GPU.
inline TablaRutas03 floydWarshallBloquesCpu03(
    const SimulationConfig& config, std::size_t tile = 16,
    std::size_t maxNodos = 512) {
  if (tile == 0 || tile > 32) {
    throw std::invalid_argument("Floyd03: tile debe estar entre 1 y 32");
  }
  auto t = construirMatrizRutas03(config, maxNodos);
  const std::size_t n = t.dimension();

  const auto actualizar = [&](std::size_t ib, std::size_t ie,
                              std::size_t jb, std::size_t je,
                              std::size_t kb, std::size_t ke) {
    for (std::size_t k = kb; k < ke; ++k) {
      for (std::size_t i = ib; i < ie; ++i) {
        const double dik = t.metros[i * n + k];
        if (!std::isfinite(dik)) continue;
        for (std::size_t j = jb; j < je; ++j) {
          const double dkj = t.metros[k * n + j];
          if (!std::isfinite(dkj)) continue;
          const double alternativa = dik + dkj;
          if (alternativa < t.metros[i * n + j]) {
            t.metros[i * n + j] = alternativa;
          }
        }
      }
    }
  };

  for (std::size_t p = 0; p < n; p += tile) {
    const std::size_t pe = std::min(n, p + tile);
    // Fase 1: bloque diagonal.
    actualizar(p, pe, p, pe, p, pe);
    // Fase 2: resto de bloques de la fila y columna pivote.
    for (std::size_t q = 0; q < n; q += tile) {
      if (q == p) continue;
      const std::size_t qe = std::min(n, q + tile);
      actualizar(p, pe, q, qe, p, pe);
      actualizar(q, qe, p, pe, p, pe);
    }
    // Fase 3: todas las demas parejas (fila,columna) de bloques.
    for (std::size_t bi = 0; bi < n; bi += tile) {
      if (bi == p) continue;
      const std::size_t ie = std::min(n, bi + tile);
      for (std::size_t bj = 0; bj < n; bj += tile) {
        if (bj == p) continue;
        const std::size_t je = std::min(n, bj + tile);
        actualizar(bi, ie, bj, je, p, pe);
      }
    }
  }
  return t;
}

}  // namespace equipego::analytics
