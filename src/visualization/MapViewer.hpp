#pragma once

#include "config/Config.hpp"

#include <opencv2/core.hpp>

#include <string>


class MapViewer {
private:
  cv::Mat baseImage;
  std::string windowTitle;

  void showFrame(const cv::Mat &frame) const;

public:
  explicit MapViewer(const MapConfig &map);

  MapViewer(const MapViewer &) = delete;
  MapViewer &operator=(const MapViewer &) = delete;

  const cv::Mat &background() const noexcept;

  cv::Mat renderNetwork(const SimulationConfig &config) const;

  void show() const;

  void showNetwork(const SimulationConfig &config) const;
};