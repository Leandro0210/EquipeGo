#include "analytics/CourierCoverageGpu06.hpp"

#include <cuda_runtime.h>

#include <chrono>
#include <initializer_list>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace equipego::stencil06 {
namespace {

constexpr int kBlock = 16;
constexpr int kInTile = 16;
constexpr int kOutTile = kInTile - 2;

void revisar(cudaError_t code, const char* operation) {
  if (code != cudaSuccess) {
    throw std::runtime_error(std::string("STENCIL06_CUDA: ") + operation +
                             ": " + cudaGetErrorString(code));
  }
}

struct Memoria {
  void* ptr = nullptr;
  void reservar(std::size_t n) { revisar(cudaMalloc(&ptr, n), "cudaMalloc"); }
  ~Memoria() { if (ptr) cudaFree(ptr); }
  Memoria() = default;
  Memoria(const Memoria&) = delete;
  Memoria& operator=(const Memoria&) = delete;
};

struct Evento {
  cudaEvent_t e = nullptr;
  Evento() { revisar(cudaEventCreate(&e), "cudaEventCreate"); }
  ~Evento() { if (e) cudaEventDestroy(e); }
  Evento(const Evento&) = delete;
  Evento& operator=(const Evento&) = delete;
  void marcar() { revisar(cudaEventRecord(e), "cudaEventRecord"); }
  void sincronizar() { revisar(cudaEventSynchronize(e), "cudaEventSynchronize"); }
};

double milisegundos(const Evento& a, const Evento& b) {
  float ms = 0.0f;
  revisar(cudaEventElapsedTime(&ms, a.e, b.e), "cudaEventElapsedTime");
  return static_cast<double>(ms);
}

// EC2/06_stencil/02_stencil_2d.cu y 03_jacobi_sweeps.cu:
// un hilo por celda, buffer anterior -> buffer siguiente.
__global__ void jacobiBasico06(const float* in, float* out,
    const float* inicial, const std::uint8_t* fijas,
    int n, float cC, float cN) {
  const int col = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
  const int row = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
  if (row >= n || col >= n) return;
  const int p = row * n + col;
  if (fijas[p]) {
    out[p] = inicial[p];
  } else {
    out[p] = cC * in[p] + cN *
        (in[p - 1] + in[p + 1] + in[p - n] + in[p + n]);
  }
}

// EC2/06_stencil/bikeshare_station_coverage.cu: jacobi5_tiled_kernel
// 16x16 entradas shared; 14x14 salidas; halo de 1 celda.
__global__ void jacobiTiled06(const float* in, float* out,
    const float* inicial, const std::uint8_t* fijas,
    int n, float cC, float cN) {
  const int tx = static_cast<int>(threadIdx.x);
  const int ty = static_cast<int>(threadIdx.y);
  const int col = static_cast<int>(blockIdx.x) * kOutTile + tx - 1;
  const int row = static_cast<int>(blockIdx.y) * kOutTile + ty - 1;
  const bool inside = col >= 0 && col < n && row >= 0 && row < n;
  __shared__ float tile[kInTile][kInTile];
  tile[ty][tx] = inside ? in[row * n + col] : 0.0f;
  __syncthreads();  // todos los hilos cargan, incluso los del halo
  if (!inside || tx == 0 || ty == 0 || tx == kInTile - 1 ||
      ty == kInTile - 1) return;
  const int p = row * n + col;
  if (fijas[p]) {
    out[p] = inicial[p];
  } else {
    out[p] = cC * tile[ty][tx] + cN *
        (tile[ty][tx - 1] + tile[ty][tx + 1] +
         tile[ty - 1][tx] + tile[ty + 1][tx]);
  }
}
} // namespace

ResultadoGpu06 evolucionarGpu(const Campo06& campo, unsigned int sweeps,
    VarianteGpu06 variante, float cC, float cN) {
  validar(campo);
  validarParametros(cC, cN, 1);
  if (variante != VarianteGpu06::BasicoGlobal &&
      variante != VarianteGpu06::TiledShared)
    throw std::invalid_argument("STENCIL06_GPU: variante desconocida");
  ResultadoGpu06 result;
  result.valores = campo.valores;
  if (sweeps == 0) return result;

  const auto start = std::chrono::steady_clock::now();
  const int n = static_cast<int>(campo.lado);
  const std::size_t count = campo.valores.size();
  const std::size_t bytes = count * sizeof(float);
  Memoria a, b, original, fijo;
  a.reservar(bytes);
  b.reservar(bytes);
  original.reservar(bytes);
  fijo.reservar(count * sizeof(std::uint8_t));
  Evento hStart, hStop, kStart, kStop, dStart, dStop;
  hStart.marcar();
  revisar(cudaMemcpy(a.ptr, campo.valores.data(), bytes,
                     cudaMemcpyHostToDevice), "H2D inicial");
  revisar(cudaMemcpy(original.ptr, campo.valores.data(), bytes,
                     cudaMemcpyHostToDevice), "H2D fuentes");
  revisar(cudaMemcpy(fijo.ptr, campo.fijas.data(), count,
                     cudaMemcpyHostToDevice), "H2D mascara");
  hStop.marcar();

  float* read = static_cast<float*>(a.ptr);
  float* write = static_cast<float*>(b.ptr);
  const dim3 block(kBlock, kBlock);
  const dim3 basicGrid((n + kBlock - 1) / kBlock,
                       (n + kBlock - 1) / kBlock);
  const dim3 tiledGrid((n + kOutTile - 1) / kOutTile,
                       (n + kOutTile - 1) / kOutTile);
  kStart.marcar();
  for (unsigned int i = 0; i < sweeps; ++i) {
    if (variante == VarianteGpu06::BasicoGlobal) {
      jacobiBasico06<<<basicGrid, block>>>(read, write,
          static_cast<const float*>(original.ptr),
          static_cast<const std::uint8_t*>(fijo.ptr), n, cC, cN);
    } else {
      jacobiTiled06<<<tiledGrid, block>>>(read, write,
          static_cast<const float*>(original.ptr),
          static_cast<const std::uint8_t*>(fijo.ptr), n, cC, cN);
    }
    revisar(cudaGetLastError(), "lanzamiento Jacobi");
    std::swap(read, write);
  }
  kStop.marcar();
  kStop.sincronizar();
  dStart.marcar();
  revisar(cudaMemcpy(result.valores.data(), read, bytes,
                     cudaMemcpyDeviceToHost), "D2H resultado");
  dStop.marcar();
  dStop.sincronizar();
  result.h2dMs = milisegundos(hStart, hStop);
  result.kernelMs = milisegundos(kStart, kStop);
  result.d2hMs = milisegundos(dStart, dStop);
  // Incluir cudaFree en total; la destruccion de eventos queda fuera de la medida.
  for (Memoria* m : {&a, &b, &original, &fijo}) {
    revisar(cudaFree(m->ptr), "cudaFree");
    m->ptr = nullptr;
  }
  const auto end = std::chrono::steady_clock::now();
  result.totalMs = std::chrono::duration<double, std::milli>(end - start).count();
  return result;
}
} // namespace equipego::stencil06
