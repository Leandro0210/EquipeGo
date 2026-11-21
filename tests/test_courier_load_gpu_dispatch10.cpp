#include "analytics/CourierLoadGpuDispatch10.hpp"
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace equipego::analytics;

static void exigir(bool correcto, const char* m) {
  if (!correcto) throw std::runtime_error(m);
}

static std::future<ResultadoCargaGpu10> listo10(ResultadoCargaGpu10 resultado) {
  std::promise<ResultadoCargaGpu10> p;
  p.set_value(std::move(resultado));
  return p.get_future();
}

int main() {
  try {
    const std::vector<std::uint32_t> reservas{0, 2, 7, 7, 1};
    const auto cpu = reducirCargaCouriersCPU10(reservas);
    const auto sinSolicitud = seleccionarCarga10(reservas, false, true);
    exigir(!sinSolicitud.gpuUsada &&
           sinSolicitud.fallback == "GPU_NO_SOLICITADA", "opt-in");
    const auto sinCuda = seleccionarCarga10(reservas, true, false);
    exigir(!sinCuda.gpuUsada && sinCuda.fallback == "USE_CUDA_OFF", "sin CUDA");
    const auto sinCola = seleccionarCarga10(reservas, true, true);
    exigir(!sinCola.gpuUsada &&
           sinCola.fallback == "SIN_PROPIETARIO_GPU", "sin cola");
    const auto vacia = seleccionarCarga10({}, true, true);
    exigir(!vacia.gpuUsada && vacia.fallback == "FLOTA_VACIA", "vacia");

    const EnviarCarga10 buena = [](std::vector<std::uint32_t> v) {
      ResultadoCargaGpu10 r;
      r.resumen = reducirCargaCouriersCPU10(v);
      r.kernelMs = 0.5;
      r.totalMs = 1.0;
      return listo10(std::move(r));
    };
    auto correcta = seleccionarCarga10(reservas, true, true, buena);
    exigir(correcta.gpuUsada && correcta.backend == "GPU" &&
           resumenIgual10(correcta.resumen, cpu) &&
           correcta.kernelMs == 0.5, "equivalencia y tiempos");

    const EnviarCarga10 incorrecta = [](std::vector<std::uint32_t> v) {
      ResultadoCargaGpu10 r;
      r.resumen = reducirCargaCouriersCPU10(v);
      ++r.resumen.totalReservas;
      return listo10(std::move(r));
    };
    auto diferente = seleccionarCarga10(reservas, true, true, incorrecta);
    exigir(!diferente.gpuUsada &&
           diferente.fallback == "DIFERENCIA_CPU_GPU" &&
           resumenIgual10(diferente.resumen, cpu), "fallback discrepancia");

    const EnviarCarga10 fallo = [](std::vector<std::uint32_t>)
        -> std::future<ResultadoCargaGpu10> {
      throw std::runtime_error("cuda unavailable");
    };
    auto error = seleccionarCarga10(reservas, true, true, fallo);
    exigir(!error.gpuUsada && error.fallback == "ERROR_GPU", "fallback error");

    auto promise = std::make_shared<std::promise<ResultadoCargaGpu10>>();
    const EnviarCarga10 lenta = [promise](std::vector<std::uint32_t>) {
      return promise->get_future();
    };
    auto timeout = seleccionarCarga10(reservas, true, true, lenta,
                                      std::chrono::milliseconds(1));
    exigir(!timeout.gpuUsada &&
           timeout.fallback == "TIMEOUT_O_NO_LISTO", "timeout");
    // Al salir de scope la promesa deja el future sin bloqueo.
    auto sinTiempo = seleccionarCarga10(reservas, true, true, buena,
                                        std::chrono::milliseconds(0));
    exigir(!sinTiempo.gpuUsada && sinTiempo.fallback == "TIMEOUT", "sin tiempo");
    auto extremos = seleccionarCarga10(
        {std::numeric_limits<std::uint32_t>::max(),
         std::numeric_limits<std::uint32_t>::max()},
        true, true, buena);
    exigir(extremos.gpuUsada && extremos.resumen.totalReservas ==
           2ULL * std::numeric_limits<std::uint32_t>::max(), "uint64");
    std::cout << "FEATURE10_GPU_DISPATCH_CPU_TEST_OK," 
                 "OPT_IN=OK,FALLBACK=OK,DESVIACION=OK,TIMEOUT=OK\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FEATURE10_GPU_DISPATCH_CPU_TEST_ERROR:" << e.what() << '\n';
    return 1;
  }
}
