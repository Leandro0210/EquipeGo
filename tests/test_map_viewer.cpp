
#include "config/Config.hpp"
#include "visualization/MapViewer.hpp"

#include <opencv2/core.hpp>

#include <exception>
#include <iostream>

// =====================================================
// DP8.2 - TEST DEL MAPA Y DE LA RED DIBUJADA
// =====================================================

int main(int argc, char *argv[]) {

  SimulationConfig config;

  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  bool loaded = false;
  bool dimensionsOk = false;
  bool originalIdentical = false;
  bool independentCopy = false;
  bool overlayGenerated = false;
  bool overlayIndependent = false;
  bool deterministic = false;
  bool topologyOk = true;

  try {

    MapViewer viewer(config.map);

    const cv::Mat &original = config.map.image;
    const cv::Mat &background = viewer.background();

    // =========================================
    // 1. VERIFICAR IMAGEN ORIGINAL
    // =========================================

    loaded = !original.empty() && !background.empty();

    dimensionsOk = original.size() == background.size() &&
                   original.type() == background.type();

    if (loaded && dimensionsOk) {
      originalIdentical = cv::norm(original, background, cv::NORM_INF) == 0.0;
    }

    independentCopy = original.data != background.data;

    // =========================================
    // 2. VERIFICAR RED DIBUJADA
    // =========================================

    const cv::Mat network = viewer.renderNetwork(config);

    overlayIndependent = network.data != background.data;

    overlayGenerated = !network.empty() && network.size() == original.size() &&
                       network.type() == original.type() &&
                       cv::norm(background, network, cv::NORM_INF) > 0.0;

    // =========================================
    // 3. VERIFICAR RENDER REPRODUCIBLE
    // =========================================

    const cv::Mat secondRender = viewer.renderNetwork(config);

    deterministic = cv::norm(network, secondRender, cv::NORM_INF) == 0.0;

    // =========================================
    // 4. VERIFICAR TOPOLOGIA
    // =========================================

    for (const Street &street : config.streets) {

      if (config.nodeIndexById.count(street.from) == 0 ||
          config.nodeIndexById.count(street.to) == 0) {

        topologyOk = false;
      }
    }

    for (const Restaurant &restaurant : config.restaurants) {

      if (config.nodeIndexById.count(restaurant.nodeId) == 0) {

        topologyOk = false;
      }
    }

    topologyOk = topologyOk && !config.nodes.empty() &&
                 !config.streets.empty() && !config.restaurants.empty();

    // =========================================
    // 5. VERIFICAR QUE EL MAPA SIGUE INTACTO
    // =========================================

    originalIdentical =
        originalIdentical &&
        cv::norm(original, viewer.background(), cv::NORM_INF) == 0.0;

  } catch (const std::exception &error) {

    std::cerr << "Error: " << error.what() << '\n';

    return 1;
  }

  // =========================================
  // 6. MOSTRAR RESULTADOS
  // =========================================

  std::cout << "=== TEST DP8.2 MAP NETWORK ===\n";

  std::cout << "Mapa cargado: " << (loaded ? "OK" : "ERROR") << '\n';

  std::cout << "Dimensiones correctas: " << (dimensionsOk ? "OK" : "ERROR")
            << '\n';

  std::cout << "Pixeles originales intactos: "
            << (originalIdentical ? "OK" : "ERROR") << '\n';

  std::cout << "Copia independiente: " << (independentCopy ? "OK" : "ERROR")
            << '\n';

  std::cout << "Red dibujada: " << (overlayGenerated ? "OK" : "ERROR") << '\n';

  std::cout << "Overlay independiente: "
            << (overlayIndependent ? "OK" : "ERROR") << '\n';

  std::cout << "Render reproducible: " << (deterministic ? "OK" : "ERROR")
            << '\n';

  std::cout << "Topologia valida: " << (topologyOk ? "OK" : "ERROR") << '\n';

  const bool passed = loaded && dimensionsOk && originalIdentical &&
                      independentCopy && overlayGenerated &&
                      overlayIndependent && deterministic && topologyOk;

  std::cout << "DP8.2 MAP NETWORK TEST: " << (passed ? "OK" : "ERROR") << '\n';

  return passed ? 0 : 1;
}
