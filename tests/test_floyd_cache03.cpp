#include "map/RoadNetwork.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void exigir(bool ok, const std::string& motivo) {
  if (!ok) throw std::runtime_error(motivo);
}

SimulationConfig grafoDirigido() {
  SimulationConfig cfg;
  for (int i = 0; i < 5; ++i) {
    cfg.nodes.push_back(Node{"n" + std::to_string(i)});
  }
  cfg.streets.push_back(Street{"s0", "n0", "n1", true, 2.0});
  cfg.streets.push_back(Street{"s1", "n1", "n2", true, 3.0});
  cfg.streets.push_back(Street{"s2", "n0", "n2", true, 10.0});
  cfg.streets.push_back(Street{"s3", "n2", "n3", false, 4.0});
  // n4 aislado. n3 no puede llegar a n0 (calles de un sentido).
  return cfg;
}

void compararTodosLosPares(RoadNetwork& red, const SimulationConfig& cfg) {
  exigir(!red.floyd03Habilitado(), "La tabla esta activa antes de habilitar");
  const auto n = cfg.nodes.size();
  std::vector<std::optional<double>> antes;
  antes.reserve(n*n);
  for (const auto& a : cfg.nodes) {
    for (const auto& b : cfg.nodes) {
      const auto ruta = red.shortestRoute(a.id, b.id);
      const auto dist = red.shortestDistanceMeters(a.id, b.id);
      exigir(ruta.has_value() == dist.has_value(), "Fallback Dijkstra: alcance");
      if (ruta) exigir(*dist == ruta->distanceMeters, "Fallback Dijkstra: metros");
      antes.push_back(dist);
    }
  }

  red.habilitarFloyd03Cpu(cfg, 16);
  exigir(red.floyd03Habilitado(), "No se habilito la tabla");
  exigir(red.floyd03Bytes() == n*n*sizeof(double), "Bytes de matriz incorrectos");
  std::size_t index = 0;
  for (const auto& a : cfg.nodes) {
    for (const auto& b : cfg.nodes) {
      const auto ruta = red.shortestRoute(a.id, b.id);
      const auto dist = red.shortestDistanceMeters(a.id, b.id);
      exigir(dist.has_value() == antes[index].has_value(), "Cache: alcance distinto");
      exigir(dist.has_value() == ruta.has_value(), "Cache: Dijkstra distinto");
      if (dist) {
        const double tol = 1e-9 * std::max(1.0, std::abs(*antes[index]));
        exigir(std::abs(*dist - *antes[index]) <= tol,
               "Cache: distancia distinta de Dijkstra: " + a.id + "->" + b.id);
        exigir(std::abs(*dist - ruta->distanceMeters) <= tol,
               "Route geometrica alterada");
      }
      ++index;
    }
  }
  exigir(!red.shortestDistanceMeters("nodo_desconocido", cfg.nodes.front().id),
         "Nodo desconocido aceptado");
  exigir(!red.shortestDistanceMeters(cfg.nodes.front().id, "nodo_desconocido"),
         "Destino desconocido aceptado");

  bool duplicado = false;
  try { red.habilitarFloyd03Cpu(cfg, 16); }
  catch (const std::logic_error&) { duplicado = true; }
  exigir(duplicado, "Permitio habilitar dos veces una tabla mutable");
}

void probarGrafoDirigido() {
  auto cfg = grafoDirigido();
  RoadNetwork red(cfg);
  compararTodosLosPares(red, cfg);
  exigir(red.shortestDistanceMeters("n0", "n3") == 9.0,
         "No compone la ruta n0-n3");
  exigir(!red.shortestDistanceMeters("n3", "n0"),
         "Ignoro calle unidireccional");
  exigir(!red.shortestDistanceMeters("n4", "n0"),
         "Acepto nodo aislado");
  exigir(red.shortestDistanceMeters("n4", "n4") == 0.0,
         "Diagonal distinta de cero");
  const auto ruta = red.shortestRoute("n0", "n3");
  exigir(ruta && ruta->nodes == std::vector<std::string>({"n0", "n1", "n2", "n3"}),
         "La cache cambio el trazado de Dijkstra");
}

void probarEquipetrol(const std::string& rutaArchivo) {
  SimulationConfig cfg;
  std::string programa = "test_floyd_cache03";
  std::string ruta = rutaArchivo;
  char* argumentos[] = {programa.data(), ruta.data()};
  exigir(loadAndValidateConfig(2, argumentos, cfg), "No se cargo Equipetrol");
  RoadNetwork red(cfg);
  compararTodosLosPares(red, cfg);
  std::cout << "FLOYD03_CACHE_CPU_OK,nodos=" << cfg.nodes.size()
            << ",pares=" << cfg.nodes.size()*cfg.nodes.size()
            << ",matriz_bytes=" << red.floyd03Bytes()
            << ",dijkstra_original_intacto=1\n";
}
} // namespace

int main(int argc, char** argv) {
  try {
    exigir(argc == 2, "Indicar config/equipetrol.json");
    probarGrafoDirigido();
    probarEquipetrol(argv[1]);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FLOYD03_CACHE_CPU_FAIL: " << error.what() << '\n';
    return 1;
  }
}
