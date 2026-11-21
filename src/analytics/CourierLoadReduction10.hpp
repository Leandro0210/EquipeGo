#pragma once
// Feature 10: reduction de carga operativa en snapshots de couriers.
// No selecciona ETA ni modifica Fleet/Dispatcher; trabaja sobre copias.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace equipego::analytics {

struct ResumenCarga10 {
  std::size_t couriers = 0;
  std::size_t conPedidos = 0;
  std::uint64_t totalReservas = 0;
  std::uint32_t maxReservas = 0;
  std::size_t indiceMax = 0;
  bool tieneMax = false;

  double promedio() const noexcept {
    return couriers == 0 ? 0.0 :
        static_cast<double>(totalReservas) / static_cast<double>(couriers);
  }
};

inline void acumularCarga10(ResumenCarga10& r, std::uint32_t carga,
                            std::size_t indice) noexcept {
  ++r.couriers;
  r.totalReservas += carga;
  if (carga > 0) ++r.conPedidos;
  if (!r.tieneMax || carga > r.maxReservas ||
      (carga == r.maxReservas && indice < r.indiceMax)) {
    r.maxReservas = carga;
    r.indiceMax = indice;
    r.tieneMax = true;
  }
}

inline void combinarCarga10(ResumenCarga10& dst,
                             const ResumenCarga10& src) noexcept {
  dst.couriers += src.couriers;
  dst.conPedidos += src.conPedidos;
  dst.totalReservas += src.totalReservas;
  if (src.tieneMax &&
      (!dst.tieneMax || src.maxReservas > dst.maxReservas ||
       (src.maxReservas == dst.maxReservas &&
        src.indiceMax < dst.indiceMax))) {
    dst.maxReservas = src.maxReservas;
    dst.indiceMax = src.indiceMax;
    dst.tieneMax = true;
  }
}

// Referencia secuencial determinista. El indice es la posicion del snapshot.
inline ResumenCarga10 reducirCargaCouriersCPU10(
    const std::vector<std::uint32_t>& reservas) noexcept {
  ResumenCarga10 r;
  for (std::size_t i = 0; i < reservas.size(); ++i)
    acumularCarga10(r, reservas[i], i);
  return r;
}

// Reduccion CPU paralela: un acumulador por particion, merge determinista.
inline ResumenCarga10 reducirCargaCouriersCPUParalelo10(
    const std::vector<std::uint32_t>& reservas, std::size_t hilos) {
  if (hilos == 0)
    throw std::invalid_argument("Feature10: hilos debe ser mayor que cero");
  if (reservas.empty()) return {};
  const std::size_t trabajadores = std::min(hilos, reservas.size());
  if (trabajadores == 1) return reducirCargaCouriersCPU10(reservas);
  std::vector<ResumenCarga10> parciales(trabajadores);
  std::vector<std::thread> pool;
  pool.reserve(trabajadores - 1);
  auto trabajo = [&](std::size_t t) {
    const std::size_t inicio = reservas.size() / trabajadores * t +
        std::min(t, reservas.size() % trabajadores);
    const std::size_t fin = reservas.size() / trabajadores * (t + 1) +
        std::min(t + 1, reservas.size() % trabajadores);
    auto& parcial = parciales[t];
    for (std::size_t i = inicio; i < fin; ++i)
      acumularCarga10(parcial, reservas[i], i);
  };
  for (std::size_t t = 1; t < trabajadores; ++t)
    pool.emplace_back(trabajo, t);
  trabajo(0);
  for (auto& hilo : pool) hilo.join();
  ResumenCarga10 resultado;
  for (const auto& parcial : parciales) combinarCarga10(resultado, parcial);
  return resultado;
}

}  // namespace equipego::analytics
