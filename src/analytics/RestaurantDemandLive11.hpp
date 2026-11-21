#pragma once
// Feature11 E4: demanda real por restaurante x minuto simulado.
// Solo lee eventos JSONL tras la auditoria; no cambia el simulador.
#include "analytics/RestaurantDemandHistogram11.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <istream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace equipego::analytics {

struct CapturaDemandaReal11 {
  std::vector<PedidoDemanda11> pedidos;
  std::vector<std::string> restaurantesIds;
  std::size_t franjas = 1;
  std::uint64_t intervaloMs = 60000;
};

inline CapturaDemandaReal11 capturarDemandaJSONL11(
    std::istream& entrada, const std::vector<std::string>& restaurantesIds,
    std::uint64_t intervaloMs = 60000) {
  if (restaurantesIds.empty() || intervaloMs == 0 ||
      restaurantesIds.size() > std::numeric_limits<std::uint32_t>::max())
    throw std::invalid_argument("Feature11 E4: restaurantes/intervalo invalidos");
  CapturaDemandaReal11 salida;
  salida.restaurantesIds = restaurantesIds;
  salida.intervaloMs = intervaloMs;
  std::unordered_map<std::string, std::uint32_t> porId;
  for (std::size_t i = 0; i < restaurantesIds.size(); ++i) {
    if (restaurantesIds[i].empty() ||
        !porId.emplace(restaurantesIds[i], static_cast<std::uint32_t>(i)).second)
      throw std::invalid_argument("Feature11 E4: id restaurante duplicado/vacio");
  }
  std::unordered_set<std::string> ordenes;
  std::string linea;
  std::size_t numeroLinea = 0;
  std::size_t maxFranja = 0;
  while (std::getline(entrada, linea)) {
    ++numeroLinea;
    if (linea.empty())
      throw std::runtime_error("Feature11 E4: linea JSONL vacia");
    const nlohmann::json e = nlohmann::json::parse(linea);
    if (!e.is_object() || !e.contains("event") || !e["event"].is_string())
      throw std::runtime_error("Feature11 E4: evento JSONL invalido");
    if (e.at("event").get<std::string>() != "orderCreated") continue;
    if (!e.contains("restaurant") || !e["restaurant"].is_string() ||
        !e.contains("order") || !e["order"].is_string() ||
        !e.contains("t") || !e["t"].is_number_integer())
      throw std::runtime_error("Feature11 E4: orderCreated incompleto");
    const auto id = e.at("restaurant").get<std::string>();
    const auto it = porId.find(id);
    if (it == porId.end())
      throw std::runtime_error("Feature11 E4: restaurante desconocido: " + id);
    const auto orden = e.at("order").get<std::string>();
    if (orden.empty() || !ordenes.insert(orden).second)
      throw std::runtime_error("Feature11 E4: orderCreated duplicado/vacio");
    const std::int64_t t = e.at("t").get<std::int64_t>();
    if (t < 0)
      throw std::runtime_error("Feature11 E4: tiempo negativo");
    const std::uint64_t bin = static_cast<std::uint64_t>(t) / intervaloMs;
    // Limite de diagnostico: evita asignar una matriz enorme por log corrupto.
    if (bin >= 20000u)
      throw std::runtime_error("Feature11 E4: demasiadas franjas de tiempo");
    maxFranja = std::max(maxFranja, static_cast<std::size_t>(bin));
    salida.pedidos.push_back({it->second, static_cast<std::uint32_t>(bin)});
  }
  if (!entrada.eof() || entrada.bad())
    throw std::runtime_error("Feature11 E4: fallo leyendo eventos JSONL");
  salida.franjas = salida.pedidos.empty() ? 1 : maxFranja + 1;
  return salida;
}

inline CapturaDemandaReal11 capturarDemandaArchivo11(
    const std::string& ruta, const std::vector<std::string>& restaurantes,
    std::uint64_t intervaloMs = 60000) {
  std::ifstream entrada(ruta);
  if (!entrada) throw std::runtime_error("Feature11 E4: no se puede leer JSONL: " + ruta);
  return capturarDemandaJSONL11(entrada, restaurantes, intervaloMs);
}

inline void escribirDemandaCSV11(std::ostream& out,
    const HistogramaDemanda11& hist, const std::vector<std::string>& restaurantes,
    std::uint64_t intervaloMs) {
  if (hist.restaurantes != restaurantes.size() || intervaloMs == 0)
    throw std::invalid_argument("Feature11 E4: CSV dimensiones invalidas");
  out << "restaurante,franja,inicio_ms,pedidos\n";
  for (std::size_t r = 0; r < hist.restaurantes; ++r)
    for (std::size_t f = 0; f < hist.franjas; ++f)
      out << restaurantes[r] << ',' << f << ','
          << (static_cast<std::uint64_t>(f) * intervaloMs) << ','
          << hist.en(r, f) << '\n';
  if (!out) throw std::runtime_error("Feature11 E4: error escribiendo CSV");
}

} // namespace equipego::analytics
