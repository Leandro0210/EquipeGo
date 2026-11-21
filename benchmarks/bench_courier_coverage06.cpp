#include "analytics/CourierCoverage06.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

using namespace equipego::stencil06;

std::vector<PuntoNormalizado06> leerPuntos(const std::string& nombre) {
  std::ifstream f(nombre);
  if (!f) throw std::runtime_error("STENCIL06: falta CSV de couriers: " + nombre);
  std::string linea;
  if (!std::getline(f, linea)) {
    throw std::runtime_error("STENCIL06: encabezado CSV ausente");
  }
  if (!linea.empty() && linea.back() == '\r') linea.pop_back();
  if (linea != "courier_id,node_id,x_norm,y_norm") {
    throw std::runtime_error("STENCIL06: encabezado CSV invalido");
  }
  std::vector<PuntoNormalizado06> puntos;
  while (std::getline(f, linea)) {
    if (!linea.empty() && linea.back() == '\r') linea.pop_back();
    if (linea.empty()) continue;
    std::stringstream ss(linea);
    std::string id, nodo, x, y, resto;
    if (!std::getline(ss, id, ',') || !std::getline(ss, nodo, ',') ||
        !std::getline(ss, x, ',') || !std::getline(ss, y, ',') ||
        std::getline(ss, resto, ',')) {
      throw std::runtime_error("STENCIL06: fila de puntos invalida");
    }
    if (id.empty() || nodo.empty()) {
      throw std::runtime_error("STENCIL06: identificador vacio");
    }
    const auto px = std::stof(x);
    const auto py = std::stof(y);
    if (!std::isfinite(px) || !std::isfinite(py) ||
        px < 0.0f || px > 1.0f || py < 0.0f || py > 1.0f) {
      throw std::runtime_error("STENCIL06: coordenada invalida");
    }
    puntos.push_back({px, py});
  }
  if (puntos.size() != 5000) {
    throw std::runtime_error("STENCIL06: se esperan exactamente 5000 couriers sinteticos");
  }
  return puntos;
}

int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      std::cerr << "Uso: bench_courier_coverage06 couriers_5000.csv resultado.csv\n";
      return 2;
    }
    const auto puntos = leerPuntos(argv[1]);
    std::ofstream csv(argv[2]);
    if (!csv) throw std::runtime_error("STENCIL06: no se puede guardar resultado");
    csv << "side,mode,threads,rep,sweeps,elapsed_ms,checksum,fixed_cells,equal\n";
    csv << std::fixed << std::setprecision(8);
    constexpr unsigned int reps = 10;
    constexpr unsigned int sweeps = 20;
    std::size_t filas = 0;
    for (std::size_t lado : {32u, 64u, 128u, 256u, 512u}) {
      const auto c = construirCampo(puntos, lado);
      const auto ref = evolucionar(c, sweeps, 1);
      const auto fuentes = contarFuentes(c);
      for (unsigned int rep = 0; rep < reps; ++rep) {
        for (unsigned int variant = 0; variant < 6; ++variant) {
          const bool secuencial = variant == 0;
          const unsigned int hilos = secuencial ? 1u : (1u << (variant - 1));
          const auto before = std::chrono::steady_clock::now();
          const auto actual = evolucionar(c, sweeps, hilos);
          const auto after = std::chrono::steady_clock::now();
          const bool igual = actual == ref;
          if (!igual) throw std::runtime_error("STENCIL06: resultado CPU diferente");
          const double ms =
              std::chrono::duration<double, std::milli>(after - before).count();
          csv << lado << ',' << (secuencial ? "sequential" : "parallel")
              << ',' << hilos << ',' << rep << ',' << sweeps << ',' << ms
              << ',' << suma(actual) << ',' << fuentes << ",1\n";
          ++filas;
        }
      }
      std::cout << "STENCIL06_BENCH_CPU_OK,side=" << lado
                << ",reps=10,sweeps=20,fuentes=" << fuentes << '\n';
    }
    csv.flush();
    if (!csv) throw std::runtime_error("STENCIL06: escritura incompleta");
    std::cout << "STENCIL06_CPU_BENCH_COMPLETO,filas=" << filas
              << ",comparacion=OK\n";
    return filas == 300 ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "STENCIL06_BENCH_ERROR: " << e.what() << '\n';
    return 1;
  }
}
