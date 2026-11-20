#pragma once

// Feature 04: vistas de instantaneas de repartidores. No muta el Fleet real.
// Posiciones x/y representan metros proyectados de nodos OSM.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace equipego::performance04 {

struct CourierAoS04 {
  float xMeters = 0.0f;
  float yMeters = 0.0f;
  float speedMetersSecond = 1.0f;
  std::int32_t reserved = 0;
  std::int32_t capacity = 1;
  std::int32_t state = 0; // 0=disponible, 1=ocupado, 2=averiado
};
static_assert(sizeof(CourierAoS04) == 24, "AoS04 requiere 24 bytes para comparacion");

struct CourierSoA04 {
  std::vector<float> xMeters;
  std::vector<float> yMeters;
  std::vector<float> speedMetersSecond;
  std::vector<std::int32_t> reserved;
  std::vector<std::int32_t> capacity;
  std::vector<std::int32_t> state;

  std::size_t size() const noexcept { return xMeters.size(); }
};

inline CourierSoA04 convertirSoA(const std::vector<CourierAoS04>& registros) {
  CourierSoA04 out;
  const auto n = registros.size();
  out.xMeters.reserve(n);
  out.yMeters.reserve(n);
  out.speedMetersSecond.reserve(n);
  out.reserved.reserve(n);
  out.capacity.reserve(n);
  out.state.reserve(n);
  for (const auto& r : registros) {
    out.xMeters.push_back(r.xMeters);
    out.yMeters.push_back(r.yMeters);
    out.speedMetersSecond.push_back(r.speedMetersSecond);
    out.reserved.push_back(r.reserved);
    out.capacity.push_back(r.capacity);
    out.state.push_back(r.state);
  }
  return out;
}

inline bool valido(const CourierAoS04& c) noexcept {
  return c.state == 0 && c.capacity > 0 && c.reserved >= 0 &&
         c.reserved < c.capacity && std::isfinite(c.xMeters) &&
         std::isfinite(c.yMeters) && std::isfinite(c.speedMetersSecond) &&
         c.speedMetersSecond > 0.0f;
}

// Tiempo aproximado en segundos usando distancia plana: NO es ETA oficial.
// El dispatcher sigue usando RoadNetwork y Dijkstra/Floyd validado.
inline float tiempoAproximado(float x, float y, float speed,
                              float restaurantX, float restaurantY,
                              float restaurantToCustomerMeters) noexcept {
  const float dx = x - restaurantX;
  const float dy = y - restaurantY;
  return (std::sqrt(dx * dx + dy * dy) + restaurantToCustomerMeters) / speed;
}

inline void validarSoA(const CourierSoA04& v) {
  const auto n = v.xMeters.size();
  if (v.yMeters.size() != n || v.speedMetersSecond.size() != n ||
      v.reserved.size() != n || v.capacity.size() != n ||
      v.state.size() != n) {
    throw std::invalid_argument("CourierSoA04: tamanos de columnas diferentes");
  }
}

// Threading por particiones independientes. Copias de instantaneas, sin locks.
template <typename Body>
inline void distribuir(std::size_t n, unsigned int hilos, Body body) {
  if (hilos == 0) throw std::invalid_argument("Cantidad de hilos = 0");
  if (n == 0) return;
  const auto trabajadores = static_cast<unsigned int>(
      std::min<std::size_t>(n, hilos));
  if (trabajadores == 1) {
    body(0, n);
    return;
  }
  std::vector<std::thread> pool;
  pool.reserve(trabajadores - 1);
  for (unsigned int t = 1; t < trabajadores; ++t) {
    pool.emplace_back([&, t] {
      body(n * t / trabajadores, n * (t + 1) / trabajadores);
    });
  }
  body(0, n / trabajadores);
  for (auto& hilo : pool) hilo.join();
}

inline std::vector<float> calcularAoS(const std::vector<CourierAoS04>& registros,
                                      float xRest, float yRest,
                                      float tramoCliente, unsigned int hilos = 1,
                                      unsigned int barridos = 1) {
  if (barridos == 0) throw std::invalid_argument("barridos = 0");
  std::vector<float> resultado(registros.size(), -1.0f);
  distribuir(registros.size(), hilos, [&](std::size_t inicio, std::size_t fin) {
    for (unsigned int vuelta = 0; vuelta < barridos; ++vuelta) {
      const float x = xRest + static_cast<float>(vuelta) * 0.03125f;
      for (std::size_t i = inicio; i < fin; ++i) {
        const auto& c = registros[i];
        if (valido(c)) {
          const float value = tiempoAproximado(c.xMeters, c.yMeters,
                                               c.speedMetersSecond, x, yRest,
                                               tramoCliente);
          resultado[i] = (vuelta == 0) ? value : resultado[i] + value;
        } else {
          resultado[i] = -1.0f;
        }
      }
    }
  });
  return resultado;
}

inline std::vector<float> calcularSoA(const CourierSoA04& registros,
                                      float xRest, float yRest,
                                      float tramoCliente, unsigned int hilos = 1,
                                      unsigned int barridos = 1) {
  validarSoA(registros);
  if (barridos == 0) throw std::invalid_argument("barridos = 0");
  std::vector<float> resultado(registros.size(), -1.0f);
  distribuir(registros.size(), hilos, [&](std::size_t inicio, std::size_t fin) {
    for (unsigned int vuelta = 0; vuelta < barridos; ++vuelta) {
      const float x = xRest + static_cast<float>(vuelta) * 0.03125f;
      for (std::size_t i = inicio; i < fin; ++i) {
        const bool activo = registros.state[i] == 0 && registros.capacity[i] > 0 &&
            registros.reserved[i] >= 0 &&
            registros.reserved[i] < registros.capacity[i] &&
            std::isfinite(registros.xMeters[i]) &&
            std::isfinite(registros.yMeters[i]) &&
            std::isfinite(registros.speedMetersSecond[i]) &&
            registros.speedMetersSecond[i] > 0.0f;
        if (activo) {
          const float value = tiempoAproximado(registros.xMeters[i],
                                               registros.yMeters[i],
                                               registros.speedMetersSecond[i],
                                               x, yRest, tramoCliente);
          resultado[i] = (vuelta == 0) ? value : resultado[i] + value;
        } else {
          resultado[i] = -1.0f;
        }
      }
    }
  });
  return resultado;
}

}  // namespace equipego::performance04
