#include "analytics/DeliveryHistogramGpu.hpp"

#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

#define REQUIRE(condition) \
  do { \
    if (!(condition)) { \
      std::cerr << "FALLO GPU 07: " #condition " (linea " << __LINE__ << ")\n"; \
      return 1; \
    } \
  } while (false)

int main() {
  using namespace equipego::analytics;
  try {
    PropietarioGpuHistograma propietario;
    for (const std::size_t n : {0u, 1u, 61u, 257u, 200000u}) {
      std::vector<long long> datos;
      datos.reserve(n);
      for (std::size_t i = 0; i < n; ++i) {
        datos.push_back(static_cast<long long>((i * 17u) % 105u) * 60000 +
                        static_cast<long long>(i % 60000u));
      }
      if (n > 0) datos[0] = 3600000;  // cubeta de desbordamiento
      const auto esperado = histogramaEntregasCPU(datos);
      // Dos variantes son experimentos de UNA feature.
      auto global = propietario.enviar(datos, VarianteHistogramaGpu::AtomicosGlobales);
      auto privado = propietario.enviar(datos, VarianteHistogramaGpu::CompartidaPrivada);
      const auto r1 = global.get();
      const auto r2 = privado.get();
      REQUIRE(r1.bins == esperado);
      REQUIRE(r2.bins == esperado);
      REQUIRE(std::accumulate(r2.bins.begin(), r2.bins.end(),
                              std::uint64_t{0}) == datos.size());
    }
    auto negativo = propietario.enviar({-1}, VarianteHistogramaGpu::CompartidaPrivada);
    bool rechazo = false;
    try { (void)negativo.get(); }
    catch (const std::invalid_argument&) { rechazo = true; }
    REQUIRE(rechazo);
    std::cout << "TEST HISTOGRAMA GPU 07: OK\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "TEST HISTOGRAMA GPU 07 ERROR: " << e.what() << '\n';
    return 1;
  }
}
