#include "analytics/OrderWaitFilter12.hpp"
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
    if (argc != 2)
      throw std::invalid_argument("Uso: bench_order_wait_filter12 salida.csv");
    std::ofstream csv(argv[1]);
    if (!csv) throw std::runtime_error("No se pudo abrir CSV");
    csv << "n,variant,rep,elapsed_ms,umbral_ms,seleccionados,descartados,checksum,equivalencia\n";
    csv << std::fixed << std::setprecision(9);
    std::size_t filas = 0;
    for (std::size_t n : {256u, 1024u, 4096u, 16384u, 65536u}) {
      std::vector<std::uint64_t> datos;
      datos.reserve(n);
      for (std::size_t i = 0; i < n; ++i)
        datos.push_back((i * 733ULL + i / 13ULL) % 120000ULL);
      constexpr std::uint64_t umbral = 45000;
      const auto referencia = filtrarEsperasCPU12(datos, umbral);
      for (const std::string variante : {"cpu_sequential", "cpu_parallel_4"}) {
        for (int rep = 0; rep < 10; ++rep) {
          const auto inicio = std::chrono::steady_clock::now();
          const auto resultado = variante == "cpu_sequential"
              ? filtrarEsperasCPU12(datos, umbral)
              : filtrarEsperasCPUParalelo12(datos, umbral, 4);
          const auto fin = std::chrono::steady_clock::now();
          if (!filtrosIguales12(resultado, referencia))
            throw std::runtime_error("Feature12: diferencia CPU sec/par");
          std::uint64_t checksum = 0;
          for (std::size_t i : resultado.indices)
            checksum += static_cast<std::uint64_t>(i + 1);
          const double ms =
              std::chrono::duration<double, std::milli>(fin-inicio).count();
          csv << n << ',' << variante << ',' << rep << ',' << ms << ','
              << umbral << ',' << resultado.seleccionados() << ','
              << resultado.descartados() << ',' << checksum << ",OK\n";
          ++filas;
        }
      }
      std::cout << "FEATURE12_CPU_BENCH_OK,n=" << n << ",reps=10\n";
    }
    csv.close();
    if (!csv) throw std::runtime_error("No se pudo completar el CSV");
    std::cout << "FEATURE12_CPU_BENCH_COMPLETO,filas=" << filas << '\n';
    return filas == 100 ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "FEATURE12_CPU_BENCH_ERROR:" << e.what() << '\n';
    return 1;
  }
}
