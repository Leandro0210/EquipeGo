#include "analytics/DeliveryHistogramGpu.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace equipego::analytics {
namespace {

constexpr int kHilos = 256;
constexpr int kBins = static_cast<int>(kHistogramaBins);

void verificar(cudaError_t error, const char* llamada) {
  if (error != cudaSuccess) {
    throw std::runtime_error(std::string(llamada) + ": " +
                             cudaGetErrorString(error));
  }
}

__device__ __forceinline__ unsigned int binDeDuracion(long long ms) {
  const auto minuto = static_cast<unsigned long long>(ms / kMsPorMinuto);
  return minuto >= static_cast<unsigned long long>(kBins - 1)
             ? static_cast<unsigned int>(kBins - 1)
             : static_cast<unsigned int>(minuto);
}

// Referencia GPU directa: un atomic global por registro.
__global__ void histogramaAtomicosGlobales(
    const long long* duraciones, std::size_t n, unsigned long long* bins) {
  const std::size_t paso = static_cast<std::size_t>(blockDim.x) * gridDim.x;
  for (std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x +
                       threadIdx.x;
       i < n; i += paso) {
    atomicAdd(&bins[binDeDuracion(duraciones[i])], 1ULL);
  }
}

// Inspirado en 07_histogram/04_private_shared.cu:
// privatizar los 61 contadores por bloque, despues combinarlos globalmente.
__global__ void histogramaPrivadoCompartido(
    const long long* duraciones, std::size_t n, unsigned long long* bins) {
  __shared__ unsigned int privados[kBins];
  for (int b = threadIdx.x; b < kBins; b += blockDim.x) privados[b] = 0;
  __syncthreads();

  const std::size_t paso = static_cast<std::size_t>(blockDim.x) * gridDim.x;
  for (std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x +
                       threadIdx.x;
       i < n; i += paso) {
    atomicAdd(&privados[binDeDuracion(duraciones[i])], 1u);
  }
  __syncthreads();

  for (int b = threadIdx.x; b < kBins; b += blockDim.x) {
    if (privados[b] != 0u) atomicAdd(&bins[b], static_cast<unsigned long long>(privados[b]));
  }
}

struct RecursosCuda {
  long long* datos = nullptr;
  unsigned long long* bins = nullptr;
  cudaEvent_t inicio = nullptr;
  cudaEvent_t fin = nullptr;

  ~RecursosCuda() {
    if (fin) (void)cudaEventDestroy(fin);
    if (inicio) (void)cudaEventDestroy(inicio);
    if (bins) (void)cudaFree(bins);
    if (datos) (void)cudaFree(datos);
  }
};

}  // namespace

ResultadoHistogramaGpu ejecutarHistogramaEnGpu(
    const std::vector<long long>& duracionesMs, VarianteHistogramaGpu variante) {
  ResultadoHistogramaGpu resultado;
  if (duracionesMs.empty()) return resultado;
  if (duracionesMs.size() > static_cast<std::size_t>(INT_MAX)) {
    throw std::invalid_argument("Demasiadas duraciones para este experimento");
  }
  for (const auto duracion : duracionesMs) {
    if (duracion < 0) throw std::invalid_argument("Duracion de entrega negativa");
  }

  using Reloj = std::chrono::steady_clock;
  const auto t0 = Reloj::now();
  RecursosCuda gpu;
  const std::size_t bytesDatos = duracionesMs.size() * sizeof(long long);
  const std::size_t bytesBins = kBins * sizeof(unsigned long long);
  verificar(cudaSetDevice(0), "cudaSetDevice");
  cudaDeviceProp propiedades{};
  verificar(cudaGetDeviceProperties(&propiedades, 0), "cudaGetDeviceProperties");
  const int bloques = static_cast<int>(std::min<std::size_t>(
      (duracionesMs.size() + kHilos - 1) / kHilos,
      static_cast<std::size_t>(propiedades.multiProcessorCount) * 8));
  verificar(cudaMalloc(reinterpret_cast<void**>(&gpu.datos), bytesDatos), "cudaMalloc(datos)");
  verificar(cudaMalloc(reinterpret_cast<void**>(&gpu.bins), bytesBins), "cudaMalloc(bins)");
  verificar(cudaMemcpy(gpu.datos, duracionesMs.data(), bytesDatos,
                      cudaMemcpyHostToDevice), "cudaMemcpy(H2D)");
  verificar(cudaMemset(gpu.bins, 0, bytesBins), "cudaMemset");
  verificar(cudaEventCreate(&gpu.inicio), "cudaEventCreate(inicio)");
  verificar(cudaEventCreate(&gpu.fin), "cudaEventCreate(fin)");

  verificar(cudaEventRecord(gpu.inicio), "cudaEventRecord(inicio)");
  if (variante == VarianteHistogramaGpu::AtomicosGlobales) {
    histogramaAtomicosGlobales<<<bloques, kHilos>>>(
        gpu.datos, duracionesMs.size(), gpu.bins);
  } else {
    histogramaPrivadoCompartido<<<bloques, kHilos>>>(
        gpu.datos, duracionesMs.size(), gpu.bins);
  }
  verificar(cudaGetLastError(), "lanzamiento histograma");
  verificar(cudaEventRecord(gpu.fin), "cudaEventRecord(fin)");
  verificar(cudaEventSynchronize(gpu.fin), "cudaEventSynchronize");
  float kernelMs = 0.0f;
  verificar(cudaEventElapsedTime(&kernelMs, gpu.inicio, gpu.fin),
           "cudaEventElapsedTime");

  std::array<unsigned long long, kBins> hostBins{};
  verificar(cudaMemcpy(hostBins.data(), gpu.bins, bytesBins,
                      cudaMemcpyDeviceToHost), "cudaMemcpy(D2H)");
  // Si se asigna memoria por llamada, su coste debe estar en el tiempo total.
  // Se libera explicítamente antes de parar el reloj; el destructor queda a salvo.
  verificar(cudaFree(gpu.datos), "cudaFree(datos)");
  gpu.datos = nullptr;
  verificar(cudaFree(gpu.bins), "cudaFree(bins)");
  gpu.bins = nullptr;
  verificar(cudaEventDestroy(gpu.inicio), "cudaEventDestroy(inicio)");
  gpu.inicio = nullptr;
  verificar(cudaEventDestroy(gpu.fin), "cudaEventDestroy(fin)");
  gpu.fin = nullptr;
  const auto t1 = Reloj::now();

  for (std::size_t b = 0; b < kHistogramaBins; ++b) resultado.bins[b] = hostBins[b];
  resultado.kernelMs = static_cast<double>(kernelMs);
  resultado.totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
  return resultado;
}

}  // namespace equipego::analytics
