#include "analytics/CourierLoadLive10.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace equipego::analytics;
static void exigir(bool ok, const char* why) {
  if (!ok) throw std::runtime_error(why);
}
struct CourierFalso10 {
  CourierSnapshot s;
  CourierSnapshot getSnapshot() const { return s; }
};
struct FleetFalsa10 {
  std::vector<CourierFalso10> datos;
  std::size_t size() const { return datos.size(); }
  CourierFalso10& at(std::size_t i) { return datos.at(i); }
};
int main() {
  try {
    FleetFalsa10 flota;
    const auto vacia = capturarCargaFleet10(flota);
    exigir(vacia.reservas.empty() && vacia.courierIds.empty(), "flota vacia");
    for (const auto& dato : std::vector<std::pair<std::string, std::size_t>>{
             {"C-a", 0}, {"C-b", 3}, {"C-c", 3}, {"C-d", 1}}) {
      CourierSnapshot s;
      s.id = dato.first;
      s.reservedCount = dato.second;
      flota.datos.push_back({s});
    }
    const auto captura = capturarCargaFleet10(flota);
    exigir(captura.reservas == std::vector<std::uint32_t>({0,3,3,1}),
           "valores de snapshots");
    exigir(captura.courierIds == std::vector<std::string>({"C-a","C-b","C-c","C-d"}),
           "orden y nombres");
    const auto r = reducirCargaCouriersCPU10(captura.reservas);
    exigir(r.couriers == 4 && r.conPedidos == 3 && r.totalReservas == 7 &&
           r.maxReservas == 3 && r.indiceMax == 1 && r.promedio() == 1.75,
           "resumen y desempate");
    // La captura es copia: cambios posteriores no mutan el resultado anterior.
    flota.datos[1].s.reservedCount = 0;
    exigir(captura.reservas[1] == 3 &&
           capturarCargaFleet10(flota).reservas[1] == 0, "inmutabilidad");
    if (sizeof(std::size_t) > sizeof(std::uint32_t)) {
      CourierSnapshot excesiva;
      excesiva.id = "C-max";
      excesiva.reservedCount =
          static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1;
      bool fallo = false;
      try { (void)prepararCargaSnapshots10({excesiva}); }
      catch (const std::overflow_error&) { fallo = true; }
      exigir(fallo, "proteccion overflow");
    }
    std::cout << "FEATURE10_FLEET_ADAPTADOR_TEST_OK,CASES=5,READ_ONLY=SI\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FEATURE10_FLEET_ADAPTADOR_ERROR:" << e.what() << '\n';
    return 1;
  }
}
