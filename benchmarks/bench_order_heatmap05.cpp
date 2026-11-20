#include "analytics/OrderHeatmap05.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace equipego::convolution05;

DemandGrid05 readGrid(const std::string& filename) {
  std::ifstream file(filename);
  if (!file) throw std::runtime_error("No se abre CSV: " + filename);
  std::string line;
  if (!std::getline(file, line)) throw std::runtime_error("CSV vacio");
  std::stringstream header(line);
  std::string first, second;
  if (!std::getline(header, first, ',') || !std::getline(header, second, ',')) {
    throw std::runtime_error("Cabecera debe ser width,height");
  }
  DemandGrid05 g;
  g.width = std::stoul(first);
  g.height = std::stoul(second);
  if (g.width != 512 || g.height != 512) {
    throw std::runtime_error("Benchmark espera grilla 512x512");
  }
  g.orders.reserve(g.width * g.height);
  while (std::getline(file, line)) {
    if (line.empty()) continue;
    std::stringstream row(line);
    std::string cell;
    std::size_t count = 0;
    while (std::getline(row, cell, ',')) {
      const long v = std::stol(cell);
      if (v < 0 || v > 1000000) throw std::runtime_error("Conteo fuera de rango");
      g.orders.push_back(static_cast<std::int32_t>(v));
      ++count;
    }
    if (count != g.width) throw std::runtime_error("Ancho de fila incorrecto");
  }
  validate(g);
  return g;
}

DemandGrid05 centerCrop(const DemandGrid05& src, std::size_t side) {
  DemandGrid05 out{side, side, std::vector<std::int32_t>(side * side)};
  const auto x0 = (src.width - side) / 2;
  const auto y0 = (src.height - side) / 2;
  for (std::size_t y = 0; y < side; ++y)
    for (std::size_t x = 0; x < side; ++x)
      out.orders[y * side + x] = src.orders[(y0 + y) * src.width + x0 + x];
  return out;
}

int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      std::cerr << "Uso: bench_order_heatmap05 entrada512.csv salida.csv\n";
      return 2;
    }
    const auto source = readGrid(argv[1]);
    std::ofstream csv(argv[2]);
    if (!csv) throw std::runtime_error("No se puede escribir salida CSV");
    csv << "side,algorithm,threads,rep,elapsed_ms,checksum\n";
    csv << std::fixed << std::setprecision(9);
    std::size_t rows = 0;
    volatile double sink = 0.0;
    for (const std::size_t side : {32u, 64u, 128u, 256u, 512u}) {
      const auto grid = centerCrop(source, side);
      const auto reference = gaussianReference05(grid);
      for (unsigned rep = 0; rep < 10; ++rep) {
        const auto before = std::chrono::steady_clock::now();
        const auto out = gaussianReference05(grid);
        const auto after = std::chrono::steady_clock::now();
        if (out != reference) throw std::runtime_error("Referencia inconsistente");
        const double checksum = std::accumulate(out.begin(), out.end(), 0.0);
        sink += checksum;
        const double ms = std::chrono::duration<double, std::milli>(after-before).count();
        csv << side << ",direct_5x5,1," << rep << ',' << ms << ',' << checksum << '\n';
        ++rows;
      }
      for (unsigned threads : {1u, 2u, 4u, 8u, 16u}) {
        (void)gaussianSeparable05(grid, threads);
        for (unsigned rep = 0; rep < 10; ++rep) {
          const auto before = std::chrono::steady_clock::now();
          const auto out = gaussianSeparable05(grid, threads);
          const auto after = std::chrono::steady_clock::now();
          if (out != reference) throw std::runtime_error("Diferencia vs referencia");
          const double checksum = std::accumulate(out.begin(), out.end(), 0.0);
          sink += checksum;
          const double ms = std::chrono::duration<double, std::milli>(after-before).count();
          csv << side << ",separable_2pass," << threads << ',' << rep << ','
              << ms << ',' << checksum << '\n';
          ++rows;
        }
        std::cout << "CONVOLUTION05_BENCH_CPU_OK,side=" << side
                  << ",threads=" << threads << ",reps=10\n";
      }
    }
    if (rows != 300) throw std::runtime_error("Numero de mediciones incorrecto");
    std::cout << "CONVOLUTION05_CPU_BENCH_COMPLETO,filas=300,comparacion=OK"
              << ",checksum_guard=" << sink << '\n';
  } catch (const std::exception& e) {
    std::cerr << "CONVOLUTION05_BENCH_ERROR: " << e.what() << '\n';
    return 1;
  }
}
