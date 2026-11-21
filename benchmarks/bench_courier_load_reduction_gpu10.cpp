#include "analytics/CourierLoadReductionGpu10.hpp"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace equipego::analytics;

static bool iguales(const ResumenCarga10& a, const ResumenCarga10& b) {
  return a.couriers == b.couriers && a.conPedidos == b.conPedidos &&
         a.totalReservas == b.totalReservas && a.maxReservas == b.maxReservas &&
         a.indiceMax == b.indiceMax && a.tieneMax == b.tieneMax;
}
int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      std::cerr << "Uso: bench_courier_load_reduction_gpu10 salida.csv\n";
      return 2;
    }
    std::ofstream out(argv[1]);
    if (!out) throw std::runtime_error("No se pudo crear CSV");
    out << "n,variant,rep,h2d_ms,kernel_ms,d2h_ms,total_ms,"
           "total_reservas,max_reservas,indice_max,con_pedidos,equivalencia\n";
    out << std::fixed << std::setprecision(9);
    int filas = 0;
    for (std::size_t n : {256u,1024u,4096u,16384u,65536u}) {
      std::vector<std::uint32_t> reservas(n);
      for (std::size_t i = 0; i < n; ++i)
        reservas[i] = static_cast<std::uint32_t>((i * 17) % 8);
      const auto cpu = reducirCargaCouriersCPU10(reservas);
      (void)ejecutarCargaCouriersGpu10(reservas); // Warmup GPU, fuera de repeticiones.
      for (int rep = 0; rep < 10; ++rep) {
        const auto gpu = ejecutarCargaCouriersGpu10(reservas);
        if (!iguales(cpu, gpu.resumen))
          throw std::runtime_error("FEATURE10 GPU/CPU no equivalentes");
        out << n << ",gpu," << rep << ',' << gpu.h2dMs << ','
            << gpu.kernelMs << ',' << gpu.d2hMs << ',' << gpu.totalMs
            << ',' << gpu.resumen.totalReservas << ',' << gpu.resumen.maxReservas
            << ',' << gpu.resumen.indiceMax << ',' << gpu.resumen.conPedidos
            << ",OK\n";
        ++filas;
      }
      std::cout << "FEATURE10_GPU_BENCH_OK,n=" << n << ",reps=10\n";
    }
    out.flush();
    if (!out || filas != 50) throw std::runtime_error("CSV incompleto");
    std::cout << "FEATURE10_GPU_BENCH_COMPLETO,filas=50\n";
    return 0;
  } catch(const std::exception& e) {
    std::cerr << "FEATURE10_GPU_BENCH_ERROR:" << e.what() << '\n';
    return 1;
  }
}
