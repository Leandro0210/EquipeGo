#include "analytics/HeatmapRuntime05.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

int main() {
  namespace fs = std::filesystem;
  try {
    SimulationConfig config;
    config.map.bounds = {11.0, 0.0, 0.0, 11.0};
    config.map.image = cv::Mat(64, 64, CV_8UC3, cv::Scalar(230, 230, 230));
    Node one; one.id = "n1"; one.lat = 1.0; one.lon = 2.0;
    Node two; two.id = "n2"; two.lat = 10.0; two.lon = 10.0;
    config.nodes = {one, two};
    config.nodeIndexById["n1"] = 0;
    config.nodeIndexById["n2"] = 1;
    Restaurant r1; r1.id = "r1"; r1.nodeId = "n1";
    Restaurant r2; r2.id = "r2"; r2.nodeId = "n2";
    config.restaurants = {r1, r2};
    const std::unordered_map<std::string, std::size_t> created{
        {"r1", 3}, {"r2", 5}};
    const auto report = equipego::convolution05::projectRestaurantOrders05(
        config, created, 32);
    if (report.created != 8 || report.restaurantsWithOrders != 2 ||
        report.grid.orders.size() != 1024 ||
        std::accumulate(report.grid.orders.begin(), report.grid.orders.end(), 0) != 8)
      throw std::runtime_error("conteo de origenes no coincide");
    const auto a = equipego::convolution05::gaussianReference05(report.grid);
    const auto b = equipego::convolution05::gaussianSeparable05(report.grid, 2);
    if (a != b) throw std::runtime_error("referencia != separable");

    const auto prefix = fs::temp_directory_path() /
        ("equipego_heatmap05_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    equipego::convolution05::saveRunHeatmap05(config, report, b, prefix.string());
    for (const auto& suffix : {".png", ".json", ".csv"}) {
      if (!fs::is_regular_file(prefix.string() + suffix) ||
          fs::file_size(prefix.string() + suffix) == 0)
        throw std::runtime_error(std::string("evidencia invalida: ") + suffix);
    }
    bool refused = false;
    try {
      equipego::convolution05::saveRunHeatmap05(config, report, b, prefix.string());
    } catch (const std::runtime_error&) { refused = true; }
    if (!refused) throw std::runtime_error("sobrescritura de evidencia permitida");
    for (const auto& suffix : {".png", ".json", ".csv"})
      fs::remove(prefix.string() + suffix);

    bool invalidRejected = false;
    try {
      const auto fail = equipego::convolution05::projectRestaurantOrders05(
          config, {{"unknown", 2}}, 32);
      (void)fail;
    } catch (const std::invalid_argument&) { invalidRejected = true; }
    if (!invalidRejected) throw std::runtime_error("origen desconocido aceptado");
    std::cout << "HEATMAP05_RUNTIME_TEST_OK,created=8,grid=32x32,files=3,exact=1\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "HEATMAP05_RUNTIME_TEST_ERROR:" << ex.what() << '\n';
    return 1;
  }
}
