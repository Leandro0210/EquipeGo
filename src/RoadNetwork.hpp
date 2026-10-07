#pragma once

#include "Config.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

class RoadNetwork {
private:
  std::unordered_map<std::string, std::vector<std::string>> adjacency;

public:
  explicit RoadNetwork(const SimulationConfig &config);

  bool isReachable(const std::string &from, const std::string &to) const;
};
