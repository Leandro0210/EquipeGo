#pragma once
// Feature 10 / Etapa 2: reduccion GPU de reservas por courier.
// Los datos son snapshots inmutables; esta etapa no toca Fleet/Dispatcher.
#include "analytics/CourierLoadReduction10.hpp"
#include <cstdint>
#include <vector>

namespace equipego::analytics {

struct ResultadoCargaGpu10 {
  ResumenCarga10 resumen;
  double h2dMs = 0.0;
  double kernelMs = 0.0;
  double d2hMs = 0.0;
  double totalMs = 0.0; // Incluye reservas, copias y liberacion GPU.
};

// Llamar en un unico hilo GPU. La integracion al propietario viene en E3.
ResultadoCargaGpu10 ejecutarCargaCouriersGpu10(
    const std::vector<std::uint32_t>& reservas);

} // namespace equipego::analytics
