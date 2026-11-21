#include "analytics/RestaurantDemandHistogramGpu11.hpp"
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
    if (argc != 2) throw std::invalid_argument("Uso: bench_gpu11 salida.csv");
    std::ofstream out(argv[1]);
    if (!out) throw std::runtime_error("No se pudo crear CSV");
    out << "n,variant,rep,h2d_ms,kernel_ms,d2h_ms,total_ms,"
           "total_pedidos,checksum,equivalencia\n";
    out << std::fixed << std::setprecision(9);
    std::size_t filas = 0;
    for (std::size_t n : {256u,1024u,4096u,16384u,65536u}) {
      std::vector<PedidoDemanda11> pedidos;
      pedidos.reserve(n);
      for (std::size_t i = 0; i < n; ++i)
        pedidos.push_back({static_cast<std::uint32_t>((i * 17 + 3) % 24),
                           static_cast<std::uint32_t>((i * 7 + 5) % 16)});
      const auto cpu = histogramaDemandaCPU11(pedidos, 24, 16);
      (void)ejecutarHistogramaDemandaGpu11(pedidos, 24, 16); // Warmup.
      for (int rep = 0; rep < 10; ++rep) {
        const auto gpu = ejecutarHistogramaDemandaGpu11(pedidos, 24, 16);
        if (!histogramasIguales11(cpu, gpu.histograma))
          throw std::runtime_error("Feature11 GPU/CPU no equivalentes");
        std::uint64_t checksum = 0;
        for (std::size_t i = 0; i < gpu.histograma.celdas.size(); ++i)
          checksum += gpu.histograma.celdas[i] * (i + 1);
        out << n << ",gpu," << rep << ',' << gpu.h2dMs << ','
            << gpu.kernelMs << ',' << gpu.d2hMs << ',' << gpu.totalMs
            << ',' << gpu.histograma.totalPedidos << ',' << checksum
            << ",OK\n";
        ++filas;
      }
      std::cout << "FEATURE11_GPU_BENCH_OK,n=" << n << ",reps=10\n";
    }
    out.flush();
    if (!out || filas != 50) throw std::runtime_error("CSV incompleto");
    std::cout << "FEATURE11_GPU_BENCH_COMPLETO,filas=" << filas << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FEATURE11_GPU_BENCH_ERROR:" << e.what() << '\n';
    return 1;
  }
}
