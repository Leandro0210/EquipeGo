#pragma once

// Feature 06, Stencil: campo diagnostico de cobertura.
// Inspiracion: ec2/06_stencil/{02_stencil_2d.cu,03_jacobi_sweeps.cu,
// bikeshare_station_coverage.cu}.
// Datos: posiciones de couriers sinteticos en nodos reales OSM.
// No cambia Fleet, Dispatcher, RoadNetwork ni asignacion de pedidos.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace equipego::stencil06 {

struct PuntoNormalizado06 {
  float x = 0.0f;
  float y = 0.0f;
};

struct Campo06 {
  std::size_t lado = 0;
  std::vector<float> valores;
  std::vector<std::uint8_t> fijas; // 1: borde o fuente fija de courier
};

inline void validar(const Campo06& c) {
  if (c.lado < 3 || c.lado > 4096 ||
      c.lado > std::numeric_limits<std::size_t>::max() / c.lado ||
      c.valores.size() != c.lado * c.lado ||
      c.fijas.size() != c.lado * c.lado) {
    throw std::invalid_argument("STENCIL06: tamano de campo invalido");
  }
  for (std::size_t i = 0; i < c.valores.size(); ++i) {
    if (!std::isfinite(c.valores[i]) || c.valores[i] < 0.0f ||
        c.valores[i] > 1.0f || c.fijas[i] > 1) {
      throw std::invalid_argument("STENCIL06: valor o mascara invalida");
    }
    const std::size_t row = i / c.lado;
    const std::size_t col = i % c.lado;
    if ((row == 0 || col == 0 || row == c.lado - 1 || col == c.lado - 1) &&
        (c.fijas[i] != 1 || c.valores[i] != 0.0f)) {
      throw std::invalid_argument("STENCIL06: borde no es cero fijo");
    }
  }
}

inline Campo06 construirCampo(
    const std::vector<PuntoNormalizado06>& couriers, std::size_t lado) {
  if (lado < 3 || lado > 4096) {
    throw std::invalid_argument("STENCIL06: lado fuera de rango");
  }
  Campo06 c;
  c.lado = lado;
  c.valores.assign(lado * lado, 0.0f);
  c.fijas.assign(lado * lado, 0);
  for (std::size_t i = 0; i < lado; ++i) {
    c.fijas[i] = 1;
    c.fijas[(lado - 1) * lado + i] = 1;
    c.fijas[i * lado] = 1;
    c.fijas[i * lado + lado - 1] = 1;
  }
  for (const auto& p : couriers) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
        p.x < 0.0f || p.x > 1.0f || p.y < 0.0f || p.y > 1.0f) {
      throw std::invalid_argument("STENCIL06: coordenadas fuera de [0,1]");
    }
    // Proyecta las fuentes en el interior; fronteras permanecen en 0.
    const auto col = std::clamp(
        static_cast<std::size_t>(p.x * static_cast<float>(lado)),
        std::size_t{1}, lado - 2);
    const auto row = std::clamp(
        static_cast<std::size_t>(p.y * static_cast<float>(lado)),
        std::size_t{1}, lado - 2);
    const auto idx = row * lado + col;
    c.fijas[idx] = 1;
    c.valores[idx] = 1.0f;
  }
  return c;
}

inline std::size_t contarFuentes(const Campo06& c) {
  validar(c);
  std::size_t n = 0;
  for (std::size_t r = 1; r + 1 < c.lado; ++r) {
    for (std::size_t col = 1; col + 1 < c.lado; ++col) {
      if (c.fijas[r * c.lado + col]) ++n;
    }
  }
  return n;
}

// Una celda lee el estado anterior y escribe en otro buffer: Jacobi (no Gauss-Seidel).
// cC + 4*cN = 1 preserva el rango [0,1] para datos no negativos acotados.
inline float actualizarCelda(const std::vector<float>& in,
                             std::size_t i, std::size_t lado,
                             float cC, float cN) noexcept {
  return cC * in[i] + cN *
      (in[i - lado] + in[i + lado] + in[i - 1] + in[i + 1]);
}

inline void validarParametros(float cC, float cN, unsigned int hilos) {
  if (hilos == 0 || !std::isfinite(cC) || !std::isfinite(cN) ||
      cC < 0.0f || cN < 0.0f ||
      std::fabs(static_cast<double>(cC) + 4.0 * cN - 1.0) > 1e-6) {
    throw std::invalid_argument("STENCIL06: parametros no estables");
  }
}

inline void barrido(const Campo06& config,
                    const std::vector<float>& in,
                    std::vector<float>& out,
                    unsigned int hilos, float cC, float cN) {
  const auto lado = config.lado;
  const auto workers = std::min<std::size_t>(hilos, lado);
  const auto ejecutarFilas = [&](std::size_t inicio, std::size_t fin) {
    for (std::size_t r = inicio; r < fin; ++r) {
      for (std::size_t col = 0; col < lado; ++col) {
        const std::size_t idx = r * lado + col;
        if (config.fijas[idx]) {
          out[idx] = config.valores[idx];
        } else {
          out[idx] = actualizarCelda(in, idx, lado, cC, cN);
        }
      }
    }
  };
  if (workers == 1) {
    ejecutarFilas(0, lado);
    return;
  }
  std::vector<std::thread> pool;
  pool.reserve(workers - 1);
  for (std::size_t t = 1; t < workers; ++t) {
    pool.emplace_back([&, t] {
      ejecutarFilas(lado * t / workers, lado * (t + 1) / workers);
    });
  }
  ejecutarFilas(0, lado / workers);
  for (auto& t : pool) t.join();
}

inline std::vector<float> evolucionar(const Campo06& config,
                                     unsigned int sweeps,
                                     unsigned int hilos = 1,
                                     float cC = 0.2f,
                                     float cN = 0.2f) {
  validar(config);
  validarParametros(cC, cN, hilos);
  std::vector<float> actual = config.valores;
  std::vector<float> siguiente = config.valores;
  for (unsigned int s = 0; s < sweeps; ++s) {
    barrido(config, actual, siguiente, hilos, cC, cN);
    actual.swap(siguiente);
  }
  return actual;
}

inline double suma(const std::vector<float>& datos) noexcept {
  double total = 0.0;
  for (const auto x : datos) total += static_cast<double>(x);
  return total;
}

} // namespace equipego::stencil06
