#pragma once

#include "Config.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct Route {
  std::vector<std::string> nodes;
  double distanceMeters = 0.0;
};

class RoadNetwork {
private:
  struct Edge {
    std::string to;
    double lengthMeters;
  };

  std::unordered_map<std::string, std::vector<Edge>> adjacency;

public:
  explicit RoadNetwork(const SimulationConfig &config);

  bool isReachable(const std::string &from, const std::string &to) const;

  // Devuelve la ruta de menor distancia respetando oneWay.
  std::optional<Route> shortestRoute(const std::string &from,
                                     const std::string &to) const;
};