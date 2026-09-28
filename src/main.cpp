#include "Config.hpp"
#include <iostream>

int main(int argc, char *argv[]) {
  SimulationConfig config;
  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  std::cout << "Config loaded successfully: " << config.nodes.size()
            << " nodes, " << config.streets.size() << " streets, "
            << config.restaurants.size() << " restaurants.\n";
  return 0;
}