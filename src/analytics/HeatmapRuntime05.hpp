#pragma once

// Feature05, Etapa3: diagnostico opcional post-simulacion.
// Integra los eventos orderCreated de EquipeGo (origen: RESTAURANTE),
// NO genera pedidos sinteticos, NO cambia despacho ni ETA.
// CPU reference: OrderHeatmap05.hpp. El backend CUDA del hilo propietario
// se integrara en una etapa independiente, tras verificar su arquitectura.

#include "analytics/OrderHeatmap05.hpp"
#include "config/Config.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace equipego::convolution05 {

struct HeatmapReport05 {
  DemandGrid05 grid;
  std::size_t created = 0;
  std::size_t restaurantsWithOrders = 0;
};

// La auditoria de eventos del programa suministra los pedidos por restaurante.
// Cada orderCreated se cuenta exactamente una vez, incluso si luego es rechazado.
// La ubicacion es la coordenada del restaurante (origen), NO la entrega.
inline HeatmapReport05 projectRestaurantOrders05(
    const SimulationConfig& config,
    const std::unordered_map<std::string, std::size_t>& createdByRestaurant,
    std::size_t side = 512) {
  if (side < 4 || side > 2048 ||
      !(config.map.bounds.east > config.map.bounds.west) ||
      !(config.map.bounds.north > config.map.bounds.south)) {
    throw std::invalid_argument("HEATMAP05: dimensiones/map bounds invalidos");
  }
  HeatmapReport05 report;
  report.grid.width = side;
  report.grid.height = side;
  report.grid.orders.assign(side * side, 0);
  std::unordered_map<std::string, const Restaurant*> restaurants;
  restaurants.reserve(config.restaurants.size());
  for (const auto& rest : config.restaurants) {
    if (!restaurants.emplace(rest.id, &rest).second)
      throw std::invalid_argument("HEATMAP05: restaurante duplicado");
  }
  for (const auto& pair : createdByRestaurant) {
    const auto& id = pair.first;
    const std::size_t count = pair.second;
    const auto it = restaurants.find(id);
    if (it == restaurants.end())
      throw std::invalid_argument("HEATMAP05: restaurante no encontrado: " + id);
    const auto nodeIt = config.nodeIndexById.find(it->second->nodeId);
    if (nodeIt == config.nodeIndexById.end() || nodeIt->second >= config.nodes.size())
      throw std::invalid_argument("HEATMAP05: nodo de restaurante no encontrado: " + id);
    const auto& node = config.nodes[nodeIt->second];
    if (!std::isfinite(node.lon) || !std::isfinite(node.lat) ||
        node.lon < config.map.bounds.west || node.lon > config.map.bounds.east ||
        node.lat < config.map.bounds.south || node.lat > config.map.bounds.north) {
      throw std::invalid_argument("HEATMAP05: nodo fuera del mapa: " + id);
    }
    const auto ux = (node.lon - config.map.bounds.west) /
                    (config.map.bounds.east - config.map.bounds.west);
    const auto uy = (config.map.bounds.north - node.lat) /
                    (config.map.bounds.north - config.map.bounds.south);
    const std::size_t x = std::min(side - 1, static_cast<std::size_t>(ux * side));
    const std::size_t y = std::min(side - 1, static_cast<std::size_t>(uy * side));
    const std::size_t index = y * side + x;
    if (count > static_cast<std::size_t>(1000000 - report.grid.orders[index]))
      throw std::overflow_error("HEATMAP05: exceso de eventos en celda");
    report.grid.orders[index] += static_cast<std::int32_t>(count);
    if (report.created > std::numeric_limits<std::size_t>::max() - count)
      throw std::overflow_error("HEATMAP05: exceso de eventos totales");
    report.created += count;
    if (count > 0) ++report.restaurantsWithOrders;
  }
  validate(report.grid);
  return report;
}

inline void saveRunHeatmap05(
    const SimulationConfig& config, const HeatmapReport05& report,
    const std::vector<float>& smoothed, const std::string& outputPrefix,
    const std::string& backend = "cpu_separable") {
  namespace fs = std::filesystem;
  if (backend != "cpu_separable" && backend != "gpu_tiled")
    throw std::invalid_argument("HEATMAP05: backend invalido");
  if (outputPrefix.empty() || smoothed.size() != report.grid.orders.size() ||
      config.map.image.empty())
    throw std::invalid_argument("HEATMAP05: imagen/salida invalida");
  const fs::path prefix(outputPrefix);
  const fs::path imagePath(outputPrefix + ".png");
  const fs::path csvPath(outputPrefix + ".csv");
  const fs::path jsonPath(outputPrefix + ".json");
  for (const auto& path : {imagePath, csvPath, jsonPath}) {
    if (fs::exists(path))
      throw std::runtime_error("HEATMAP05: no sobrescribir evidencia: " + path.string());
  }
  if (prefix.has_parent_path()) fs::create_directories(prefix.parent_path());

  cv::Mat output;
  cv::resize(config.map.image, output,
             cv::Size(static_cast<int>(report.grid.width),
                      static_cast<int>(report.grid.height)));
  if (output.type() != CV_8UC3)
    throw std::runtime_error("HEATMAP05: mapa OSM esperado en BGR");
  float maxVal = 0.0f;
  for (float v : smoothed) {
    if (!std::isfinite(v) || v < 0.0f)
      throw std::runtime_error("HEATMAP05: salida de convolution invalida");
    maxVal = std::max(maxVal, v);
  }
  for (std::size_t y = 0; y < report.grid.height; ++y) {
    for (std::size_t x = 0; x < report.grid.width; ++x) {
      const auto value = smoothed[y * report.grid.width + x];
      if (value <= 0.0f || maxVal <= 0.0f) continue;
      const double ratio = static_cast<double>(value / maxVal);
      const double alpha = 0.80 * std::sqrt(ratio);
      const cv::Vec3b color{0, static_cast<unsigned char>(255.0 * (1.0 - ratio)), 255};
      auto& px = output.at<cv::Vec3b>(static_cast<int>(y), static_cast<int>(x));
      for (int c = 0; c < 3; ++c) {
        const double blended = (1.0 - alpha) * px[c] + alpha * color[c];
        px[c] = static_cast<unsigned char>(std::clamp(blended, 0.0, 255.0));
      }
    }
  }
  cv::putText(output, "HEATMAP05: ORIGEN DE PEDIDOS (RESTAURANTES)",
              cv::Point(8, 20), cv::FONT_HERSHEY_SIMPLEX, 0.36,
              cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
  cv::putText(output, "HEATMAP05: ORIGEN DE PEDIDOS (RESTAURANTES)",
              cv::Point(8, 20), cv::FONT_HERSHEY_SIMPLEX, 0.36,
              cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
  if (!cv::imwrite(imagePath.string(), output))
    throw std::runtime_error("HEATMAP05: no se pudo guardar PNG");

  std::ofstream csv(csvPath);
  if (!csv) throw std::runtime_error("HEATMAP05: no se pudo crear CSV");
  csv << "x,y,created_at_restaurant,smoothed_5x5\n" << std::fixed
      << std::setprecision(8);
  for (std::size_t y = 0; y < report.grid.height; ++y)
    for (std::size_t x = 0; x < report.grid.width; ++x) {
      const auto index = y * report.grid.width + x;
      csv << x << ',' << y << ',' << report.grid.orders[index] << ','
          << smoothed[index] << '\n';
    }
  if (!csv) throw std::runtime_error("HEATMAP05: fallo escribiendo CSV");
  csv.close();
  nlohmann::json meta{
      {"feature", "05_convolution"},
      {"backend", backend},
      {"source", "simulation_orderCreated_restaurant_origin"},
      {"not_delivery_destinations", true},
      {"not_observed_real_world_demand", true},
      {"orders_created", report.created},
      {"restaurants_with_orders", report.restaurantsWithOrders},
      {"grid_width", report.grid.width},
      {"grid_height", report.grid.height},
      {"gaussian_filter", "5x5_binomial_zero_padding"},
      {"image", imagePath.string()},
      {"grid_csv", csvPath.string()}};
  std::ofstream metadata(jsonPath);
  if (!metadata) throw std::runtime_error("HEATMAP05: no se pudo crear metadata");
  metadata << meta.dump(2) << '\n';
  if (!metadata) throw std::runtime_error("HEATMAP05: fallo escribiendo metadata");
}

} // namespace equipego::convolution05
