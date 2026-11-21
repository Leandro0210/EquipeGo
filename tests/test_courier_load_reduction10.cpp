#include "analytics/CourierLoadReduction10.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace equipego::analytics;

#define EXIGIR(cond) do { if (!(cond)) { \
  std::cerr << "FEATURE10_CPU_FAIL,linea=" << __LINE__ \
            << ",condicion=" #cond << '\n'; return 1; \
} } while(false)

static bool iguales(const ResumenCarga10& a, const ResumenCarga10& b) {
  return a.couriers == b.couriers && a.conPedidos == b.conPedidos &&
         a.totalReservas == b.totalReservas &&
         a.maxReservas == b.maxReservas &&
         a.indiceMax == b.indiceMax && a.tieneMax == b.tieneMax &&
         a.promedio() == b.promedio();
}

int main() {
  const auto vacio = reducirCargaCouriersCPU10({});
  EXIGIR(vacio.couriers == 0 && vacio.totalReservas == 0);
  EXIGIR(!vacio.tieneMax && vacio.promedio() == 0);
  EXIGIR(iguales(vacio, reducirCargaCouriersCPUParalelo10({}, 3)));

  const auto uno = reducirCargaCouriersCPU10({7});
  EXIGIR(uno.couriers == 1 && uno.conPedidos == 1);
  EXIGIR(uno.totalReservas == 7 && uno.maxReservas == 7);
  EXIGIR(uno.indiceMax == 0 && uno.promedio() == 7.0);

  const auto todosCero = reducirCargaCouriersCPU10({0, 0, 0});
  EXIGIR(todosCero.tieneMax && todosCero.indiceMax == 0);
  EXIGIR(todosCero.conPedidos == 0 && todosCero.maxReservas == 0);

  const auto empate = reducirCargaCouriersCPU10({2, 8, 0, 8, 3});
  EXIGIR(empate.totalReservas == 21 && empate.conPedidos == 4);
  EXIGIR(empate.maxReservas == 8 && empate.indiceMax == 1);
  EXIGIR(empate.promedio() == 4.2);

  const auto limite = std::numeric_limits<std::uint32_t>::max();
  const auto grande = reducirCargaCouriersCPU10({limite, limite});
  EXIGIR(grande.totalReservas == 2ULL * limite);
  EXIGIR(grande.maxReservas == limite && grande.indiceMax == 0);

  std::vector<std::uint32_t> reservas(100003);
  for (std::size_t i = 0; i < reservas.size(); ++i)
    reservas[i] = static_cast<std::uint32_t>((i * 7) % 9);
  reservas[100] = 100;
  reservas[60000] = 100;  // Mismo maximo: gana el indice 100.
  const auto referencia = reducirCargaCouriersCPU10(reservas);
  EXIGIR(referencia.maxReservas == 100 && referencia.indiceMax == 100);
  for (std::size_t hilos : {1u, 2u, 3u, 4u, 8u, 16u, 32u}) {
    const auto paralelo = reducirCargaCouriersCPUParalelo10(reservas, hilos);
    EXIGIR(iguales(referencia, paralelo));
  }

  bool rechazo = false;
  try { (void)reducirCargaCouriersCPUParalelo10({}, 0); }
  catch (const std::invalid_argument&) { rechazo = true; }
  EXIGIR(rechazo);

  std::cout << "FEATURE10_REDUCTION_CPU_OK,casos=7,paralelo=equivalente"
            << ",n=100003,empate_determinista=OK\n";
  return 0;
}
