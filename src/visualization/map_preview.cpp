#include "config/Config.hpp"
#include "visualization/MapViewer.hpp"

#include <opencv2/core.hpp>

#include <exception>
#include <iostream>
#include <string>

// =====================================================
// DP8.2 - PREVISUALIZACION DE RED EQUIPETROL
// =====================================================

int main(int argc, char *argv[]) {

  if (argc < 2 || argc > 3) {

    std::cerr << "Uso:\n"
              << "  map_preview <config.json>\n"
              << "  map_preview <config.json> --base\n"
              << "  map_preview <config.json> --check\n";

    return 1;
  }

  const std::string mode = argc == 3 ? argv[2] : "";

  if (mode != "" && mode != "--base" && mode != "--check") {

    std::cerr << "Modo no reconocido: " << mode << '\n';

    return 1;
  }

  char *configArguments[] = {argv[0], argv[1]};

  SimulationConfig config;

  if (!loadAndValidateConfig(2, configArguments, config)) {
    return 1;
  }

  try {

    MapViewer viewer(config.map);

    const cv::Mat &original = viewer.background();

    const cv::Mat network = viewer.renderNetwork(config);

    const bool originalUnchanged =
        cv::norm(config.map.image, original, cv::NORM_INF) == 0.0;

    const bool overlayGenerated =
        cv::norm(original, network, cv::NORM_INF) > 0.0;

    if (!originalUnchanged || !overlayGenerated) {

      throw std::runtime_error("No se pudo construir la red visual");
    }

    std::cout << "=== DP8.2 MAPA EQUIPEGO ===\n";

    std::cout << "Imagen original: " << config.map.resolvedPath << '\n';

    std::cout << "Resolucion: " << original.cols << " x " << original.rows
              << '\n';

    std::cout << "Nodos: " << config.nodes.size() << '\n';

    std::cout << "Calles: " << config.streets.size() << '\n';

    std::cout << "Restaurantes: " << config.restaurants.size() << '\n';

    // =========================================
    // MOSTRAR RELACION DE RESTAURANTES
    // =========================================

    for (const Restaurant &restaurant : config.restaurants) {

      std::cout << "  " << restaurant.id << " - " << restaurant.name << " ("
                << restaurant.nodeId << ")\n";
    }

    // =========================================
    // COMPROBACION SIN INTERFAZ GRAFICA
    // =========================================

    if (mode == "--check") {

      std::cout << "DP8.2 MAP CHECK: OK\n";

      return 0;
    }

    std::cout << "Pulsa Q o ESC para cerrar.\n";

    // =========================================
    // ELEGIR IMAGEN A VISUALIZAR
    // =========================================

    if (mode == "--base") {

      viewer.show();

    } else {

      viewer.showNetwork(config);
    }

    std::cout << "DP8.2 MAP VIEWER: OK\n";

    return 0;

  } catch (const std::exception &error) {

    std::cerr << "Error DP8.2: " << error.what() << '\n';

    return 1;
  }
}