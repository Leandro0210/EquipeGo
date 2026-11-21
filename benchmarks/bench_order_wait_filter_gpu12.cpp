#include "analytics/OrderWaitFilterGpu12.hpp"
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
  if (argc != 2) throw std::invalid_argument("Uso: bench_order_wait_filter_gpu12 salida.csv");
  std::ofstream csv(argv[1]);
  if (!csv) throw std::runtime_error("Error creando CSV GPU12");
  csv << "n,variant,rep,h2d_ms,kernel_ms,d2h_ms,total_ms,umbral_ms,"
         "seleccionados,descartados,checksum,equivalencia\n";
  csv << std::fixed << std::setprecision(9);
  std::size_t rows = 0;
  for (std::size_t n : {256u,1024u,4096u,16384u,65536u}) {
    std::vector<std::uint64_t> v;
    v.reserve(n);
    for (std::size_t i=0; i<n; ++i)
      v.push_back((i*733ULL + i/13ULL) % 120000ULL);
    constexpr std::uint64_t threshold = 45000;
    const auto cpu = filtrarEsperasCPU12(v,threshold);
    (void)ejecutarFiltroEsperasGpu12(v,threshold); // GPU warmup.
    for (int rep=0; rep<10; ++rep) {
      const auto gpu = ejecutarFiltroEsperasGpu12(v,threshold);
      if (!filtrosIguales12(cpu,gpu.filtro))
        throw std::runtime_error("Feature12 GPU/CPU: no equivalentes");
      std::uint64_t checksum = 0;
      for (auto index : gpu.filtro.indices)
        checksum += static_cast<std::uint64_t>(index + 1);
      csv << n << ",gpu," << rep << ',' << gpu.h2dMs << ','
          << gpu.kernelMs << ',' << gpu.d2hMs << ',' << gpu.totalMs << ','
          << threshold << ',' << gpu.filtro.seleccionados() << ','
          << gpu.filtro.descartados() << ',' << checksum << ",OK\n";
      ++rows;
    }
    std::cout << "FEATURE12_GPU_BENCH_OK,n=" << n << ",reps=10\n";
  }
  csv.close();
  if (!csv || rows != 50) throw std::runtime_error("Feature12 GPU CSV incompleto");
  std::cout << "FEATURE12_GPU_BENCH_COMPLETO,filas=" << rows << '\n';
  return 0;
 } catch (const std::exception& e) {
  std::cerr << "FEATURE12_GPU_BENCH_ERROR:" << e.what() << '\n';
  return 1;
 }
}
