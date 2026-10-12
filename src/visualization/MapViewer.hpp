#pragma once

#include "config/Config.hpp"

#include <opencv2/core.hpp>

#include <string>

// =====================================================
// DP8.2 - VISOR DE LA RED DE EQUIPETROL
// =====================================================

class MapViewer {
private:
  cv::Mat baseImage;
  std::string windowTitle;

  // Mostrar cualquier fotograma OpenCV.
  void showFrame(const cv::Mat &frame) const;

public:
  explicit MapViewer(const MapConfig &map);

  MapViewer(const MapViewer &) = delete;
  MapViewer &operator=(const MapViewer &) = delete;

  // Imagen original sin modificaciones.
  const cv::Mat &background() const noexcept;

  // Crear una imagen nueva con:
  // - calles;
  // - nodos;
  // - restaurantes;
  // - leyenda.
  cv::Mat renderNetwork(const SimulationConfig &config) const;

  // Visualizar solamente el mapa original.
  void show() const;

  // Visualizar el mapa con la red dibujada.
  void showNetwork(const SimulationConfig &config) const;
};