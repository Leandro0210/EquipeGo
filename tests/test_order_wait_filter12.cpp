#include "analytics/OrderWaitFilter12.hpp"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace equipego::analytics;
#define CHECK12(c) do { if (!(c)) { std::cerr << "FEATURE12_CPU_FAIL,line=" \
    << __LINE__ << ",cond=" #c << '\n'; return 1; } } while (false)

int main() {
  const auto vacio = filtrarEsperasCPU12({}, 1000);
  CHECK12(vacio.seleccionados() == 0 && vacio.descartados() == 0);
  CHECK12(filtrosIguales12(vacio, filtrarEsperasCPUParalelo12({}, 1000, 4)));

  const std::vector<std::uint64_t> base{0, 1000, 1001, 5000, 1000, 9000};
  const auto antes = base;
  const auto r = filtrarEsperasCPU12(base, 1000);
  CHECK12(r.totalEntradas == 6 && r.umbralMs == 1000);
  CHECK12((r.indices == std::vector<std::size_t>{2, 3, 5}));
  CHECK12(r.seleccionados() == 3 && r.descartados() == 3);
  CHECK12(base == antes);
  for (std::size_t t : {1u, 2u, 3u, 4u, 8u})
    CHECK12(filtrosIguales12(r, filtrarEsperasCPUParalelo12(base, 1000, t)));

  CHECK12((filtrarEsperasCPU12(base, 0).indices ==
           std::vector<std::size_t>{1, 2, 3, 4, 5}));
  CHECK12(filtrarEsperasCPU12(base, 9000).seleccionados() == 0);
  CHECK12(filtrarEsperasCPU12(base, UINT64_MAX).seleccionados() == 0);
  CHECK12((filtrarEsperasCPU12({UINT64_MAX, 0, UINT64_MAX-1},
                              UINT64_MAX-1).indices ==
           std::vector<std::size_t>{0}));
  CHECK12((filtrarEsperasCPU12({1, 1, 1}, 0).indices ==
           std::vector<std::size_t>{0, 1, 2}));

  std::vector<std::uint64_t> grande;
  grande.reserve(100003);
  for (std::size_t i = 0; i < 100003; ++i)
    grande.push_back((i * 931ULL + i / 7) % 90000ULL);
  const auto ref = filtrarEsperasCPU12(grande, 45000);
  CHECK12(ref.totalEntradas == 100003 && ref.seleccionados() > 0);
  for (std::size_t t : {1u, 2u, 3u, 4u, 8u, 16u})
    CHECK12(filtrosIguales12(ref,
        filtrarEsperasCPUParalelo12(grande, 45000, t)));
  for (std::size_t i = 1; i < ref.indices.size(); ++i)
    CHECK12(ref.indices[i - 1] < ref.indices[i]);

  bool falloHilos = false;
  try { (void)filtrarEsperasCPUParalelo12(base, 1000, 0); }
  catch (const std::invalid_argument&) { falloHilos = true; }
  CHECK12(falloHilos);
  CHECK12((filtrarEsperasCPU12({42}, 41).indices ==
           std::vector<std::size_t>{0}));
  CHECK12(filtrarEsperasCPU12({42}, 42).seleccionados() == 0);

  std::cout << "FEATURE12_FILTER_CPU_OK,casos=9,paralelo=equivalente,n=100003\n";
  return 0;
}
