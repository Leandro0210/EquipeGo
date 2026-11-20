#include "analytics/CourierCoverageGpu06.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace equipego::stencil06;

#define REQUIRE(cond) do { if (!(cond)) throw std::runtime_error("STENCIL06_GPU_TEST: " #cond); } while(false)

void comparar(const std::vector<float>& cpu, const std::vector<float>& gpu) {
  REQUIRE(cpu.size() == gpu.size());
  for (std::size_t i = 0; i < cpu.size(); ++i) {
    REQUIRE(std::isfinite(gpu[i]));
    REQUIRE(gpu[i] >= -1e-6f && gpu[i] <= 1.000001f);
    const float diff = std::fabs(cpu[i] - gpu[i]);
    REQUIRE(diff <= 1e-5f);
  }
}

int main() {
  std::vector<PuntoNormalizado06> points;
  for (int i = 0; i < 1300; ++i)
    points.push_back({static_cast<float>((i * 73) % 997) / 997.0f,
                      static_cast<float>((i * 127) % 991) / 991.0f});
  for (std::size_t n : {3u, 5u, 17u, 32u, 64u, 128u, 512u}) {
    const auto field = construirCampo(points, n);
    const auto expected = evolucionar(field, 20, 1);
    for (auto mode : {VarianteGpu06::BasicoGlobal, VarianteGpu06::TiledShared}) {
      const auto out = evolucionarGpu(field, 20, mode);
      comparar(expected, out.valores);
      REQUIRE(out.kernelMs > 0 && out.totalMs > 0);
      for (std::size_t i = 0; i < expected.size(); ++i) {
        if (field.fijas[i]) REQUIRE(out.valores[i] == field.valores[i]);
      }
    }
    std::cout << "STENCIL06_GPU_EQUIVALENCIA_OK,side=" << n
              << ",sweeps=20,tolerancia_absoluta=1e-5\n";
  }
  const auto small = construirCampo(points, 17);
  comparar(small.valores, evolucionarGpu(small, 0, VarianteGpu06::BasicoGlobal).valores);
  bool bad = false;
  try { (void)evolucionarGpu(small, 1, static_cast<VarianteGpu06>(-1)); }
  catch(const std::invalid_argument&) { bad=true; }
  REQUIRE(bad);
  std::cout << "STENCIL06_GPU_TEST_COMPLETO,modos=2,casos=7,bordes=OK,fijas=OK\n";
}
