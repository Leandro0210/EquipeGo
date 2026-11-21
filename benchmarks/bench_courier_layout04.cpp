#include "analytics/CourierLayout04.hpp"

#include <chrono>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace equipego::performance04;

std::vector<CourierAoS04> leer(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("No se puede abrir entrada: " + path);
  std::string line;
  if (!std::getline(f, line) || line !=
      "id,xMeters,yMeters,speedMetersSecond,reserved,capacity,state") {
    throw std::runtime_error("CSV encabezado inesperado");
  }
  std::vector<CourierAoS04> out;
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    std::stringstream ss(line);
    std::string col;
    std::vector<std::string> fields;
    while (std::getline(ss, col, ',')) fields.push_back(col);
    if (fields.size() != 7) throw std::runtime_error("Fila CSV invalida");
    CourierAoS04 r{};
    r.xMeters = std::stof(fields[1]);
    r.yMeters = std::stof(fields[2]);
    r.speedMetersSecond = std::stof(fields[3]);
    r.reserved = std::stoi(fields[4]);
    r.capacity = std::stoi(fields[5]);
    r.state = std::stoi(fields[6]);
    if (!(r.capacity > 0 && r.reserved >= 0 && r.speedMetersSecond > 0))
      throw std::runtime_error("Valor de courier invalido");
    out.push_back(r);
  }
  if (out.size() < 5000) throw std::runtime_error("Se requieren 5000 registros");
  return out;
}

int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      std::cerr << "Uso: bench_courier_layout04 entrada.csv salida.csv\n";
      return 2;
    }
    const auto all = leer(argv[1]);
    std::ofstream csv(argv[2]);
    if (!csv) throw std::runtime_error("No se puede escribir CSV");
    csv << "n,layout,threads,rep,sweeps,elapsed_ms,per_sweep_ms,valid_count,checksum\n";
    csv << std::fixed << std::setprecision(8);

    constexpr unsigned int reps = 10;
    for (const std::size_t n : {64u, 256u, 512u, 2000u, 5000u}) {
      const unsigned int sweeps = static_cast<unsigned int>(500000 / n);
      const std::vector<CourierAoS04> aos(all.begin(), all.begin() + n);
      const CourierSoA04 soa = convertirSoA(aos);
      for (unsigned int hilos : {1u, 2u, 4u, 8u, 16u}) {
        // Calentamiento fuera de las mediciones.
        (void)calcularAoS(aos, 2100.f, -1600.f, 870.f, hilos, 3);
        (void)calcularSoA(soa, 2100.f, -1600.f, 870.f, hilos, 3);
        for (unsigned int rep = 0; rep < reps; ++rep) {
          // Alternar orden AoS/SoA para reducir sesgo por temperatura/frecuencia.
          for (unsigned int turno = 0; turno < 2; ++turno) {
            const bool isAos = (turno == (rep % 2));
            using Clock = std::chrono::steady_clock;
            const auto before = Clock::now();
            const auto result = isAos
                ? calcularAoS(aos, 2100.f, -1600.f, 870.f, hilos, sweeps)
                : calcularSoA(soa, 2100.f, -1600.f, 870.f, hilos, sweeps);
            const auto after = Clock::now();
            const auto expected = calcularAoS(aos, 2100.f, -1600.f, 870.f, 1, sweeps);
            if (result != expected) throw std::runtime_error("AoS/SoA difieren");
            std::size_t valid = 0;
            double sum = 0.0;
            for (auto r : result) {
              if (r >= 0) { ++valid; sum += r; }
            }
            const double elapsed =
                std::chrono::duration<double, std::milli>(after - before).count();
            csv << n << ',' << (isAos ? "AoS" : "SoA") << ',' << hilos << ','
                << rep << ',' << sweeps << ',' << elapsed << ','
                << elapsed / sweeps << ',' << valid << ',' << sum << '\n';
          }
        }
        std::cout << "LAYOUT04_BENCH_CPU_OK,n=" << n << ",threads=" << hilos
                  << ",reps=" << reps << '\n';
      }
    }
    std::cout << "LAYOUT04_CPU_BENCH_COMPLETO,filas=500,comparacion=OK\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "LAYOUT04_BENCH_ERROR: " << e.what() << '\n';
    return 1;
  }
}
