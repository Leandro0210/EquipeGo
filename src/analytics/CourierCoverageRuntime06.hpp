#pragma once

// Feature 06 E3: mapa de cobertura post-simulacion usando posiciones del
// Fleet. SOLO lectura: no cambia Courier, Dispatcher, rutas ni pedidos.
// Estimacion cualitativa sobre imagen OSM, no cobertura vial ni demanda real.
#include "analytics/CourierCoverageLive06.hpp"
#include "config/Config.hpp"
#include "fleet/Fleet.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace equipego::stencil06 {

inline InformeCobertura06 capturarFleet06(const SimulationConfig& config,
                                          Fleet& fleet,
                                          std::size_t lado = 512) {
  const auto& bounds = config.map.bounds;
  if (!(bounds.east > bounds.west && bounds.north > bounds.south))
    throw std::invalid_argument("STENCIL06_RUNTIME: bounds invalidos");

  auto puntoNodo = [&](const std::string& nodeId) -> std::pair<double, double> {
    const auto it = config.nodeIndexById.find(nodeId);
    if (it == config.nodeIndexById.end() || it->second >= config.nodes.size())
      throw std::runtime_error("STENCIL06_RUNTIME: nodo desconocido: " + nodeId);
    const auto& node = config.nodes[it->second];
    if (!std::isfinite(node.lat) || !std::isfinite(node.lon))
      throw std::runtime_error("STENCIL06_RUNTIME: coordenadas OSM invalidas");
    return {node.lon, node.lat};
  };

  std::vector<Muestra06> muestras;
  muestras.reserve(fleet.size());
  for (std::size_t i = 0; i < fleet.size(); ++i) {
    const CourierSnapshot snapshot = fleet.at(i).getSnapshot();
    if (snapshot.state == CourierState::Broken) {
      muestras.push_back({0.0f, 0.0f, true, false});
      continue;
    }
    auto [lon, lat] = puntoNodo(snapshot.currentNode);
    bool interpolado = false;
    if (snapshot.motion) {
      const auto& movimiento = *snapshot.motion;
      if (!std::isfinite(movimiento.progress) ||
          movimiento.progress < 0.0 || movimiento.progress > 1.0)
        throw std::runtime_error("STENCIL06_RUNTIME: progreso invalido");
      const auto [lon0, lat0] = puntoNodo(movimiento.from);
      const auto [lon1, lat1] = puntoNodo(movimiento.to);
      lon = lon0 + (lon1 - lon0) * movimiento.progress;
      lat = lat0 + (lat1 - lat0) * movimiento.progress;
      interpolado = true;
    }
    const double x = (lon - bounds.west) / (bounds.east - bounds.west);
    const double y = (bounds.north - lat) / (bounds.north - bounds.south);
    // Pequeñas diferencias numericas no deben fallar si el nodo esta en un borde.
    if (!std::isfinite(x) || !std::isfinite(y) ||
        x < -1e-6 || x > 1.0 + 1e-6 ||
        y < -1e-6 || y > 1.0 + 1e-6)
      throw std::runtime_error("STENCIL06_RUNTIME: courier fuera del mapa");
    muestras.push_back({static_cast<float>(std::clamp(x, 0.0, 1.0)),
                       static_cast<float>(std::clamp(y, 0.0, 1.0)),
                       false, interpolado});
  }
  return proyectarCobertura06(muestras, lado);
}

inline void guardarCobertura06(const SimulationConfig& config,
                               const InformeCobertura06& informe,
                               const std::vector<float>& valores,
                               const std::string& prefijo,
                               unsigned int barridos = 20,
                               const std::string& backend = "CPU") {
  namespace fs = std::filesystem;
  if (prefijo.empty() || prefijo.size() > 4096 ||
      (backend != "CPU" && backend != "GPU") ||
      valores.size() != informe.campo.valores.size() ||
      config.map.image.empty())
    throw std::invalid_argument("STENCIL06_RUNTIME: argumentos invalidos");
  validar(informe.campo);
  if (informe.campo.lado > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("STENCIL06_RUNTIME: lado fuera de rango");
  fs::path base(prefijo);
  const fs::path png(prefijo + ".png");
  const fs::path csv(prefijo + ".csv");
  const fs::path json(prefijo + ".json");
  for (const auto& p : {png, csv, json}) {
    if (fs::exists(p))
      throw std::runtime_error("STENCIL06_RUNTIME: no sobrescribir evidencia: " + p.string());
  }
  if (base.has_parent_path()) fs::create_directories(base.parent_path());
  cv::Mat img;
  cv::resize(config.map.image, img,
             cv::Size(static_cast<int>(informe.campo.lado),
                      static_cast<int>(informe.campo.lado)));
  if (img.type() != CV_8UC3)
    throw std::runtime_error("STENCIL06_RUNTIME: imagen OSM debe ser BGR 8 bits");
  double sum = 0.0;
  float peak = 0.0f;
  for (float v : valores) {
    if (!std::isfinite(v) || v < -1e-5f || v > 1.00001f)
      throw std::runtime_error("STENCIL06_RUNTIME: campo no estable");
    sum += v;
    peak = std::max(peak, v);
  }
  const auto n = informe.campo.lado;
  for (std::size_t row = 0; row < n; ++row) {
    for (std::size_t col = 0; col < n; ++col) {
      const auto index = row * n + col;
      const float val = valores[index];
      if (val <= 0.0f || peak <= 0.0f) continue;
      const float unit = val / peak;
      const double alpha = 0.75 * std::sqrt(static_cast<double>(unit));
      // naranja/rojo: intensidad Jacobi, sin prometer cobertura vial exacta.
      const cv::Vec3b color{0, static_cast<unsigned char>(100*(1-unit)), 255};
      auto& px = img.at<cv::Vec3b>(static_cast<int>(row), static_cast<int>(col));
      for (int c = 0; c < 3; ++c)
        px[c] = static_cast<unsigned char>(std::clamp(
            (1.0-alpha)*px[c] + alpha*color[c], 0.0, 255.0));
    }
  }
  for (std::size_t row = 1; row + 1 < n; ++row)
    for (std::size_t col = 1; col + 1 < n; ++col)
      if (informe.campo.fijas[row*n + col] &&
          informe.campo.valores[row*n + col] == 1.0f)
        cv::circle(img, cv::Point(static_cast<int>(col), static_cast<int>(row)),
                   2, cv::Scalar(255, 255, 255), -1, cv::LINE_AA);
  cv::putText(img, "STENCIL06: COBERTURA DE COURIERS (SNAPSHOT)",
              cv::Point(6, 18), cv::FONT_HERSHEY_SIMPLEX, 0.32,
              cv::Scalar(255,255,255), 2, cv::LINE_AA);
  cv::putText(img, "STENCIL06: COBERTURA DE COURIERS (SNAPSHOT)",
              cv::Point(6, 18), cv::FONT_HERSHEY_SIMPLEX, 0.32,
              cv::Scalar(0,0,0), 1, cv::LINE_AA);
  if (!cv::imwrite(png.string(), img))
    throw std::runtime_error("STENCIL06_RUNTIME: fallo al guardar PNG");

  std::ofstream output(csv, std::ios::out | std::ios::trunc);
  if (!output) throw std::runtime_error("STENCIL06_RUNTIME: fallo CSV");
  output << "x,y,source_fixed,coverage_jacobi20\n" << std::fixed
         << std::setprecision(8);
  for (std::size_t row = 0; row < n; ++row)
    for (std::size_t col = 0; col < n; ++col)
      output << col << ',' << row << ','
             << (informe.campo.fijas[row*n+col] && informe.campo.valores[row*n+col]==1.0f ? 1:0)
             << ',' << valores[row*n+col] << '\n';
  if (!output) throw std::runtime_error("STENCIL06_RUNTIME: escritura CSV incompleta");
  output.close();
  nlohmann::json meta{
      {"feature", "06_stencil"}, {"backend", backend},
      {"source", "Fleet_CourierSnapshot_after_join"},
      {"not_live_dashboard", true},
      {"not_road_network_coverage", true},
      {"not_synthetic_couriers", true},
      {"couriers_total", informe.couriersTotales},
      {"couriers_used", informe.couriersUsados},
      {"couriers_broken_excluded", informe.couriersAveriados},
      {"motion_interpolated", informe.muestrasMovimiento},
      {"fixed_source_cells", informe.fuentesUnicas},
      {"grid_side", n}, {"sweeps", barridos},
      {"jacobi_center", 0.2}, {"jacobi_neighbor", 0.2},
      {"coverage_sum", sum}, {"coverage_max", peak},
      {"map_image", png.string()}, {"grid_csv", csv.string()}};
  std::ofstream manifest(json, std::ios::out | std::ios::trunc);
  if (!manifest) throw std::runtime_error("STENCIL06_RUNTIME: fallo metadata JSON");
  manifest << meta.dump(2) << '\n';
  if (!manifest) throw std::runtime_error("STENCIL06_RUNTIME: escritura metadata incompleta");
}

} // namespace equipego::stencil06
