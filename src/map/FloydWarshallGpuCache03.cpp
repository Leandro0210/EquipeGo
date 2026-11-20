#include "map/RoadNetwork.hpp"
#include "map/FloydWarshallCuda03.hpp"
#include "map/FloydWarshallTiled03.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

// Determinista entre WSL y Colab: no depende de std::random_device.
std::uint64_t splitmix64(std::uint64_t& state) {
  state += 0x9e3779b97f4a7c15ULL;
  std::uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

}  // namespace

Floyd03GpuInitStats RoadNetwork::habilitarFloyd03Gpu(
    const SimulationConfig& config, std::size_t maxNodos) {
  if (floyd03_) {
    throw std::logic_error("Floyd03 GPU: tabla previamente habilitada");
  }
  if (maxNodos == 0 || maxNodos > 4096) {
    throw std::invalid_argument("Floyd03 GPU: maxNodos debe ser 1..4096");
  }

  // Usa las longitudes REALES que ConfigLoader construye para Street.
  auto tabla = std::make_shared<equipego::analytics::TablaRutas03>(
      equipego::analytics::construirMatrizRutas03(config, maxNodos));
  const std::size_t n = tabla->dimension();
  if (n != adjacency.size()) {
    throw std::invalid_argument("Floyd03 GPU: dimensiones distintas de RoadNetwork");
  }
  for (const auto& id : tabla->nodos) {
    if (adjacency.find(id) == adjacency.end()) {
      throw std::invalid_argument("Floyd03 GPU: nodo ajeno a RoadNetwork");
    }
  }

  // GPU solo precalcula; la tabla final y las consultas del dispatcher son CPU.
  // Todo se ejecuta antes de que arranquen los hilos del simulador.
  auto gpu = equipego::analytics::floydWarshallCuda03(tabla->metros, n, maxNodos);
  if (gpu.metros.size() != n * n) {
    throw std::runtime_error("Floyd03 GPU: tamanio de matriz inesperado");
  }
  tabla->metros = std::move(gpu.metros);

  // Nunca publicar una matriz con valores imposibles. El algoritmo trabaja
  // con aristas no negativas; +inf representa destinos sin camino.
  for (std::size_t i = 0; i < n; ++i) {
    if (tabla->metros[i * n + i] != 0.0) {
      throw std::logic_error("Floyd03 GPU: diagonal no nula");
    }
  }
  for (double d : tabla->metros) {
    if (std::isnan(d) || d < 0.0 || (std::isinf(d) && d < 0.0)) {
      throw std::logic_error("Floyd03 GPU: matriz con valor no permitido");
    }
  }

  std::size_t verificadas = 0;
  const auto comparar = [&](std::size_t i, std::size_t j) {
    const std::string& origen = tabla->nodos[i];
    const std::string& destino = tabla->nodos[j];
    const auto referencia = shortestRoute(origen, destino);
    const auto gpuDist = tabla->distanciaMetros(origen, destino);
    if (referencia.has_value() != gpuDist.has_value()) {
      throw std::logic_error("Floyd03 GPU: alcanzabilidad distinta de Dijkstra: " +
                             origen + " -> " + destino);
    }
    if (referencia) {
      const double esperado = referencia->distanceMeters;
      const double tolerancia = 1e-9 * std::max(1.0, std::abs(esperado));
      if (std::abs(esperado - *gpuDist) > tolerancia) {
        throw std::logic_error("Floyd03 GPU: distancia distinta de Dijkstra: " +
                               origen + " -> " + destino);
      }
    }
    ++verificadas;
  };

  // En la ciudad grande no verificar n*n veces mediante Dijkstra. La etapa 6
  // ya comparo matrices completas; aqui hacemos comprobacion de arranque.
  if (n <= 64) {
    for (std::size_t i = 0; i < n; ++i) {
      for (std::size_t j = 0; j < n; ++j) comparar(i, j);
    }
  } else {
    for (std::size_t i : {std::size_t{0}, n / 4, n / 2, 3 * n / 4, n - 1}) {
      comparar(i, i);
      comparar(i, 0);
      comparar(i, n - 1);
    }
    std::uint64_t seed = 0xF103C0FFEE123456ULL;
    for (std::size_t k = 0; k < 512; ++k) {
      const std::size_t i = static_cast<std::size_t>(splitmix64(seed) % n);
      const std::size_t j = static_cast<std::size_t>(splitmix64(seed) % n);
      comparar(i, j);
    }
  }

  const Floyd03GpuInitStats info{
      n, verificadas, tabla->bytesMatriz(), gpu.kernelMs, gpu.totalHostMs};

  // Unica publicacion. A partir de aqui floyd03_ no se modifica, y solo se
  // consulta desde multiples hilos mediante shared_ptr<const TablaRutas03>.
  floyd03_ = std::move(tabla);
  return info;
}
