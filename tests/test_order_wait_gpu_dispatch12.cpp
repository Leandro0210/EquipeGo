#include "analytics/OrderWaitGpuDispatch12.hpp"
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace equipego::analytics;
static void exigir12(bool v, const char* m) {
  if (!v) throw std::runtime_error(m);
}
static std::future<ResultadoFiltroGpu12> listo12(ResultadoFiltroGpu12 v) {
  std::promise<ResultadoFiltroGpu12> p;
  p.set_value(std::move(v));
  return p.get_future();
}
int main() {
  try {
    const std::vector<std::uint64_t> tiempos{0, 100, 101, 999, 101, 100};
    const auto ref = filtrarEsperasCPU12(tiempos, 100);
    exigir12((ref.indices == std::vector<std::size_t>{2,3,4}), "estricto y estable");
    const auto off = seleccionarFiltro12(tiempos, 100, false, true);
    exigir12(!off.gpuUsada && off.fallback == "GPU_NO_SOLICITADA" &&
             filtrosIguales12(ref, off.filtro), "opt-in");
    const auto nocuda = seleccionarFiltro12(tiempos, 100, true, false);
    exigir12(!nocuda.gpuUsada && nocuda.fallback == "USE_CUDA_OFF", "CPU fallback");
    const auto vacio = seleccionarFiltro12({}, 100, true, true);
    exigir12(vacio.fallback == "SIN_PEDIDOS", "vacio");
    const auto sinCola = seleccionarFiltro12(tiempos, 100, true, true);
    exigir12(sinCola.fallback == "SIN_PROPIETARIO_GPU", "sin cola");
    const EnviarFiltro12 bueno = [](std::vector<std::uint64_t> v, std::uint64_t u) {
      ResultadoFiltroGpu12 r;
      r.filtro = filtrarEsperasCPU12(v, u);
      r.kernelMs = 0.33;
      r.totalMs = 0.88;
      return listo12(std::move(r));
    };
    auto gpu = seleccionarFiltro12(tiempos, 100, true, true, bueno);
    exigir12(gpu.gpuUsada && gpu.backend == "GPU" &&
             filtrosIguales12(gpu.filtro, ref) && gpu.kernelMs == 0.33, "GPU OK");
    const EnviarFiltro12 distinto = [](std::vector<std::uint64_t> v, std::uint64_t u) {
      ResultadoFiltroGpu12 r;
      r.filtro = filtrarEsperasCPU12(v, u);
      r.filtro.indices.pop_back();
      return listo12(std::move(r));
    };
    auto diff = seleccionarFiltro12(tiempos, 100, true, true, distinto);
    exigir12(!diff.gpuUsada && diff.fallback == "DIFERENCIA_CPU_GPU" &&
             filtrosIguales12(diff.filtro, ref), "diferencia");
    const EnviarFiltro12 error = [](std::vector<std::uint64_t>, std::uint64_t)
        -> std::future<ResultadoFiltroGpu12> {
      throw std::runtime_error("CUDA unavailable");
    };
    auto fail = seleccionarFiltro12(tiempos, 100, true, true, error);
    exigir12(!fail.gpuUsada && fail.fallback == "ERROR_GPU", "excepcion");
    auto promesa = std::make_shared<std::promise<ResultadoFiltroGpu12>>();
    const EnviarFiltro12 lento = [promesa](std::vector<std::uint64_t>, std::uint64_t) {
      return promesa->get_future();
    };
    auto tmo = seleccionarFiltro12(tiempos, 100, true, true, lento,
                                  std::chrono::milliseconds(1));
    exigir12(!tmo.gpuUsada && tmo.fallback == "TIMEOUT_O_NO_LISTO", "timeout");
    auto tmo0 = seleccionarFiltro12(tiempos, 100, true, true, bueno,
                                   std::chrono::milliseconds(0));
    exigir12(tmo0.fallback == "TIMEOUT", "timeout cero");
    const std::vector<std::uint64_t> vacios{0, 0, 0};
    auto ningun = seleccionarFiltro12(vacios, 100, true, true, bueno);
    exigir12(ningun.gpuUsada && ningun.filtro.indices.empty(), "sin seleccionados");
    auto todos = seleccionarFiltro12(tiempos, 0, true, true, bueno);
    exigir12(todos.gpuUsada && todos.filtro.seleccionados() == 5, "todos");
    exigir12(tiempos == (std::vector<std::uint64_t>{0,100,101,999,101,100}),
             "no modifica origen");
    std::cout << "FEATURE12_GPU_DISPATCH_CPU_TEST_OK,CASES=12,"
                 "FALLBACK=OK,OPT_IN=OK,EQUIVALENCIA=OK\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FEATURE12_GPU_DISPATCH_CPU_ERROR:" << e.what() << '\n';
    return 1;
  }
}
