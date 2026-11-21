#pragma once

// Feature 05: convolution 2D sobre conteos enteros de pedidos simulados.
// Kernel gaussiano binomial 5x5 = outer([1,4,6,4,1]) / 256.
// Borde: zero padding. Resultados CPU exactos e independientes de hilos.
// No modifica OrderProducer, Dispatcher, la flota ni datos reales.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace equipego::convolution05 {

constexpr std::array<int, 5> kGaussian05{{1, 4, 6, 4, 1}};

struct DemandGrid05 {
  std::size_t width = 0;
  std::size_t height = 0;
  std::vector<std::int32_t> orders;
};

inline void validate(const DemandGrid05& g) {
  if (g.width == 0 || g.height == 0 ||
      g.width > std::numeric_limits<std::size_t>::max() / g.height ||
      g.width * g.height != g.orders.size()) {
    throw std::invalid_argument("Convolution05: dimensiones invalidas");
  }
  for (const auto value : g.orders) {
    if (value < 0 || value > 1000000) {
      throw std::invalid_argument("Convolution05: demanda fuera de rango");
    }
  }
}

inline std::vector<float> gaussianReference05(const DemandGrid05& g) {
  validate(g);
  std::vector<float> output(g.orders.size(), 0.0f);
  for (std::size_t y = 0; y < g.height; ++y) {
    for (std::size_t x = 0; x < g.width; ++x) {
      std::int64_t sum = 0;
      for (int dy = -2; dy <= 2; ++dy) {
        const auto yy = static_cast<long long>(y) + dy;
        if (yy < 0 || yy >= static_cast<long long>(g.height)) continue;
        for (int dx = -2; dx <= 2; ++dx) {
          const auto xx = static_cast<long long>(x) + dx;
          if (xx < 0 || xx >= static_cast<long long>(g.width)) continue;
          sum += static_cast<std::int64_t>(kGaussian05[dy + 2]) *
                 kGaussian05[dx + 2] *
                 g.orders[static_cast<std::size_t>(yy) * g.width +
                          static_cast<std::size_t>(xx)];
        }
      }
      output[y * g.width + x] = static_cast<float>(sum) / 256.0f;
    }
  }
  return output;
}

// Separable: sumatoria horizontal, luego vertical. Ambas pasadas sobre filas
// independientes. Cada hilo escribe celdas disjuntas; no hay data races.
template <class F>
inline void parallelRows05(std::size_t height, unsigned threads, F work) {
  if (threads == 0 || threads > 1024) {
    throw std::invalid_argument("Convolution05: threads debe estar en 1..1024");
  }
  const auto count = std::min<std::size_t>(height, threads);
  if (count == 1) { work(0, height); return; }
  std::vector<std::thread> pool;
  pool.reserve(count - 1);
  try {
    for (std::size_t t = 1; t < count; ++t) {
      pool.emplace_back([&, t] {
        work(height * t / count, height * (t + 1) / count);
      });
    }
  } catch (...) {
    for (auto& thread : pool) thread.join();
    throw;
  }
  work(0, height / count);
  for (auto& thread : pool) thread.join();
}

inline std::vector<float> gaussianSeparable05(const DemandGrid05& g,
                                              unsigned threads = 1) {
  validate(g);
  std::vector<std::int64_t> intermediate(g.orders.size(), 0);
  std::vector<float> output(g.orders.size(), 0.0f);
  parallelRows05(g.height, threads, [&](std::size_t first, std::size_t last) {
    for (std::size_t y = first; y < last; ++y) {
      for (std::size_t x = 0; x < g.width; ++x) {
        std::int64_t sum = 0;
        for (int dx = -2; dx <= 2; ++dx) {
          const auto xx = static_cast<long long>(x) + dx;
          if (xx < 0 || xx >= static_cast<long long>(g.width)) continue;
          sum += static_cast<std::int64_t>(kGaussian05[dx + 2]) *
                 g.orders[y * g.width + static_cast<std::size_t>(xx)];
        }
        intermediate[y * g.width + x] = sum;
      }
    }
  });
  parallelRows05(g.height, threads, [&](std::size_t first, std::size_t last) {
    for (std::size_t y = first; y < last; ++y) {
      for (std::size_t x = 0; x < g.width; ++x) {
        std::int64_t sum = 0;
        for (int dy = -2; dy <= 2; ++dy) {
          const auto yy = static_cast<long long>(y) + dy;
          if (yy < 0 || yy >= static_cast<long long>(g.height)) continue;
          sum += static_cast<std::int64_t>(kGaussian05[dy + 2]) *
                 intermediate[static_cast<std::size_t>(yy) * g.width + x];
        }
        output[y * g.width + x] = static_cast<float>(sum) / 256.0f;
      }
    }
  });
  return output;
}

} // namespace equipego::convolution05
