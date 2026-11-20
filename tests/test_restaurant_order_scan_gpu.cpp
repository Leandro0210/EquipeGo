#include "analytics/DeliveryHistogramGpu.hpp"
#include "analytics/RestaurantOrderScanGpu.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

#define EXIGIR(cond) do { if (!(cond)) { \
  std::cerr << "FALLO: " #cond " en linea " << __LINE__ << '\n'; return 1; \
} } while (false)

int main() {
  using namespace equipego::analytics;
  PropietarioGpuHistograma propietario;
  auto verificarIgualdad = [&](const std::vector<std::size_t>& ids,
                               std::size_t restaurantes) {
    const auto esperado = agruparPedidosReferencia09(ids, restaurantes);
    const auto cpu = agruparPedidosScanCPU09(ids, restaurantes);
    auto futuro = propietario.enviarScan09(ids, restaurantes);
    const auto gpu = futuro.get();
    return cpu.starts == esperado.starts && cpu.indices == esperado.indices &&
           gpu.agrupacion.starts == esperado.starts &&
           gpu.agrupacion.indices == esperado.indices &&
           gpu.h2dMs >= 0 && gpu.kernelMs >= 0 && gpu.d2hMs >= 0 &&
           gpu.totalMs >= 0;
  };

  EXIGIR(verificarIgualdad({}, 0));
  EXIGIR(verificarIgualdad({}, 5));
  EXIGIR(verificarIgualdad({0}, 1));
  EXIGIR(verificarIgualdad({2, 0, 2, 1, 0, 2}, 4));
  EXIGIR(verificarIgualdad({0, 0, 0, 0, 0}, 1));
  EXIGIR(verificarIgualdad({7, 7, 0, 7, 0, 4}, 8));

  std::mt19937 rng(2026);
  const bool rapido = std::getenv("SCAN09_TEST_RAPIDO") != nullptr;
  for (const std::size_t r : {1u, 2u, 5u, 6u, 17u, 255u, 256u, 257u,
                              511u, 512u, 1025u}) {
    if (rapido && r != 1 && r != 6 && r != 257) continue;
    for (const std::size_t n : {1u, 7u, 31u, 255u, 256u, 257u,
                               1000u, 10000u}) {
      if (rapido && n != 1 && n != 257 && n != 1000) continue;
      std::vector<std::size_t> ids(n);
      for (std::size_t i = 0; i < n; ++i) ids[i] = rng() % r;
      EXIGIR(verificarIgualdad(ids, r));
    }
  }
  if (!rapido) {
    std::vector<std::size_t> grandes(100000);
    for (auto& id : grandes) id = rng() % 6;
    EXIGIR(verificarIgualdad(grandes, 6));
  }

  bool rechazado = false;
  try {
    (void)propietario.enviarScan09({0, 2}, 2).get();
  } catch (const std::invalid_argument&) {
    rechazado = true;
  }
  EXIGIR(rechazado);

  // Verificar que Histogram 07, Reduction 08 y Scan 09 comparten propietario.
  auto histogram = propietario.enviar({0, 60000, 60000});
  auto reduction = propietario.enviarEta({{3, 2.0, true}, {1, 1.0, true}});
  auto scan = propietario.enviarScan09({1, 0, 1, 0}, 2);
  EXIGIR(histogram.get().bins[0] == 1);
  EXIGIR(reduction.get().mejor->courierIndex == 1);
  EXIGIR(scan.get().agrupacion.indices ==
         (std::vector<std::size_t>{1, 3, 0, 2}));

  std::cout << "TEST SCAN GPU 09 + PROPIETARIO COMPARTIDO: OK\n";
  return 0;
}
