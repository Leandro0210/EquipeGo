#include "analytics/OrderHeatmapGpu05.hpp"
#include <cuda_runtime.h>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace equipego::convolution05 {
namespace {
constexpr int R = 2;
constexpr int FILTER_SIZE = 5;
constexpr int IN_TILE = 20;
constexpr int OUT_TILE = IN_TILE - 2 * R; // 16x16 output, 20x20 cooperative input.

// Adaptado de ec2/05_conv/03_conv2d_const_mem.cu + bikeshare_demand_heatmap.cu.
// Integer weights: exact 5x5 separable binomial mask (sum 256).
__constant__ int weights05[FILTER_SIZE * FILTER_SIZE];

__global__ void convolutionBasic05(const std::int32_t* input,
                                   float* output, int width, int height) {
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= width || y >= height) return;
  std::int64_t sum = 0;
  #pragma unroll
  for (int dy = -R; dy <= R; ++dy) {
    const int yy = y + dy;
    if (yy < 0 || yy >= height) continue;
    #pragma unroll
    for (int dx = -R; dx <= R; ++dx) {
      const int xx = x + dx;
      if (xx >= 0 && xx < width) {
        sum += static_cast<std::int64_t>(weights05[(dy + R) * FILTER_SIZE + dx + R]) *
               input[yy * width + xx];
      }
    }
  }
  output[y * width + x] = static_cast<float>(sum) / 256.0f;
}

// EC2 05_conv/04_conv2d_tiled.cu, adaptado de radio 1 a radio 2.
// Los 400 hilos del bloque cargan incluso el halo y se sincronizan antes de leer.
__global__ void convolutionTiled05(const std::int32_t* input,
                                   float* output, int width, int height) {
  const int tx = threadIdx.x;
  const int ty = threadIdx.y;
  const int x = blockIdx.x * OUT_TILE + tx - R;
  const int y = blockIdx.y * OUT_TILE + ty - R;
  __shared__ std::int32_t tile[IN_TILE][IN_TILE];
  tile[ty][tx] = (x >= 0 && x < width && y >= 0 && y < height)
                     ? input[y * width + x] : 0;
  __syncthreads();
  const bool isOutput = tx >= R && tx < R + OUT_TILE &&
                        ty >= R && ty < R + OUT_TILE &&
                        x >= 0 && x < width && y >= 0 && y < height;
  if (isOutput) {
    std::int64_t sum = 0;
    #pragma unroll
    for (int ky = 0; ky < FILTER_SIZE; ++ky) {
      #pragma unroll
      for (int kx = 0; kx < FILTER_SIZE; ++kx) {
        sum += static_cast<std::int64_t>(weights05[ky * FILTER_SIZE + kx]) *
               tile[ty - R + ky][tx - R + kx];
      }
    }
    output[y * width + x] = static_cast<float>(sum) / 256.0f;
  }
}

void check(cudaError_t err, const char* name) {
  if (err != cudaSuccess)
    throw std::runtime_error(std::string(name) + ": " + cudaGetErrorString(err));
}

struct Resources {
  std::int32_t* input = nullptr;
  float* output = nullptr;
  cudaEvent_t start = nullptr;
  cudaEvent_t stop = nullptr;
  ~Resources() {
    if (stop) cudaEventDestroy(stop);
    if (start) cudaEventDestroy(start);
    if (output) cudaFree(output);
    if (input) cudaFree(input);
  }
};

float eventMs(cudaEvent_t start, cudaEvent_t stop) {
  check(cudaEventRecord(stop), "cudaEventRecord(stop)");
  check(cudaEventSynchronize(stop), "cudaEventSynchronize(stop)");
  float ms = 0.0f;
  check(cudaEventElapsedTime(&ms, start, stop), "cudaEventElapsedTime");
  return ms;
}
}  // namespace

GpuResult05 gaussianGpu05(const DemandGrid05& grid, GpuAlgorithm05 algorithm) {
  validate(grid);
  if (grid.width > 32768 || grid.height > 32768 ||
      grid.orders.size() > 16777216) {
    throw std::invalid_argument("Convolution05 GPU: dimensiones excesivas");
  }
  const int w = static_cast<int>(grid.width);
  const int h = static_cast<int>(grid.height);
  using Clock = std::chrono::steady_clock;
  const auto begun = Clock::now();
  GpuResult05 result;
  result.inputBytes = grid.orders.size() * sizeof(std::int32_t);
  result.outputBytes = grid.orders.size() * sizeof(float);
  result.values.resize(grid.orders.size());

  const int g[5] = {1, 4, 6, 4, 1};
  int kernelWeights[25]{};
  for (int y = 0; y < 5; ++y)
    for (int x = 0; x < 5; ++x)
      kernelWeights[y * 5 + x] = g[y] * g[x];
  check(cudaSetDevice(0), "cudaSetDevice");
  check(cudaMemcpyToSymbol(weights05, kernelWeights, sizeof(kernelWeights)),
        "cudaMemcpyToSymbol(weights05)");
  Resources r;
  check(cudaMalloc(reinterpret_cast<void**>(&r.input), result.inputBytes),
        "cudaMalloc(input)");
  check(cudaMalloc(reinterpret_cast<void**>(&r.output), result.outputBytes),
        "cudaMalloc(output)");
  check(cudaEventCreate(&r.start), "cudaEventCreate(start)");
  check(cudaEventCreate(&r.stop), "cudaEventCreate(stop)");

  check(cudaEventRecord(r.start), "cudaEventRecord(H2D)");
  check(cudaMemcpy(r.input, grid.orders.data(), result.inputBytes,
                   cudaMemcpyHostToDevice), "cudaMemcpy(H2D)");
  result.h2dMs = eventMs(r.start, r.stop);

  check(cudaEventRecord(r.start), "cudaEventRecord(kernel)");
  if (algorithm == GpuAlgorithm05::BasicConstant) {
    const dim3 block(16, 16);
    const dim3 blocks((w + 15) / 16, (h + 15) / 16);
    convolutionBasic05<<<blocks, block>>>(r.input, r.output, w, h);
  } else if (algorithm == GpuAlgorithm05::TiledSharedConstant) {
    const dim3 block(IN_TILE, IN_TILE);
    const dim3 blocks((w + OUT_TILE - 1) / OUT_TILE,
                      (h + OUT_TILE - 1) / OUT_TILE);
    convolutionTiled05<<<blocks, block>>>(r.input, r.output, w, h);
  } else {
    throw std::invalid_argument("Convolution05 GPU: algoritmo invalido");
  }
  check(cudaGetLastError(), "convolution kernel launch");
  result.kernelMs = eventMs(r.start, r.stop);

  check(cudaEventRecord(r.start), "cudaEventRecord(D2H)");
  check(cudaMemcpy(result.values.data(), r.output, result.outputBytes,
                   cudaMemcpyDeviceToHost), "cudaMemcpy(D2H)");
  result.d2hMs = eventMs(r.start, r.stop);

  // Medicion total = host startup, symbol copy, allocations, H2D, kernel,
  // D2H, event setup AND resource release (done before final timestamp).
  check(cudaEventDestroy(r.start), "cudaEventDestroy(start)"); r.start = nullptr;
  check(cudaEventDestroy(r.stop), "cudaEventDestroy(stop)"); r.stop = nullptr;
  check(cudaFree(r.input), "cudaFree(input)"); r.input = nullptr;
  check(cudaFree(r.output), "cudaFree(output)"); r.output = nullptr;
  result.totalMs = std::chrono::duration<double, std::milli>(
      Clock::now() - begun).count();
  return result;
}
}  // namespace equipego::convolution05
