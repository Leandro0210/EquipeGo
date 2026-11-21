#include "analytics/EtaReduction.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#define REQUIRE(cond) \
  do { \
    if (!(cond)) { \
      std::cerr << "FALLO: " #cond << " (linea " << __LINE__ << ")\n"; \
      return 1; \
    } \
  } while (false)

int main() {
  using equipego::analytics::EtaCandidato;
  using equipego::analytics::reducirMejorEtaCPU;
  using equipego::analytics::reducirMejorEtaCPUParalelo;

  REQUIRE(!reducirMejorEtaCPU({}));
  REQUIRE(!reducirMejorEtaCPUParalelo({}, 8));
  REQUIRE(!reducirMejorEtaCPU({{1, 3.0, false}}));
  REQUIRE(reducirMejorEtaCPU({{8, 12.0, true}})->courierIndex == 8);
  REQUIRE(reducirMejorEtaCPU({{8, 6.0, true}, {3, 6.0, true}})
              ->courierIndex == 3);
  REQUIRE(reducirMejorEtaCPU({{1, 1.0, false}, {2, 4.0, true}})
              ->courierIndex == 2);
  REQUIRE(reducirMejorEtaCPU({
              {1, -3.0, true},
              {2, std::numeric_limits<double>::quiet_NaN(), true},
              {3, std::numeric_limits<double>::infinity(), true},
              {4, 7.0, true}})->courierIndex == 4);

  std::vector<EtaCandidato> grandes;
  grandes.reserve(100003);
  for (std::size_t i = 0; i < 100003; ++i) {
    grandes.push_back({100003 - i, 50.0 + static_cast<double>(i % 1000), true});
  }
  grandes[1234] = {42, 0.25, true};
  grandes[98765] = {7, 0.25, true};
  grandes[2] = {1, 0.001, false};
  REQUIRE(reducirMejorEtaCPU(grandes)->courierIndex == 7);
  for (std::size_t hilos : {1u, 2u, 4u, 8u, 16u}) {
    const auto actual = reducirMejorEtaCPUParalelo(grandes, hilos);
    REQUIRE(actual && actual->courierIndex == 7);
    REQUIRE(actual->etaSimulatedSeconds == 0.25);
  }

  bool rechazoHilosCero = false;
  try {
    (void)reducirMejorEtaCPUParalelo(grandes, 0);
  } catch (const std::invalid_argument&) {
    rechazoHilosCero = true;
  }
  REQUIRE(rechazoHilosCero);

  std::cout << "TEST REDUCTION CPU 08: OK\n";
  return 0;
}
