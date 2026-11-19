#include "analytics/EtaReductionGpu.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace equipego::analytics {
namespace {

constexpr int kHilos = 256;

// Estructura uniforme CPU/GPU: sin pointers host, strings ni tipos STL.
struct CandidatoGpu {
  double eta;
  unsigned long long indice;
  int valido;
};

struct MejorGpu {
  double eta;
  unsigned long long indice;
  int valido;
};

__device__ __forceinline__ bool gana(const MejorGpu& a, const MejorGpu& b) {
  if (!a.valido) return false;
  if (!b.valido) return true;
  return a.eta < b.eta || (a.eta == b.eta && a.indice < b.indice);
}

__global__ void reducirBloques(const CandidatoGpu* datos,
                               unsigned long long n, MejorGpu* parciales) {
  __shared__ MejorGpu local[kHilos];
  const unsigned int tid = threadIdx.x;
  const unsigned long long comienzo =
      static_cast<unsigned long long>(blockIdx.x) * blockDim.x + tid;
  const unsigned long long paso =
      static_cast<unsigned long long>(gridDim.x) * blockDim.x;
  MejorGpu mejor{0.0, 0ULL, 0};
  for (unsigned long long i = comienzo; i < n; i += paso) {
    const CandidatoGpu c = datos[i];
    if (c.valido) {
      const MejorGpu actual{c.eta, c.indice, 1};
      if (gana(actual, mejor)) mejor = actual;
    }
  }
  local[tid] = mejor;
  __syncthreads();
  for (unsigned int salto = kHilos / 2; salto > 0; salto >>= 1) {
    if (tid < salto && gana(local[tid + salto], local[tid])) {
      local[tid] = local[tid + salto];
    }
    __syncthreads();
  }
  if (tid == 0) parciales[blockIdx.x] = local[0];
}

__global__ void reducirFinal(const MejorGpu* parciales,
                             unsigned int n, MejorGpu* resultado) {
  __shared__ MejorGpu local[kHilos];
  const unsigned int tid = threadIdx.x;
  MejorGpu mejor{0.0, 0ULL, 0};
  for (unsigned int i = tid; i < n; i += blockDim.x) {
    if (gana(parciales[i], mejor)) mejor = parciales[i];
  }
  local[tid] = mejor;
  __syncthreads();
  for (unsigned int salto = kHilos / 2; salto > 0; salto >>= 1) {
    if (tid < salto && gana(local[tid + salto], local[tid])) {
      local[tid] = local[tid + salto];
    }
    __syncthreads();
  }
  if (tid == 0) resultado[0] = local[0];
}

void verificar(cudaError_t valor, const char* llamada) {
  if (valor != cudaSuccess) {
    throw std::runtime_error(std::string(llamada) + ": " + cudaGetErrorString(valor));
  }
}

struct Recursos {
  CandidatoGpu* datos = nullptr;
  MejorGpu* parciales = nullptr;
  MejorGpu* resultado = nullptr;
  cudaEvent_t inicio = nullptr;
  cudaEvent_t fin = nullptr;
  ~Recursos() {
    if (fin) cudaEventDestroy(fin);
    if (inicio) cudaEventDestroy(inicio);
    if (resultado) cudaFree(resultado);
    if (parciales) cudaFree(parciales);
    if (datos) cudaFree(datos);
  }
};

float medir(cudaEvent_t inicio, cudaEvent_t fin) {
  verificar(cudaEventRecord(fin), "cudaEventRecord(fin)");
  verificar(cudaEventSynchronize(fin), "cudaEventSynchronize(fin)");
  float ms = 0.0f;
  verificar(cudaEventElapsedTime(&ms, inicio, fin), "cudaEventElapsedTime");
  return ms;
}

}  // namespace

ResultadoReduccionGpu ejecutarReduccionEtaEnGpu(
    const std::vector<EtaCandidato>& candidatos) {
  ResultadoReduccionGpu salida;
  if (candidatos.empty()) return salida;
  if (candidatos.size() > static_cast<std::size_t>(INT_MAX)) {
    throw std::invalid_argument("Demasiados candidatos para reduction CUDA 08");
  }

  using Reloj = std::chrono::steady_clock;
  const auto tiempo0 = Reloj::now();
  std::vector<CandidatoGpu> entrada;
  entrada.reserve(candidatos.size());
  for (const EtaCandidato& c : candidatos) {
    entrada.push_back(CandidatoGpu{c.etaSimulatedSeconds,
                                   static_cast<unsigned long long>(c.courierIndex),
                                   etaValida(c) ? 1 : 0});
  }

  verificar(cudaSetDevice(0), "cudaSetDevice");
  cudaDeviceProp dispositivo{};
  verificar(cudaGetDeviceProperties(&dispositivo, 0), "cudaGetDeviceProperties");
  const unsigned long long n = entrada.size();
  const unsigned long long bloquesNecesarios = (n + kHilos - 1) / kHilos;
  const unsigned long long maxBloques =
      static_cast<unsigned long long>(std::max(1, dispositivo.multiProcessorCount)) * 8ULL;
  const unsigned int bloques = static_cast<unsigned int>(
      std::min(bloquesNecesarios, maxBloques));
  Recursos r;
  verificar(cudaMalloc(reinterpret_cast<void**>(&r.datos),
                       entrada.size() * sizeof(CandidatoGpu)), "cudaMalloc(datos)");
  verificar(cudaMalloc(reinterpret_cast<void**>(&r.parciales),
                       bloques * sizeof(MejorGpu)), "cudaMalloc(parciales)");
  verificar(cudaMalloc(reinterpret_cast<void**>(&r.resultado),
                       sizeof(MejorGpu)), "cudaMalloc(resultado)");
  verificar(cudaEventCreate(&r.inicio), "cudaEventCreate(inicio)");
  verificar(cudaEventCreate(&r.fin), "cudaEventCreate(fin)");

  verificar(cudaEventRecord(r.inicio), "cudaEventRecord(H2D)");
  verificar(cudaMemcpy(r.datos, entrada.data(), entrada.size() * sizeof(CandidatoGpu),
                      cudaMemcpyHostToDevice), "cudaMemcpy(H2D)");
  salida.h2dMs = medir(r.inicio, r.fin);

  verificar(cudaEventRecord(r.inicio), "cudaEventRecord(kernel)");
  reducirBloques<<<bloques, kHilos>>>(r.datos, n, r.parciales);
  verificar(cudaGetLastError(), "reducirBloques");
  reducirFinal<<<1, kHilos>>>(r.parciales, bloques, r.resultado);
  verificar(cudaGetLastError(), "reducirFinal");
  salida.kernelMs = medir(r.inicio, r.fin);

  MejorGpu host{0.0, 0ULL, 0};
  verificar(cudaEventRecord(r.inicio), "cudaEventRecord(D2H)");
  verificar(cudaMemcpy(&host, r.resultado, sizeof(host),
                      cudaMemcpyDeviceToHost), "cudaMemcpy(D2H)");
  salida.d2hMs = medir(r.inicio, r.fin);

  // El total incluye las liberaciones y el costo de crear eventos.
  verificar(cudaEventDestroy(r.inicio), "cudaEventDestroy(inicio)");
  r.inicio = nullptr;
  verificar(cudaEventDestroy(r.fin), "cudaEventDestroy(fin)");
  r.fin = nullptr;
  verificar(cudaFree(r.datos), "cudaFree(datos)");
  r.datos = nullptr;
  verificar(cudaFree(r.parciales), "cudaFree(parciales)");
  r.parciales = nullptr;
  verificar(cudaFree(r.resultado), "cudaFree(resultado)");
  r.resultado = nullptr;

  if (host.valido) {
    salida.mejor = EtaCandidato{
        static_cast<std::size_t>(host.indice), host.eta, true};
  }
  salida.totalMs = std::chrono::duration<double, std::milli>(
      Reloj::now() - tiempo0).count();
  return salida;
}

}  // namespace equipego::analytics
