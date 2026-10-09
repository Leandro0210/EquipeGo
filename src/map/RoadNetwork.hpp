
#pragma once

#include "config/Config.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Representa una calle dentro de una ruta.
struct RouteSegment {
  std::string from;
  std::string to;
  double distanceMeters = 0.0;
};

// Representa una ruta completa.
struct Route {
  std::vector<std::string> nodes;
  std::vector<RouteSegment> segments;
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

  // Dijkstra: busca la ruta de menor distancia.
  std::optional<Route> shortestRoute(const std::string &from,
                                     const std::string &to) const;
};
