#include "analytics/DeliveryHistogram.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

#define REQUIRE(condition) \
  do { \
    if (!(condition)) { \
      std::cerr << "FALLO: " #condition << " (linea " << __LINE__ << ")\n"; \
      return 1; \
    } \
  } while (false)

int main() {
  using equipego::analytics::histogramaEntregasCPU;
  using equipego::analytics::histogramaEntregasCPUParalelo;
  using equipego::analytics::kHistogramaBins;

  const std::vector<long long> casos = {
      0, 59999, 60000, 119999, 120000, 3599999, 3600000, 7200000};
  const auto hist = histogramaEntregasCPU(casos);
  REQUIRE(hist.size() == kHistogramaBins);
  REQUIRE(hist[0] == 2);
  REQUIRE(hist[1] == 2);
  REQUIRE(hist[2] == 1);
  REQUIRE(hist[59] == 1);
  REQUIRE(hist[60] == 2);
  REQUIRE(std::accumulate(hist.begin(), hist.end(), std::uint64_t{0}) == casos.size());
  REQUIRE(histogramaEntregasCPU({}) == histogramaEntregasCPUParalelo({}, 4));

  std::vector<long long> datos;
  datos.reserve(100003);
  for (int i = 0; i < 100003; ++i) {
    datos.push_back(static_cast<long long>((i * 17) % 105) * 60000 + (i % 60000));
  }
  const auto referencia = histogramaEntregasCPU(datos);
  for (std::size_t hilos : {1u, 2u, 4u, 8u}) {
    REQUIRE(histogramaEntregasCPUParalelo(datos, hilos) == referencia);
  }
  REQUIRE(std::accumulate(referencia.begin(), referencia.end(), std::uint64_t{0}) == datos.size());

  bool negativoRechazado = false;
  try { (void)histogramaEntregasCPU({-1}); }
  catch (const std::invalid_argument&) { negativoRechazado = true; }
  REQUIRE(negativoRechazado);

  negativoRechazado = false;
  try { (void)histogramaEntregasCPUParalelo({1, -1}, 2); }
  catch (const std::invalid_argument&) { negativoRechazado = true; }
  REQUIRE(negativoRechazado);

  std::cout << "TEST HISTOGRAMA CPU 07: OK\n";
  return 0;
}
