#include "analytics/EtaDispatchOrder.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <vector>

struct CotizacionPrueba {
  std::size_t courierIndex;
  std::string courierId;
  double etaSimulatedSeconds;
};

#define EXIGIR(x) do { if (!(x)) { \
  std::cerr << "FALLO " #x " linea " << __LINE__ << '\n'; return 1; \
} } while (false)

int main() {
  using equipego::analytics::ordenarCotizacionesPorReduccion;
  std::vector<CotizacionPrueba> vacio;
  ordenarCotizacionesPorReduccion(vacio);
  EXIGIR(vacio.empty());

  std::vector<CotizacionPrueba> v = {
      {8, "c8", 9.0}, {4, "c4", 3.0}, {2, "c2", 3.0}, {9, "c9", 2.0},
      {1, "c1", 5.0}};
  ordenarCotizacionesPorReduccion(v);
  EXIGIR(v.size() == 5);
  EXIGIR(v[0].courierIndex == 9);
  EXIGIR(v[1].courierIndex == 2);
  EXIGIR(v[2].courierIndex == 4);
  EXIGIR(v[3].courierIndex == 1);
  EXIGIR(v[4].courierIndex == 8);

  v = {{8, "c8", 1.0}, {4, "c4", 1.0}, {2, "c2", 1.0}};
  ordenarCotizacionesPorReduccion(v);
  EXIGIR(v[0].courierIndex == 2);
  EXIGIR(v[1].courierIndex == 4);
  EXIGIR(v[2].courierIndex == 8);

  v = {{8, "c8", -1.0}, {9, "c9", std::numeric_limits<double>::infinity()},
       {5, "c5", std::numeric_limits<double>::quiet_NaN()}, {1, "c1", 2.0}};
  ordenarCotizacionesPorReduccion(v);
  EXIGIR(v.size() == 1 && v[0].courierIndex == 1);

  std::mt19937_64 rng(2026);
  for (int n : {1, 2, 10, 200, 10000}) {
    std::vector<CotizacionPrueba> datos;
    datos.reserve(n);
    for (int i = 0; i < n; ++i) {
      datos.push_back({static_cast<std::size_t>(n - i), "c" + std::to_string(i),
                       static_cast<double>(rng() % 100)});
    }
    auto esperado = datos;
    std::sort(esperado.begin(), esperado.end(), [](const auto& a, const auto& b) {
      if (a.etaSimulatedSeconds != b.etaSimulatedSeconds)
        return a.etaSimulatedSeconds < b.etaSimulatedSeconds;
      return a.courierIndex < b.courierIndex;
    });
    ordenarCotizacionesPorReduccion(datos);
    EXIGIR(datos.size() == esperado.size());
    for (std::size_t i = 0; i < datos.size(); ++i) {
      EXIGIR(datos[i].courierIndex == esperado[i].courierIndex);
    }
  }

  std::cout << "TEST ORDEN DEL DISPATCHER POR REDUCTION 08: OK\n";
  return 0;
}
