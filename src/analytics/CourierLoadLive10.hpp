#pragma once
// Feature10 E4: estadisticas observacionales de la flota real.
// Capturas individuales mutex-protegidas, no transaccion global de Fleet.
#include "analytics/CourierLoadReduction10.hpp"
#include "fleet/Courier.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace equipego::analytics {

struct CapturaCargaFleet10 {
  std::vector<std::uint32_t> reservas;
  std::vector<std::string> courierIds;
};

inline CapturaCargaFleet10 prepararCargaSnapshots10(
    const std::vector<CourierSnapshot>& snapshots) {
  CapturaCargaFleet10 salida;
  salida.reservas.reserve(snapshots.size());
  salida.courierIds.reserve(snapshots.size());
  for (const auto& courier : snapshots) {
    if (courier.reservedCount >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
      throw std::overflow_error("FEATURE10: reservas exceden uint32");
    }
    salida.reservas.push_back(static_cast<std::uint32_t>(courier.reservedCount));
    salida.courierIds.push_back(courier.id);
  }
  return salida;
}

// Compatible con Fleet real y con doubles de prueba que ofrezcan size/at/getSnapshot.
// Fleet::at(i).getSnapshot() protege el estado de CADA courier por mutex.
// No se requiere una instantanea global atomica entre couriers distintos.
template <typename FleetLike>
inline CapturaCargaFleet10 capturarCargaFleet10(FleetLike& fleet) {
  std::vector<CourierSnapshot> snapshots;
  snapshots.reserve(fleet.size());
  for (std::size_t i = 0; i < fleet.size(); ++i)
    snapshots.push_back(fleet.at(i).getSnapshot());
  return prepararCargaSnapshots10(snapshots);
}

} // namespace equipego::analytics
