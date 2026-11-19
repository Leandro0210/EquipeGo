#include "analytics/DeliveryHistogramGpu.hpp"
#include "analytics/EtaReductionGpu.hpp"

#include <cmath>
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
  auto comprobar = [&](std::vector<EtaCandidato> candidatos) {
    const auto referencia = reducirMejorEtaCPU(candidatos);
    auto futuro = propietario.enviarEta(std::move(candidatos));
    const auto gpu = futuro.get();
    if (referencia.has_value() != gpu.mejor.has_value()) return false;
    if (referencia &&
        (referencia->courierIndex != gpu.mejor->courierIndex ||
         referencia->etaSimulatedSeconds != gpu.mejor->etaSimulatedSeconds)) return false;
    return gpu.kernelMs >= 0.0 && gpu.h2dMs >= 0.0 && gpu.d2hMs >= 0.0 &&
           gpu.totalMs >= 0.0;
  };
  EXIGIR(comprobar({}));
  EXIGIR(comprobar({{2, 1.0, false}}));
  EXIGIR(comprobar({{8, 4.0, true}, {3, 4.0, true}, {1, 0.0, false}}));
  EXIGIR(comprobar({{1, -2.0, true}, {2, std::numeric_limits<double>::quiet_NaN(), true},
                   {3, std::numeric_limits<double>::infinity(), true}}));
  EXIGIR(comprobar({{1, -2.0, true}, {2, 123.0, true}}));

  std::mt19937_64 gen(2026);
  for (std::size_t n : {1u, 2u, 7u, 255u, 256u, 257u, 1000u, 10000u, 100000u}) {
    std::vector<EtaCandidato> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      v.push_back({n - i, static_cast<double>(gen() % 250), (i % 17) != 0});
    }
    if (n > 500) {
      v[n / 4] = {42, 0.125, true};
      v[n / 2] = {7, 0.125, true};
    }
    EXIGIR(comprobar(std::move(v)));
  }

  // Misma cola propietaria procesa solicitudes de Histogram y Reduction.
  auto hist = propietario.enviar({0, 60000, 60000, 120000});
  auto eta = propietario.enviarEta({{4, 10.0, true}, {2, 2.0, true}});
  const auto resultadoHist = hist.get();
  EXIGIR(resultadoHist.bins[0] == 1 && resultadoHist.bins[1] == 2 &&
         resultadoHist.bins[2] == 1);
  EXIGIR(eta.get().mejor->courierIndex == 2);
  std::cout << "TEST REDUCTION GPU 08 + PROPIETARIO COMPARTIDO: OK\n";
  return 0;
}
