#include "analytics/RestaurantDemandGpuDispatch11.hpp"
#include "analytics/DeliveryHistogramGpu.hpp"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace equipego::analytics;
static void require11(bool v, const char* reason) {
  if (!v) throw std::runtime_error(reason);
}
int main() {
  try {
    PropietarioGpuHistograma owner;
    for (std::size_t n : {1u, 2u, 257u, 4096u, 65536u}) {
      std::vector<PedidoDemanda11> pedidos;
      pedidos.reserve(n);
      for (std::size_t i = 0; i < n; ++i)
        pedidos.push_back({static_cast<std::uint32_t>((i * 13 + 1) % 24),
                           static_cast<std::uint32_t>((i * 7 + 2) % 16)});
      const auto elegido = seleccionarDemanda11(pedidos, 24, 16, true, true,
        [&owner](std::vector<PedidoDemanda11> datos,
                 std::size_t restaurantes, std::size_t franjas) {
          return owner.enviarDemanda11(std::move(datos), restaurantes, franjas);
        });
      require11(elegido.gpuUsada && elegido.backend == "GPU", "backend");
      require11(histogramasIguales11(elegido.histograma,
                   histogramaDemandaCPU11(pedidos, 24, 16)), "equivalencia");
      std::cout << "FEATURE11_OWNER_GPU_OK,n=" << n
                << ",kernel_ms=" << elegido.kernelMs << '\n';
    }
    auto f1 = owner.enviarDemanda11({{0,0},{1,1},{0,0}}, 2, 2);
    auto f2 = owner.enviarDemanda11({{0,3},{1,0},{1,0}}, 2, 4);
    require11(f1.get().histograma.en(0,0) == 2, "cola f1");
    require11(f2.get().histograma.en(1,0) == 2, "cola f2");
    std::cout << "FEATURE11_OWNER_GPU_TEST_OK,CASES=7,QUEUE=OK\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FEATURE11_OWNER_GPU_TEST_ERROR:" << e.what() << '\n';
    return 1;
  }
}
