#include "analytics/DeliveryHistogramGpu.hpp"
#include "analytics/CoverageGpuDispatch06.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error("CHECK failed: " #condition); } while(0)
using namespace equipego::stencil06;
int main() {
  equipego::analytics::PropietarioGpuHistograma owner;
  for (std::size_t side : {4u, 17u, 64u}) {
    const auto campo = construirCampo({{0.2f, 0.3f}, {0.72f, 0.72f}}, side);
    for (const auto variant : {VarianteGpu06::BasicoGlobal, VarianteGpu06::TiledShared}) {
      const auto result = seleccionarCobertura06(campo, 20, true, true,
          [&owner, variant](Campo06 c, unsigned int sweeps) {
            return owner.enviarCobertura06(std::move(c), sweeps, variant);
          });
      CHECK(result.gpuUsada && result.backend == "GPU");
      CHECK(result.errorMaximo <= 1e-5f && result.totalMs >= 0.0);
      std::cout << "STENCIL06_OWNER_T4_OK,side=" << side
                << ",variant=" << (variant == VarianteGpu06::BasicoGlobal ? "basic" : "tiled")
                << ",kernel_ms=" << result.kernelMs << ",total_ms=" << result.totalMs
                << ",max_error=" << result.errorMaximo << '\n';
    }
  }
  std::cout << "STENCIL06_OWNER_GPU_TEST_COMPLETO,cases=6\n";
}
