#include "map/RoadNetwork.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void comprobar(bool condicion, const std::string& msg) {
  if (!condicion) throw std::runtime_error(msg);
}

SimulationConfig dirigir() {
  SimulationConfig cfg;
  for (int i = 0; i < 5; ++i) cfg.nodes.push_back(Node{"n" + std::to_string(i)});
  cfg.streets.push_back(Street{"s0", "n0", "n1", true, 2.0});
  cfg.streets.push_back(Street{"s1", "n1", "n2", true, 3.0});
  cfg.streets.push_back(Street{"s2", "n0", "n2", true, 10.0});
  cfg.streets.push_back(Street{"s3", "n2", "n3", false, 4.0});
  return cfg;  // n4 aislado
}

void todosPares(RoadNetwork& red, const SimulationConfig& cfg) {
  const auto n = cfg.nodes.size();
  std::vector<std::optional<double>> referencia;
  referencia.reserve(n * n);
  for (const auto& a : cfg.nodes) {
    for (const auto& b : cfg.nodes) {
      const auto ruta = red.shortestRoute(a.id, b.id);
      referencia.push_back(ruta ? std::optional<double>(ruta->distanceMeters)
                                : std::nullopt);
    }
  }
  comprobar(!red.floyd03Habilitado(), "GPU activada antes de precalculo");
  auto stats = red.habilitarFloyd03Gpu(cfg);
  comprobar(red.floyd03Habilitado(), "GPU no publico cache");
  comprobar(stats.nodos == n, "dimension incorrecta");
  comprobar(stats.paresVerificados == n * n, "faltan verificaciones Dijkstra");
  comprobar(stats.bytesMatriz == n * n * sizeof(double), "bytes incorrectos");
  comprobar(stats.kernelMs >= 0.0 && stats.totalMs >= 0.0, "tiempos negativos");
  std::size_t pos = 0;
  for (const auto& a : cfg.nodes) {
    for (const auto& b : cfg.nodes) {
      const auto d = red.shortestDistanceMeters(a.id, b.id);
      const auto ruta = red.shortestRoute(a.id, b.id);
      comprobar(d.has_value() == referencia[pos].has_value(), "alcance alterado");
      comprobar(ruta.has_value() == referencia[pos].has_value(), "Dijkstra alterado");
      if (d) {
        const double tol = 1e-9 * std::max(1.0, std::abs(*referencia[pos]));
        comprobar(std::abs(*d - *referencia[pos]) <= tol, "distancia alterada");
        comprobar(std::abs(ruta->distanceMeters - *referencia[pos]) <= tol,
                  "ruta geometrica alterada");
      }
      ++pos;
    }
  }
  comprobar(!red.shortestDistanceMeters("desconocido", cfg.nodes[0].id),
            "origen desconocido permitido");
  comprobar(!red.shortestDistanceMeters(cfg.nodes[0].id, "desconocido"),
            "destino desconocido permitido");
  bool rechazo = false;
  try { red.habilitarFloyd03Gpu(cfg); }
  catch (const std::logic_error&) { rechazo = true; }
  comprobar(rechazo, "permite doble inicializacion");
  std::cout << "FLOYD03_GPU_CACHE_INTEGRACION_OK,n=" << n
            << ",pares=" << stats.paresVerificados
            << ",kernel_ms=" << stats.kernelMs
            << ",total_ms=" << stats.totalMs << '\n';
}
}

int main(int argc, char** argv) {
  try {
    auto cfg = dirigir();
    RoadNetwork red(cfg);
    // Falla previa a CUDA: debe quedar en modo Dijkstra.
    bool fallo = false;
    try { red.habilitarFloyd03Gpu(cfg, 2); }
    catch (const std::invalid_argument&) { fallo = true; }
    comprobar(fallo && !red.floyd03Habilitado(), "fallo no mantiene Dijkstra");
    comprobar(red.shortestDistanceMeters("n0", "n3") == 9.0,
              "fallback no usa Dijkstra");
    todosPares(red, cfg);
    comprobar(red.shortestDistanceMeters("n0", "n3") == 9.0,
              "longitud dirigida equivocada");
    comprobar(!red.shortestDistanceMeters("n3", "n0"), "calle one-way ignorada");
    comprobar(!red.shortestDistanceMeters("n4", "n0"), "nodo aislado aceptado");

    comprobar(argc == 2, "Indicar config/equipetrol.json");
    SimulationConfig mapa;
    std::string programa = "test_floyd_cache_gpu_integracion03";
    std::string ruta = argv[1];
    char* args[] = {programa.data(), ruta.data()};
    comprobar(loadAndValidateConfig(2, args, mapa), "error al cargar mapa real");
    RoadNetwork otra(mapa);
    todosPares(otra, mapa);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FLOYD03_GPU_CACHE_INTEGRACION_ERROR: " << e.what() << '\n';
    return 1;
  }
}
