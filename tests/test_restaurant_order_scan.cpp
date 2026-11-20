#include "analytics/RestaurantOrderScan.hpp"
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

#define EXIGIR(expr) do { if (!(expr)) { \
  std::cerr << "FALLO: " #expr " linea " << __LINE__ << '\n'; return 1; \
} } while(false)

using equipego::analytics::AgrupacionPedidosRestaurante;
using equipego::analytics::agruparPedidosReferencia09;
using equipego::analytics::agruparPedidosScanCPU09;
using equipego::analytics::agruparPedidosScanParaleloCPU09;

bool correcta(const std::vector<std::size_t>& ids, std::size_t restaurantes) {
  const auto ref = agruparPedidosReferencia09(ids, restaurantes);
  auto check = [&](const AgrupacionPedidosRestaurante& a) {
    if (a.starts != ref.starts || a.indices != ref.indices) return false;
    if (a.indices.size() != ids.size() || a.starts.size() != restaurantes + 1) return false;
    if (a.starts.back() != ids.size()) return false;
    for (std::size_t r = 0; r < restaurantes; ++r) {
      for (auto pos = a.starts[r]; pos < a.starts[r + 1]; ++pos) {
        if (ids[a.indices[pos]] != r) return false;
        if (pos > a.starts[r] && a.indices[pos - 1] >= a.indices[pos]) return false;
      }
    }
    return true;
  };
  if (!check(agruparPedidosScanCPU09(ids, restaurantes))) return false;
  for (const auto h : std::vector<std::size_t>{1, 2, 4, 8}) {
    if (!check(agruparPedidosScanParaleloCPU09(ids, restaurantes, h))) return false;
  }
  return true;
}

int main() {
  EXIGIR(correcta({}, 0));
  EXIGIR(correcta({}, 5));
  EXIGIR(correcta({0}, 1));
  EXIGIR(correcta({2,0,1,2,0,2,1,0}, 3));
  EXIGIR(correcta({0,0,0,0}, 4));
  EXIGIR(correcta({3,3,3,3,3,3}, 5));
  EXIGIR(correcta({4,2,1,0,3,2,4,0,3,2}, 6));
  EXIGIR(correcta({6,1,1,6,3}, 8));
  std::mt19937_64 rng(20261119);
  for (auto n : std::vector<std::size_t>{0, 1, 2, 5, 255, 256, 257, 1000, 10001}) {
    for (auto restaurantes : std::vector<std::size_t>{1, 2, 6, 37}) {
      std::vector<std::size_t> x(n);
      for (auto& id : x) id = rng() % restaurantes;
      EXIGIR(correcta(x, restaurantes));
    }
  }
  bool invalid = false;
  try { (void)agruparPedidosScanCPU09({0,3}, 3); }
  catch (const std::invalid_argument&) { invalid = true; }
  EXIGIR(invalid);
  invalid = false;
  try { (void)agruparPedidosScanParaleloCPU09({0}, 1, 0); }
  catch (const std::invalid_argument&) { invalid = true; }
  EXIGIR(invalid);
  std::cout << "TEST SCAN 09 CPU: OK\n";
  return 0;
}
