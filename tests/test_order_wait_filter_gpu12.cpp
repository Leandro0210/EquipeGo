#include "analytics/OrderWaitFilterGpu12.hpp"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace equipego::analytics;
#define VERIFY12(c) do { if (!(c)) { std::cerr << "FILTER12_GPU_FAIL,line=" \
  << __LINE__ << ",cond=" #c << '\n'; return 1; } } while (false)

static bool check12(const std::vector<std::uint64_t>& v, std::uint64_t threshold) {
  const auto cpu = filtrarEsperasCPU12(v, threshold);
  const auto gpu = ejecutarFiltroEsperasGpu12(v, threshold);
  if (!filtrosIguales12(cpu, gpu.filtro)) return false;
  if (gpu.h2dMs < 0 || gpu.kernelMs < 0 || gpu.d2hMs < 0 || gpu.totalMs < 0)
    return false;
  for (std::size_t i=1; i<gpu.filtro.indices.size(); ++i)
    if (gpu.filtro.indices[i-1] >= gpu.filtro.indices[i]) return false;
  return true;
}
int main() {
 try {
  VERIFY12(check12({}, 1000));
  VERIFY12(check12({0}, 0));
  VERIFY12(check12({1}, 0));
  VERIFY12(check12({42}, 42));
  VERIFY12(check12({42}, 41));
  VERIFY12(check12({100, 100, 101, 99, UINT64_MAX}, 100));
  VERIFY12(check12({100, 100, 101, 99, UINT64_MAX}, UINT64_MAX));
  for (std::size_t n : {1u,2u,17u,255u,256u,257u,511u,512u,513u,
                         1024u,4096u,65536u,100003u}) {
    std::vector<std::uint64_t> v;
    v.reserve(n);
    for (std::size_t i=0; i<n; ++i)
      v.push_back((i*733ULL + i/13) % 120000ULL);
    VERIFY12(check12(v,45000));
    VERIFY12(check12(v,0));
    VERIFY12(check12(v,120000));
    std::cout << "FEATURE12_GPU_CASE_OK,n=" << n << '\n';
  }
  std::vector<std::uint64_t> all(100003, UINT64_MAX);
  VERIFY12(check12(all, UINT64_MAX-1));
  VERIFY12(check12(all, UINT64_MAX));
  std::cout << "FEATURE12_GPU_EQUIVALENCIA_OK,ESTABLE=SI\n";
  return 0;
 } catch (const std::exception& e) {
  std::cerr << "FEATURE12_GPU_EXCEPTION:" << e.what() << '\n';
  return 1;
 }
}
