#pragma once

// Feature 04 / Etapa 3: snapshot REAL de Fleet, solo diagnostico CPU.
// No altera ETA del Dispatcher, estados, pedidos ni comportamiento de Courier.
#include "analytics/CourierLayout04.hpp"
#include "config/Config.hpp"
#include "fleet/Fleet.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace equipego::performance04 {

struct PuntoMetros04 {
  float x = 0.0f;
  float y = 0.0f;
};

// Proyeccion equirectangular LOCAL para experimento de layout (no ETA vial).
inline PuntoMetros04 proyectarMetros04(const SimulationConfig& config,
                                       double lat, double lon) {
  const auto& b = config.map.bounds;
  if (!std::isfinite(b.north) || !std::isfinite(b.south) ||
      !std::isfinite(b.east) || !std::isfinite(b.west) ||
      !(b.north > b.south) || !(b.east > b.west) ||
      !std::isfinite(lat) || !std::isfinite(lon)) {
    throw std::invalid_argument("LAYOUT04_RUNTIME: coordenadas o mapa invalidos");
  }
  constexpr double rad = 0.01745329251994329577;
  constexpr double radioTierra = 6371000.0;
  const double lat0 = (b.north + b.south) * 0.5;
  const double x = radioTierra * (lon - b.west) * rad * std::cos(lat0 * rad);
  const double y = radioTierra * (lat - b.south) * rad;
  if (!std::isfinite(x) || !std::isfinite(y) ||
      std::abs(x) > std::numeric_limits<float>::max() ||
      std::abs(y) > std::numeric_limits<float>::max()) {
    throw std::invalid_argument("LAYOUT04_RUNTIME: proyeccion fuera de rango");
  }
  return {static_cast<float>(x), static_cast<float>(y)};
}

inline std::pair<double, double> nodoLatLon04(const SimulationConfig& config,
                                              const std::string& id) {
  const auto it = config.nodeIndexById.find(id);
  if (it == config.nodeIndexById.end() || it->second >= config.nodes.size()) {
    throw std::invalid_argument("LAYOUT04_RUNTIME: nodo inexistente: " + id);
  }
  const auto& n = config.nodes[it->second];
  return {n.lat, n.lon};
}

inline PuntoMetros04 posicionSnapshot04(const SimulationConfig& config,
                                         const CourierSnapshot& snap) {
  auto [lat, lon] = nodoLatLon04(config, snap.currentNode);
  if (snap.motion) {
    const auto& m = *snap.motion;
    if (!std::isfinite(m.progress) || m.progress < 0.0 || m.progress > 1.0) {
      throw std::invalid_argument("LAYOUT04_RUNTIME: progreso de movimiento invalido");
    }
    const auto [lat0, lon0] = nodoLatLon04(config, m.from);
    const auto [lat1, lon1] = nodoLatLon04(config, m.to);
    lat = lat0 + (lat1 - lat0) * m.progress;
    lon = lon0 + (lon1 - lon0) * m.progress;
  }
  return proyectarMetros04(config, lat, lon);
}

inline std::vector<CourierAoS04> prepararSnapshots04(
    const SimulationConfig& config, const std::vector<CourierSnapshot>& snaps) {
  if (config.fleet.bagCapacity <= 0 ||
      !(std::isfinite(config.fleet.speedKmh) && config.fleet.speedKmh > 0)) {
    throw std::invalid_argument("LAYOUT04_RUNTIME: configuracion Fleet invalida");
  }
  if (snaps.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
    throw std::invalid_argument("LAYOUT04_RUNTIME: demasiados couriers");
  }
  std::vector<CourierAoS04> result;
  result.reserve(snaps.size());
  for (const auto& snap : snaps) {
    if (snap.reservedCount > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
      throw std::invalid_argument("LAYOUT04_RUNTIME: conteo reservado fuera de rango");
    }
    const auto pos = posicionSnapshot04(config, snap);
    // Moving puede aceptar mas pedidos si tiene espacio, tal como Dispatcher.
    result.push_back(CourierAoS04{
        pos.x, pos.y, static_cast<float>(config.fleet.speedKmh / 3.6),
        static_cast<std::int32_t>(snap.reservedCount),
        static_cast<std::int32_t>(config.fleet.bagCapacity),
        snap.state == CourierState::Broken ? 2 : 0});
  }
  return result;
}

inline std::vector<CourierAoS04> capturarFleet04(const SimulationConfig& config,
                                                 Fleet& fleet) {
  std::vector<CourierSnapshot> snaps;
  snaps.reserve(fleet.size());
  for (std::size_t i = 0; i < fleet.size(); ++i) {
    snaps.push_back(fleet.at(i).getSnapshot());
  }
  return prepararSnapshots04(config, snaps);
}

struct ComparacionRuntime04 {
  std::size_t couriers = 0;
  std::size_t disponibles = 0;
  double sumaSegundos = 0.0;  // Diagnostico, NO ETA oficial
};

inline ComparacionRuntime04 compararLayoutRuntime04(
    const SimulationConfig& config, const std::vector<CourierAoS04>& aos) {
  if (config.restaurants.empty()) {
    throw std::invalid_argument("LAYOUT04_RUNTIME: sin restaurantes");
  }
  const auto [lat, lon] = nodoLatLon04(config, config.restaurants.front().nodeId);
  const auto target = proyectarMetros04(config, lat, lon);
  const auto soa = convertirSoA(aos);
  const auto rAos = calcularAoS(aos, target.x, target.y, 0.0f, 1, 1);
  const auto rSoa = calcularSoA(soa, target.x, target.y, 0.0f, 1, 1);
  if (rAos.size() != rSoa.size()) {
    throw std::logic_error("LAYOUT04_RUNTIME: longitudes distintas");
  }
  ComparacionRuntime04 stats;
  stats.couriers = aos.size();
  for (std::size_t i = 0; i < rAos.size(); ++i) {
    if (rAos[i] != rSoa[i]) {
      throw std::logic_error("LAYOUT04_RUNTIME: diferencia AoS/SoA");
    }
    if (rAos[i] >= 0.0f) {
      ++stats.disponibles;
      stats.sumaSegundos += rAos[i];
    }
  }
  return stats;
}

}  // namespace equipego::performance04
