#include "map/FloydWarshallTiled03.hpp"
#include "map/RoadNetwork.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void exigir(bool condicion, const std::string& mensaje) {
  if (!condicion) throw std::runtime_error(mensaje);
}

template <class F>
void exigirExcepcion(F&& accion, const std::string& caso) {
  bool fallo = false;
  try {
    accion();
  } catch (const std::invalid_argument&) {
    fallo = true;
  }
  exigir(fallo, "Faltaba invalid_argument: " + caso);
}

SimulationConfig configurarGrafo(std::size_t n) {
  SimulationConfig c;
  for (std::size_t i = 0; i < n; ++i) {
    c.nodes.push_back(Node{"n" + std::to_string(i)});
  }
  return c;
}

void agregar(SimulationConfig& cfg, std::size_t desde, std::size_t hasta,
             double metros, bool unidireccional = true) {
  cfg.streets.push_back(Street{
      "s" + std::to_string(cfg.streets.size()),
      "n" + std::to_string(desde), "n" + std::to_string(hasta),
      unidireccional, metros});
}

void compararConDijkstra(const SimulationConfig& cfg,
                         std::size_t tile = 16) {
  const auto ref = equipego::analytics::floydWarshallReferenciaCpu03(cfg);
  const auto blo = equipego::analytics::floydWarshallBloquesCpu03(cfg, tile);
  RoadNetwork red(cfg);
  const std::size_t n = cfg.nodes.size();
  exigir(ref.bytesMatriz() == n * n * sizeof(double), "Dimension referencia");
  exigir(blo.bytesMatriz() == n * n * sizeof(double), "Dimension tiled");
  for (const auto& a : cfg.nodes) {
    for (const auto& b : cfg.nodes) {
      const auto dijkstra = red.shortestRoute(a.id, b.id);
      const auto aRef = ref.distanciaMetros(a.id, b.id);
      const auto aBlo = blo.distanciaMetros(a.id, b.id);
      exigir(dijkstra.has_value() == aRef.has_value(),
             "Referencia/Dijkstra: alcanzabilidad " + a.id + "->" + b.id);
      exigir(dijkstra.has_value() == aBlo.has_value(),
             "Tiled/Dijkstra: alcanzabilidad " + a.id + "->" + b.id);
      if (dijkstra) {
        const double esperado = dijkstra->distanceMeters;
        const double tol = 1e-9 * std::max(1.0, std::abs(esperado));
        exigir(std::abs(*aRef - esperado) <= tol,
               "Referencia distinta de Dijkstra: " + a.id + "->" + b.id);
        exigir(std::abs(*aBlo - esperado) <= tol,
               "Tiled distinto de Dijkstra: " + a.id + "->" + b.id);
      }
    }
  }
  exigir(!blo.distanciaMetros("nodo_inexistente", cfg.nodes[0].id),
         "No rechaza nodo desconocido");
}

void casosDirigidosYAislados() {
  auto cfg = configurarGrafo(5);
  agregar(cfg, 0, 1, 2.0);
  agregar(cfg, 0, 1, 7.0);  // Paralela peor: debe conservar el minimo.
  agregar(cfg, 1, 2, 3.0);
  agregar(cfg, 0, 2, 10.0);
  agregar(cfg, 2, 3, 4.0, false);  // Bidireccional.
  // n4 aislado; n2 no puede volver a n0.
  for (std::size_t tile : std::vector<std::size_t>{1, 2, 3, 4, 8, 16, 32}) {
    compararConDijkstra(cfg, tile);
  }
  const auto tabla = equipego::analytics::floydWarshallBloquesCpu03(cfg, 2);
  exigir(tabla.distanciaMetros("n0", "n2") == 5.0,
         "No usa camino indirecto por calles");
  exigir(tabla.distanciaMetros("n0", "n3") == 9.0,
         "No compone la distancia por segmentos");
  exigir(!tabla.distanciaMetros("n2", "n0"), "Infringe una calle unidireccional");
  exigir(!tabla.distanciaMetros("n4", "n0"), "Nodo aislado alcanzable");
  exigir(tabla.distanciaMetros("n4", "n4") == 0.0,
         "Diagonal no vale cero");
}

void grafoMasGrandeConBordes() {
  auto cfg = configurarGrafo(35);  // No multiplo de 8/16/32.
  for (std::size_t i = 0; i + 1 < 33; ++i) {
    agregar(cfg, i, i + 1, 1.0 + (i % 7), i % 3 != 0);
  }
  for (std::size_t i = 0; i + 5 < 33; i += 3) {
    agregar(cfg, i, i + 5, 7.0 + (i % 5), true);
  }
  // n33/n34 sin calles: verificar +inf y bordes.
  for (std::size_t tile : std::vector<std::size_t>{7, 8, 16, 32}) {
    compararConDijkstra(cfg, tile);
  }
}

void rechazarEntradasInvalidas() {
  const auto cfg = configurarGrafo(2);
  exigirExcepcion([&] { (void)equipego::analytics::construirMatrizRutas03(cfg, 0); },
                  "maxNodos=0");
  exigirExcepcion([&] { (void)equipego::analytics::construirMatrizRutas03(cfg, 1); },
                  "demasiados nodos");
  exigirExcepcion([&] { (void)equipego::analytics::floydWarshallBloquesCpu03(cfg, 0); },
                  "tile=0");
  exigirExcepcion([&] { (void)equipego::analytics::floydWarshallBloquesCpu03(cfg, 33); },
                  "tile>32");
  auto mal = cfg;
  mal.nodes[1].id = mal.nodes[0].id;
  exigirExcepcion([&] { (void)equipego::analytics::construirMatrizRutas03(mal); },
                  "nodo duplicado");
  mal = cfg;
  mal.streets.push_back(Street{"s", "n0", "desconocido", true, 2.0});
  exigirExcepcion([&] { (void)equipego::analytics::construirMatrizRutas03(mal); },
                  "extremo inexistente");
  mal = cfg;
  agregar(mal, 0, 1, -2.0);
  exigirExcepcion([&] { (void)equipego::analytics::construirMatrizRutas03(mal); },
                  "metros negativos");
  mal = cfg;
  agregar(mal, 0, 1, std::numeric_limits<double>::quiet_NaN());
  exigirExcepcion([&] { (void)equipego::analytics::construirMatrizRutas03(mal); },
                  "NaN");
}

void cargarGrafoEquipetrol(int argc, char* argv[]) {
  exigir(argc == 2, "Indicar config/equipetrol.json");
  SimulationConfig config;
  std::string nombre = "test_floyd_warshall03";
  std::string ruta = argv[1];
  char* argumentos[] = {nombre.data(), ruta.data()};
  exigir(loadAndValidateConfig(2, argumentos, config),
         "No se pudo cargar la configuracion real de Equipetrol");
  const std::size_t n = config.nodes.size();
  exigir(n > 0, "Grafo de Equipetrol vacio");
  const auto inicio = std::chrono::steady_clock::now();
  const auto referencia = equipego::analytics::floydWarshallReferenciaCpu03(config);
  const auto medio = std::chrono::steady_clock::now();
  const auto tiled = equipego::analytics::floydWarshallBloquesCpu03(config, 16);
  const auto fin = std::chrono::steady_clock::now();
  RoadNetwork dijkstra(config);
  std::size_t pares = 0;
  std::size_t inaccesibles = 0;
  for (const auto& origen : config.nodes) {
    for (const auto& destino : config.nodes) {
      const auto esperado = dijkstra.shortestRoute(origen.id, destino.id);
      const auto dRef = referencia.distanciaMetros(origen.id, destino.id);
      const auto dTiled = tiled.distanciaMetros(origen.id, destino.id);
      exigir(esperado.has_value() == dRef.has_value() &&
             esperado.has_value() == dTiled.has_value(),
             "Direccion o alcance no coincide con Dijkstra en Equipetrol");
      if (!esperado) {
        ++inaccesibles;
      } else {
        const double tol = 1e-9 * std::max(1.0, esperado->distanceMeters);
        exigir(std::abs(*dRef - esperado->distanceMeters) <= tol,
               "Referencia distinta de Dijkstra en Equipetrol");
        exigir(std::abs(*dTiled - esperado->distanceMeters) <= tol,
               "Tiled distinto de Dijkstra en Equipetrol");
      }
      ++pares;
    }
  }
  const auto ms = [](auto a, auto b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  std::cout << "FLOYD03_CPU_OK,nodos=" << n << ",pares=" << pares
            << ",inalcanzables=" << inaccesibles
            << ",matriz_bytes=" << tiled.bytesMatriz()
            << ",referencia_ms=" << ms(inicio, medio)
            << ",tiled_ms=" << ms(medio, fin)
            << ",tile=16\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    casosDirigidosYAislados();
    grafoMasGrandeConBordes();
    rechazarEntradasInvalidas();
    cargarGrafoEquipetrol(argc, argv);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FLOYD03_CPU_FAIL: " << e.what() << '\n';
    return 1;
  }
}
