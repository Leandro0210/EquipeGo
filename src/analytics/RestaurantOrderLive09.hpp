#pragma once

#include "analytics/RestaurantOrderScan.hpp"

#include <cstddef>
#include <deque>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace equipego::analytics {

// Indexar la configuracion una vez (fuera del camino critico).
// No dependemos de OpenCV ni del tipo SimulationConfig para probarlo.
template <class Restaurante>
std::unordered_map<std::string, std::size_t> indexarRestaurantes09(
    const std::vector<Restaurante>& restaurantes) {
  std::unordered_map<std::string, std::size_t> indices;
  indices.reserve(restaurantes.size());
  for (std::size_t r = 0; r < restaurantes.size(); ++r) {
    if (!indices.emplace(restaurantes[r].id, r).second) {
      throw std::invalid_argument("Scan09: restaurante duplicado: " + restaurantes[r].id);
    }
  }
  return indices;
}

struct VistaPendientesScan09 {
  AgrupacionPedidosRestaurante agrupacion;
  std::size_t gruposNoVacios = 0;
  std::size_t maximoGrupo = 0;
};

// Los indices del resultado se refieren a posiciones de la cola *en esta
// instantanea*. No copiamos ni movemos objetos Order y no cambiamos FIFO.
// Llamar desde el hilo Dispatcher mientras posee 'waiting', no desde Producer.
template <class ContenedorPedidos>
VistaPendientesScan09 agruparPendientesReales09(
    const ContenedorPedidos& waiting,
    const std::unordered_map<std::string, std::size_t>& indiceRestaurantes,
    std::size_t numeroRestaurantes) {
  if (indiceRestaurantes.size() != numeroRestaurantes) {
    throw std::invalid_argument("Scan09: mapa de restaurantes incompleto");
  }
  std::vector<std::size_t> ids;
  ids.reserve(waiting.size());
  for (const auto& order : waiting) {
    const auto it = indiceRestaurantes.find(order.restauranteId);
    if (it == indiceRestaurantes.end()) {
      throw std::invalid_argument("Scan09: restaurante desconocido: " + order.restauranteId);
    }
    ids.push_back(it->second);
  }
  VistaPendientesScan09 vista;
  vista.agrupacion = agruparPedidosScanCPU09(ids, numeroRestaurantes);
  for (std::size_t r = 0; r < numeroRestaurantes; ++r) {
    const auto n = vista.agrupacion.starts[r + 1] - vista.agrupacion.starts[r];
    if (n > 0) ++vista.gruposNoVacios;
    if (n > vista.maximoGrupo) vista.maximoGrupo = n;
  }
  return vista;
}

}  // namespace equipego::analytics
