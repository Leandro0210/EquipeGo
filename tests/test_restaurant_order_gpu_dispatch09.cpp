#include "analytics/RestaurantOrderGpuDispatch09.hpp"

#include <chrono>
#include <deque>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define EXIGIR(x) do { if (!(x)) { \
  std::cerr << "FALLO " #x " linea " << __LINE__ << '\n'; return 1; \
} } while(false)

struct PedidoPrueba { std::string restauranteId; std::string orderId; };
struct RestaurantPrueba { std::string id; };

int main() {
  using namespace equipego::analytics;
  using Reloj = std::chrono::steady_clock;
  const auto futuroLimite = [] { return Reloj::now() + std::chrono::seconds(2); };
  const auto indices = indexarRestaurantes09(
      std::vector<RestaurantPrueba>{{"r0"}, {"r1"}, {"r2"}});
  const std::deque<PedidoPrueba> pedidos{
      {"r2", "o0"}, {"r1", "o1"}, {"r2", "o2"},
      {"r0", "o3"}, {"r1", "o4"}, {"r2", "o5"}};
  const auto copia = pedidos;
  const auto cpu = agruparPendientesReales09(pedidos, indices, 3);
  EXIGIR(cpu.agrupacion.indices ==
         (std::vector<std::size_t>{3, 1, 4, 0, 2, 5}));

  const EnviarScanGpu09 correcta = [](std::vector<std::size_t> ids,
                                      std::size_t r) {
    std::promise<ResultadoScanGpu09> promesa;
    ResultadoScanGpu09 res;
    res.agrupacion = agruparPedidosScanCPU09(ids, r);
    promesa.set_value(std::move(res));
    return promesa.get_future();
  };
  const EnviarScanGpu09 incorrecta = [](std::vector<std::size_t>,
                                        std::size_t) {
    std::promise<ResultadoScanGpu09> promesa;
    ResultadoScanGpu09 res;
    res.agrupacion.starts = {0, 1, 2, 6};
    res.agrupacion.indices = {0, 1, 2, 3, 4, 5};
    promesa.set_value(std::move(res));
    return promesa.get_future();
  };
  const EnviarScanGpu09 falla = [](std::vector<std::size_t>, std::size_t)
      -> std::future<ResultadoScanGpu09> {
    throw std::runtime_error("GPU no disponible");
  };

  auto resultado = agruparPendientesConGpu09(pedidos, indices, 3,
                                             correcta, futuroLimite(), 1);
  EXIGIR(resultado.estado == EstadoScanGpu09::Usada);
  EXIGIR(resultado.vista.agrupacion.starts == cpu.agrupacion.starts);
  EXIGIR(resultado.vista.agrupacion.indices == cpu.agrupacion.indices);

  resultado = agruparPendientesConGpu09(pedidos, indices, 3,
                                        incorrecta, futuroLimite(), 1);
  EXIGIR(resultado.estado == EstadoScanGpu09::DiferenciaCpuGpu);
  EXIGIR(resultado.vista.agrupacion.indices == cpu.agrupacion.indices);

  resultado = agruparPendientesConGpu09(pedidos, indices, 3,
                                        falla, futuroLimite(), 1);
  EXIGIR(resultado.estado == EstadoScanGpu09::FalloGpu);
  EXIGIR(resultado.vista.agrupacion.starts == cpu.agrupacion.starts);

  resultado = agruparPendientesConGpu09(pedidos, indices, 3,
                                        {}, futuroLimite(), 1);
  EXIGIR(resultado.estado == EstadoScanGpu09::SoloCpu);
  resultado = agruparPendientesConGpu09(pedidos, indices, 3,
                                        correcta, futuroLimite(), 100);
  EXIGIR(resultado.estado == EstadoScanGpu09::SoloCpu);
  resultado = agruparPendientesConGpu09(pedidos, indices, 3,
                                        correcta,
                                        Reloj::now() - std::chrono::seconds(1), 1);
  EXIGIR(resultado.estado == EstadoScanGpu09::SinTiempo);

  std::vector<std::shared_ptr<std::promise<ResultadoScanGpu09>>> pendientes;
  const EnviarScanGpu09 lenta = [&pendientes](std::vector<std::size_t>, std::size_t) {
    auto promesa = std::make_shared<std::promise<ResultadoScanGpu09>>();
    auto futuro = promesa->get_future();
    pendientes.push_back(std::move(promesa));
    return futuro;
  };
  resultado = agruparPendientesConGpu09(pedidos, indices, 3, lenta,
      Reloj::now() + std::chrono::milliseconds(2), 1);
  EXIGIR(resultado.estado == EstadoScanGpu09::SinTiempo);
  EXIGIR(resultado.vista.agrupacion.indices == cpu.agrupacion.indices);

  // Orden original y campos de todos los pedidos intactos.
  EXIGIR(pedidos.size() == copia.size());
  for (std::size_t i = 0; i < pedidos.size(); ++i) {
    EXIGIR(pedidos[i].orderId == copia[i].orderId);
    EXIGIR(pedidos[i].restauranteId == copia[i].restauranteId);
  }

  bool error = false;
  try {
    (void)agruparPendientesConGpu09(pedidos, indices, 3,
                                    correcta, futuroLimite(), 0);
  } catch (const std::invalid_argument&) {
    error = true;
  }
  EXIGIR(error);
  const std::deque<PedidoPrueba> vacio;
  resultado = agruparPendientesConGpu09(vacio, indices, 3,
                                        correcta, futuroLimite(), 1);
  EXIGIR(resultado.estado == EstadoScanGpu09::SoloCpu);
  EXIGIR(resultado.vista.agrupacion.starts ==
         (std::vector<std::size_t>{0, 0, 0, 0}));

  std::cout << "TEST SCAN09 DISPATCH GPU/CPU CON FALLBACK: OK\n";
  return 0;
}
