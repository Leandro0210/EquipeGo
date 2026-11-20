#include "analytics/HeatmapGpuDispatch05.hpp"
#include <stdexcept>
#include <future>
#include <stdexcept>
#include <vector>
#include <iostream>

using namespace equipego::convolution05;

static void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

static std::future<GpuResult05> resultFuture(GpuResult05 data) {
  std::promise<GpuResult05> p;
  p.set_value(std::move(data));
  return p.get_future();
}

int main() {
  DemandGrid05 grid{8, 8, std::vector<std::int32_t>(64, 0)};
  grid.orders[1] = 5;
  grid.orders[23] = 17;
  grid.orders[63] = 6;
  const auto reference = gaussianSeparable05(grid);
  int calls = 0;
  const HeatmapSubmit05 success = [&](DemandGrid05 submitted) {
    ++calls;
    require(submitted.orders == grid.orders,"F05 E4 check failed");
    GpuResult05 result;
    result.values = reference;
    result.kernelMs = 0.11;
    result.totalMs = 0.42;
    return resultFuture(std::move(result));
  };
  const auto cpu = selectHeatmap05(grid, false, true, success);
  require(cpu.backend == "cpu_separable" && !cpu.gpuRequested,"F05 E4 check failed");
  require(cpu.values == reference && calls == 0,"F05 E4 check failed");
  const auto noCuda = selectHeatmap05(grid, true, false, success);
  require(!noCuda.usedGpu && noCuda.fallbackReason == "USE_CUDA_OFF","F05 E4 check failed");
  require(noCuda.values == reference && calls == 0,"F05 E4 check failed");
  const auto gpu = selectHeatmap05(grid, true, true, success);
  require(gpu.usedGpu && gpu.backend == "gpu_tiled" && calls == 1,"F05 E4 check failed");
  require(gpu.values == reference && gpu.kernelMs == 0.11,"F05 E4 check failed");
  const auto unavailable = selectHeatmap05(grid, true, true);
  require(!unavailable.usedGpu && unavailable.fallbackReason == "OWNER_UNAVAILABLE","F05 E4 check failed");
  const auto mismatch = selectHeatmap05(grid, true, true, [&](DemandGrid05) {
    GpuResult05 r;
    r.values.assign(grid.orders.size(), -1.0f);
    return resultFuture(std::move(r));
  });
  require(!mismatch.usedGpu && mismatch.fallbackReason == "MISMATCH_CPU_GPU","F05 E4 check failed");
  require(mismatch.values == reference,"F05 E4 check failed");
  const auto exception = selectHeatmap05(grid, true, true, [&](DemandGrid05) -> std::future<GpuResult05> {
    throw std::runtime_error("CUDA test failure");
  });
  require(!exception.usedGpu && exception.fallbackReason.find("GPU_EXCEPTION:") == 0,"F05 E4 check failed");
  require(exception.values == reference,"F05 E4 check failed");
  std::cout << "HEATMAP05_GPU_DISPATCH_CPU_OK,cases=6\n";
}
