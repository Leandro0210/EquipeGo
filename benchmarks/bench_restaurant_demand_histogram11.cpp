#include "analytics/RestaurantDemandHistogram11.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace equipego::analytics;

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::invalid_argument("Uso: bench_histogram11 salida.csv");
    std::ofstream csv(argv[1]);
    if (!csv) throw std::runtime_error("No se pudo abrir CSV");
    csv << "n,variant,rep,elapsed_ms,total_pedidos,checksum,equivalencia\n";
    csv << std::fixed << std::setprecision(9);
    std::size_t filas = 0;
    for (std::size_t n : {256u, 1024u, 4096u, 16384u, 65536u}) {
      std::vector<PedidoDemanda11> datos;
      datos.reserve(n);
      for (std::size_t i = 0; i < n; ++i)
        datos.push_back({static_cast<std::uint32_t>((i * 17 + 3) % 24),
                         static_cast<std::uint32_t>((i * 7 + 5) % 16)});
      const auto ref = histogramaDemandaCPU11(datos, 24, 16);
      for (const std::string variante : {"cpu_sequential", "cpu_parallel_4"}) {
        for (int rep = 0; rep < 10; ++rep) {
          const auto inicio = std::chrono::steady_clock::now();
          const auto resultado = variante == "cpu_sequential"
              ? histogramaDemandaCPU11(datos, 24, 16)
              : histogramaDemandaCPUParalelo11(datos, 24, 16, 4);
          const auto fin = std::chrono::steady_clock::now();
          if (!histogramasIguales11(ref, resultado))
            throw std::runtime_error("Equivalencia CPU perdida");
          std::uint64_t checksum = 0;
          for (std::size_t i = 0; i < resultado.celdas.size(); ++i)
            checksum += resultado.celdas[i] * (i + 1);
          const double ms = std::chrono::duration<double, std::milli>(fin-inicio).count();
          csv << n << ',' << variante << ',' << rep << ',' << ms << ','
              << resultado.totalPedidos << ',' << checksum << ",OK\n";
          ++filas;
        }
      }
      std::cout << "FEATURE11_CPU_BENCH_OK,n=" << n << ",reps=10\n";
    }
    csv.close();
    if (!csv) throw std::runtime_error("Error escribiendo CSV");
    std::cout << "FEATURE11_CPU_BENCH_COMPLETO,filas=" << filas << '\n';
    return filas == 100 ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "FEATURE11_CPU_BENCH_ERROR:" << error.what() << '\n';
    return 1;
  }
}
