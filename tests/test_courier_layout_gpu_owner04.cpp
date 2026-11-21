// Se compila SOLO con USE_CUDA=ON y se ejecuta en NVIDIA T4.
#include "analytics/CourierLayoutGpuDispatch04.hpp"
#include "analytics/DeliveryHistogramGpu.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace equipego::performance04;

int main() {
  try {
    equipego::analytics::PropietarioGpuHistograma propietario;
    std::vector<CourierAoS04> couriers;
    for (unsigned i = 0; i < 513; ++i) {
      couriers.push_back(CourierAoS04{
          static_cast<float>(i) * 4.0f,
          static_cast<float>(i % 51) * 9.0f,
          3.0f + static_cast<float>(i % 7),
          static_cast<std::int32_t>(i % 3), 3,
          i % 19 == 0 ? 2 : 0});
    }
    auto info = seleccionarLayoutGpu04(
        couriers, 200.0f, 300.0f, 110.0f, 3, true, true,
        [&propietario](std::vector<CourierAoS04> v, float x, float y,
                       float tramo, unsigned barridos) {
          return propietario.enviarLayoutAoS04(
              std::move(v), x, y, tramo, barridos);
        },
        [&propietario](CourierSoA04 v, float x, float y,
                       float tramo, unsigned barridos) {
          return propietario.enviarLayoutSoA04(
              std::move(v), x, y, tramo, barridos);
        });
    if (!info.usada || info.backend != "GPU") {
      throw std::runtime_error("no se uso propietario GPU para ambos layouts: " +
                               info.fallback);
    }
    std::cout << "LAYOUT04_GPU_OWNER_TEST_OK,n=" << info.couriers
              << ",aos_kernel_ms=" << info.aosKernelMs
              << ",soa_kernel_ms=" << info.soaKernelMs
              << ",max_error=" << info.maxError << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "LAYOUT04_GPU_OWNER_TEST_FAIL," << e.what() << '\n';
    return 1;
  }
}
