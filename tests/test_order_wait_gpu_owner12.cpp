#include "analytics/OrderWaitGpuDispatch12.hpp"
#include "analytics/DeliveryHistogramGpu.hpp"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace equipego::analytics;
static void exigir12(bool x, const char* m) {
  if (!x) throw std::runtime_error(m);
}
int main() {
  try {
    PropietarioGpuHistograma owner;
    for (std::size_t n : {1u, 2u, 257u, 4096u, 65536u, 100003u}) {
      std::vector<std::uint64_t> esperas;
      esperas.reserve(n);
      for (std::size_t i = 0; i < n; ++i)
        esperas.push_back((i * 1399ULL + i/7) % 120000ULL);
      constexpr std::uint64_t umbral = 45000ULL;
      auto elegido = seleccionarFiltro12(esperas, umbral, true, true,
          [&owner](std::vector<std::uint64_t> datos, std::uint64_t t) {
            return owner.enviarFiltro12(std::move(datos), t);
          });
      exigir12(elegido.gpuUsada && elegido.backend == "GPU", "backend");
      exigir12(filtrosIguales12(elegido.filtro,
             filtrarEsperasCPU12(esperas, umbral)), "equivalencia");
      std::cout << "FEATURE12_OWNER_GPU_OK,n=" << n
                << ",kernel_ms=" << elegido.kernelMs << '\n';
    }
    auto f1 = owner.enviarFiltro12({0, 101, 100, 500}, 100);
    auto f2 = owner.enviarFiltro12({0, 0, 7, 7, 9}, 7);
    exigir12((f1.get().filtro.indices == std::vector<std::size_t>{1,3}), "cola1");
    exigir12((f2.get().filtro.indices == std::vector<std::size_t>{4}), "cola2");
    std::cout << "FEATURE12_OWNER_GPU_TEST_OK,CASES=8,QUEUE=OK\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FEATURE12_OWNER_GPU_TEST_ERROR:" << e.what() << '\n';
    return 1;
  }
}
