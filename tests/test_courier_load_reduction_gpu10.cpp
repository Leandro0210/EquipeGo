#include "analytics/CourierLoadReductionGpu10.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace equipego::analytics;

static void exigir(bool ok, const char* mensaje) {
  if (!ok) throw std::runtime_error(mensaje);
}
static void comparar(const std::vector<std::uint32_t>& cargas) {
  const auto cpu = reducirCargaCouriersCPU10(cargas);
  const auto gpu = ejecutarCargaCouriersGpu10(cargas);
  const auto& g = gpu.resumen;
  exigir(cpu.couriers == g.couriers, "couriers");
  exigir(cpu.conPedidos == g.conPedidos, "conPedidos");
  exigir(cpu.totalReservas == g.totalReservas, "totalReservas");
  exigir(cpu.maxReservas == g.maxReservas, "maxReservas");
  exigir(cpu.indiceMax == g.indiceMax, "indiceMax");
  exigir(cpu.tieneMax == g.tieneMax, "tieneMax");
  exigir(cpu.promedio() == g.promedio(), "promedio");
  if (!cargas.empty())
    exigir(gpu.totalMs >= 0 && gpu.kernelMs >= 0 &&
           gpu.h2dMs >= 0 && gpu.d2hMs >= 0, "tiempos");
}
int main() {
  try {
    comparar({});
    comparar({0});
    comparar({7});
    comparar({2,8,0,8,3});
    comparar({0,0,0,0,0});
    comparar({std::numeric_limits<std::uint32_t>::max(),
              std::numeric_limits<std::uint32_t>::max()});
    for (std::size_t n : {2u,17u,255u,256u,257u,1024u,4096u,65536u,100003u}) {
      std::vector<std::uint32_t> cargas(n);
      for (std::size_t i = 0; i < n; ++i)
        cargas[i] = static_cast<std::uint32_t>((i * 11) % 13);
      if (n > 60000) { cargas[100] = 1000; cargas[60000] = 1000; }
      comparar(cargas);
      std::cout << "FEATURE10_GPU_CASE_OK,n=" << n << '\n';
    }
    std::cout << "FEATURE10_GPU_EQUIVALENCIA_OK\n";
    return 0;
  } catch(const std::exception& e) {
    std::cerr << "FEATURE10_GPU_TEST_ERROR:" << e.what() << '\n';
    return 1;
  }
}
