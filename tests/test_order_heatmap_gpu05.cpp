#include "analytics/OrderHeatmapGpu05.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace equipego::convolution05;

static void same(std::size_t width, std::size_t height) {
  DemandGrid05 g{width, height, std::vector<std::int32_t>(width * height)};
  for (std::size_t i = 0; i < g.orders.size(); ++i)
    g.orders[i] = (i % 13 == 0) ? 1000000 : static_cast<int>((i*47+11)%313);
  const auto ref = gaussianReference05(g);
  for (auto algo : {GpuAlgorithm05::BasicConstant,
                    GpuAlgorithm05::TiledSharedConstant}) {
    const auto gpu = gaussianGpu05(g, algo);
    if (gpu.values.size() != ref.size()) throw std::runtime_error("size mismatch");
    for (std::size_t i = 0; i < ref.size(); ++i) {
      if (gpu.values[i] != ref[i])
        throw std::runtime_error("Convolution GPU mismatch index=" + std::to_string(i)
                                 + " side=" + std::to_string(width));
    }
    if (!(gpu.totalMs > 0 && gpu.kernelMs >= 0 && gpu.h2dMs >= 0))
      throw std::runtime_error("invalid times");
  }
}

int main() {
  try {
    for (auto [w,h] : std::vector<std::pair<std::size_t,std::size_t>>{
           {1,1},{2,3},{5,7},{17,19},{31,23},{32,32},
           {63,59},{128,67},{512,512}}) {
      same(w,h);
    }
    DemandGrid05 invalid{2,2,{1,2,3,-1}};
    bool thrown = false;
    try { (void)gaussianGpu05(invalid, GpuAlgorithm05::BasicConstant); }
    catch (const std::invalid_argument&) { thrown = true; }
    if (!thrown) throw std::runtime_error("validation not enforced");
    std::cout << "CONVOLUTION05_GPU_EQUIVALENCIA_OK,variants=basic_const/tiled_shared_const,zero_padding=1\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "CONVOLUTION05_GPU_TEST_ERROR: " << ex.what() << '\n';
    return 1;
  }
}
