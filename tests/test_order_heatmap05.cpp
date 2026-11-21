#include "analytics/OrderHeatmap05.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

using namespace equipego::convolution05;

static void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

static DemandGrid05 makeGrid(std::size_t width, std::size_t height) {
  return {width, height, std::vector<std::int32_t>(width * height, 0)};
}

static void same(const DemandGrid05& g) {
  const auto reference = gaussianReference05(g);
  for (unsigned t : {1u, 2u, 4u, 8u, 16u}) {
    require(gaussianSeparable05(g, t) == reference,
            "Convolution05: divergencia CPU threads=" + std::to_string(t));
  }
}

static bool throws(const DemandGrid05& g) {
  try { (void)gaussianSeparable05(g); } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

int main() {
  try {
    { auto g = makeGrid(1, 1); g.orders[0] = 256; same(g);
      require(gaussianReference05(g).at(0) == 36.0f, "n=1 incorrecto"); }
    { auto g = makeGrid(9, 9); g.orders[4 * 9 + 4] = 256; same(g);
      const auto o = gaussianReference05(g);
      require(o[4 * 9 + 4] == 36.0f, "centro incorrecto");
      require(std::accumulate(o.begin(), o.end(), 0.0f) == 256.0f,
              "masa interior incorrecta"); }
    { auto g = makeGrid(9, 9); g.orders[0] = 256; same(g);
      const auto o = gaussianReference05(g);
      require(o[0] == 36.0f, "esquina incorrecta");
      require(std::accumulate(o.begin(), o.end(), 0.0f) < 256.0f,
              "zero padding incorrecto"); }
    { auto g = makeGrid(10, 7); std::fill(g.orders.begin(), g.orders.end(), 37);
      same(g); require(gaussianReference05(g)[3 * 10 + 5] == 37.0f,
                      "constante interior incorrecta"); }
    for (const auto side : {2u, 3u, 4u, 17u, 31u, 64u}) {
      auto g = makeGrid(side, side + 3);
      for (std::size_t i = 0; i < g.orders.size(); ++i)
        g.orders[i] = static_cast<std::int32_t>((i * 29 + 5) % 101);
      same(g);
    }
    { auto g = makeGrid(5, 8); g.orders[0] = -1;
      require(throws(g), "negativos deben rechazarse"); }
    { auto g = makeGrid(5, 8); g.orders[0] = 1000001;
      require(throws(g), "conteo demasiado grande debe rechazarse"); }
    require(throws(DemandGrid05{0, 1, {}}), "ancho cero debe rechazarse");
    require(throws(DemandGrid05{2, 2, {1, 2, 3}}), "forma irregular debe rechazarse");
    { auto g = makeGrid(3, 3); bool caught = false;
      try { (void)gaussianSeparable05(g, 0); }
      catch (const std::invalid_argument&) { caught = true; }
      require(caught, "threads cero debe rechazarse"); }
    std::cout << "CONVOLUTION05_CPU_OK,kernel=5x5,zero_padding=1,threads=1/2/4/8/16\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "CONVOLUTION05_TEST_ERROR: " << e.what() << '\n';
    return 1;
  }
}
