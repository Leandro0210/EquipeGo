#pragma once

#include "analytics/CourierCoverage06.hpp"

#include <vector>

namespace equipego::stencil06 {

enum class VarianteGpu06 { BasicoGlobal, TiledShared };

struct ResultadoGpu06 {
  std::vector<float> valores;
  double kernelMs = 0.0;
  double h2dMs = 0.0;
  double d2hMs = 0.0;
  double totalMs = 0.0;
};

// Aislado, sin tocar Fleet/Dispatcher. El llamador es responsable del
// acceso exclusivo al dispositivo; en la futura integracion usar propietario GPU.
ResultadoGpu06 evolucionarGpu(const Campo06& campo, unsigned int sweeps,
    VarianteGpu06 variante, float cC = 0.2f, float cN = 0.2f);

} // namespace equipego::stencil06
