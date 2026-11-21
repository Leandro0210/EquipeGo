#include "analytics/RestaurantDemandHistogramGpu11.hpp"
#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace equipego::analytics {
namespace {
constexpr unsigned kHilos11 = 256;
constexpr unsigned kMaxBloques11 = 256;
constexpr unsigned kMaxCeldas11 = 1024;
static_assert(sizeof(PedidoDemanda11) == sizeof(std::uint32_t) * 2,
              "PedidoDemanda11 debe ocupar 8 bytes");
static_assert(std::is_trivially_copyable<PedidoDemanda11>::value,
              "PedidoDemanda11 debe ser trivialmente copiable");

__global__ void histogramaPrivadoCompartido11(
    const PedidoDemanda11* pedidos, unsigned n, unsigned franjas,
    unsigned celdas, unsigned* parciales) {
  extern __shared__ unsigned privado[];
  const unsigned tid = threadIdx.x;
  for (unsigned b = tid; b < celdas; b += blockDim.x)
    privado[b] = 0u;
  __syncthreads();
  const unsigned long long inicio =
      static_cast<unsigned long long>(blockIdx.x) * blockDim.x + tid;
  const unsigned long long paso =
      static_cast<unsigned long long>(blockDim.x) * gridDim.x;
  for (unsigned long long i = inicio; i < n; i += paso) {
    const PedidoDemanda11 e = pedidos[i];
    const unsigned indice = e.restaurante * franjas + e.franja;
    atomicAdd(&privado[indice], 1u);
  }
  __syncthreads();
  for (unsigned b = tid; b < celdas; b += blockDim.x)
    parciales[static_cast<std::size_t>(blockIdx.x) * celdas + b] = privado[b];
}

__global__ void combinarHistograma11(
    const unsigned* parciales, unsigned bloques, unsigned celdas,
    unsigned long long* resultado) {
  const unsigned bin = blockIdx.x * blockDim.x + threadIdx.x;
  if (bin >= celdas) return;
  unsigned long long suma = 0;
  for (unsigned b = 0; b < bloques; ++b)
    suma += parciales[static_cast<std::size_t>(b) * celdas + bin];
  resultado[bin] = suma;
}

void verificar11(cudaError_t error, const char* nombre) {
  if (error != cudaSuccess)
    throw std::runtime_error(std::string("FEATURE11 CUDA: ") + nombre +
                             ": " + cudaGetErrorString(error));
}

struct Recursos11 {
  PedidoDemanda11* pedidos = nullptr;
  unsigned* parciales = nullptr;
  unsigned long long* celdas = nullptr;
  cudaEvent_t start = nullptr, stop = nullptr;
  Recursos11() = default;
  Recursos11(const Recursos11&) = delete;
  Recursos11& operator=(const Recursos11&) = delete;
  ~Recursos11() {
    if (stop) cudaEventDestroy(stop);
    if (start) cudaEventDestroy(start);
    if (celdas) cudaFree(celdas);
    if (parciales) cudaFree(parciales);
    if (pedidos) cudaFree(pedidos);
  }
  void eventos() {
    verificar11(cudaEventCreate(&start), "create start");
    verificar11(cudaEventCreate(&stop), "create stop");
  }
  void comenzar() { verificar11(cudaEventRecord(start), "event start"); }
  double terminar() {
    verificar11(cudaEventRecord(stop), "event stop");
    verificar11(cudaEventSynchronize(stop), "event sync");
    float ms = 0.0f;
    verificar11(cudaEventElapsedTime(&ms, start, stop), "event elapsed");
    return ms;
  }
};
} // namespace

ResultadoDemandaGpu11 ejecutarHistogramaDemandaGpu11(
    const std::vector<PedidoDemanda11>& pedidos,
    std::size_t restaurantes, std::size_t franjas) {
  const auto t0 = std::chrono::steady_clock::now();
  if (restaurantes == 0 || franjas == 0 ||
      restaurantes > kMaxCeldas11 / franjas)
    throw std::invalid_argument(
        "Feature11 CUDA: numero de celdas debe estar entre 1 y 1024");
  if (pedidos.size() > std::numeric_limits<unsigned>::max())
    throw std::length_error("Feature11 CUDA: demasiados pedidos");
  validarPedidosDemanda11(pedidos, restaurantes, franjas);
  ResultadoDemandaGpu11 out;
  out.histograma = crearHistogramaDemanda11(restaurantes, franjas);
  if (pedidos.empty()) {
    out.totalMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    return out;
  }
  const unsigned n = static_cast<unsigned>(pedidos.size());
  const unsigned bins = static_cast<unsigned>(restaurantes * franjas);
  const unsigned bloques = static_cast<unsigned>(std::min<std::size_t>(
      kMaxBloques11, (pedidos.size() + kHilos11 - 1) / kHilos11));
  Recursos11 r;
  verificar11(cudaSetDevice(0), "set device");
  verificar11(cudaMalloc(reinterpret_cast<void**>(&r.pedidos),
      pedidos.size() * sizeof(PedidoDemanda11)), "malloc pedidos");
  verificar11(cudaMalloc(reinterpret_cast<void**>(&r.parciales),
      static_cast<std::size_t>(bloques) * bins * sizeof(unsigned)),
      "malloc parciales");
  verificar11(cudaMalloc(reinterpret_cast<void**>(&r.celdas),
      static_cast<std::size_t>(bins) * sizeof(unsigned long long)),
      "malloc resultado");
  r.eventos();
  r.comenzar();
  verificar11(cudaMemcpy(r.pedidos, pedidos.data(),
      pedidos.size() * sizeof(PedidoDemanda11), cudaMemcpyHostToDevice),
      "memcpy H2D");
  out.h2dMs = r.terminar();
  r.comenzar();
  histogramaPrivadoCompartido11<<<bloques, kHilos11,
      static_cast<std::size_t>(bins) * sizeof(unsigned)>>>(
      r.pedidos, n, static_cast<unsigned>(franjas), bins, r.parciales);
  verificar11(cudaGetLastError(), "histogramaPrivadoCompartido11");
  combinarHistograma11<<<(bins + kHilos11 - 1) / kHilos11, kHilos11>>>(
      r.parciales, bloques, bins, r.celdas);
  verificar11(cudaGetLastError(), "combinarHistograma11");
  out.kernelMs = r.terminar();
  r.comenzar();
  verificar11(cudaMemcpy(out.histograma.celdas.data(), r.celdas,
      static_cast<std::size_t>(bins) * sizeof(std::uint64_t),
      cudaMemcpyDeviceToHost), "memcpy D2H");
  out.d2hMs = r.terminar();
  for (std::size_t ri = 0; ri < restaurantes; ++ri) {
    for (std::size_t fi = 0; fi < franjas; ++fi)
      out.histograma.porRestaurante[ri] +=
          out.histograma.celdas[ri * franjas + fi];
    out.histograma.totalPedidos += out.histograma.porRestaurante[ri];
  }
  // Se contabilizan reservas/liberaciones dentro de totalMs.
  // Recursos11 libera RAII al salir del bloque interno (antes de medir total).
  // La destruccion de eventos y buffers queda registrada como overhead total.
  verificar11(cudaEventDestroy(r.stop), "destroy stop"); r.stop = nullptr;
  verificar11(cudaEventDestroy(r.start), "destroy start"); r.start = nullptr;
  verificar11(cudaFree(r.celdas), "free celdas"); r.celdas = nullptr;
  verificar11(cudaFree(r.parciales), "free parciales"); r.parciales = nullptr;
  verificar11(cudaFree(r.pedidos), "free pedidos"); r.pedidos = nullptr;
  out.totalMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - t0).count();
  return out;
}
} // namespace equipego::analytics
