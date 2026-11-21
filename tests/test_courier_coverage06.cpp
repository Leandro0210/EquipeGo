#include "analytics/CourierCoverage06.hpp"

#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace equipego::stencil06;

// No usar assert: CMake Release define NDEBUG y lo deshabilita.
#define REQUIRE(cond) do { if (!(cond)) { \
  throw std::runtime_error("STENCIL06_TEST_FAIL: " #cond); \
} } while (false)

template <typename Body>
void debeFallar(Body fn) {
  bool fallo = false;
  try { fn(); } catch (const std::invalid_argument&) { fallo = true; }
  REQUIRE(fallo);
}

void probarBordesFuentesYUnBarrido() {
  const auto c = construirCampo({{0.5f, 0.5f}}, 5);
  REQUIRE(contarFuentes(c) == 1);
  const auto i = 2u * 5u + 2u;
  REQUIRE(c.valores[i] == 1.0f && c.fijas[i] == 1);
  const auto r = evolucionar(c, 1);
  REQUIRE(r[i] == 1.0f);
  REQUIRE(r[2 * 5 + 1] == 0.2f);
  REQUIRE(r[2 * 5 + 3] == 0.2f);
  REQUIRE(r[1 * 5 + 2] == 0.2f);
  REQUIRE(r[3 * 5 + 2] == 0.2f);
  REQUIRE(r[1 * 5 + 1] == 0.0f);
  for (std::size_t j = 0; j < 5; ++j) {
    REQUIRE(r[j] == 0.0f && r[20 + j] == 0.0f);
    REQUIRE(r[j * 5] == 0.0f && r[j * 5 + 4] == 0.0f);
  }
  REQUIRE(evolucionar(c, 0) == c.valores);
  const auto later = evolucionar(c, 100);
  REQUIRE(later[i] == 1.0f);
  for (const auto v : later) REQUIRE(std::isfinite(v) && v >= 0.0f && v <= 1.0f);
}

void probarExactitudHilos() {
  std::vector<PuntoNormalizado06> pts;
  for (int i = 0; i < 750; ++i) {
    pts.push_back({static_cast<float>((i * 73) % 997) / 997.0f,
                   static_cast<float>((i * 127) % 991) / 991.0f});
  }
  for (std::size_t side : {3u, 5u, 17u, 32u, 64u}) {
    const auto c = construirCampo(pts, side);
    const auto ref = evolucionar(c, 20, 1);
    for (unsigned int hilos : {1u, 2u, 4u, 8u, 16u}) {
      const auto out = evolucionar(c, 20, hilos);
      REQUIRE(out == ref);
    }
  }
}

void probarValidaciones() {
  debeFallar([] { (void)construirCampo({}, 2); });
  debeFallar([] { (void)construirCampo({{-0.01f, 0.3f}}, 8); });
  debeFallar([] { (void)construirCampo({{std::numeric_limits<float>::quiet_NaN(), 0.5f}}, 8); });
  auto c = construirCampo({{0.5f, 0.5f}}, 8);
  debeFallar([&] { (void)evolucionar(c, 10, 0); });
  debeFallar([&] { (void)evolucionar(c, 10, 1, 0.5f, 0.2f); });
  c.fijas[0] = 0;
  debeFallar([&] { (void)evolucionar(c, 1); });
  c = construirCampo({}, 8);
  c.valores.resize(4);
  debeFallar([&] { (void)evolucionar(c, 1); });
  c = construirCampo({}, 8);
  c.valores[4 * 8 + 3] = std::numeric_limits<float>::infinity();
  debeFallar([&] { (void)evolucionar(c, 1); });
}

int main() {
  probarBordesFuentesYUnBarrido();
  probarExactitudHilos();
  probarValidaciones();
  std::cout << "STENCIL06_CPU_TEST_OK,fuentes_fijas=OK,bordes=OK,"
               "jacobi=OK,hilos=1-16,equivalencia_exacta=OK\n";
}
