#include "analytics/CourierLoadReduction10.hpp"

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
using Clock10 = std::chrono::steady_clock;

static bool equivalentes(const ResumenCarga10& a,
                          const ResumenCarga10& b) {
  return a.couriers == b.couriers && a.conPedidos == b.conPedidos &&
         a.totalReservas == b.totalReservas && a.maxReservas == b.maxReservas &&
         a.indiceMax == b.indiceMax && a.tieneMax == b.tieneMax;
}

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      std::cerr << "Uso: bench_courier_load_reduction10 salida.csv\n";
      return 2;
    }
    std::ofstream out(argv[1]);
    if (!out) throw std::runtime_error("No se pudo abrir CSV");
    out << "n,variant,rep,elapsed_ms,total_reservas,max_reservas,indice_max"
           ",con_pedidos,equivalencia\n";
    out << std::fixed << std::setprecision(9);
    int filas = 0;
    for (std::size_t n : {256u, 1024u, 4096u, 16384u, 65536u}) {
      std::vector<std::uint32_t> cargas(n);
      for (std::size_t i = 0; i < n; ++i)
        cargas[i] = static_cast<std::uint32_t>((i * 17) % 8);
      const auto referencia = reducirCargaCouriersCPU10(cargas);
      (void)reducirCargaCouriersCPUParalelo10(cargas, 4);
      for (int rep = 0; rep < 10; ++rep) {
        for (int turno = 0; turno < 2; ++turno) {
          const bool paralelo = (turno == (rep % 2));
          const auto inicio = Clock10::now();
          const auto actual = paralelo ?
              reducirCargaCouriersCPUParalelo10(cargas, 4) :
              reducirCargaCouriersCPU10(cargas);
          const auto fin = Clock10::now();
          if (!equivalentes(referencia, actual))
            throw std::runtime_error("Divergencia CPU10");
          const double ms = std::chrono::duration<double, std::milli>(
              fin - inicio).count();
          out << n << ',' << (paralelo ? "cpu_parallel_4" : "cpu_sequential")
              << ',' << rep << ',' << ms << ',' << actual.totalReservas
              << ',' << actual.maxReservas << ',' << actual.indiceMax
              << ',' << actual.conPedidos << ",OK\n";
          ++filas;
        }
      }
      std::cout << "FEATURE10_CPU_BENCH_OK,n=" << n << ",reps=10\n";
    }
    out.flush();
    if (!out || filas != 100)
      throw std::runtime_error("CSV incompleto");
    std::cout << "FEATURE10_CPU_BENCH_COMPLETO,filas=100\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FEATURE10_CPU_BENCH_ERROR:" << e.what() << '\n';
    return 1;
  }
}
