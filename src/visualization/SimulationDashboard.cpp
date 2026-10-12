#include "visualization/SimulationDashboard.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr int kPanelWidth = 310;
constexpr double kPi = 3.14159265358979323846;

const std::array<cv::Scalar, 8> kColors = {{
    {245, 100, 40}, {35, 160, 245}, {75, 195, 65}, {220, 65, 180},
    {30, 195, 195}, {165, 100, 65}, {95, 85, 245}, {165, 150, 40}
}};

cv::Scalar colorFor(std::size_t i) {
  return kColors[i % kColors.size()];
}

void drawText(cv::Mat &image, const std::string &text, int x, int y,
              double scale = 0.39,
              cv::Scalar color = cv::Scalar(40, 40, 40)) {
  cv::putText(image, text, cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX,
              scale, color, 1, cv::LINE_AA);
}

} // namespace

SimulationDashboard::SimulationDashboard(const SimulationConfig &cfg,
                                         const RoadNetwork &net)
    : config(cfg), network(net), mapViewer(cfg.map),
      base(mapViewer.renderNetwork(cfg)) {}

SimulationDashboard::~SimulationDashboard() { close(); }

cv::Point2d SimulationDashboard::nodePoint(const std::string &id) const {
  const auto found = config.nodeIndexById.find(id);
  if (found == config.nodeIndexById.end() ||
      found->second >= config.nodes.size()) {
    throw std::runtime_error("DP8.5: nodo desconocido: " + id);
  }
  const cv::Point p = config.nodes[found->second].pixelPos;
  return cv::Point2d(p.x, p.y);
}

std::string SimulationDashboard::restaurantNode(const std::string &id) const {
  for (const Restaurant &r : config.restaurants) {
    if (r.id == id) return r.nodeId;
  }
  return "";
}

void SimulationDashboard::open() {
#if defined(__linux__)
  const char *x = std::getenv("DISPLAY");
  const char *w = std::getenv("WAYLAND_DISPLAY");
  if ((!x || !*x) && (!w || !*w)) {
    throw std::runtime_error("DP8.5: no hay servidor grafico WSLg/X11");
  }
#endif
  cv::namedWindow(title, cv::WINDOW_NORMAL);
  windowOpen = true;
  cv::resizeWindow(title, base.cols + kPanelWidth, base.rows);
}

std::vector<SimulationDashboard::VisualCourier>
SimulationDashboard::takeSamples(Fleet &fleet) {
  if (trackers.size() != fleet.size()) {
    trackers.resize(fleet.size());
  }

  const auto now = Clock::now();
  std::vector<VisualCourier> result;
  result.reserve(fleet.size());

  for (std::size_t i = 0; i < fleet.size(); ++i) {
    VisualCourier item;
    item.courier = fleet.at(i).getSnapshot();
    const auto &s = item.courier;

    // Los pedidos y estados se leen juntos bajo el mutex de Courier.
    if (s.state == CourierState::Broken) {
      item.phase = "AVERIADA";
    } else if (s.active) {
      const auto &o = *s.active;
      if (o.state == OrderState::PickedUp) {
        item.target = o.deliveryNodeId;
        item.phase = s.currentNode == item.target
                         ? "Finalizando entrega"
                         : "ENTREGANDO AL CLIENTE";
      } else {
        item.target = restaurantNode(o.restauranteId);
        item.phase = s.currentNode == item.target
                         ? "Esperando recogida"
                         : "YENDO AL RESTAURANTE";
      }
    } else if (s.nextOrder) {
      item.phase = "Pedido en espera";
    } else {
      item.phase = "Disponible";
    }

    // La posicion entre nodos es una ESTIMACION visual. El nodo
    // confirmado sigue siendo el informado por Courier::getSnapshot().
    item.point = nodePoint(s.currentNode);
    auto &tracker = trackers[i];
    if (s.state != CourierState::Moving || item.target.empty() ||
        item.target == s.currentNode) {
      tracker.active = false;
    } else {
      const auto route = network.shortestRoute(s.currentNode, item.target);
      if (!route || route->segments.empty()) {
        tracker.active = false;
      } else {
        const auto &segment = route->segments.front();
        if (!tracker.active || tracker.node != s.currentNode ||
            tracker.target != item.target || tracker.next != segment.to) {
          tracker.active = true;
          tracker.node = s.currentNode;
          tracker.target = item.target;
          tracker.next = segment.to;
          tracker.since = now;
        }
        const double speed = config.fleet.speedKmh / 3.6;
        const double duration = segment.distanceMeters / speed /
                                config.simulation.timeScale;
        const double elapsed =
            std::chrono::duration<double>(now - tracker.since).count();
        const double progress = duration > 0.0
                                    ? std::clamp(elapsed / duration, 0.0, 1.0)
                                    : 1.0;
        item.point += (nodePoint(segment.to) - item.point) * progress;
      }
    }
    result.push_back(std::move(item));
  }
  return result;
}

cv::Mat SimulationDashboard::drawFrame(
    const std::vector<VisualCourier> &samples,
    const LiveEventStats &stats, long long elapsedMs) const {

  cv::Mat frame(base.rows, base.cols + kPanelWidth, CV_8UC3,
                cv::Scalar(246, 246, 246));
  base.copyTo(frame(cv::Rect(0, 0, base.cols, base.rows)));

  // Destacar ruta REAL de la moto seleccionada, respetando oneWay.
  if (selected < samples.size() && !samples[selected].target.empty()) {
    const auto &chosen = samples[selected];
    const auto route = network.shortestRoute(chosen.courier.currentNode,
                                             chosen.target);
    if (route) {
      for (const RouteSegment &segment : route->segments) {
        const auto from = nodePoint(segment.from);
        const auto to = nodePoint(segment.to);
        cv::line(frame, cv::Point(cvRound(from.x), cvRound(from.y)),
                 cv::Point(cvRound(to.x), cvRound(to.y)),
                 cv::Scalar(0, 210, 255), 3, cv::LINE_AA);
      }
    }
  }

  // Todos los identificadores se conservan, incluso si hay motos juntas.
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const cv::Point2d actual = samples[i].point;
    std::size_t count = 0;
    std::size_t rank = 0;
    for (std::size_t j = 0; j < samples.size(); ++j) {
      const cv::Point2d delta = samples[j].point - actual;
      if (delta.dot(delta) < 18.0 * 18.0) {
        ++count;
        if (j < i) ++rank;
      }
    }
    cv::Point2d display = actual;
    if (count > 1) {
      const double angle = 2.0 * kPi * static_cast<double>(rank) / count;
      display.x += 19.0 * std::cos(angle);
      display.y += 19.0 * std::sin(angle);
    }
    const cv::Point center(cvRound(display.x), cvRound(display.y));
    cv::circle(frame, center, i == selected ? 17 : 15,
               i == selected ? cv::Scalar(0, 210, 255)
                             : cv::Scalar(255, 255, 255),
               cv::FILLED, cv::LINE_AA);
    cv::circle(frame, center, 12, colorFor(i), cv::FILLED, cv::LINE_AA);
    drawText(frame, samples[i].courier.id, center.x - 8, center.y + 4,
             0.34, cv::Scalar(255, 255, 255));
  }

  const int x = base.cols + 13;
  drawText(frame, "EquipeGo | Simulacion", x, 27, 0.49);
  drawText(frame, "Tiempo sim: " + std::to_string(elapsedMs / 1000) +
                 " s | " + std::to_string(config.simulation.timeScale).substr(0, 4) +
                 "x", x, 49, 0.36);

  drawText(frame, "Creados: " + std::to_string(stats.created) +
                 " | Entregados: " + std::to_string(stats.delivered),
           x, 71, 0.35);
  drawText(frame, "Rechazados: " + std::to_string(stats.rejected) +
                 " | En curso: " + std::to_string(stats.inProgress()),
           x, 89, 0.35);
  drawText(frame, "Averias: " + std::to_string(stats.breakdowns) +
                 " | Reasignados: " + std::to_string(stats.reassigned),
           x, 107, 0.35);
  drawText(frame, "0-7: destacar moto | Q/ESC: salir", x, 127, 0.34);

  for (std::size_t i = 0; i < samples.size(); ++i) {
    const auto &v = samples[i];
    const auto &s = v.courier;
    const int y = 157 + static_cast<int>(i) * 55;
    if (y + 38 > frame.rows - 12) break;
    if (i == selected) {
      cv::rectangle(frame, cv::Rect(x - 4, y - 15, kPanelWidth - 18, 52),
                    cv::Scalar(222, 239, 255), cv::FILLED);
    }
    cv::circle(frame, cv::Point(x + 8, y - 4), 7,
               colorFor(i), cv::FILLED, cv::LINE_AA);
    drawText(frame, s.id + " | " + s.currentNode +
                 " | bolsa: " + std::to_string(s.reservedCount),
             x + 22, y, 0.39);
    const auto &order = s.active ? s.active : s.nextOrder;
    if (order) {
      drawText(frame, order->orderId + "  " + order->restauranteId +
                   " -> " + order->deliveryNodeId,
               x + 22, y + 17, 0.35);
    } else {
      drawText(frame, "Sin pedido activo", x + 22, y + 17, 0.35);
    }
    drawText(frame, v.phase, x + 22, y + 33, 0.33);
  }

  return frame;
}

bool SimulationDashboard::frame(Fleet &fleet, const LiveEventStats &stats,
                                long long elapsedMs) {
  if (!windowOpen) return false;

  const auto samples = takeSamples(fleet);
  cv::imshow(title, drawFrame(samples, stats, elapsedMs));
  const int key = cv::waitKey(33);
  if (key == 27 || key == 'q' || key == 'Q') return false;
  if (key >= '0' && key <= '7') {
    selected = static_cast<std::size_t>(key - '0');
  }
  try {
    if (cv::getWindowProperty(title, cv::WND_PROP_VISIBLE) < 1.0) {
      return false;
    }
  } catch (const cv::Exception &) {
    return false;
  }
  return true;
}

void SimulationDashboard::close() noexcept {
  if (!windowOpen) return;
  windowOpen = false;
  try {
    cv::destroyWindow(title);
  } catch (const cv::Exception &) {
  }
}
