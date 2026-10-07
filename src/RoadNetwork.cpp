#include "RoadNetwork.hpp"

#include <queue>
#include <unordered_set>

RoadNetwork::RoadNetwork(const SimulationConfig &config) {
  for (const Node &node : config.nodes) {
    adjacency[node.id];
  }

  for (const Street &street : config.streets) {
    adjacency[street.from].push_back(street.to);

    if (!street.oneWay) {
      adjacency[street.to].push_back(street.from);
    }
  }
}

bool RoadNetwork::isReachable(const std::string &from,
                              const std::string &to) const {
  if (adjacency.find(from) == adjacency.end() ||
      adjacency.find(to) == adjacency.end()) {
    return false;
  }

  if (from == to) {
    return true;
  }

  std::queue<std::string> pending;
  std::unordered_set<std::string> visited;

  pending.push(from);
  visited.insert(from);

  while (!pending.empty()) {
    const std::string current = pending.front();
    pending.pop();

    const auto it = adjacency.find(current);

    if (it == adjacency.end()) {
      continue;
    }

    for (const std::string &neighbor : it->second) {
      if (neighbor == to) {
        return true;
      }

      if (visited.insert(neighbor).second) {
        pending.push(neighbor);
      }
    }
  }

  return false;
}