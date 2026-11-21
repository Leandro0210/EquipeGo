#pragma once

#include <cstddef>
#include <vector>

namespace equipego::analytics {

// Entrada: matriz dirigida densa n*n (metros, +infinito si no hay ruta).
// Salida: Floyd-Warshall por bloques FP64. No toca el dispatcher ni el mapa.
struct ResultadoFloydGpu03 {
  std::vector<double> metros;
  double h2dMs = 0;
  double kernelMs = 0;
  double d2hMs = 0;
  double totalHostMs = 0;  // incluye reserva/liberacion y copias
};

ResultadoFloydGpu03 floydWarshallCuda03(
    const std::vector<double>& matrizInicial,
    std::size_t n,
    std::size_t maxNodos = 4096);

}  // namespace equipego::analytics
