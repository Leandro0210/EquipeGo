#include "analytics/OrderWaitFilterGpu12.hpp"
#include <cuda_runtime.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace equipego::analytics {
namespace {
constexpr unsigned kThreads12 = 256;
constexpr unsigned kMaxBlocks12 = 256;

void ck12(cudaError_t result, const char* where) {
  if (result != cudaSuccess)
    throw std::runtime_error(std::string("Feature12 CUDA ") + where +
                             ": " + cudaGetErrorString(result));
}

// Each block owns one contiguous segment. Each tile scans predicates in shared
// memory, storing a zero-based rank among selected items *in that block*.
__global__ void ranksPorBloque12(const std::uint64_t* esperas,
    std::size_t n, std::uint64_t threshold, unsigned blocks,
    std::uint32_t* localRank, std::uint32_t* blockCount) {
  __shared__ unsigned scan[kThreads12];
  const unsigned tid = threadIdx.x;
  const std::size_t seg = (n + blocks - 1) / blocks;
  const std::size_t start = static_cast<std::size_t>(blockIdx.x) * seg;
  const std::size_t rest = n - start;
  const std::size_t end = start + (seg < rest ? seg : rest);
  std::uint32_t running = 0;
  for (std::size_t base = start; base < end; base += kThreads12) {
    const std::size_t pos = base + tid;
    const unsigned selected = pos < end && esperas[pos] > threshold ? 1u : 0u;
    scan[tid] = selected;
    __syncthreads();
    for (unsigned stride = 1; stride < kThreads12; stride <<= 1) {
      const unsigned val = tid >= stride ? scan[tid - stride] : 0u;
      __syncthreads();
      scan[tid] += val;
      __syncthreads();
    }
    const unsigned tileTotal = scan[kThreads12 - 1];
    if (selected) localRank[pos] = running + scan[tid] - 1;
    __syncthreads();
    running += tileTotal;
  }
  if (tid == 0) blockCount[blockIdx.x] = running;
}

// Exclusive prefix of block counts, computed entirely on the GPU.
__global__ void prefijosDeBloques12(const std::uint32_t* counts,
    unsigned blocks, std::uint32_t* offsets, std::uint32_t* total) {
  __shared__ unsigned scan[kThreads12];
  const unsigned tid = threadIdx.x;
  scan[tid] = tid < blocks ? counts[tid] : 0u;
  __syncthreads();
  for (unsigned stride = 1; stride < kThreads12; stride <<= 1) {
    const unsigned val = tid >= stride ? scan[tid - stride] : 0u;
    __syncthreads();
    scan[tid] += val;
    __syncthreads();
  }
  if (tid < blocks) offsets[tid] = scan[tid] - counts[tid];
  if (tid == 0) *total = scan[kThreads12 - 1];
}

__global__ void dispersarEstable12(const std::uint64_t* esperas,
    std::size_t n, std::uint64_t threshold, unsigned blocks,
    const std::uint32_t* localRank, const std::uint32_t* offsets,
    std::uint32_t* selectedIndices) {
  const std::size_t seg = (n + blocks - 1) / blocks;
  const std::size_t start = static_cast<std::size_t>(blockIdx.x) * seg;
  const std::size_t rest = n - start;
  const std::size_t end = start + (seg < rest ? seg : rest);
  for (std::size_t pos = start + threadIdx.x; pos < end; pos += blockDim.x) {
    if (esperas[pos] > threshold) {
      selectedIndices[static_cast<std::size_t>(offsets[blockIdx.x]) +
                      localRank[pos]] = static_cast<std::uint32_t>(pos);
    }
  }
}

struct Device12 {
  std::uint64_t* esperas = nullptr;
  std::uint32_t* rank = nullptr;
  std::uint32_t* counts = nullptr;
  std::uint32_t* offsets = nullptr;
  std::uint32_t* indices = nullptr;
  std::uint32_t* count = nullptr;
  cudaEvent_t start = nullptr;
  cudaEvent_t stop = nullptr;
  Device12() = default;
  Device12(const Device12&) = delete;
  Device12& operator=(const Device12&) = delete;
  ~Device12() {
    if (stop) cudaEventDestroy(stop);
    if (start) cudaEventDestroy(start);
    if (count) cudaFree(count);
    if (indices) cudaFree(indices);
    if (offsets) cudaFree(offsets);
    if (counts) cudaFree(counts);
    if (rank) cudaFree(rank);
    if (esperas) cudaFree(esperas);
  }
  void initEvents() {
    ck12(cudaEventCreate(&start), "create start");
    ck12(cudaEventCreate(&stop), "create stop");
  }
  void begin() { ck12(cudaEventRecord(start), "record start"); }
  double end() {
    ck12(cudaEventRecord(stop), "record stop");
    ck12(cudaEventSynchronize(stop), "sync stop");
    float ms = 0.0f;
    ck12(cudaEventElapsedTime(&ms, start, stop), "elapsed events");
    return ms;
  }
};
}  // namespace

ResultadoFiltroGpu12 ejecutarFiltroEsperasGpu12(
    const std::vector<std::uint64_t>& esperasMs, std::uint64_t umbralMs) {
  const auto t0 = std::chrono::steady_clock::now();
  ResultadoFiltroGpu12 out;
  out.filtro.totalEntradas = esperasMs.size();
  out.filtro.umbralMs = umbralMs;
  if (esperasMs.size() > std::numeric_limits<std::uint32_t>::max())
    throw std::length_error("Feature12: demasiado grande para indices u32");
  if (esperasMs.empty()) {
    out.totalMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    return out;
  }
  const std::size_t n = esperasMs.size();
  const unsigned blocks = static_cast<unsigned>(std::min<std::size_t>(
      kMaxBlocks12, (n + kThreads12 - 1) / kThreads12));
  Device12 dev;
  ck12(cudaSetDevice(0), "set device");
  ck12(cudaMalloc(reinterpret_cast<void**>(&dev.esperas),
                  n * sizeof(std::uint64_t)), "malloc input");
  ck12(cudaMalloc(reinterpret_cast<void**>(&dev.rank),
                  n * sizeof(std::uint32_t)), "malloc ranks");
  ck12(cudaMalloc(reinterpret_cast<void**>(&dev.indices),
                  n * sizeof(std::uint32_t)), "malloc output");
  ck12(cudaMalloc(reinterpret_cast<void**>(&dev.counts),
                  blocks * sizeof(std::uint32_t)), "malloc counts");
  ck12(cudaMalloc(reinterpret_cast<void**>(&dev.offsets),
                  blocks * sizeof(std::uint32_t)), "malloc offsets");
  ck12(cudaMalloc(reinterpret_cast<void**>(&dev.count),
                  sizeof(std::uint32_t)), "malloc total");
  dev.initEvents();

  dev.begin();
  ck12(cudaMemcpy(dev.esperas, esperasMs.data(), n * sizeof(std::uint64_t),
                  cudaMemcpyHostToDevice), "H2D");
  out.h2dMs = dev.end();

  dev.begin();
  ranksPorBloque12<<<blocks, kThreads12>>>(
      dev.esperas, n, umbralMs, blocks, dev.rank, dev.counts);
  ck12(cudaGetLastError(), "ranksPorBloque12 launch");
  prefijosDeBloques12<<<1, kThreads12>>>(
      dev.counts, blocks, dev.offsets, dev.count);
  ck12(cudaGetLastError(), "prefijosDeBloques12 launch");
  dispersarEstable12<<<blocks, kThreads12>>>(
      dev.esperas, n, umbralMs, blocks, dev.rank, dev.offsets, dev.indices);
  ck12(cudaGetLastError(), "dispersarEstable12 launch");
  out.kernelMs = dev.end();

  dev.begin();
  std::uint32_t count = 0;
  ck12(cudaMemcpy(&count, dev.count, sizeof(count),
                  cudaMemcpyDeviceToHost), "D2H total");
  if (count > n) throw std::runtime_error("Feature12: count invalido");
  std::vector<std::uint32_t> tmp(count);
  if (count) ck12(cudaMemcpy(tmp.data(), dev.indices,
      static_cast<std::size_t>(count) * sizeof(std::uint32_t),
      cudaMemcpyDeviceToHost), "D2H indices");
  out.d2hMs = dev.end();
  out.filtro.indices.assign(tmp.begin(), tmp.end());
  // Release buffers/events before taking totalMs (includes allocations and frees).
  ck12(cudaEventDestroy(dev.stop), "destroy event stop"); dev.stop = nullptr;
  ck12(cudaEventDestroy(dev.start), "destroy event start"); dev.start = nullptr;
  ck12(cudaFree(dev.count), "free total"); dev.count = nullptr;
  ck12(cudaFree(dev.indices), "free output"); dev.indices = nullptr;
  ck12(cudaFree(dev.offsets), "free offsets"); dev.offsets = nullptr;
  ck12(cudaFree(dev.counts), "free counts"); dev.counts = nullptr;
  ck12(cudaFree(dev.rank), "free ranks"); dev.rank = nullptr;
  ck12(cudaFree(dev.esperas), "free input"); dev.esperas = nullptr;
  out.totalMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - t0).count();
  return out;
}
}  // namespace equipego::analytics
