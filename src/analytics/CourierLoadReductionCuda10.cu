#include "analytics/CourierLoadReductionGpu10.hpp"
#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace equipego::analytics {
namespace {
constexpr unsigned HILOS10 = 256;
constexpr unsigned MAX_BLOQUES10 = 256;

struct Parcial10 {
  unsigned long long total;
  unsigned long long conPedidos;
  unsigned maximo;
  unsigned indice;
  unsigned presente;
};

__device__ __forceinline__ Parcial10 unir10(Parcial10 a, Parcial10 b) {
  a.total += b.total;
  a.conPedidos += b.conPedidos;
  if (b.presente && (!a.presente || b.maximo > a.maximo ||
      (b.maximo == a.maximo && b.indice < a.indice))) {
    a.maximo = b.maximo;
    a.indice = b.indice;
    a.presente = 1;
  }
  return a;
}

__global__ void reducirBloquesCarga10(const std::uint32_t* datos,
                                      unsigned n, Parcial10* parciales) {
  __shared__ Parcial10 s[HILOS10];
  const unsigned tid = threadIdx.x;
  const unsigned salto = blockDim.x * gridDim.x;
  const unsigned inicio = blockIdx.x * blockDim.x + tid;
  Parcial10 local{0ULL, 0ULL, 0u, 0u, 0u};
  for (unsigned long long i = inicio; i < n; i += salto) {
    const unsigned v = datos[i];
    local.total += v;
    local.conPedidos += static_cast<unsigned>(v > 0);
    if (!local.presente || v > local.maximo ||
        (v == local.maximo && i < local.indice)) {
      local.maximo = v;
      local.indice = static_cast<unsigned>(i);
      local.presente = 1;
    }
  }
  s[tid] = local;
  __syncthreads();
  for (unsigned distancia = HILOS10 / 2; distancia; distancia >>= 1) {
    if (tid < distancia) s[tid] = unir10(s[tid], s[tid + distancia]);
    __syncthreads();
  }
  if (tid == 0) parciales[blockIdx.x] = s[0];
}

__global__ void reducirFinalCarga10(const Parcial10* parciales,
                                    unsigned n, Parcial10* salida) {
  __shared__ Parcial10 s[HILOS10];
  const unsigned tid = threadIdx.x;
  s[tid] = tid < n ? parciales[tid] : Parcial10{0ULL,0ULL,0u,0u,0u};
  __syncthreads();
  for (unsigned distancia = HILOS10 / 2; distancia; distancia >>= 1) {
    if (tid < distancia) s[tid] = unir10(s[tid], s[tid + distancia]);
    __syncthreads();
  }
  if (tid == 0) *salida = s[0];
}

void comprobar10(cudaError_t err, const char* operacion) {
  if (err != cudaSuccess)
    throw std::runtime_error(std::string("FEATURE10_CUDA: ") + operacion +
                             ": " + cudaGetErrorString(err));
}

class Recursos10 {
 public:
  std::uint32_t* entrada = nullptr;
  Parcial10* parciales = nullptr;
  Parcial10* resultado = nullptr;
  cudaEvent_t comienzo = nullptr;
  cudaEvent_t fin = nullptr;
  Recursos10() = default;
  Recursos10(const Recursos10&) = delete;
  Recursos10& operator=(const Recursos10&) = delete;
  ~Recursos10() {
    if (fin) cudaEventDestroy(fin);
    if (comienzo) cudaEventDestroy(comienzo);
    if (resultado) cudaFree(resultado);
    if (parciales) cudaFree(parciales);
    if (entrada) cudaFree(entrada);
  }
  void inicializarEventos() {
    comprobar10(cudaEventCreate(&comienzo), "cudaEventCreate comienzo");
    comprobar10(cudaEventCreate(&fin), "cudaEventCreate fin");
  }
  void liberar() {
    comprobar10(cudaEventDestroy(fin), "cudaEventDestroy fin"); fin = nullptr;
    comprobar10(cudaEventDestroy(comienzo), "cudaEventDestroy comienzo");
    comienzo = nullptr;
    comprobar10(cudaFree(resultado), "cudaFree resultado"); resultado = nullptr;
    comprobar10(cudaFree(parciales), "cudaFree parciales"); parciales = nullptr;
    comprobar10(cudaFree(entrada), "cudaFree entrada"); entrada = nullptr;
  }
  void iniciar() { comprobar10(cudaEventRecord(comienzo), "cudaEventRecord comienzo"); }
  double detener() {
    comprobar10(cudaEventRecord(fin), "cudaEventRecord fin");
    comprobar10(cudaEventSynchronize(fin), "cudaEventSynchronize fin");
    float ms = 0.0f;
    comprobar10(cudaEventElapsedTime(&ms, comienzo, fin), "cudaEventElapsedTime");
    return ms;
  }
};
} // namespace

ResultadoCargaGpu10 ejecutarCargaCouriersGpu10(
    const std::vector<std::uint32_t>& reservas) {
  ResultadoCargaGpu10 out;
  if (reservas.empty()) return out;
  if (reservas.size() > std::numeric_limits<unsigned>::max())
    throw std::length_error("FEATURE10_CUDA: mas de UINT_MAX elementos");
  const auto t0 = std::chrono::steady_clock::now();
  const unsigned n = static_cast<unsigned>(reservas.size());
  const unsigned bloques = std::min(MAX_BLOQUES10,
      static_cast<unsigned>((static_cast<unsigned long long>(n) + HILOS10 - 1) /
                            HILOS10));
  Recursos10 r;
  comprobar10(cudaSetDevice(0), "cudaSetDevice");
  comprobar10(cudaMalloc(reinterpret_cast<void**>(&r.entrada),
                         static_cast<std::size_t>(n) * sizeof(std::uint32_t)),
             "cudaMalloc entrada");
  comprobar10(cudaMalloc(reinterpret_cast<void**>(&r.parciales),
                         bloques * sizeof(Parcial10)), "cudaMalloc parciales");
  comprobar10(cudaMalloc(reinterpret_cast<void**>(&r.resultado), sizeof(Parcial10)),
             "cudaMalloc resultado");
  r.inicializarEventos();

  r.iniciar();
  comprobar10(cudaMemcpy(r.entrada, reservas.data(),
      static_cast<std::size_t>(n) * sizeof(std::uint32_t),
      cudaMemcpyHostToDevice), "cudaMemcpy H2D");
  out.h2dMs = r.detener();

  r.iniciar();
  reducirBloquesCarga10<<<bloques, HILOS10>>>(r.entrada, n, r.parciales);
  comprobar10(cudaGetLastError(), "reducirBloquesCarga10");
  reducirFinalCarga10<<<1, HILOS10>>>(r.parciales, bloques, r.resultado);
  comprobar10(cudaGetLastError(), "reducirFinalCarga10");
  out.kernelMs = r.detener();

  Parcial10 host{0ULL,0ULL,0u,0u,0u};
  r.iniciar();
  comprobar10(cudaMemcpy(&host, r.resultado, sizeof(host),
                         cudaMemcpyDeviceToHost), "cudaMemcpy D2H");
  out.d2hMs = r.detener();
  out.resumen.couriers = reservas.size();
  out.resumen.conPedidos = static_cast<std::size_t>(host.conPedidos);
  out.resumen.totalReservas = static_cast<std::uint64_t>(host.total);
  out.resumen.maxReservas = host.maximo;
  out.resumen.indiceMax = host.indice;
  out.resumen.tieneMax = host.presente != 0;
  r.liberar();
  out.totalMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - t0).count();
  return out;
}

} // namespace equipego::analytics
