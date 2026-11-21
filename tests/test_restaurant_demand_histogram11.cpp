#include "analytics/RestaurantDemandHistogram11.hpp"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace equipego::analytics;
#define CHECK11(c) do { if (!(c)) { std::cerr << "FEATURE11_CPU_FAIL,line=" \
    << __LINE__ << ",cond=" #c << '\n'; return 1; } } while (false)

int main() {
  const auto vacio = histogramaDemandaCPU11({}, 3, 4);
  CHECK11(vacio.celdas.size() == 12 && vacio.totalPedidos == 0);
  CHECK11(vacio.porRestaurante.size() == 3 && vacio.en(0, 0) == 0);
  CHECK11(histogramasIguales11(vacio, histogramaDemandaCPUParalelo11({}, 3, 4, 4)));

  const std::vector<PedidoDemanda11> base = {
      {0, 0}, {2, 3}, {0, 0}, {1, 2}, {2, 3}, {2, 0}};
  const auto ref = histogramaDemandaCPU11(base, 3, 4);
  CHECK11(ref.totalPedidos == 6 && ref.en(0, 0) == 2);
  CHECK11(ref.en(2, 3) == 2 && ref.en(1, 2) == 1);
  CHECK11(ref.porRestaurante[0] == 2 && ref.porRestaurante[1] == 1);
  CHECK11(ref.porRestaurante[2] == 3);
  for (std::size_t t : {1u, 2u, 3u, 4u, 8u})
    CHECK11(histogramasIguales11(ref,
        histogramaDemandaCPUParalelo11(base, 3, 4, t)));

  std::vector<PedidoDemanda11> grande;
  grande.reserve(100003);
  for (std::size_t i = 0; i < 100003; ++i)
    grande.push_back({static_cast<std::uint32_t>((i * 7) % 17),
                      static_cast<std::uint32_t>((i * 11) % 24)});
  const auto grandeRef = histogramaDemandaCPU11(grande, 17, 24);
  CHECK11(grandeRef.totalPedidos == 100003);
  for (std::size_t t : {1u, 2u, 4u, 8u, 16u})
    CHECK11(histogramasIguales11(grandeRef,
        histogramaDemandaCPUParalelo11(grande, 17, 24, t)));

  auto rechaza = [](const auto& accion) {
    try { accion(); return false; }
    catch (const std::invalid_argument&) { return true; }
  };
  CHECK11(rechaza([] { (void)histogramaDemandaCPU11({}, 0, 4); }));
  CHECK11(rechaza([] { (void)histogramaDemandaCPU11({}, 3, 0); }));
  CHECK11(rechaza([] { (void)histogramaDemandaCPU11({{3, 0}}, 3, 4); }));
  CHECK11(rechaza([] { (void)histogramaDemandaCPU11({{0, 4}}, 3, 4); }));
  CHECK11(rechaza([] { (void)histogramaDemandaCPUParalelo11({{0, 0}}, 3, 4, 0); }));
  CHECK11(rechaza([] { (void)histogramaDemandaCPUParalelo11({{0, 8}}, 3, 4, 4); }));
  bool indiceFuera = false;
  try { (void)ref.en(3, 0); }
  catch (const std::out_of_range&) { indiceFuera = true; }
  CHECK11(indiceFuera);

  std::cout << "FEATURE11_HISTOGRAM_CPU_OK,casos=7,paralelo=equivalente,n=100003\n";
  return 0;
}
