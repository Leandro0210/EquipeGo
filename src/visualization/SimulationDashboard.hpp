#pragma once

#include "config/Config.hpp"
#include "core/Logger.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "visualization/MapViewer.hpp"

#include <opencv2/core.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

// DP8.5: visor del MISMO Fleet, Dispatcher y Logger usados por delivery_sim.
// Toda operacion HighGUI se ejecuta desde el hilo de main.
class SimulationDashboard {
private:
  using Clock = std::chrono::steady_clock;

  struct MotionTracker {
    std::string node;
    std::string target;
    std::string next;
    Clock::time_point since{};
    bool active = false;
  };

  struct VisualCourier {
    CourierSnapshot courier;
    cv::Point2d point;
    std::string target;
    std::string phase;
  };

  const SimulationConfig &config;
  const RoadNetwork &network;
  MapViewer mapViewer;
  cv::Mat base;
  std::vector<MotionTracker> trackers;
  std::string title = "EquipeGo - Simulacion de entregas";
  std::size_t selected = 0;
  bool windowOpen = false;

  cv::Point2d nodePoint(const std::string &id) const;
  std::string restaurantNode(const std::string &id) const;
  std::vector<VisualCourier> takeSamples(Fleet &fleet);
  cv::Mat drawFrame(const std::vector<VisualCourier> &samples,
                    const LiveEventStats &stats, long long elapsedMs) const;

public:
  SimulationDashboard(const SimulationConfig &config,
                      const RoadNetwork &network);
  ~SimulationDashboard();

  SimulationDashboard(const SimulationDashboard &) = delete;
  SimulationDashboard &operator=(const SimulationDashboard &) = delete;

  void open();
  // Devuelve false cuando el usuario pide finalizar (Q, ESC, cerrar ventana).
  bool frame(Fleet &fleet, const LiveEventStats &stats, long long elapsedMs);
  void close() noexcept;
};
