#include "analytics/CourierLayoutGpuDispatch04.hpp"

#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace equipego::performance04;

static void exigir04(bool ok, const char* msg) {
  if (!ok) throw std::runtime_error(msg);
}

static std::future<ResultadoGpu04> listo04(ResultadoGpu04 out) {
  std::promise<ResultadoGpu04> p;
  p.set_value(std::move(out));
  return p.get_future();
}

int main() {
  try {
    const std::vector<CourierAoS04> datos{
        {10.f, 20.f, 5.f, 0, 2, 0},
        {50.f, 90.f, 4.f, 1, 2, 0},
        {0.f, 0.f, 1.f, 0, 1, 2},
        {100.f, 100.f, 1.f, 2, 2, 0},
    };
    const auto sinSolicitud = seleccionarLayoutGpu04(datos, 5, 20, 0, 1,
                                                      false, true);
    exigir04(!sinSolicitud.usada && sinSolicitud.backend == "CPU",
             "opt-in debe estar desactivado");
    const auto sinCuda = seleccionarLayoutGpu04(datos, 5, 20, 0, 1,
                                                true, false);
    exigir04(!sinCuda.usada && sinCuda.fallback == "USE_CUDA_OFF",
             "fallback sin CUDA ausente");
    const auto sinCola = seleccionarLayoutGpu04(datos, 5, 20, 0, 1,
                                                true, true);
    exigir04(!sinCola.usada && sinCola.fallback == "SIN_PROPIETARIO_GPU",
             "sin propietarios debe usar CPU");

    const EnviarLayoutAoS04 fakeAoS = [](
        std::vector<CourierAoS04> v, float x, float y, float t, unsigned b) {
      ResultadoGpu04 r;
      r.values = calcularAoS(v, x, y, t, 1, b);
      r.kernelMs = 0.25;
      r.totalMs = 0.75;
      return listo04(std::move(r));
    };
    const EnviarLayoutSoA04 fakeSoA = [](
        CourierSoA04 v, float x, float y, float t, unsigned b) {
      ResultadoGpu04 r;
      r.values = calcularSoA(v, x, y, t, 1, b);
      r.kernelMs = 0.10;
      r.totalMs = 0.50;
      return listo04(std::move(r));
    };
    auto ok = seleccionarLayoutGpu04(datos, 5, 20, 0, 1,
                                     true, true, fakeAoS, fakeSoA);
    exigir04(ok.usada && ok.backend == "GPU" && ok.maxError == 0,
             "respuesta GPU simulada no equivalente");
    exigir04(ok.aosTotalMs == 0.75 && ok.soaTotalMs == 0.50,
             "tiempos de los backends no propagados");

    EnviarLayoutSoA04 incorrecta = [](
        CourierSoA04 v, float x, float y, float t, unsigned b) {
      ResultadoGpu04 r;
      r.values = calcularSoA(v, x, y, t, 1, b);
      r.values.front() += 50.0f;
      return listo04(std::move(r));
    };
    const auto diff = seleccionarLayoutGpu04(datos, 5, 20, 0, 1,
                                            true, true, fakeAoS, incorrecta);
    exigir04(!diff.usada && diff.backend == "CPU" &&
                 diff.fallback == "ERROR_O_DIFERENCIA",
             "diferencia numerica no produjo fallback");

    auto promise = std::make_shared<std::promise<ResultadoGpu04>>();
    EnviarLayoutAoS04 lenta = [promise](
        std::vector<CourierAoS04>, float, float, float, unsigned) {
      return promise->get_future();
    };
    const auto timeout = seleccionarLayoutGpu04(
        datos, 5, 20, 0, 1, true, true, lenta, fakeSoA,
        std::chrono::milliseconds(2));
    exigir04(!timeout.usada && timeout.fallback == "TIMEOUT",
             "timeout no produjo fallback");

    const auto vacio = seleccionarLayoutGpu04({}, 5, 20, 0, 1,
                                              true, true, fakeAoS, fakeSoA);
    exigir04(!vacio.usada && vacio.fallback == "FLOTA_VACIA",
             "no se filtro flota vacia");
    std::cout << "LAYOUT04_GPU_DISPATCH_TEST_OK,CPU_FALLBACK=OK,"
                 "EQUIVALENCIA=OK,TIMEOUT=OK\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "LAYOUT04_GPU_DISPATCH_TEST_FAIL," << e.what() << '\n';
    return 1;
  }
}
