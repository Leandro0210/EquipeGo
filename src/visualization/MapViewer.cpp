#include "visualization/MapViewer.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {

// =====================================================
// COLORES - OPENCV UTILIZA BGR
// =====================================================

// Azul: doble circulacion.
const cv::Scalar kTwoWayBlue(245, 140, 45);

// Rosado: circulacion unica.
const cv::Scalar kOneWayPink(185, 85, 240);

const cv::Scalar kStreetShadow(38, 38, 38);
const cv::Scalar kNodeFill(255, 255, 255);
const cv::Scalar kNodeBorder(30, 30, 30);
const cv::Scalar kNodeText(20, 20, 20);
const cv::Scalar kRestaurantRed(45, 45, 225);
const cv::Scalar kRestaurantBorder(255, 255, 255);
const cv::Scalar kRestaurantText(255, 255, 255);

constexpr int kNodeRadius = 12;
constexpr int kRestaurantRadius = 13;

// =====================================================
// ETIQUETAS CENTRADAS
// =====================================================

void drawCenteredLabel(
    cv::Mat &image,
    const std::string &label,
    const cv::Point &center,
    const cv::Scalar &color,
    double fontScale) {

  int baseline = 0;

  const cv::Size textSize = cv::getTextSize(
      label, cv::FONT_HERSHEY_SIMPLEX,
      fontScale, 1, &baseline);

  const cv::Point origin(
      center.x - textSize.width / 2,
      center.y + textSize.height / 2);

  cv::putText(
      image, label, origin,
      cv::FONT_HERSHEY_SIMPLEX,
      fontScale, color, 1, cv::LINE_AA);
}

// =====================================================
// POSICION DE NODOS
// =====================================================

cv::Point findNodePosition(
    const SimulationConfig &config,
    const std::string &nodeId) {

  const auto it =
      config.nodeIndexById.find(nodeId);

  if (it == config.nodeIndexById.end() ||
      it->second >= config.nodes.size()) {
    throw std::runtime_error(
        "DP8: nodo no encontrado: " + nodeId);
  }

  return config.nodes[it->second].pixelPos;
}

// =====================================================
// INTERPOLACION PARA FLECHAS
// =====================================================

cv::Point interpolate(
    const cv::Point &from,
    const cv::Point &to,
    double fraction) {

  return cv::Point(
      static_cast<int>(std::lround(
          from.x + (to.x - from.x) * fraction)),
      static_cast<int>(std::lround(
          from.y + (to.y - from.y) * fraction)));
}

// =====================================================
// CALLES DE DOBLE Y UNICO SENTIDO
// =====================================================

void drawStreets(
    cv::Mat &frame,
    const SimulationConfig &config) {

  // Primera pasada: dibujar las calles.
  for (const Street &street : config.streets) {

    const cv::Point from =
        findNodePosition(config, street.from);

    const cv::Point to =
        findNodePosition(config, street.to);

    const cv::Scalar color =
        street.oneWay
            ? kOneWayPink
            : kTwoWayBlue;

    // Contorno oscuro.
    cv::line(
        frame, from, to,
        kStreetShadow, 6, cv::LINE_AA);

    // Color segun el sentido.
    cv::line(
        frame, from, to,
        color, 4, cv::LINE_AA);
  }

  // Segunda pasada: flechas en calles unicas.
  for (const Street &street : config.streets) {

    if (!street.oneWay) {
      continue;
    }

    const cv::Point from =
        findNodePosition(config, street.from);

    const cv::Point to =
        findNodePosition(config, street.to);

    const cv::Point arrowStart =
        interpolate(from, to, 0.25);

    const cv::Point arrowEnd =
        interpolate(from, to, 0.75);

    // Flecha oscura de contorno.
    cv::arrowedLine(
        frame, arrowStart, arrowEnd,
        kStreetShadow, 5,
        cv::LINE_AA, 0, 0.35);

    // Flecha blanca visible sobre el rosado.
    cv::arrowedLine(
        frame, arrowStart, arrowEnd,
        cv::Scalar(255, 255, 255), 2,
        cv::LINE_AA, 0, 0.35);
  }
}

// =====================================================
// NODOS
// =====================================================

void drawNodes(
    cv::Mat &frame,
    const SimulationConfig &config) {

  for (const Node &node : config.nodes) {

    cv::circle(
        frame, node.pixelPos,
        kNodeRadius, kNodeBorder,
        cv::FILLED, cv::LINE_AA);

    cv::circle(
        frame, node.pixelPos,
        kNodeRadius - 2, kNodeFill,
        cv::FILLED, cv::LINE_AA);

    drawCenteredLabel(
        frame, node.id, node.pixelPos,
        kNodeText, 0.34);
  }
}

// =====================================================
// RESTAURANTES
// =====================================================

void drawRestaurants(
    cv::Mat &frame,
    const SimulationConfig &config) {

  for (const Restaurant &restaurant :
       config.restaurants) {

    const cv::Point position =
        findNodePosition(config, restaurant.nodeId);

    cv::circle(
        frame, position,
        kRestaurantRadius + 2,
        kRestaurantBorder,
        cv::FILLED, cv::LINE_AA);

    cv::circle(
        frame, position,
        kRestaurantRadius,
        kRestaurantRed,
        cv::FILLED, cv::LINE_AA);

    drawCenteredLabel(
        frame, restaurant.id, position,
        kRestaurantText, 0.35);
  }
}

// =====================================================
// LEYENDA
// =====================================================

void drawLegend(cv::Mat &frame) {

  const int panelWidth =
      std::min(225, frame.cols - 16);

  constexpr int kPanelHeight = 138;

  if (panelWidth < 150 ||
      frame.rows < kPanelHeight + 16) {
    return;
  }

  const cv::Rect panel(
      8, 8, panelWidth, kPanelHeight);

  cv::rectangle(
      frame, panel,
      cv::Scalar(246, 246, 246),
      cv::FILLED);

  cv::rectangle(
      frame, panel,
      cv::Scalar(55, 55, 55), 1);

  cv::putText(
      frame, "EquipeGo | Red vial",
      cv::Point(18, 28),
      cv::FONT_HERSHEY_SIMPLEX,
      0.48, kNodeText, 1, cv::LINE_AA);

  // Doble sentido - azul.
  cv::line(
      frame, cv::Point(20, 49),
      cv::Point(49, 49),
      kTwoWayBlue, 4, cv::LINE_AA);

  cv::putText(
      frame, "Doble sentido",
      cv::Point(61, 53),
      cv::FONT_HERSHEY_SIMPLEX,
      0.39, kNodeText, 1, cv::LINE_AA);

  // Un solo sentido - rosado.
  cv::arrowedLine(
      frame, cv::Point(20, 73),
      cv::Point(49, 73),
      kOneWayPink, 4,
      cv::LINE_AA, 0, 0.35);

  cv::putText(
      frame, "Un solo sentido",
      cv::Point(61, 77),
      cv::FONT_HERSHEY_SIMPLEX,
      0.39, kNodeText, 1, cv::LINE_AA);

  // Nodo blanco.
  cv::circle(
      frame, cv::Point(34, 97),
      9, kNodeFill,
      cv::FILLED, cv::LINE_AA);

  cv::circle(
      frame, cv::Point(34, 97),
      9, kNodeBorder, 1, cv::LINE_AA);

  cv::putText(
      frame, "Nodos",
      cv::Point(61, 101),
      cv::FONT_HERSHEY_SIMPLEX,
      0.39, kNodeText, 1, cv::LINE_AA);

  // Restaurante rojo.
  cv::circle(
      frame, cv::Point(34, 121),
      9, kRestaurantRed,
      cv::FILLED, cv::LINE_AA);

  cv::putText(
      frame, "Restaurantes",
      cv::Point(61, 125),
      cv::FONT_HERSHEY_SIMPLEX,
      0.39, kNodeText, 1, cv::LINE_AA);
}

} // namespace

// =====================================================
// CONSTRUCTOR
// =====================================================

MapViewer::MapViewer(const MapConfig &map)
    : baseImage(map.image.clone()),
      windowTitle("EquipeGo - Red vial de Equipetrol") {

  if (baseImage.empty()) {
    throw std::invalid_argument(
        "DP8: imagen del mapa vacia");
  }

  if (baseImage.type() != CV_8UC3) {
    throw std::invalid_argument(
        "DP8: se esperaba imagen BGR");
  }
}

// =====================================================
// MAPA ORIGINAL
// =====================================================

const cv::Mat &MapViewer::background() const noexcept {
  return baseImage;
}

// =====================================================
// MAPA CON RED SUPERPUESTA
// =====================================================

cv::Mat MapViewer::renderNetwork(
    const SimulationConfig &config) const {

  if (config.map.image.empty() ||
      config.map.image.size() != baseImage.size()) {

    throw std::invalid_argument(
        "DP8: mapa y configuracion incompatibles");
  }

  cv::Mat frame = baseImage.clone();

  drawStreets(frame, config);
  drawNodes(frame, config);
  drawRestaurants(frame, config);
  drawLegend(frame);

  return frame;
}

// =====================================================
// VENTANA OPENCV
// =====================================================

void MapViewer::showFrame(
    const cv::Mat &frame) const {

#if defined(__linux__)

  const char *display = std::getenv("DISPLAY");
  const char *wayland = std::getenv("WAYLAND_DISPLAY");

  const bool hasX11 =
      display != nullptr && display[0] != '\0';

  const bool hasWayland =
      wayland != nullptr && wayland[0] != '\0';

  if (!hasX11 && !hasWayland) {
    throw std::runtime_error(
        "DP8: no hay servidor grafico disponible");
  }

#endif

  cv::namedWindow(windowTitle, cv::WINDOW_NORMAL);

  constexpr int kMaxWidth = 1100;
  constexpr int kMaxHeight = 800;

  const double scaleX =
      static_cast<double>(kMaxWidth) / frame.cols;

  const double scaleY =
      static_cast<double>(kMaxHeight) / frame.rows;

  const double scale =
      std::min({1.0, scaleX, scaleY});

  const int width = std::max(
      1, static_cast<int>(frame.cols * scale));

  const int height = std::max(
      1, static_cast<int>(frame.rows * scale));

  cv::resizeWindow(windowTitle, width, height);
  cv::imshow(windowTitle, frame);

  while (true) {

    const int key = cv::waitKey(30);

    if (key == 27 || key == 'q' || key == 'Q') {
      break;
    }

    try {
      if (cv::getWindowProperty(
              windowTitle,
              cv::WND_PROP_VISIBLE) < 1.0) {
        break;
      }
    } catch (const cv::Exception &) {
      break;
    }
  }

  try {
    cv::destroyWindow(windowTitle);
  } catch (const cv::Exception &) {
  }
}

// =====================================================
// MOSTRAR MAPA ORIGINAL
// =====================================================

void MapViewer::show() const {
  showFrame(baseImage);
}

// =====================================================
// MOSTRAR RED
// =====================================================

void MapViewer::showNetwork(
    const SimulationConfig &config) const {

  const cv::Mat frame = renderNetwork(config);
  showFrame(frame);
}
