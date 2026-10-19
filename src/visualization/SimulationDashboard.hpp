#pragma once

#include "config/Config.hpp"
#include "core/Logger.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "visualization/MapViewer.hpp"

#include <opencv2/core.hpp>

#include <cstddef>
#include <string>
#include <vector>

class SimulationDashboard {
private:
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
  bool frame(Fleet &fleet, const LiveEventStats &stats, long long elapsedMs);
  void close() noexcept;
};
