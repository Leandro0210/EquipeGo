#include "map/FloydWarshallCuda03.hpp"
#include "map/FloydWarshallTiled03.hpp"
#include "map/RoadNetwork.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

SimulationConfig fixture(std::size_t n) {
  SimulationConfig cfg;
  for (std::size_t i = 0; i < n; ++i)
    cfg.nodes.push_back(Node{"n" + std::to_string(i)});
  std::size_t seq = 0;
  const auto add = [&](std::size_t a, std::size_t b, double meters,
                       bool oneWay = true) {
    cfg.streets.push_back(Street{"f" + std::to_string(seq++),
        cfg.nodes[a].id, cfg.nodes[b].id, oneWay, meters});
  };
  // n-2 nodos conectados por direcciones y atajos. Dos nodos aislados.
  for (std::size_t i = 0; i + 3 < n; ++i) {
    add(i, i + 1, 1.0 + (i % 9), i % 5 != 0);
    if (i + 4 < n - 2 && i % 3 == 0) add(i, i + 4, 2.5 + (i % 7));
    if (i + 2 < n - 2 && i % 4 == 0) add(i, i + 2, 6.25);
  }
  return cfg;
}

void compare(const SimulationConfig& cfg, bool againstDijkstra = false) {
  const std::size_t n = cfg.nodes.size();
  auto initial = equipego::analytics::construirMatrizRutas03(cfg, 2048);
  const auto cpu = equipego::analytics::floydWarshallReferenciaCpu03(cfg, 2048);
  const auto gpu = equipego::analytics::floydWarshallCuda03(initial.metros, n, 2048);
  require(gpu.metros.size() == n*n, "Dimension GPU incorrecta");
  std::size_t unreachable = 0;
  for (std::size_t i=0;i<n*n;++i) {
    const double c = cpu.metros[i];
    const double g = gpu.metros[i];
    require(std::isfinite(c) == std::isfinite(g), "Alcanzabilidad CPU/GPU distinta: " + std::to_string(i));
    if (!std::isfinite(c)) { ++unreachable; continue; }
    const double tol = 1e-9 * std::max(1.0, std::abs(c));
    require(std::abs(c-g) <= tol, "Metros CPU/GPU distintos: " + std::to_string(i));
  }
  if (againstDijkstra) {
    RoadNetwork network(cfg);
    for (std::size_t i=0;i<n;++i) {
      for (std::size_t j=0;j<n;++j) {
        const auto route = network.shortestRoute(cfg.nodes[i].id, cfg.nodes[j].id);
        const double d = gpu.metros[i*n+j];
        require(route.has_value() == std::isfinite(d), "GPU vs Dijkstra alcanzabilidad");
        if (route) {
          const double tol = 1e-9*std::max(1.0, std::abs(route->distanceMeters));
          require(std::abs(d-route->distanceMeters) <= tol, "GPU vs Dijkstra distancia");
        }
      }
    }
  }
  std::cout << "FLOYD03_GPU_OK,n=" << n << ",pairs=" << n*n
            << ",unreachable=" << unreachable
            << ",h2d_ms=" << gpu.h2dMs
            << ",kernels_ms=" << gpu.kernelMs
            << ",d2h_ms=" << gpu.d2hMs
            << ",host_total_ms=" << gpu.totalHostMs << '\n';
}

void invalidInputs() {
  bool rejected = false;
  try { equipego::analytics::floydWarshallCuda03({}, 0); }
  catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "GPU03: no rechazo n=0");
  rejected = false;
  try { equipego::analytics::floydWarshallCuda03({0.0}, 2); }
  catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "GPU03: no rechazo matriz incorrecta");
  rejected = false;
  try { equipego::analytics::floydWarshallCuda03({-1.0}, 1); }
  catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "GPU03: no rechazo negativo");
}

void realEquipetrol(const std::string& path) {
  SimulationConfig config;
  std::string program="test_floyd_warshall_gpu03", file=path;
  char* args[] = {program.data(), file.data()};
  require(loadAndValidateConfig(2, args, config), "No se pudo cargar Equipetrol");
  compare(config, true);
}
}

int main(int argc, char** argv) {
  try {
    invalidInputs();
    for (const std::size_t n : {std::size_t(1), std::size_t(5), std::size_t(16),
                                std::size_t(17), std::size_t(30), std::size_t(31),
                                std::size_t(64), std::size_t(95), std::size_t(129),
                                std::size_t(257)}) {
      compare(fixture(n), n <= 31);
    }
    if (argc == 2) realEquipetrol(argv[1]);
    else require(argc == 1, "Uso: [config/equipetrol.json]");
    std::cout << "FLOYD03_GPU_ETAPA4_VALIDADA\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FLOYD03_GPU_FAIL: " << e.what() << '\n';
    return 1;
  }
}
