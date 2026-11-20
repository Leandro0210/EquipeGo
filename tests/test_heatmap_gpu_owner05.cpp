#include "analytics/DeliveryHistogramGpu.hpp"
#include "analytics/HeatmapGpuDispatch05.hpp"
#include <stdexcept>
#include <cstdint>
#include <future>
#include <iostream>
#include <vector>

using namespace equipego::convolution05;

static void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

int main() {
  equipego::analytics::PropietarioGpuHistograma owner;
  for (int side : {4, 17, 64}) {
    DemandGrid05 grid;
    grid.width = static_cast<std::size_t>(side);
    grid.height = static_cast<std::size_t>(side);
    grid.orders.assign(grid.width * grid.height, 0);
    for (std::size_t i = 0; i < grid.orders.size(); ++i)
      grid.orders[i] = static_cast<std::int32_t>((i * 7u + 5u) % 97u);
    const auto result = selectHeatmap05(grid, true, true,
       [&owner](DemandGrid05 copy) {
         return owner.enviarHeatmap05(std::move(copy), GpuAlgorithm05::TiledSharedConstant);
       });
    require(result.usedGpu && result.backend == "gpu_tiled","F05 E4 check failed");
    require(result.values == gaussianReference05(grid),"F05 E4 check failed");
    std::cout << "HEATMAP05_OWNER_T4_OK,side=" << side
              << ",kernel_ms=" << result.kernelMs
              << ",total_ms=" << result.totalMs << '\n';
  }
}
