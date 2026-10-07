#include "map/RoadNetwork.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>

RoadNetwork::RoadNetwork(const SimulationConfig &config) {
  for (const Node &node : config.nodes) {
    adjacency[node.id];
  }

  for (const Street &street : config.streets) {
    adjacency[street.from].push_back(Edge{street.to, street.lengthMeters});

    if (!street.oneWay) {
      adjacency[street.to].push_back(Edge{street.from, street.lengthMeters});
    }
  }
}

bool RoadNetwork::isReachable(const std::string &from,
                              const std::string &to) const {
  return shortestRoute(from, to).has_value();
}

std::optional<Route> RoadNetwork::shortestRoute(const std::string &from,
                                                const std::string &to) const {
  if (adjacency.find(from) == adjacency.end() ||
      adjacency.find(to) == adjacency.end()) {
    return std::nullopt;
  }

  if (from == to) {
    return Route{{from}, 0.0};
  }

  using QueueItem = std::pair<double, std::string>;

  std::priority_queue<QueueItem, std::vector<QueueItem>,
                      std::greater<QueueItem>>
      pending;

  std::unordered_map<std::string, double> distance;
  std::unordered_map<std::string, std::string> previous;

  const double infinity = std::numeric_limits<double>::infinity();

  for (const auto &entry : adjacency) {
    distance[entry.first] = infinity;
  }

  distance[from] = 0.0;
  pending.push({0.0, from});

  while (!pending.empty()) {
    const auto [currentDistance, current] = pending.top();
    pending.pop();

    if (currentDistance > distance[current]) {
      continue;
    }

    if (current == to) {
      break;
    }

    for (const Edge &edge : adjacency.at(current)) {
      const double candidate = currentDistance + edge.lengthMeters;

      if (candidate < distance[edge.to]) {
        distance[edge.to] = candidate;
        previous[edge.to] = current;
        pending.push({candidate, edge.to});
      }
    }
  }

  if (distance[to] == infinity) {
    return std::nullopt;
  }

  std::vector<std::string> path;

  std::string current = to;

  while (current != from) {
    path.push_back(current);
    current = previous.at(current);
  }

  path.push_back(from);

  std::reverse(path.begin(), path.end());

  return Route{std::move(path), distance[to]};
}