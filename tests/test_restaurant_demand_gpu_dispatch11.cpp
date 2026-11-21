#include "analytics/RestaurantDemandGpuDispatch11.hpp"
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace equipego::analytics;
static void require11(bool v, const char* reason) {
  if (!v) throw std::runtime_error(reason);
}
static std::future<ResultadoDemandaGpu11> listo11(ResultadoDemandaGpu11 v) {
  std::promise<ResultadoDemandaGpu11> p;
  p.set_value(std::move(v));
  return p.get_future();
}
int main() {
  try {
    const std::vector<PedidoDemanda11> datos{{0,0},{0,1},{0,1},{2,3},{1,2}};
    const auto cpu = histogramaDemandaCPU11(datos, 3, 4);
    const auto off = seleccionarDemanda11(datos, 3, 4, false, true);
    require11(!off.gpuUsada && off.fallback == "GPU_NO_SOLICITADA", "opt-in");
    require11(histogramasIguales11(off.histograma, cpu), "referencia CPU");
    const auto sinCuda = seleccionarDemanda11(datos, 3, 4, true, false);
    require11(sinCuda.fallback == "USE_CUDA_OFF", "sin CUDA");
    const auto vacio = seleccionarDemanda11({}, 3, 4, true, true);
    require11(vacio.fallback == "SIN_PEDIDOS", "sin pedidos");
    const auto dimensiones = seleccionarDemanda11({{0,0}}, 1025, 1, true, true);
    require11(dimensiones.fallback == "GPU_DIMENSIONES_NO_SOPORTADAS", "limite GPU");
    const auto sinCola = seleccionarDemanda11(datos, 3, 4, true, true);
    require11(sinCola.fallback == "SIN_PROPIETARIO_GPU", "sin propietario");
    const EnviarDemanda11 bueno = [](std::vector<PedidoDemanda11> e,
                                   std::size_t r, std::size_t f) {
      ResultadoDemandaGpu11 h;
      h.histograma = histogramaDemandaCPU11(e, r, f);
      h.kernelMs = 0.25;
      h.totalMs = 1.0;
      return listo11(std::move(h));
    };
    const auto correcto = seleccionarDemanda11(datos, 3, 4, true, true, bueno);
    require11(correcto.gpuUsada && correcto.backend == "GPU" &&
              correcto.kernelMs == 0.25 &&
              histogramasIguales11(correcto.histograma, cpu), "GPU equivalente");
    const EnviarDemanda11 distinto = [](std::vector<PedidoDemanda11> e,
                                      std::size_t r, std::size_t f) {
      ResultadoDemandaGpu11 h;
      h.histograma = histogramaDemandaCPU11(e, r, f);
      ++h.histograma.totalPedidos;
      return listo11(std::move(h));
    };
    const auto diff = seleccionarDemanda11(datos, 3, 4, true, true, distinto);
    require11(!diff.gpuUsada && diff.fallback == "DIFERENCIA_CPU_GPU" &&
              histogramasIguales11(diff.histograma, cpu), "discrepancia");
    const EnviarDemanda11 fallo = [](std::vector<PedidoDemanda11>,
                                   std::size_t, std::size_t)
                                   -> std::future<ResultadoDemandaGpu11> {
      throw std::runtime_error("sin GPU");
    };
    const auto error = seleccionarDemanda11(datos, 3, 4, true, true, fallo);
    require11(!error.gpuUsada && error.fallback == "ERROR_GPU", "error CUDA");
    auto promesa = std::make_shared<std::promise<ResultadoDemandaGpu11>>();
    const EnviarDemanda11 lento = [promesa](std::vector<PedidoDemanda11>,
                                           std::size_t, std::size_t) {
      return promesa->get_future();
    };
    const auto timeout = seleccionarDemanda11(datos, 3, 4, true, true, lento,
                                               std::chrono::milliseconds(1));
    require11(timeout.fallback == "TIMEOUT_O_NO_LISTO", "timeout futuro");
    const auto cero = seleccionarDemanda11(datos, 3, 4, true, true, bueno,
                                           std::chrono::milliseconds(0));
    require11(cero.fallback == "TIMEOUT", "timeout cero");
    bool invalido = false;
    try {(void)seleccionarDemanda11({{3,0}}, 3, 4, true, true);}
    catch (const std::invalid_argument&) {invalido = true;}
    require11(invalido, "datos invalidos");
    std::cout << "FEATURE11_GPU_DISPATCH_CPU_TEST_OK,CASES=11,"
                 "FALLBACK=OK,OPT_IN=OK,EQUIVALENCIA=OK\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FEATURE11_GPU_DISPATCH_CPU_TEST_ERROR:" << e.what() << '\n';
    return 1;
  }
}
