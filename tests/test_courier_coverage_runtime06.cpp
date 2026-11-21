#include "analytics/CourierCoverageLive06.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
  using namespace equipego::stencil06;
  // Fuentes fijas, duplicados y averiados; no cuentan una persona dos veces.
  const std::vector<Muestra06> points = {
      {0.25f, 0.25f, false, false},
      {0.25f, 0.25f, false, true},
      {0.75f, 0.75f, false, false},
      {0.50f, 0.50f, true, false}};
  const auto result = proyectarCobertura06(points, 32);
  assert(result.couriersTotales == 4);
  assert(result.couriersUsados == 3);
  assert(result.couriersAveriados == 1);
  assert(result.muestrasMovimiento == 1);
  assert(result.fuentesUnicas == 2);
  const auto field = evolucionar(result.campo, 20, 1);
  assert(field.size() == 32u*32u);
  for (std::size_t i = 0; i < field.size(); ++i) {
    assert(std::isfinite(field[i]));
    assert(field[i] >= 0.0f && field[i] <= 1.0f);
    if (result.campo.fijas[i]) assert(field[i] == result.campo.valores[i]);
  }
  const auto empty = proyectarCobertura06({}, 16);
  assert(empty.couriersTotales == 0 && empty.fuentesUnicas == 0);
  for (const auto x : evolucionar(empty.campo, 20)) assert(x == 0.0f);
  bool bad = false;
  try { (void)proyectarCobertura06({{1.2f,0.1f,false,false}}, 16); }
  catch (const std::invalid_argument&) { bad = true; }
  assert(bad);
  const auto border = proyectarCobertura06({{1.0f,1.0f,false,false}}, 16);
  assert(border.fuentesUnicas == 1);
  std::cout << "STENCIL06_RUNTIME_CPU_TEST_OK,fuentes=2,sweeps=20\n";
}
