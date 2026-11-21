#include "analytics/RestaurantDemandHistogramGpu11.hpp"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace equipego::analytics;
#define REQUIRE11(c) do { if (!(c)) { std::cerr << "GPU11_ERROR,line=" << \
    __LINE__ << ",cond=" #c << '\n'; return 1; } } while (false)

static bool comparar(std::vector<PedidoDemanda11> pedidos,
                    std::size_t r, std::size_t f) {
  const auto ref = histogramaDemandaCPU11(pedidos, r, f);
  const auto gpu = ejecutarHistogramaDemandaGpu11(pedidos, r, f);
  return histogramasIguales11(ref, gpu.histograma) &&
         gpu.h2dMs >= 0 && gpu.kernelMs >= 0 &&
         gpu.d2hMs >= 0 && gpu.totalMs >= 0;
}
int main() {
  try {
    REQUIRE11(comparar({}, 1, 1));
    REQUIRE11(comparar({}, 24, 16));
    REQUIRE11(comparar({{0,0}}, 1, 1));
    REQUIRE11(comparar({{0,0},{0,0},{1,3},{2,1}}, 3, 4));
    for (std::size_t n : {1u,2u,17u,255u,256u,257u,1024u,4096u,
                           65536u,100003u}) {
      std::vector<PedidoDemanda11> pedidos(n);
      for (std::size_t i = 0; i < n; ++i)
        pedidos[i] = {static_cast<std::uint32_t>((i * 17 + 3) % 24),
                      static_cast<std::uint32_t>((i * 7 + 5) % 16)};
      REQUIRE11(comparar(pedidos, 24, 16));
      std::cout << "FEATURE11_GPU_CASE_OK,n=" << n << '\n';
    }
    std::vector<PedidoDemanda11> uno(4096, {0,0});
    REQUIRE11(comparar(uno, 1, 1));
    REQUIRE11(comparar({{31,31},{0,0}}, 32, 32));
    auto rechaza = [](const auto& accion) {
      try { accion(); return false; }
      catch (const std::invalid_argument&) { return true; }
    };
    REQUIRE11(rechaza([] { (void)ejecutarHistogramaDemandaGpu11({}, 0, 1); }));
    REQUIRE11(rechaza([] { (void)ejecutarHistogramaDemandaGpu11({}, 1, 0); }));
    REQUIRE11(rechaza([] { (void)ejecutarHistogramaDemandaGpu11({}, 33, 32); }));
    REQUIRE11(rechaza([] { (void)ejecutarHistogramaDemandaGpu11({{1,0}}, 1, 1); }));
    std::cout << "FEATURE11_GPU_EQUIVALENCIA_OK\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FEATURE11_GPU_EXCEPTION:" << e.what() << '\n';
    return 1;
  }
}
