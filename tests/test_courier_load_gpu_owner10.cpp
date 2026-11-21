#include "analytics/CourierLoadGpuDispatch10.hpp"
#include "analytics/DeliveryHistogramGpu.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace equipego::analytics;

static void exigir(bool cond, const char* message) {
  if (!cond) throw std::runtime_error(message);
}

int main() {
  try {
    PropietarioGpuHistograma owner;
    for (std::size_t n : {1u, 2u, 257u, 100003u}) {
      std::vector<std::uint32_t> cargas(n);
      for (std::size_t i = 0; i < n; ++i)
        cargas[i] = static_cast<std::uint32_t>((i * 17) % 13);
      if (n > 100000) {
        cargas[100] = 77;
        cargas[99999] = 77;
      }
      const auto resultado = seleccionarCarga10(cargas, true, true,
        [&owner](std::vector<std::uint32_t> input) {
          return owner.enviarCarga10(std::move(input));
        });
      exigir(resultado.gpuUsada && resultado.backend == "GPU", "backend GPU");
      exigir(resumenIgual10(resultado.resumen,
                            reducirCargaCouriersCPU10(cargas)), "equivalencia");
      std::cout << "FEATURE10_OWNER_GPU_OK,n=" << n
                << ",kernel_ms=" << resultado.kernelMs
                << ",total_ms=" << resultado.totalMs << '\n';
    }
    // Solicitudes concurrentes al mismo propietario, sin uso CUDA externo.
    auto f1 = owner.enviarCarga10({3, 0, 8});
    auto f2 = owner.enviarCarga10({2, 2, 2, 2});
    exigir(resumenIgual10(f1.get().resumen,
                          reducirCargaCouriersCPU10({3, 0, 8})), "cola f1");
    exigir(resumenIgual10(f2.get().resumen,
                          reducirCargaCouriersCPU10({2, 2, 2, 2})), "cola f2");
    std::cout << "FEATURE10_OWNER_GPU_TEST_OK,CASES=6,QUEUE=OK\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FEATURE10_OWNER_GPU_TEST_ERROR:" << e.what() << '\n';
    return 1;
  }
}
