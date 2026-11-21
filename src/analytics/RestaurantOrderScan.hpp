#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

namespace equipego::analytics {

// CSR-like stable grouping of orders by restaurant.
// starts[r] ... starts[r+1] are original order indices of restaurant r.
struct AgrupacionPedidosRestaurante {
  std::vector<std::size_t> starts;   // size == restaurantCount + 1
  std::vector<std::size_t> indices;  // original positions, stable per restaurant
};

inline void validarEntradasScan09(const std::vector<std::size_t>& restaurantIds,
                                  std::size_t restaurantCount) {
  if (restaurantCount == std::numeric_limits<std::size_t>::max()) {
    throw std::invalid_argument("Demasiados restaurantes para prefijos");
  }
  for (std::size_t id : restaurantIds) {
    if (id >= restaurantCount) {
      throw std::invalid_argument("Indice de restaurante fuera de rango");
    }
  }
}

// Reference: stable_sort by restaurant ID, preserving input arrival order.
inline AgrupacionPedidosRestaurante agruparPedidosReferencia09(
    const std::vector<std::size_t>& restaurantIds,
    std::size_t restaurantCount) {
  validarEntradasScan09(restaurantIds, restaurantCount);
  AgrupacionPedidosRestaurante result;
  result.starts.assign(restaurantCount + 1, 0);
  result.indices.resize(restaurantIds.size());
  std::iota(result.indices.begin(), result.indices.end(), std::size_t{0});
  std::stable_sort(result.indices.begin(), result.indices.end(),
      [&restaurantIds](std::size_t a, std::size_t b) {
        return restaurantIds[a] < restaurantIds[b];
      });
  for (std::size_t id : restaurantIds) ++result.starts[id + 1];
  for (std::size_t r = 0; r < restaurantCount; ++r) {
    result.starts[r + 1] += result.starts[r];
  }
  return result;
}

// CPU: histogram counts -> exclusive scan -> stable scatter of original indices.
inline AgrupacionPedidosRestaurante agruparPedidosScanCPU09(
    const std::vector<std::size_t>& restaurantIds,
    std::size_t restaurantCount) {
  validarEntradasScan09(restaurantIds, restaurantCount);
  AgrupacionPedidosRestaurante result;
  result.starts.assign(restaurantCount + 1, 0);
  result.indices.resize(restaurantIds.size());
  for (std::size_t r : restaurantIds) ++result.starts[r + 1];
  for (std::size_t r = 0; r < restaurantCount; ++r) {
    result.starts[r + 1] += result.starts[r];
  }
  std::vector<std::size_t> cursor(result.starts.begin(), result.starts.end() - 1);
  for (std::size_t i = 0; i < restaurantIds.size(); ++i) {
    result.indices[cursor[restaurantIds[i]]++] = i;
  }
  return result;
}

// Multi-thread CPU, stable by construction:
// each thread owns a contiguous arrival chunk, counts its restaurants;
// each restaurant's chunks receive disjoint ordered ranges after the scan.
inline AgrupacionPedidosRestaurante agruparPedidosScanParaleloCPU09(
    const std::vector<std::size_t>& restaurantIds,
    std::size_t restaurantCount, std::size_t threadsRequested) {
  if (threadsRequested == 0) throw std::invalid_argument("hilos debe ser > 0");
  validarEntradasScan09(restaurantIds, restaurantCount);
  const auto n = restaurantIds.size();
  if (n == 0 || restaurantCount == 0) {
    return agruparPedidosScanCPU09(restaurantIds, restaurantCount);
  }
  const std::size_t workers = std::min(n, threadsRequested);
  if (workers == 1) return agruparPedidosScanCPU09(restaurantIds, restaurantCount);

  // Hard cap on temporary 2-D table to avoid accidental huge allocations.
  if (restaurantCount > std::numeric_limits<std::size_t>::max() / workers ||
      restaurantCount * workers > 10000000ULL) {
    return agruparPedidosScanCPU09(restaurantIds, restaurantCount);
  }

  std::vector<std::vector<std::size_t>> counts(
      workers, std::vector<std::size_t>(restaurantCount, 0));
  std::vector<std::thread> pool;
  pool.reserve(workers);
  for (std::size_t t = 0; t < workers; ++t) {
    pool.emplace_back([&, t] {
      const auto begin = (n / workers) * t + (n % workers) * t / workers;
      const auto end = (n / workers) * (t + 1) + (n % workers) * (t + 1) / workers;
      for (auto i = begin; i < end; ++i) ++counts[t][restaurantIds[i]];
    });
  }
  for (auto& worker : pool) worker.join();

  AgrupacionPedidosRestaurante result;
  result.starts.assign(restaurantCount + 1, 0);
  result.indices.resize(n);
  for (std::size_t r = 0; r < restaurantCount; ++r) {
    for (std::size_t t = 0; t < workers; ++t) {
      result.starts[r + 1] += counts[t][r];
    }
  }
  for (std::size_t r = 0; r < restaurantCount; ++r) {
    result.starts[r + 1] += result.starts[r];
  }
  // Each worker gets a distinct cursor for every restaurant.
  std::vector<std::vector<std::size_t>> cursors(
      workers, std::vector<std::size_t>(restaurantCount, 0));
  for (std::size_t r = 0; r < restaurantCount; ++r) {
    std::size_t offset = result.starts[r];
    for (std::size_t t = 0; t < workers; ++t) {
      cursors[t][r] = offset;
      offset += counts[t][r];
    }
  }
  pool.clear();
  for (std::size_t t = 0; t < workers; ++t) {
    pool.emplace_back([&, t] {
      const auto begin = (n / workers) * t + (n % workers) * t / workers;
      const auto end = (n / workers) * (t + 1) + (n % workers) * (t + 1) / workers;
      for (auto i = begin; i < end; ++i) {
        result.indices[cursors[t][restaurantIds[i]]++] = i;
      }
    });
  }
  for (auto& worker : pool) worker.join();
  return result;
}

}  // namespace equipego::analytics
