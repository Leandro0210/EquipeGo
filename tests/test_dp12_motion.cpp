#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/Courier.hpp"
#include "map/RoadNetwork.hpp"
#include "orders/Order.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using Clock = std::chrono::steady_clock;

namespace {
void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

SimulationConfig makeConfig() {
  SimulationConfig c;
  c.nodes.push_back(Node{"n0", -17.75, -63.20, cv::Point{0, 0}});
  c.nodes.push_back(Node{"n1", -17.75, -63.19, cv::Point{100, 0}});
  c.nodes.push_back(Node{"n2", -17.75, -63.18, cv::Point{200, 0}});
  c.streets.push_back(Street{"s0", "n0", "n1", false, 1000.0});
  c.streets.push_back(Street{"s1", "n1", "n2", false, 1000.0});
  c.restaurants.push_back(Restaurant{"r0", "Ejemplo", "n1", 1, 0, 0});
  c.fleet.startNode = "n0";
  c.fleet.speedKmh = 36.0;
  c.fleet.bagCapacity = 1;
  c.simulation.timeScale = 60.0; 
  c.incidents.breakdownProbability = 0.0;
  return c;
}

template <class Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout) {
  const auto deadline = Clock::now() + timeout;
  do {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (Clock::now() < deadline);
  return predicate();
}

void testRealProgress() {
  auto cfg = makeConfig();
  RoadNetwork network(cfg);
  SimulationClock simClock;
  simClock.start(cfg.simulation.timeScale);
  Courier courier("c0", cfg, network, simClock);
  std::thread worker(&Courier::run, &courier);

  try {
    require(courier.assignDestination("n1"), "No se pudo asignar destino");
    require(waitUntil([&] {
              const auto s = courier.getSnapshot();
              return s.motion && s.motion->from == "n0" &&
                     s.motion->to == "n1" && s.motion->progress > 0.02 &&
                     s.motion->progress < 0.8;
            }, std::chrono::milliseconds(900)),
            "No se publico el segmento real del repartidor");
    const auto a = courier.getSnapshot();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto b = courier.getSnapshot();
    require(a.motion && b.motion, "El intervalo desaparecio demasiado pronto");
    require(b.motion->from == a.motion->from && b.motion->to == a.motion->to,
            "El segmento cambio inesperadamente");
    require(b.motion->progress > a.motion->progress &&
                b.motion->progress < 1.0,
            "El progreso no avanzo continuamente en el segmento");
    require(a.currentNode == "n0" && b.currentNode == "n0",
            "El nodo confirmado avanzo antes de completar el tramo");
  } catch (...) {
    courier.requestStop();
    worker.join();
    throw;
  }
  courier.requestStop();
  worker.join();
}

void testBreakdownFrozen() {
  auto cfg = makeConfig();
  cfg.incidents.breakdownProbability = 1.0;
  RoadNetwork network(cfg);
  SimulationClock simClock;
  simClock.start(cfg.simulation.timeScale);
  Courier courier("c1", cfg, network, simClock);
  std::thread worker(&Courier::run, &courier);

  try {
    const std::string orderId = "dp12_motion_averia";
    Order order(orderId, "r0", "n2", simClock.getSimulatedTimeMs());
    require(courier.tryAssignOrder(std::move(order)),
            "No se pudo asignar pedido para probar averia");
    require(waitUntil([&] {
              return courier.getState() == CourierState::Broken;
            }, std::chrono::milliseconds(3500)),
            "No ocurrio averia con probabilidad uno");
    const auto a = courier.getSnapshot();
    require(a.motion && a.motion->from == "n0" && a.motion->to == "n1" &&
                a.motion->progress > 0.0 && a.motion->progress < 1.0,
            "La averia no conservo su posicion intermedia");
    std::this_thread::sleep_for(std::chrono::milliseconds(90));
    const auto b = courier.getSnapshot();
    require(b.motion && std::abs(a.motion->progress - b.motion->progress) < 1e-9,
            "La moto averiada siguio avanzando o regreso al nodo");
    require(b.currentNode == "n0", "Se falsifico la llegada al siguiente nodo");
  } catch (...) {
    courier.requestStop();
    worker.join();
    throw;
  }
  courier.requestStop();
  worker.join();
}
} 

int main() {
  try {
    testRealProgress();
    testBreakdownFrozen();
    std::cout << "DP12.4 PASS: segmento real, progreso monotono, averia inmovil" << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "DP12.4 FAIL: " << e.what() << std::endl;
    return 1;
  }
}
