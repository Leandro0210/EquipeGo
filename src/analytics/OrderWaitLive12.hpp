#pragma once
// Feature12 E4: filtro observacional de esperas reales a partir de JSONL.
// Espera: desde orderCreated hasta PRIMER orderAssigned, orderRejected,
// o simulationStopping para pedidos aun pendientes. NO usa tiempo de entrega.
// Se conserva el orden de creacion, sin alterar la simulacion.
#include "analytics/OrderWaitFilter12.hpp"
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <istream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace equipego::analytics {

struct PedidoEsperaReal12 {
  std::string id;
  std::string restaurante;
  std::string estado = "pendiente";
  std::uint64_t creadoMs = 0;
  std::uint64_t finEsperaMs = 0;
  std::uint64_t esperaMs = 0;
};

struct CapturaEsperasReales12 {
  std::vector<PedidoEsperaReal12> pedidos;
  std::vector<std::uint64_t> esperasMs;
  std::size_t asignados = 0;
  std::size_t rechazados = 0;
  std::size_t pendientes = 0;
  std::uint64_t cierreMs = 0;
};

inline CapturaEsperasReales12 capturarEsperasJSONL12(std::istream& in) {
  CapturaEsperasReales12 result;
  std::unordered_map<std::string, std::size_t> porId;
  std::vector<bool> esperaCerrada;
  bool stopSeen = false;
  std::string linea;
  std::size_t numero = 0;
  while (std::getline(in, linea)) {
    ++numero;
    if (linea.empty())
      throw std::runtime_error("Feature12 E4: linea JSONL vacia");
    const auto ev = nlohmann::json::parse(linea);
    if (!ev.is_object() || !ev.contains("event") || !ev.at("event").is_string() ||
        !ev.contains("t") || !ev.at("t").is_number_integer())
      throw std::runtime_error("Feature12 E4: evento incompleto");
    const auto nombre = ev.at("event").get<std::string>();
    const std::int64_t tSigned = ev.at("t").get<std::int64_t>();
    if (tSigned < 0)
      throw std::runtime_error("Feature12 E4: tiempo negativo");
    const auto t = static_cast<std::uint64_t>(tSigned);
    if (stopSeen)
      throw std::runtime_error("Feature12 E4: evento despues del cierre");
    if (nombre == "orderCreated") {
      if (!ev.contains("order") || !ev.at("order").is_string() ||
          !ev.contains("restaurant") || !ev.at("restaurant").is_string())
        throw std::runtime_error("Feature12 E4: creacion incompleta");
      const std::string id = ev.at("order").get<std::string>();
      const std::string rest = ev.at("restaurant").get<std::string>();
      if (id.empty() || rest.empty() || !porId.emplace(id, result.pedidos.size()).second)
        throw std::runtime_error("Feature12 E4: creacion duplicada o vacia");
      result.pedidos.push_back({id, rest, "pendiente", t, t, 0});
      esperaCerrada.push_back(false);
    } else if (nombre == "orderAssigned" || nombre == "orderRejected") {
      if (!ev.contains("order") || !ev.at("order").is_string())
        throw std::runtime_error("Feature12 E4: orden sin id");
      const auto it = porId.find(ev.at("order").get<std::string>());
      if (it == porId.end())
        throw std::runtime_error("Feature12 E4: orden sin creacion");
      const auto pos = it->second;
      auto& pedido = result.pedidos[pos];
      if (t < pedido.creadoMs)
        throw std::runtime_error("Feature12 E4: espera negativa");
      // Solo la primera asignacion define la espera. Una reasignacion posterior
      // no altera el dato observado.
      if (nombre == "orderAssigned" && esperaCerrada[pos]) continue;
      if (esperaCerrada[pos])
        throw std::runtime_error("Feature12 E4: cierre de espera duplicado");
      esperaCerrada[pos] = true;
      pedido.finEsperaMs = t;
      pedido.esperaMs = t - pedido.creadoMs;
      pedido.estado = nombre == "orderAssigned" ? "asignado" : "rechazado";
      if (nombre == "orderAssigned") ++result.asignados;
      else ++result.rechazados;
    } else if (nombre == "simulationStopping") {
      result.cierreMs = t;
      stopSeen = true;
    }
  }
  if (!in.eof() || in.bad())
    throw std::runtime_error("Feature12 E4: fallo leyendo JSONL");
  if (!stopSeen)
    throw std::runtime_error("Feature12 E4: simulationStopping ausente");
  result.esperasMs.reserve(result.pedidos.size());
  for (std::size_t i = 0; i < result.pedidos.size(); ++i) {
    auto& p = result.pedidos[i];
    if (!esperaCerrada[i]) {
      if (result.cierreMs < p.creadoMs)
        throw std::runtime_error("Feature12 E4: cierre anterior a creacion");
      p.finEsperaMs = result.cierreMs;
      p.esperaMs = result.cierreMs - p.creadoMs;
      ++result.pendientes;
    }
    result.esperasMs.push_back(p.esperaMs);
  }
  return result;
}

inline CapturaEsperasReales12 capturarEsperasArchivo12(const std::string& ruta) {
  std::ifstream in(ruta);
  if (!in) throw std::runtime_error("Feature12 E4: no se puede leer JSONL: " + ruta);
  return capturarEsperasJSONL12(in);
}

inline void campoCSV12(std::ostream& out, const std::string& valor) {
  out << '"';
  for (char c : valor) {
    if (c == '"') out << '"';
    out << c;
  }
  out << '"';
}

inline void escribirFiltroCSV12(std::ostream& out,
    const CapturaEsperasReales12& datos, const ResultadoFiltro12& filtro) {
  if (filtro.totalEntradas != datos.pedidos.size() ||
      datos.esperasMs.size() != datos.pedidos.size())
    throw std::invalid_argument("Feature12 E4: dimensiones invalidas");
  out << "indice_original,order,restaurant,estado,creado_ms,fin_espera_ms,espera_ms,umbral_ms\n";
  std::size_t anterior = 0;
  bool primero = true;
  for (std::size_t idx : filtro.indices) {
    if (idx >= datos.pedidos.size() || (!primero && idx <= anterior))
      throw std::invalid_argument("Feature12 E4: indices no estables");
    const auto& p = datos.pedidos[idx];
    if (p.esperaMs <= filtro.umbralMs || p.esperaMs != datos.esperasMs[idx])
      throw std::invalid_argument("Feature12 E4: seleccion inconsistente");
    out << idx << ',';
    campoCSV12(out, p.id);
    out << ',';
    campoCSV12(out, p.restaurante);
    out << ',' << p.estado << ',' << p.creadoMs << ',' << p.finEsperaMs
        << ',' << p.esperaMs << ',' << filtro.umbralMs << '\n';
    anterior = idx;
    primero = false;
  }
  if (!out) throw std::runtime_error("Feature12 E4: error escribiendo CSV");
}

} // namespace equipego::analytics
