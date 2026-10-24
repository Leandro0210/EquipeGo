#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "core/Logger.hpp"
#include "dispatch/Dispatcher.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/OrderBook.hpp"
#include "orders/OrderProducer.hpp"
#include "visualization/MapViewer.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using SteadyClock = std::chrono::steady_clock;
constexpr double kDefaultScale = 3.0;
constexpr int kPanelWidth = 290;
constexpr int kFrameMs = 33;
constexpr double kPi = 3.14159265358979323846;

const std::array<cv::Scalar, 8> kColors = {{
    {245, 100, 40}, {35, 160, 245}, {75, 195, 65}, {220, 65, 180},
    {30, 195, 195}, {165, 100, 65}, {95, 85, 245}, {165, 150, 40}
}};

cv::Scalar colorFor(std::size_t i) { return kColors[i % kColors.size()]; }

cv::Point2d nodePoint(const SimulationConfig &config,
                      const std::string &nodeId) {
  auto found = config.nodeIndexById.find(nodeId);
  if (found == config.nodeIndexById.end()) {
    throw std::runtime_error("Nodo desconocido: " + nodeId);
  }
  const cv::Point p = config.nodes.at(found->second).pixelPos;
  return {static_cast<double>(p.x), static_cast<double>(p.y)};
}

std::string restaurantNode(const SimulationConfig &config,
                           const std::string &restaurantId) {
  for (const auto &restaurant : config.restaurants) {
    if (restaurant.id == restaurantId) return restaurant.nodeId;
  }
  return "";
}

struct VisualCourier {
  CourierSnapshot snapshot;
  cv::Point2d point;
  std::string target;
  std::string phase;
};

struct MotionTracker {
  std::string origin;
  std::string target;
  std::string next;
  SteadyClock::time_point since{};
  bool active = false;

  cv::Point2d locate(const CourierSnapshot &snap,
                     const std::string &destination,
                     const SimulationConfig &config,
                     const RoadNetwork &network,
                     SteadyClock::time_point now) {
    const cv::Point2d confirmed = nodePoint(config, snap.currentNode);
    if (snap.state != CourierState::Moving || destination.empty() ||
        destination == snap.currentNode) {
      active = false;
      return confirmed;
    }
    const auto route = network.shortestRoute(snap.currentNode, destination);
    if (!route || route->segments.empty()) {
      active = false;
      return confirmed;
    }
    const RouteSegment &segment = route->segments.front();
    if (!active || origin != snap.currentNode || target != destination ||
        next != segment.to) {
      origin = snap.currentNode;
      target = destination;
      next = segment.to;
      since = now;
      active = true;
    }
    const double duration = segment.distanceMeters /
                            (config.fleet.speedKmh / 3.6) /
                            config.simulation.timeScale;
    const double elapsed = std::chrono::duration<double>(now - since).count();
    const double fraction = duration > 0.0
        ? std::clamp(elapsed / duration, 0.0, 1.0) : 1.0;
    const cv::Point2d finish = nodePoint(config, segment.to);
    return confirmed + (finish - confirmed) * fraction;
  }
};

std::vector<VisualCourier> takeSamples(
    Fleet &fleet, const SimulationConfig &config, const RoadNetwork &network,
    std::vector<MotionTracker> &trackers) {
  const auto now = SteadyClock::now();
  std::vector<VisualCourier> result;
  result.reserve(fleet.size());
  for (std::size_t i = 0; i < fleet.size(); ++i) {
    VisualCourier item;
    item.snapshot = fleet.at(i).getSnapshot();
    const auto &s = item.snapshot;
    if (s.state == CourierState::Broken) {
      item.phase = "AVERIADA";
    // NUEVO: VARIANTE C - reflejar el nuevo estado también en el preview.
    } else if (s.state == CourierState::Resting) {
      item.phase = "DESCANSANDO";
    } else if (s.active) {
      const auto &o = *s.active;
      if (o.state == OrderState::PickedUp) {
        item.target = o.deliveryNodeId;
        item.phase = s.currentNode == item.target
            ? "Finalizando entrega" : "ENTREGANDO AL CLIENTE";
      } else {
        item.target = restaurantNode(config, o.restauranteId);
        item.phase = s.currentNode == item.target
            ? "Esperando recogida" : "YENDO AL RESTAURANTE";
      }
    } else if (s.nextOrder) {
      item.phase = "Pedido asignado en cola";
    } else {
      item.phase = "Disponible";
    }
    item.point = trackers.at(i).locate(
        s, item.target, config, network, now);
    result.push_back(std::move(item));
  }
  return result;
}

void drawText(cv::Mat &image, const std::string &value, int x, int y,
              double scale = 0.38, cv::Scalar color = {40, 40, 40}) {
  cv::putText(image, value, {x, y}, cv::FONT_HERSHEY_SIMPLEX,
              scale, color, 1, cv::LINE_AA);
}

cv::Mat drawFrame(const cv::Mat &networkImage,
                  const SimulationConfig &config,
                  const RoadNetwork &network,
                  const std::vector<VisualCourier> &samples,
                  long long elapsedMs, double scale, std::size_t selected) {
  cv::Mat frame(networkImage.rows, networkImage.cols + kPanelWidth,
                CV_8UC3, cv::Scalar(246, 246, 246));
  networkImage.copyTo(frame(cv::Rect(0, 0, networkImage.cols,
                                    networkImage.rows)));

  if (selected < samples.size() && !samples[selected].target.empty()) {
    const auto &chosen = samples[selected];
    const auto route = network.shortestRoute(
        chosen.snapshot.currentNode, chosen.target);
    if (route) {
      for (const auto &segment : route->segments) {
        const auto from = nodePoint(config, segment.from);
        const auto to = nodePoint(config, segment.to);
        cv::line(frame, cv::Point(cvRound(from.x), cvRound(from.y)),
                 cv::Point(cvRound(to.x), cvRound(to.y)),
                 cv::Scalar(0, 210, 255), 3, cv::LINE_AA);
      }
    }
  }

  for (std::size_t i = 0; i < samples.size(); ++i) {
    const cv::Point2d actual = samples[i].point;
    std::size_t size = 0, rank = 0;
    for (std::size_t j = 0; j < samples.size(); ++j) {
      const cv::Point2d delta = samples[j].point - actual;
      if (delta.dot(delta) < 18.0 * 18.0) {
        if (j < i) ++rank;
        ++size;
      }
    }
    cv::Point2d display = actual;
    if (size > 1) {
      const double angle = 2.0 * kPi * static_cast<double>(rank) / size;
      display.x += 19.0 * std::cos(angle);
      display.y += 19.0 * std::sin(angle);
    }
    const cv::Point p(static_cast<int>(std::lround(display.x)),
                      static_cast<int>(std::lround(display.y)));
    cv::circle(frame, p, i == selected ? 17 : 15,
               i == selected ? cv::Scalar(0, 210, 255)
                             : cv::Scalar(255, 255, 255),
               cv::FILLED, cv::LINE_AA);
    cv::circle(frame, p, 12, colorFor(i), cv::FILLED, cv::LINE_AA);
    drawText(frame, samples[i].snapshot.id, p.x - 8, p.y + 4,
             0.34, cv::Scalar(255, 255, 255));
  }

  const int x = networkImage.cols + 12;
  drawText(frame, "EquipeGo | Pedidos reales", x, 29, 0.48);
  drawText(frame, "Escala: " + std::to_string(scale).substr(0, 4) +
           "x   Tiempo: " + std::to_string(elapsedMs / 1000) + "s", x, 52);
  drawText(frame, "0-7: destacar moto | Q: salir", x, 73, 0.35);

  for (std::size_t i = 0; i < samples.size(); ++i) {
    const auto &v = samples[i];
    const auto &s = v.snapshot;
    const int y = 98 + static_cast<int>(i) * 63;
    if (y + 50 > frame.rows - 20) break;
    if (i == selected) {
      cv::rectangle(frame, cv::Rect(x - 3, y - 17, kPanelWidth - 20, 57),
                    cv::Scalar(222, 239, 255), cv::FILLED);
    }
    cv::circle(frame, {x + 8, y - 3}, 7, colorFor(i), cv::FILLED, cv::LINE_AA);
    drawText(frame, s.id + " | " + s.currentNode +
             " | bolsa: " + std::to_string(s.reservedCount), x + 23, y);
    const auto &order = s.active ? s.active : s.nextOrder;
    if (order) {
      drawText(frame, order->orderId + "  " + order->restauranteId +
               " -> " + order->deliveryNodeId, x + 23, y + 18, 0.36);
    } else {
      drawText(frame, "Sin pedido activo", x + 23, y + 18, 0.35);
    }
    drawText(frame, v.phase, x + 23, y + 35, 0.34);
  }
  return frame;
}

void requireDisplay() {
#if defined(__linux__)
  const char *x = std::getenv("DISPLAY");
  const char *w = std::getenv("WAYLAND_DISPLAY");
  if ((!x || !*x) && (!w || !*w)) {
    throw std::runtime_error("No hay servidor grafico (WSLg/X11)");
  }
#endif
}

} 

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Uso: real_orders_preview config.json [--scale N] [--check]\n";
    return 1;
  }
  double chosenScale = kDefaultScale;
  bool checkOnly = false;
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--check") {
      checkOnly = true;
    } else if (arg == "--scale" && i + 1 < argc) {
      try {
        std::size_t end = 0;
        const std::string value = argv[++i];
        chosenScale = std::stod(value, &end);
        if (end != value.size()) throw std::invalid_argument("escala");
      } catch (const std::exception &) {
        std::cerr << "Escala invalida\n";
        return 1;
      }
    } else {
      std::cerr << "Opcion desconocida: " << arg << '\n';
      return 1;
    }
  }
  if (!std::isfinite(chosenScale) || chosenScale < 0.25 || chosenScale > 120.0) {
    std::cerr << "La escala debe estar entre 0.25 y 120\n";
    return 1;
  }
  char *args[] = {argv[0], argv[1]};
  SimulationConfig config;
  if (!loadAndValidateConfig(2, args, config)) return 1;

  try {
    if (!checkOnly) requireDisplay();
    config.simulation.timeScale = checkOnly ? 60.0 : chosenScale;
    MapViewer viewer(config.map);
    const cv::Mat networkImage = viewer.renderNetwork(config);
    RoadNetwork network(config);
    SimulationClock clock;
    clock.start(config.simulation.timeScale);
    const auto stamp = SteadyClock::now().time_since_epoch().count();
    const auto logPath = std::filesystem::temp_directory_path() /
        ("equipego_visual_" + std::to_string(stamp) + ".jsonl");
    EventLogger logger(logPath.string(), false);
    OrderBook book(static_cast<std::size_t>(config.orders.maxPending));
    OrderProducer producer(config, book, logger, clock, network);
    Fleet fleet(config, network, clock);
    Dispatcher dispatcher(config, network, fleet);

    std::thread producerThread;
    std::thread dispatcherThread;
    bool stopped = false;
    auto stopAll = [&] {
      if (stopped) return;
      stopped = true;
      producer.requestStop();
      if (producerThread.joinable()) producerThread.join();
      dispatcher.requestStop(book);
      if (dispatcherThread.joinable()) dispatcherThread.join();
      fleet.requestStop();
      fleet.join();
      logger.logEvent({{"event", "simulationStopping"}},
                      clock.getSimulatedTimeMs());
      logger.flush();
    };

    bool validCheck = true;
    try {
      fleet.start();
      for (std::size_t i = 0; i < fleet.size(); ++i) {
        fleet.at(i).setEventLogger(&logger);
      }
      fleet.startRescue();
      dispatcherThread = std::thread(&Dispatcher::run, &dispatcher,
                                     std::ref(book), std::ref(logger),
                                     std::ref(clock));
      logger.logStart();
      producerThread = std::thread(&OrderProducer::run, &producer);
      std::vector<MotionTracker> trackers(fleet.size());
      if (checkOnly) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1300));
        const auto samples = takeSamples(fleet, config, network, trackers);
        const auto frame = drawFrame(networkImage, config, network, samples,
                                     clock.getSimulatedTimeMs(),
                                     config.simulation.timeScale, 0);
        validCheck = samples.size() == fleet.size() &&
                     frame.cols == networkImage.cols + kPanelWidth &&
                     frame.rows == networkImage.rows;
      } else {
        const std::string title = "EquipeGo - Pedidos reales";
        cv::namedWindow(title, cv::WINDOW_NORMAL);
        cv::resizeWindow(title, networkImage.cols + kPanelWidth,
                         networkImage.rows);
        const auto realStart = SteadyClock::now();
        const double maxRealSeconds =
            config.simulation.durationS / config.simulation.timeScale;
        std::size_t selected = 0;
        while (std::chrono::duration<double>(SteadyClock::now() - realStart)
                   .count() < maxRealSeconds) {
          const auto samples = takeSamples(fleet, config, network, trackers);
          const cv::Mat frame = drawFrame(networkImage, config, network,
                                         samples, clock.getSimulatedTimeMs(),
                                         chosenScale, selected);
          cv::imshow(title, frame);
          const int key = cv::waitKey(kFrameMs);
          if (key == 27 || key == 'q' || key == 'Q') break;
          if (key >= '0' && key <= '7' &&
              static_cast<std::size_t>(key - '0') < fleet.size()) {
            selected = static_cast<std::size_t>(key - '0');
          }
          try {
            if (cv::getWindowProperty(title, cv::WND_PROP_VISIBLE) < 1.0)
              break;
          } catch (const cv::Exception &) {
            break;
          }
        }
        cv::destroyWindow(title);
      }
      stopAll();
    } catch (...) {
      try { stopAll(); } catch (...) {}
      throw;
    }

    const auto results = fleet.takeFinishedOrders();
    std::size_t delivered = 0;
    for (const auto &entry : results) {
      if (entry.order.getState() == OrderState::Delivered) ++delivered;
    }
    std::cout << "Pedidos creados: " << producer.getCreatedCount() << '\n'
              << "Asignados: " << dispatcher.getAssignedCount() << '\n'
              << "Entregados: " << delivered << '\n'
              << "Eventos JSONL: " << logPath.string() << '\n';
    if (checkOnly) {
      std::cout << "DP8.4 REAL ORDERS CHECK: "
                << (validCheck ? "OK" : "ERROR") << '\n';
      return validCheck ? 0 : 1;
    }
    std::cout << "DP8.4 REAL ORDERS VIEWER: OK\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Error DP8.4: " << error.what() << '\n';
    return 1;
  }
}
