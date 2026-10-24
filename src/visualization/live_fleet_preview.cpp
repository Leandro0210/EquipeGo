
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/Fleet.hpp"
#include "map/RoadNetwork.hpp"
#include "visualization/MapViewer.hpp"

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using SteadyClock = std::chrono::steady_clock;

constexpr double kDefaultTimeScale = 3.0;
constexpr int kPanelWidth = 260;
constexpr int kFrameDelayMs = 33;


const std::array<cv::Scalar, 8> kCourierColors = {{{255, 85, 40},
                                                   {50, 170, 250},
                                                   {75, 190, 65},
                                                   {220, 70, 190},
                                                   {50, 190, 190},
                                                   {175, 110, 70},
                                                   {90, 100, 245},
                                                   {180, 170, 45}}};

cv::Scalar courierColor(std::size_t index) {
  return kCourierColors[index % kCourierColors.size()];
}

cv::Scalar stateColor(CourierState state) {
  switch (state) {
  case CourierState::Idle:
    return {65, 175, 65};
  case CourierState::Moving:
    return {45, 185, 245};
  case CourierState::Broken:
    return {45, 45, 230};
  // NUEVO: VARIANTE C - rama obligatoria del nuevo enum.
  case CourierState::Resting:
    return {185, 70, 165};
  }
  return {90, 90, 90};
}

std::string stateName(CourierState state) {
  switch (state) {
  case CourierState::Idle:
    return "Disponible";
  case CourierState::Moving:
    return "En ruta";
  case CourierState::Broken:
    return "Averiado";
  // NUEVO: VARIANTE C - nombre visible del descanso.
  case CourierState::Resting:
    return "Descansando";
  }
  return "Desconocido";
}


cv::Point2d nodePosition(const SimulationConfig &config,
                         const std::string &nodeId) {

  const auto it = config.nodeIndexById.find(nodeId);

  if (it == config.nodeIndexById.end() || it->second >= config.nodes.size()) {
    throw std::runtime_error("DP8.3: nodo desconocido: " + nodeId);
  }

  const cv::Point p = config.nodes[it->second].pixelPos;
  return cv::Point2d(p.x, p.y);
}


cv::Point2d interpolate(const cv::Point2d &from, const cv::Point2d &to,
                        double progress) {

  const double alpha = std::clamp(progress, 0.0, 1.0);

  return cv::Point2d(from.x + (to.x - from.x) * alpha,
                     from.y + (to.y - from.y) * alpha);
}


struct CourierSample {
  std::string id;
  std::string node;
  CourierState state = CourierState::Idle;
  cv::Point2d position{0.0, 0.0};
  std::string nextNode;
  double progress = 0.0;
};


class MotionTracker {
private:
  bool active = false;

  std::string lastNode;
  std::string lastTarget;
  std::string lastNextNode;

  SteadyClock::time_point segmentStart{};

public:
  void reset() { active = false; }

  cv::Point2d position(const SimulationConfig &config,
                       const RoadNetwork &network,
                       const std::string &currentNode,
                       const std::string &targetNode, CourierState state,
                       SteadyClock::time_point now, std::string &nextNode,
                       double &progress) {

    const cv::Point2d confirmed = nodePosition(config, currentNode);

    nextNode.clear();
    progress = 0.0;

    if (state != CourierState::Moving || currentNode == targetNode) {
      reset();
      return confirmed;
    }

    const auto route = network.shortestRoute(currentNode, targetNode);

    if (!route || route->segments.empty()) {
      reset();
      return confirmed;
    }

    const RouteSegment &segment = route->segments.front();

    nextNode = segment.to;

    if (!active || lastNode != currentNode || lastTarget != targetNode ||
        lastNextNode != nextNode) {

      active = true;
      lastNode = currentNode;
      lastTarget = targetNode;
      lastNextNode = nextNode;
      segmentStart = now;
    }

    const double speedMetersPerSecond = config.fleet.speedKmh / 3.6;

    const double realDurationSeconds = segment.distanceMeters /
                                       speedMetersPerSecond /
                                       config.simulation.timeScale;

    if (realDurationSeconds <= 0.0) {
      progress = 1.0;
    } else {
      const double elapsedSeconds =
          std::chrono::duration<double>(now - segmentStart).count();

      progress = std::clamp(elapsedSeconds / realDurationSeconds, 0.0, 1.0);
    }

    const cv::Point2d next = nodePosition(config, nextNode);

    return interpolate(confirmed, next, progress);
  }
};


std::vector<CourierSample>
sampleFleet(Fleet &fleet, const SimulationConfig &config,
            const RoadNetwork &network, const std::vector<std::string> &targets,
            std::vector<MotionTracker> &trackers, SteadyClock::time_point now) {

  std::vector<CourierSample> samples;
  samples.reserve(fleet.size());

  for (std::size_t i = 0; i < fleet.size(); ++i) {

    Courier &courier = fleet.at(i);

    CourierSample sample;
    sample.id = courier.getId();

    sample.node = courier.getCurrentNode();
    sample.state = courier.getState();

    sample.position = trackers.at(i).position(config, network, sample.node,
                                              targets.at(i), sample.state, now,
                                              sample.nextNode, sample.progress);

    samples.push_back(std::move(sample));
  }

  return samples;
}


void drawCourier(cv::Mat &frame, const CourierSample &sample,
                 std::size_t index) {

  const cv::Point center(static_cast<int>(std::lround(sample.position.x)),
                         static_cast<int>(std::lround(sample.position.y)));

  const cv::Scalar color = courierColor(index);

  cv::circle(frame, center, 17, stateColor(sample.state), cv::FILLED,
             cv::LINE_AA);

  cv::circle(frame, center, 14, cv::Scalar(255, 255, 255), cv::FILLED,
             cv::LINE_AA);

  cv::circle(frame, center, 12, color, cv::FILLED, cv::LINE_AA);

  int baseline = 0;

  const cv::Size labelSize =
      cv::getTextSize(sample.id, cv::FONT_HERSHEY_SIMPLEX, 0.38, 1, &baseline);

  cv::putText(frame, sample.id,
              cv::Point(center.x - labelSize.width / 2,
                        center.y + labelSize.height / 2),
              cv::FONT_HERSHEY_SIMPLEX, 0.38, cv::Scalar(255, 255, 255), 1,
              cv::LINE_AA);
}


cv::Mat renderFleet(const cv::Mat &networkImage,
                    const std::vector<CourierSample> &samples,
                    long long simulatedMs, double timeScale) {

  const int width = networkImage.cols + kPanelWidth;

  cv::Mat frame(networkImage.rows, width, CV_8UC3, cv::Scalar(247, 247, 247));

  networkImage.copyTo(
      frame(cv::Rect(0, 0, networkImage.cols, networkImage.rows)));


  std::vector<bool> drawn(samples.size(), false);

  for (std::size_t i = 0; i < samples.size(); ++i) {

    if (drawn[i]) {
      continue;
    }

    std::vector<std::size_t> overlapping{i};
    drawn[i] = true;

    for (std::size_t j = i + 1; j < samples.size(); ++j) {

      if (drawn[j]) {
        continue;
      }

      const cv::Point2d difference = samples[j].position - samples[i].position;

      const double distanceSquared =
          difference.x * difference.x + difference.y * difference.y;

      if (distanceSquared <= 18.0 * 18.0) {
        overlapping.push_back(j);
        drawn[j] = true;
      }
    }

    if (overlapping.size() == 1) {

      drawCourier(frame, samples[i], i);
      continue;
    }

    const cv::Point center(
        static_cast<int>(std::lround(samples[i].position.x)),
        static_cast<int>(std::lround(samples[i].position.y)));

    cv::circle(frame, center, 19, cv::Scalar(255, 255, 255), cv::FILLED,
               cv::LINE_AA);

    cv::circle(frame, center, 16, cv::Scalar(145, 75, 160), cv::FILLED,
               cv::LINE_AA);

    const std::string count = std::to_string(overlapping.size());

    cv::putText(frame, count, cv::Point(center.x - 5, center.y + 6),
                cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(255, 255, 255), 2,
                cv::LINE_AA);
  }


  const int x = networkImage.cols + 15;

  cv::putText(frame, "EquipeGo", cv::Point(x, 31), cv::FONT_HERSHEY_SIMPLEX,
              0.65, cv::Scalar(35, 35, 35), 2, cv::LINE_AA);

  cv::putText(frame, "Flota - Movimiento suave", cv::Point(x, 53),
              cv::FONT_HERSHEY_SIMPLEX, 0.38, cv::Scalar(75, 75, 75), 1,
              cv::LINE_AA);

  cv::putText(frame, "Escala: " + std::to_string(timeScale).substr(0, 4) + "x",
              cv::Point(x, 79), cv::FONT_HERSHEY_SIMPLEX, 0.43,
              cv::Scalar(60, 60, 60), 1, cv::LINE_AA);

  cv::putText(frame, "Tiempo sim: " + std::to_string(simulatedMs / 1000) + " s",
              cv::Point(x, 103), cv::FONT_HERSHEY_SIMPLEX, 0.42,
              cv::Scalar(60, 60, 60), 1, cv::LINE_AA);

  int y = 142;

  for (std::size_t i = 0; i < samples.size(); ++i) {

    const CourierSample &sample = samples[i];

    cv::circle(frame, cv::Point(x + 7, y - 5), 7, courierColor(i), cv::FILLED,
               cv::LINE_AA);

    cv::putText(frame, sample.id + " | " + sample.node, cv::Point(x + 23, y),
                cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(30, 30, 30), 1,
                cv::LINE_AA);

    cv::putText(frame,
                stateName(sample.state) +
                    (sample.nextNode.empty() ? "" : " -> " + sample.nextNode),
                cv::Point(x + 23, y + 18), cv::FONT_HERSHEY_SIMPLEX, 0.34,
                cv::Scalar(90, 90, 90), 1, cv::LINE_AA);

    y += 53;

    if (y > frame.rows - 80) {
      break;
    }
  }

  cv::putText(frame, "Q / ESC para cerrar", cv::Point(x, frame.rows - 24),
              cv::FONT_HERSHEY_SIMPLEX, 0.39, cv::Scalar(90, 90, 90), 1,
              cv::LINE_AA);

  return frame;
}


std::vector<std::string> chooseDestinations(const SimulationConfig &config,
                                            const RoadNetwork &network,
                                            std::size_t courierCount) {

  std::vector<std::string> candidates;

  for (const Node &node : config.nodes) {

    if (node.id == config.fleet.startNode) {
      continue;
    }

    if (network.isReachable(config.fleet.startNode, node.id) &&
        network.isReachable(node.id, config.fleet.startNode)) {
      candidates.push_back(node.id);
    }
  }

  if (candidates.empty()) {
    throw std::runtime_error("DP8.3: no existen destinos de ida y vuelta");
  }

  std::vector<std::string> destinations;
  destinations.reserve(courierCount);

  for (std::size_t i = 0; i < courierCount; ++i) {
    destinations.push_back(candidates.at(i * candidates.size() / courierCount));
  }

  return destinations;
}


void requireGraphics() {

#if defined(__linux__)
  const char *display = std::getenv("DISPLAY");
  const char *wayland = std::getenv("WAYLAND_DISPLAY");

  const bool hasX11 = display != nullptr && display[0] != '\0';

  const bool hasWayland = wayland != nullptr && wayland[0] != '\0';

  if (!hasX11 && !hasWayland) {
    throw std::runtime_error("DP8.3: WSLg/X11 no disponible");
  }
#endif
}


bool testInterpolation() {

  const cv::Point2d from(10.0, 20.0);
  const cv::Point2d to(110.0, 220.0);

  const cv::Point2d start = interpolate(from, to, 0.0);

  const cv::Point2d middle = interpolate(from, to, 0.5);

  const cv::Point2d end = interpolate(from, to, 1.0);

  return cv::norm(start - from) < 1e-9 &&
         cv::norm(middle - cv::Point2d(60.0, 120.0)) < 1e-9 &&
         cv::norm(end - to) < 1e-9;
}


} 

int main(int argc, char *argv[]) {

  bool checkOnly = false;
  double chosenScale = kDefaultTimeScale;

  if (argc < 2) {
    std::cerr << "Uso: live_fleet_preview <config.json>"
              << " [--check] [--scale N]\n";
    return 1;
  }


  for (int i = 2; i < argc; ++i) {

    const std::string option = argv[i];

    if (option == "--check") {
      checkOnly = true;

    } else if (option == "--scale" && i + 1 < argc) {

      try {
        const std::string value = argv[++i];
        std::size_t consumed = 0;

        chosenScale = std::stod(value, &consumed);

        if (consumed != value.size()) {
          throw std::invalid_argument("escala invalida");
        }

      } catch (const std::exception &) {
        std::cerr << "Escala invalida.\n";
        return 1;
      }

    } else {
      std::cerr << "Opcion desconocida: " << option << '\n';
      return 1;
    }
  }

  if (!std::isfinite(chosenScale) || chosenScale < 0.25 ||
      chosenScale > 120.0) {

    std::cerr << "La escala debe estar entre 0.25 y 120.\n";
    return 1;
  }

  char *configArgs[] = {argv[0], argv[1]};

  SimulationConfig config;

  if (!loadAndValidateConfig(2, configArgs, config)) {
    return 1;
  }

  try {


    config.simulation.timeScale = chosenScale;

    MapViewer viewer(config.map);

    const cv::Mat networkImage = viewer.renderNetwork(config);

    RoadNetwork network(config);

    if (!checkOnly) {
      requireGraphics();
    }

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    Fleet fleet(config, network, clock);
    fleet.start();

    const std::vector<std::string> destinations =
        chooseDestinations(config, network, fleet.size());

    std::vector<std::string> nextTargets = destinations;

    std::vector<bool> goingOut(fleet.size(), true);

    std::vector<MotionTracker> trackers(fleet.size());


    for (std::size_t i = 0; i < fleet.size(); ++i) {

      if (!fleet.at(i).assignDestination(nextTargets.at(i))) {

        throw std::runtime_error("No se pudo asignar ruta a " +
                                 fleet.at(i).getId());
      }
    }


    if (checkOnly) {

      std::this_thread::sleep_for(std::chrono::milliseconds(50));

      const auto samples = sampleFleet(fleet, config, network, nextTargets,
                                       trackers, SteadyClock::now());

      const cv::Mat frame = renderFleet(
          networkImage, samples, clock.getSimulatedTimeMs(), chosenScale);

      bool valid = testInterpolation() && samples.size() == fleet.size() &&
                   frame.cols == networkImage.cols + kPanelWidth &&
                   frame.rows == networkImage.rows && frame.type() == CV_8UC3;

      for (const CourierSample &sample : samples) {
        valid = valid && config.nodeIndexById.count(sample.node) != 0 &&
                std::isfinite(sample.position.x) &&
                std::isfinite(sample.position.y);
      }

      fleet.requestStop();
      fleet.join();

      std::cout << "=== TEST DP8.3 MOVIMIENTO SUAVE ===\n"
                << "Motos: " << samples.size() << '\n'
                << "Escala: " << chosenScale << "x\n"
                << "Interpolacion: " << (testInterpolation() ? "OK" : "ERROR")
                << '\n'
                << "Render y posiciones: " << (valid ? "OK" : "ERROR") << '\n'
                << "DP8.3 SMOOTH MOTION CHECK: " << (valid ? "OK" : "ERROR")
                << '\n';

      return valid ? 0 : 1;
    }


    const std::string title = "EquipeGo - Motos en movimiento";

    cv::namedWindow(title, cv::WINDOW_NORMAL);

    cv::resizeWindow(title, networkImage.cols + kPanelWidth, networkImage.rows);

    std::cout << "DP8.3: movimiento suave iniciado.\n"
              << "Escala visual: " << chosenScale << "x\n"
              << "Pulsa Q o ESC para salir.\n";


    while (true) {

      for (std::size_t i = 0; i < fleet.size(); ++i) {

        Courier &courier = fleet.at(i);

        if (courier.getState() != CourierState::Idle) {
          continue;
        }

        if (courier.getCurrentNode() != nextTargets.at(i)) {
          continue;
        }

        const bool outbound = goingOut.at(i);

        const std::string target =
            outbound ? config.fleet.startNode : destinations.at(i);

        if (courier.assignDestination(target)) {
          nextTargets.at(i) = target;
          goingOut.at(i) = !outbound;
          trackers.at(i).reset();
        }
      }

      const auto samples = sampleFleet(fleet, config, network, nextTargets,
                                       trackers, SteadyClock::now());

      const cv::Mat frame = renderFleet(
          networkImage, samples, clock.getSimulatedTimeMs(), chosenScale);

      cv::imshow(title, frame);

      const int key = cv::waitKey(kFrameDelayMs);

      if (key == 27 || key == 'q' || key == 'Q') {
        break;
      }

      try {
        if (cv::getWindowProperty(title, cv::WND_PROP_VISIBLE) < 1.0) {
          break;
        }
      } catch (const cv::Exception &) {
        break;
      }
    }


    fleet.requestStop();
    fleet.join();

    try {
      cv::destroyWindow(title);
    } catch (const cv::Exception &) {
    }

    std::cout << "DP8.3 SMOOTH MOTION VIEWER: OK\n";

    return 0;

  } catch (const std::exception &error) {

    std::cerr << "Error DP8.3: " << error.what() << '\n';

    return 1;
  }
}
