#include "map/FloydWarshallCuda03.hpp"

#include <cuda_runtime.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace equipego::analytics {
namespace {

constexpr int B = 16;
constexpr double INF = std::numeric_limits<double>::infinity();

void cudaCheck(cudaError_t result, const char* operation) {
  if (result != cudaSuccess) {
    throw std::runtime_error(std::string("FLOYD03 CUDA ") + operation + ": " +
                             cudaGetErrorString(result));
  }
}

class DeviceBuffer {
 public:
  explicit DeviceBuffer(std::size_t bytes) {
    cudaCheck(cudaMalloc(reinterpret_cast<void**>(&data_), bytes), "cudaMalloc");
  }
  ~DeviceBuffer() { if (data_ != nullptr) cudaFree(data_); }
  DeviceBuffer(const DeviceBuffer&) = delete;
  DeviceBuffer& operator=(const DeviceBuffer&) = delete;
  double* get() const { return data_; }
 private:
  double* data_ = nullptr;
};

class Events {
 public:
  Events() {
    cudaCheck(cudaEventCreate(&a_), "cudaEventCreate start");
    try { cudaCheck(cudaEventCreate(&b_), "cudaEventCreate end"); }
    catch (...) { cudaEventDestroy(a_); a_ = nullptr; throw; }
  }
  ~Events() {
    if (b_) cudaEventDestroy(b_);
    if (a_) cudaEventDestroy(a_);
  }
  Events(const Events&) = delete;
  Events& operator=(const Events&) = delete;
  void start() { cudaCheck(cudaEventRecord(a_), "cudaEventRecord start"); }
  double stop() {
    cudaCheck(cudaEventRecord(b_), "cudaEventRecord end");
    cudaCheck(cudaEventSynchronize(b_), "cudaEventSynchronize");
    float ms = 0.0f;
    cudaCheck(cudaEventElapsedTime(&ms, a_, b_), "cudaEventElapsedTime");
    return static_cast<double>(ms);
  }
 private:
  cudaEvent_t a_ = nullptr;
  cudaEvent_t b_ = nullptr;
};

__device__ __forceinline__ double loadValue(const double* d, int n, int i, int j) {
  return (i < n && j < n) ? d[static_cast<std::size_t>(i) * n + j] : INF;
}

// Fase 1: solo pivote. Dos barreras por iteracion previenen read/write races.
__global__ void pivot03(double* d, int n, int p) {
  __shared__ double x[B][B];
  const int y = threadIdx.y, xx = threadIdx.x;
  const int i = p * B + y, j = p * B + xx;
  x[y][xx] = loadValue(d, n, i, j);
  __syncthreads();
  for (int k = 0; k < B; ++k) {
    const double ik = x[y][k];
    const double kj = x[k][xx];
    __syncthreads();
    const double alt = ik + kj;
    if (alt < x[y][xx]) x[y][xx] = alt;
    __syncthreads();
  }
  if (i < n && j < n) d[static_cast<std::size_t>(i) * n + j] = x[y][xx];
}

// Fase 2: bloques de fila y columna pivote. La diagonal ya esta terminada.
__global__ void borders03(double* d, int n, int p) {
  const int q = static_cast<int>(blockIdx.x) >= p ? static_cast<int>(blockIdx.x) + 1
                                                 : static_cast<int>(blockIdx.x);
  const bool isRow = (blockIdx.y == 0);
  const int y = threadIdx.y, x = threadIdx.x;
  const int i = (isRow ? p : q) * B + y;
  const int j = (isRow ? q : p) * B + x;
  __shared__ double a[B][B];
  __shared__ double diagonal[B][B];
  a[y][x] = loadValue(d, n, i, j);
  diagonal[y][x] = loadValue(d, n, p * B + y, p * B + x);
  __syncthreads();
  for (int k = 0; k < B; ++k) {
    const double term = isRow ? a[k][x] : a[y][k];
    const double pivot = isRow ? diagonal[y][k] : diagonal[k][x];
    __syncthreads();
    const double alt = term + pivot;
    if (alt < a[y][x]) a[y][x] = alt;
    __syncthreads();
  }
  if (i < n && j < n) d[static_cast<std::size_t>(i) * n + j] = a[y][x];
}

// Fase 3: resto de bloques independientes; reutiliza dos tiles shared.
__global__ void remaining03(double* d, int n, int p) {
  const int bi = static_cast<int>(blockIdx.y) >= p ? static_cast<int>(blockIdx.y) + 1
                                                  : static_cast<int>(blockIdx.y);
  const int bj = static_cast<int>(blockIdx.x) >= p ? static_cast<int>(blockIdx.x) + 1
                                                  : static_cast<int>(blockIdx.x);
  const int y = threadIdx.y, x = threadIdx.x;
  const int i = bi * B + y, j = bj * B + x;
  __shared__ double col[B][B];
  __shared__ double row[B][B];
  col[y][x] = loadValue(d, n, i, p * B + x);
  row[y][x] = loadValue(d, n, p * B + y, j);
  const double original = loadValue(d, n, i, j);
  __syncthreads();
  double best = original;
  for (int k = 0; k < B; ++k) {
    const double alt = col[y][k] + row[k][x];
    if (alt < best) best = alt;
  }
  if (i < n && j < n) d[static_cast<std::size_t>(i) * n + j] = best;
}

} // namespace

ResultadoFloydGpu03 floydWarshallCuda03(
    const std::vector<double>& matrizInicial, std::size_t n, std::size_t maxNodos) {
  if (n == 0 || maxNodos == 0 || n > maxNodos || n > 4096 ||
      n > std::numeric_limits<std::size_t>::max() / n ||
      matrizInicial.size() != n * n) {
    throw std::invalid_argument("FLOYD03 GPU: dimensiones no validas (limite 4096)");
  }
  for (double d : matrizInicial) {
    if (std::isnan(d) || d < 0.0 || (std::isinf(d) && d < 0.0)) {
      throw std::invalid_argument("FLOYD03 GPU: matriz contiene NaN o negativos");
    }
  }
  const int count = static_cast<int>((n + B - 1) / B);
  const std::size_t bytes = n * n * sizeof(double);
  const auto hostStart = std::chrono::steady_clock::now();
  ResultadoFloydGpu03 out;
  out.metros.resize(n * n);
  DeviceBuffer dev(bytes);
  Events timing;
  timing.start();
  cudaCheck(cudaMemcpy(dev.get(), matrizInicial.data(), bytes, cudaMemcpyHostToDevice),
            "H2D");
  out.h2dMs = timing.stop();

  const dim3 block(B, B);
  timing.start();
  for (int p = 0; p < count; ++p) {
    pivot03<<<1, block>>>(dev.get(), static_cast<int>(n), p);
    cudaCheck(cudaPeekAtLastError(), "pivot launch");
    if (count > 1) {
      borders03<<<dim3(count - 1, 2), block>>>(dev.get(), static_cast<int>(n), p);
      cudaCheck(cudaPeekAtLastError(), "borders launch");
      remaining03<<<dim3(count - 1, count - 1), block>>>(
          dev.get(), static_cast<int>(n), p);
      cudaCheck(cudaPeekAtLastError(), "remaining launch");
    }
  }
  out.kernelMs = timing.stop();
  timing.start();
  cudaCheck(cudaMemcpy(out.metros.data(), dev.get(), bytes, cudaMemcpyDeviceToHost),
            "D2H");
  out.d2hMs = timing.stop();
  const auto hostStop = std::chrono::steady_clock::now();
  out.totalHostMs = std::chrono::duration<double, std::milli>(hostStop - hostStart).count();
  return out;
}

} // namespace equipego::analytics
