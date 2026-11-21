#pragma once
// EC2 05_conv: convolution basic + tiled shared + constant weights.
// Experimental heatmap. No modification to the live simulator.
#include "analytics/OrderHeatmap05.hpp"
#include <vector>

namespace equipego::convolution05 {

enum class GpuAlgorithm05 { BasicConstant, TiledSharedConstant };

struct GpuResult05 {
  std::vector<float> values;
  double h2dMs = 0.0;
  double kernelMs = 0.0;
  double d2hMs = 0.0;
  double totalMs = 0.0;
  std::size_t inputBytes = 0;
  std::size_t outputBytes = 0;
};

GpuResult05 gaussianGpu05(const DemandGrid05& grid, GpuAlgorithm05 algorithm);

}  // namespace equipego::convolution05
