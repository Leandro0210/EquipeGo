#pragma once

#include "analytics/EtaReduction.hpp"

#include <optional>
#include <vector>

namespace equipego::analytics {

// Backend generico: menor ETA y luego menor indice numerico.
// En el dispatcher, el indice recibido es el rango lexicografico de courierId
// (prepararEtaPorCourierId); el benchmark aislado conserva indices directos.
// CPU y GPU reciben el mismo vector, incluso si hay entradas invalidas.
struct ResultadoReduccionGpu {
  std::optional<EtaCandidato> mejor;
  double h2dMs = 0.0;
  double kernelMs = 0.0;
  double d2hMs = 0.0;
  double totalMs = 0.0;   // Incluye empaquetado, cudaMalloc y cudaFree.
};

// Ejecutar SOLO desde el hilo propietario CUDA, no desde Dispatcher.
ResultadoReduccionGpu ejecutarReduccionEtaEnGpu(
    const std::vector<EtaCandidato>& candidatos);

}  // namespace equipego::analytics
