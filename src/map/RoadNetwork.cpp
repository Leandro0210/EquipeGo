
#include "map/RoadNetwork.hpp"
#include "map/FloydWarshallTiled03.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
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


// El camino geometrico sigue siendo Dijkstra. Esta operacion retorna solo metros.
std::optional<double> RoadNetwork::shortestDistanceMeters(
    const std::string &from, const std::string &to) const {
  if (floyd03_) return floyd03_->distanciaMetros(from, to);
  const auto ruta = shortestRoute(from, to);
  if (!ruta) return std::nullopt;
  return ruta->distanceMeters;
}

std::size_t RoadNetwork::floyd03Bytes() const noexcept {
  return floyd03_ ? floyd03_->bytesMatriz() : 0;
}

void RoadNetwork::habilitarFloyd03Cpu(const SimulationConfig &config,
                                      std::size_t tile) {
  if (floyd03_) throw std::logic_error("Floyd03: cache ya habilitada");
  // Construir y verificar una tabla local antes de publicarla; la mutacion
  // solo esta permitida durante inicializacion, previo a hilos de simulacion.
  auto tabla = std::make_shared<equipego::analytics::TablaRutas03>(
      equipego::analytics::floydWarshallBloquesCpu03(config, tile));
  if (tabla->dimension() != adjacency.size()) {
    throw std::invalid_argument("Floyd03: configuracion distinta de RoadNetwork");
  }
  for (const std::string &origen : tabla->nodos) {
    if (adjacency.find(origen) == adjacency.end()) {
      throw std::invalid_argument("Floyd03: nodo inexistente en RoadNetwork");
    }
    for (const std::string &destino : tabla->nodos) {
      const auto ruta = shortestRoute(origen, destino);
      const auto d = tabla->distanciaMetros(origen, destino);
      if (ruta.has_value() != d.has_value()) {
        throw std::logic_error("Floyd03: alcance distinto de Dijkstra");
      }
      if (ruta && std::abs(*d - ruta->distanceMeters) >
                      1e-9 * std::max(1.0, std::abs(ruta->distanceMeters))) {
        throw std::logic_error("Floyd03: distancia distinta de Dijkstra");
      }
    }
  }
  floyd03_ = std::move(tabla);
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
    return Route{{from}, {}, 0.0};
  }

  using QueueItem = std::pair<double, std::string>;

  std::priority_queue<QueueItem, std::vector<QueueItem>,
                      std::greater<QueueItem>>
      pending;

  std::unordered_map<std::string, double> distance;
  std::unordered_map<std::string, std::string> previous;

  std::unordered_map<std::string, double> previousEdgeLength;

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
        previousEdgeLength[edge.to] = edge.lengthMeters;

        pending.push({candidate, edge.to});
      }
    }
  }

  if (distance[to] == infinity) {
    return std::nullopt;
  }

  std::vector<std::string> path;
  std::vector<RouteSegment> segments;

  std::string current = to;

  while (current != from) {
    path.push_back(current);

    const std::string previousNode = previous.at(current);

    segments.push_back(
        RouteSegment{previousNode, current, previousEdgeLength.at(current)});

    current = previousNode;
  }

  path.push_back(from);

  std::reverse(path.begin(), path.end());
  std::reverse(segments.begin(), segments.end());

  return Route{std::move(path), std::move(segments), distance[to]};
}
