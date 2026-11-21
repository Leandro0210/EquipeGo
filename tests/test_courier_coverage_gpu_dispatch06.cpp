#include "analytics/CoverageGpuDispatch06.hpp"
#include <cassert>
#include <cmath>
#include <future>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error("CHECK failed: " #condition); } while(0)
using namespace equipego::stencil06;

static std::future<ResultadoGpu06> resolved(ResultadoGpu06 r) {
  std::promise<ResultadoGpu06> promise;
  auto future = promise.get_future();
  promise.set_value(std::move(r));
  return future;
}

int main() {
  const auto field = construirCampo({{0.26f, 0.32f}, {0.7f, 0.7f}}, 32);
  const auto expected = evolucionar(field, 20, 1);
  const auto baseline = seleccionarCobertura06(field, 20, false, false);
  CHECK(!baseline.gpuUsada && baseline.backend == "CPU");
  CHECK(baseline.valores == expected);
  const auto noCuda = seleccionarCobertura06(field, 20, true, false);
  CHECK(!noCuda.gpuUsada && noCuda.motivoFallback == "USE_CUDA_OFF");
  const auto noOwner = seleccionarCobertura06(field, 20, true, true);
  CHECK(!noOwner.gpuUsada && noOwner.motivoFallback == "OWNER_UNAVAILABLE");

  auto fake = [&](Campo06 c, unsigned int sweeps) {
    CHECK(sweeps == 20 && c.lado == 32);
    ResultadoGpu06 gpu;
    gpu.valores = expected;
    gpu.kernelMs = 0.1;
    gpu.totalMs = 0.2;
    return resolved(std::move(gpu));
  };
  auto pass = seleccionarCobertura06(field, 20, true, true, fake);
  CHECK(pass.gpuUsada && pass.backend == "GPU" && pass.errorMaximo == 0.0f);
  auto tiny = seleccionarCobertura06(field, 20, true, true,
     [&](Campo06, unsigned int) { auto r = ResultadoGpu06{};
       r.valores = expected; r.valores[4*32+4] += 1e-6f;
       return resolved(std::move(r)); });
  CHECK(tiny.gpuUsada && tiny.errorMaximo <= 1e-5f);
  auto wrong = seleccionarCobertura06(field, 20, true, true,
     [&](Campo06, unsigned int) { auto r = ResultadoGpu06{};
       r.valores = expected; r.valores[3*32+3] += 0.2f;
       return resolved(std::move(r)); });
  CHECK(!wrong.gpuUsada && wrong.motivoFallback == "MISMATCH_CPU_GPU" && wrong.valores == expected);
  auto broken = seleccionarCobertura06(field, 20, true, true,
     [&](Campo06, unsigned int) { auto r = ResultadoGpu06{};
       r.valores = expected; r.valores[0] = 1.0f;
       return resolved(std::move(r)); });
  CHECK(!broken.gpuUsada && broken.motivoFallback == "FIXED_CELL_MISMATCH");
  auto nonFinite = seleccionarCobertura06(field, 20, true, true,
     [&](Campo06, unsigned int) { auto r = ResultadoGpu06{};
       r.valores = expected; r.valores[4*32+4] = std::numeric_limits<float>::quiet_NaN();
       return resolved(std::move(r)); });
  CHECK(!nonFinite.gpuUsada && nonFinite.motivoFallback == "NONFINITE_GPU");
  auto sized = seleccionarCobertura06(field, 20, true, true,
     [](Campo06, unsigned int) { return resolved(ResultadoGpu06{}); });
  CHECK(!sized.gpuUsada && sized.motivoFallback == "SIZE_MISMATCH");
  auto error = seleccionarCobertura06(field, 20, true, true,
     [](Campo06, unsigned int)->std::future<ResultadoGpu06> {
       throw std::runtime_error("fallo simulado"); });
  CHECK(!error.gpuUsada && error.motivoFallback.find("GPU_EXCEPTION:") == 0);
  std::cout << "STENCIL06_DISPATCH_E4_OK,cases=10,fallback=OK\n";
}
