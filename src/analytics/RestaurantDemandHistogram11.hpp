#pragma once
// Feature 11: demanda por restaurante y franja de tiempo.
// Histograma 2D por contadores. Entrada inmutable, no afecta Dispatcher.
// Distinto del Scan09: no reordena pedidos ni genera offsets.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace equipego::analytics {

struct PedidoDemanda11 {
  std::uint32_t restaurante = 0;
  std::uint32_t franja = 0;
};

struct HistogramaDemanda11 {
  std::size_t restaurantes = 0;
  std::size_t franjas = 0;
  std::vector<std::uint64_t> celdas;
  std::vector<std::uint64_t> porRestaurante;
  std::uint64_t totalPedidos = 0;

  std::uint64_t en(std::size_t restaurante, std::size_t franja) const {
    if (restaurante >= restaurantes || franja >= franjas)
      throw std::out_of_range("Feature11: indice de celda invalido");
    return celdas[restaurante * franjas + franja];
  }
};

inline HistogramaDemanda11 crearHistogramaDemanda11(
    std::size_t restaurantes, std::size_t franjas) {
  if (restaurantes == 0 || franjas == 0 ||
      restaurantes > std::numeric_limits<std::size_t>::max() / franjas)
    throw std::invalid_argument("Feature11: dimensiones invalidas");
  HistogramaDemanda11 h;
  h.restaurantes = restaurantes;
  h.franjas = franjas;
  h.celdas.assign(restaurantes * franjas, 0);
  h.porRestaurante.assign(restaurantes, 0);
  return h;
}

inline void validarPedidosDemanda11(
    const std::vector<PedidoDemanda11>& eventos,
    std::size_t restaurantes, std::size_t franjas) {
  for (const auto& e : eventos)
    if (e.restaurante >= restaurantes || e.franja >= franjas)
      throw std::invalid_argument("Feature11: pedido fuera de rango");
}

inline HistogramaDemanda11 histogramaDemandaCPU11(
    const std::vector<PedidoDemanda11>& eventos,
    std::size_t restaurantes, std::size_t franjas) {
  auto h = crearHistogramaDemanda11(restaurantes, franjas);
  validarPedidosDemanda11(eventos, restaurantes, franjas);
  for (const auto& e : eventos) {
    ++h.celdas[static_cast<std::size_t>(e.restaurante) * franjas + e.franja];
    ++h.porRestaurante[e.restaurante];
    ++h.totalPedidos;
  }
  return h;
}

// Un histograma privado por hilo; merge final sin atomics, resultado exacto.
inline HistogramaDemanda11 histogramaDemandaCPUParalelo11(
    const std::vector<PedidoDemanda11>& eventos,
    std::size_t restaurantes, std::size_t franjas, std::size_t hilos) {
  if (hilos == 0)
    throw std::invalid_argument("Feature11: hilos debe ser mayor que cero");
  auto h = crearHistogramaDemanda11(restaurantes, franjas);
  validarPedidosDemanda11(eventos, restaurantes, franjas);
  if (eventos.empty()) return h;
  const std::size_t trabajadores = std::min(hilos, eventos.size());
  if (trabajadores == 1) return histogramaDemandaCPU11(eventos, restaurantes, franjas);
  std::vector<HistogramaDemanda11> privados;
  privados.reserve(trabajadores);
  for (std::size_t i = 0; i < trabajadores; ++i)
    privados.push_back(crearHistogramaDemanda11(restaurantes, franjas));
  auto procesar = [&](std::size_t t) {
    const auto inicio = eventos.size() * t / trabajadores;
    const auto fin = eventos.size() * (t + 1) / trabajadores;
    auto& local = privados[t];
    for (std::size_t i = inicio; i < fin; ++i) {
      const auto& e = eventos[i];
      ++local.celdas[static_cast<std::size_t>(e.restaurante) * franjas + e.franja];
      ++local.porRestaurante[e.restaurante];
      ++local.totalPedidos;
    }
  };
  std::vector<std::thread> pool;
  pool.reserve(trabajadores - 1);
  for (std::size_t t = 1; t < trabajadores; ++t)
    pool.emplace_back(procesar, t);
  procesar(0);
  for (auto& thread : pool) thread.join();
  for (const auto& local : privados) {
    for (std::size_t i = 0; i < h.celdas.size(); ++i)
      h.celdas[i] += local.celdas[i];
    for (std::size_t i = 0; i < restaurantes; ++i)
      h.porRestaurante[i] += local.porRestaurante[i];
    h.totalPedidos += local.totalPedidos;
  }
  return h;
}

inline bool histogramasIguales11(
    const HistogramaDemanda11& a, const HistogramaDemanda11& b) noexcept {
  return a.restaurantes == b.restaurantes && a.franjas == b.franjas &&
      a.celdas == b.celdas && a.porRestaurante == b.porRestaurante &&
      a.totalPedidos == b.totalPedidos;
}

} // namespace equipego::analytics
