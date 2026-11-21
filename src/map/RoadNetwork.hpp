
#pragma once

#include "config/Config.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace equipego::analytics { struct TablaRutas03; }

// Resultado de inicializacion: la GPU solo se usa durante el precalculo.
struct Floyd03GpuInitStats {
  std::size_t nodos = 0;
  std::size_t paresVerificados = 0;
  std::size_t bytesMatriz = 0;
  double kernelMs = 0.0;
  double totalMs = 0.0;
};

struct RouteSegment {
  std::string from;
  std::string to;
  double distanceMeters = 0.0;
};

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
  // Solo se asigna antes de arrancar hilos; luego es inmutable.
  std::shared_ptr<const equipego::analytics::TablaRutas03> floyd03_;

public:
  explicit RoadNetwork(const SimulationConfig &config);

  bool isReachable(const std::string &from, const std::string &to) const;

  // Sin tabla usa Dijkstra; con tabla consulta n*n. No cambia shortestRoute.
  std::optional<double> shortestDistanceMeters(const std::string &from,
                                               const std::string &to) const;
  void habilitarFloyd03Cpu(const SimulationConfig &config,
                           std::size_t tile = 16);
  bool floyd03Habilitado() const noexcept { return bool(floyd03_); }
  // Solo disponible con USE_CUDA=ON y antes de iniciar los hilos.
  Floyd03GpuInitStats habilitarFloyd03Gpu(const SimulationConfig &config,
                                         std::size_t maxNodos = 4096);
  std::size_t floyd03Bytes() const noexcept;

  std::optional<Route> shortestRoute(const std::string &from,
                                     const std::string &to) const;
};
