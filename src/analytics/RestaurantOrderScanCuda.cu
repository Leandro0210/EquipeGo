#include "analytics/RestaurantOrderScanGpu.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace equipego::analytics {
namespace {

constexpr unsigned kHilos = 256;
// Dos matrices de uint32_t: conteo y prefijos por bloque/restaurante.
// Limita el volumen temporal antes de reservar memoria GPU.
constexpr std::size_t kMaxCeldasBloquesRestaurante = 50000000ULL;

void verificar(cudaError_t estado, const char* operacion) {
  if (estado != cudaSuccess) {
    throw std::runtime_error(std::string(operacion) + ": " +
                             cudaGetErrorString(estado));
  }
}

template <class T>
class MemoriaGpu {
 public:
  MemoriaGpu() = default;
  MemoriaGpu(const MemoriaGpu&) = delete;
  MemoriaGpu& operator=(const MemoriaGpu&) = delete;
  ~MemoriaGpu() {
    if (p_) (void)cudaFree(p_);
  }
  void reservar(std::size_t n) {
    if (n == 0) return;
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::length_error("Scan09: numero de bytes desbordado");
    }
    verificar(cudaMalloc(reinterpret_cast<void**>(&p_), n * sizeof(T)),
             "cudaMalloc Scan09");
  }
  T* get() const { return p_; }
  void liberar() {
    if (p_) {
      verificar(cudaFree(p_), "cudaFree Scan09");
      p_ = nullptr;
    }
  }
 private:
  T* p_ = nullptr;
};

struct Eventos {
  cudaEvent_t a = nullptr;
  cudaEvent_t b = nullptr;
  Eventos() {
    verificar(cudaEventCreate(&a), "cudaEventCreate(a)");
    try {
      verificar(cudaEventCreate(&b), "cudaEventCreate(b)");
    } catch (...) {
      (void)cudaEventDestroy(a);
      a = nullptr;
      throw;
    }
  }
  ~Eventos() {
    if (b) (void)cudaEventDestroy(b);
    if (a) (void)cudaEventDestroy(a);
  }
  void liberar() {
    if (b) {
      verificar(cudaEventDestroy(b), "cudaEventDestroy(b)");
      b = nullptr;
    }
    if (a) {
      verificar(cudaEventDestroy(a), "cudaEventDestroy(a)");
      a = nullptr;
    }
  }
  void comienzo() { verificar(cudaEventRecord(a), "cudaEventRecord(a)"); }
  double fin() {
    verificar(cudaEventRecord(b), "cudaEventRecord(b)");
    verificar(cudaEventSynchronize(b), "cudaEventSynchronize(b)");
    float ms = 0;
    verificar(cudaEventElapsedTime(&ms, a, b), "cudaEventElapsedTime");
    return static_cast<double>(ms);
  }
};

// Cada bloque tiene su propia fila de histogramas: no hay atomicos globales
// compitiendo entre bloques, ni depende del orden de ejecucion de los bloques.
__global__ void conteosPorBloque09(const unsigned* ids, unsigned n,
                                   unsigned restaurantes,
                                   unsigned* cuentas) {
  const unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) {
    atomicAdd(cuentas + static_cast<std::size_t>(blockIdx.x) * restaurantes +
              ids[i], 1u);
  }
}

// Para cada restaurante, fija el offset estable de cada bloque en orden de
// llegada. Produce tambien los conteos totales para el scan exclusivo.
__global__ void prefijosDeBloquesPorRestaurante09(
    const unsigned* cuentas, unsigned* bloquesInicio,
    unsigned* totales, unsigned bloques, unsigned restaurantes) {
  const unsigned r = blockIdx.x * blockDim.x + threadIdx.x;
  if (r >= restaurantes) return;
  unsigned total = 0;
  for (unsigned b = 0; b < bloques; ++b) {
    const std::size_t pos = static_cast<std::size_t>(b) * restaurantes + r;
    bloquesInicio[pos] = total;
    total += cuentas[pos];
  }
  totales[r] = total;
}

// Scan exclusivo por segmento de 256. Hillis-Steele con barreras para evitar
// lecturas y escrituras simultaneas sobre posiciones de memoria compartida.
__global__ void escanearBloques09(const unsigned* entrada,
                                  unsigned* salida, unsigned* sumas,
                                  unsigned n) {
  __shared__ unsigned s[kHilos];
  const unsigned t = threadIdx.x;
  const unsigned i = blockIdx.x * blockDim.x + t;
  const unsigned valor = i < n ? entrada[i] : 0u;
  s[t] = valor;
  __syncthreads();
  for (unsigned desplazamiento = 1; desplazamiento < kHilos;
       desplazamiento <<= 1) {
    const unsigned previo = (t >= desplazamiento) ? s[t - desplazamiento] : 0u;
    __syncthreads();
    s[t] += previo;
    __syncthreads();
  }
  if (i < n) salida[i] = s[t] - valor;
  if (sumas && t == kHilos - 1) sumas[blockIdx.x] = s[t];
}

__global__ void sumarOffsets09(unsigned* datos, const unsigned* prefijos,
                               unsigned n) {
  const unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) datos[i] += prefijos[blockIdx.x];
}

// Recursion sobre los totales por bloque: resuelve R>256 (R arbitrario dentro
// del rango uint32). Cada nivel disminuye el tamano por un factor 256.
void scanJerarquico09(const unsigned* entrada, unsigned* salida,
                      unsigned n, std::vector<void*>& temporales) {
  const unsigned bloques = (n - 1u) / kHilos + 1u;
  unsigned *sumas = nullptr, *prefijos = nullptr;
  if (bloques > 1) {
    verificar(cudaMalloc(reinterpret_cast<void**>(&sumas),
                        static_cast<std::size_t>(bloques) * sizeof(unsigned)),
             "cudaMalloc scan sums");
    temporales.push_back(sumas);
    verificar(cudaMalloc(reinterpret_cast<void**>(&prefijos),
                        static_cast<std::size_t>(bloques) * sizeof(unsigned)),
             "cudaMalloc scan prefixes");
    temporales.push_back(prefijos);
  }
  escanearBloques09<<<bloques, kHilos>>>(entrada, salida, sumas, n);
  verificar(cudaGetLastError(), "escanearBloques09");
  if (bloques > 1) {
    scanJerarquico09(sumas, prefijos, bloques, temporales);
    sumarOffsets09<<<bloques, kHilos>>>(salida, prefijos, n);
    verificar(cudaGetLastError(), "sumarOffsets09");
  }
}

// Dispersa indices originales. Cada hilo calcula su rango dentro del bloque
// en memoria compartida; bloques anteriores se contabilizan por restaurante.
// No se usan atomicos para la dispersion: orden determinista y ESTABLE.
__global__ void dispersarEstable09(const unsigned* ids, unsigned n,
                                   unsigned restaurantes,
                                   const unsigned* inicios,
                                   const unsigned* bloquesInicio,
                                   unsigned* indices) {
  __shared__ unsigned local[kHilos];
  const unsigned tid = threadIdx.x;
  const unsigned i = blockIdx.x * blockDim.x + tid;
  const unsigned r = i < n ? ids[i] : 0xffffffffu;
  local[tid] = r;
  __syncthreads();
  if (i >= n) return;
  unsigned rangoLocal = 0;
  for (unsigned j = 0; j < tid; ++j) {
    if (local[j] == r) ++rangoLocal;
  }
  const unsigned salida = inicios[r] +
      bloquesInicio[static_cast<std::size_t>(blockIdx.x) * restaurantes + r] +
      rangoLocal;
  indices[salida] = i;
}

struct Temporales {
  std::vector<void*> ptrs;
  ~Temporales() {
    for (auto it = ptrs.rbegin(); it != ptrs.rend(); ++it) {
      (void)cudaFree(*it);
    }
  }
  void liberar() {
    for (auto it = ptrs.rbegin(); it != ptrs.rend(); ++it) {
      verificar(cudaFree(*it), "cudaFree jerarquia Scan09");
    }
    ptrs.clear();
  }
};

}  // namespace

ResultadoScanGpu09 ejecutarAgrupacionScanGpu09(
    const std::vector<std::size_t>& restaurantIds,
    std::size_t restaurantCount) {
  validarEntradasScan09(restaurantIds, restaurantCount);
  ResultadoScanGpu09 salida;
  salida.agrupacion.starts.assign(restaurantCount + 1, 0);
  if (restaurantIds.empty()) return salida;
  if (restaurantCount > std::numeric_limits<unsigned>::max() ||
      restaurantIds.size() > std::numeric_limits<unsigned>::max()) {
    throw std::length_error("Scan09 CUDA solo admite tamanos <= UINT_MAX");
  }

  const auto tiempo0 = std::chrono::steady_clock::now();
  const unsigned n = static_cast<unsigned>(restaurantIds.size());
  const unsigned restaurantes = static_cast<unsigned>(restaurantCount);
  const unsigned bloques = (n - 1u) / kHilos + 1u;
  if (restaurantCount > kMaxCeldasBloquesRestaurante / bloques) {
    throw std::length_error("Scan09: histograma por bloques demasiado grande");
  }
  const std::size_t celdas = static_cast<std::size_t>(bloques) * restaurantes;
  std::vector<unsigned> hostIds;
  hostIds.reserve(n);
  for (auto id : restaurantIds) hostIds.push_back(static_cast<unsigned>(id));

  verificar(cudaSetDevice(0), "cudaSetDevice Scan09");
  MemoriaGpu<unsigned> dIds, dCuentas, dBloquesInicio, dTotales, dInicios, dIndices;
  dIds.reservar(n);
  dCuentas.reservar(celdas);
  dBloquesInicio.reservar(celdas);
  dTotales.reservar(restaurantes);
  dInicios.reservar(restaurantes);
  dIndices.reservar(n);
  Eventos eventos;
  Temporales temporales;

  eventos.comienzo();
  verificar(cudaMemcpy(dIds.get(), hostIds.data(),
                      static_cast<std::size_t>(n) * sizeof(unsigned),
                      cudaMemcpyHostToDevice), "cudaMemcpy H2D Scan09");
  salida.h2dMs = eventos.fin();

  eventos.comienzo();
  verificar(cudaMemset(dCuentas.get(), 0, celdas * sizeof(unsigned)),
           "cudaMemset cuentas Scan09");
  conteosPorBloque09<<<bloques, kHilos>>>(dIds.get(), n, restaurantes,
                                         dCuentas.get());
  verificar(cudaGetLastError(), "conteosPorBloque09");
  const unsigned bloquesRest = (restaurantes - 1u) / kHilos + 1u;
  prefijosDeBloquesPorRestaurante09<<<bloquesRest, kHilos>>>(
      dCuentas.get(), dBloquesInicio.get(), dTotales.get(),
      bloques, restaurantes);
  verificar(cudaGetLastError(), "prefijosDeBloquesPorRestaurante09");
  scanJerarquico09(dTotales.get(), dInicios.get(), restaurantes,
                    temporales.ptrs);
  dispersarEstable09<<<bloques, kHilos>>>(
      dIds.get(), n, restaurantes, dInicios.get(), dBloquesInicio.get(),
      dIndices.get());
  verificar(cudaGetLastError(), "dispersarEstable09");
  salida.kernelMs = eventos.fin();

  std::vector<unsigned> hostInicios(restaurantes);
  std::vector<unsigned> hostIndices(n);
  eventos.comienzo();
  verificar(cudaMemcpy(hostInicios.data(), dInicios.get(),
                      static_cast<std::size_t>(restaurantes) * sizeof(unsigned),
                      cudaMemcpyDeviceToHost), "cudaMemcpy D2H starts Scan09");
  verificar(cudaMemcpy(hostIndices.data(), dIndices.get(),
                      static_cast<std::size_t>(n) * sizeof(unsigned),
                      cudaMemcpyDeviceToHost), "cudaMemcpy D2H indices Scan09");
  salida.d2hMs = eventos.fin();

  salida.agrupacion.starts.resize(restaurantCount + 1);
  for (std::size_t r = 0; r < restaurantCount; ++r) {
    salida.agrupacion.starts[r] = hostInicios[r];
  }
  salida.agrupacion.starts[restaurantCount] = n;
  salida.agrupacion.indices.assign(hostIndices.begin(), hostIndices.end());
  temporales.liberar();
  eventos.liberar();
  dIndices.liberar();
  dInicios.liberar();
  dTotales.liberar();
  dBloquesInicio.liberar();
  dCuentas.liberar();
  dIds.liberar();
  salida.totalMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - tiempo0).count();
  return salida;
}

}  // namespace equipego::analytics
