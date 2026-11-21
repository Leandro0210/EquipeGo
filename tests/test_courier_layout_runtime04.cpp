#include "analytics/CourierLayoutRuntime04.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace equipego::performance04;

static void require04(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

int main() {
  try {
    SimulationConfig conf;
    conf.map.bounds.north = -17.6;
    conf.map.bounds.south = -17.8;
    conf.map.bounds.west = -63.3;
    conf.map.bounds.east = -63.1;
    conf.fleet.bagCapacity = 2;
    conf.fleet.speedKmh = 36.0;
    Node a;
    a.id = "a"; a.lat = -17.7; a.lon = -63.2;
    Node b;
    b.id = "b"; b.lat = -17.71; b.lon = -63.21;
    conf.nodes = {a, b};
    conf.nodeIndexById["a"] = 0;
    conf.nodeIndexById["b"] = 1;
    Restaurant r;
    r.id = "r1";
    r.nodeId = "a";
    conf.restaurants.push_back(r);

    CourierSnapshot idle;
    idle.id = "c0"; idle.currentNode = "a";
    idle.state = CourierState::Idle; idle.reservedCount = 0;
    CourierSnapshot moving;
    moving.id = "c1"; moving.currentNode = "a";
    moving.state = CourierState::Moving; moving.reservedCount = 1;
    moving.motion = CourierMotionSnapshot{"a", "b", 0.5};
    CourierSnapshot broken;
    broken.id = "c2"; broken.currentNode = "b";
    broken.state = CourierState::Broken; broken.reservedCount = 0;
    CourierSnapshot full = idle;
    full.id = "c3"; full.reservedCount = 2;

    const auto aos = prepararSnapshots04(conf, {idle, moving, broken, full});
    require04(aos.size() == 4, "cantidad incorrecta");
    require04(aos[0].speedMetersSecond == 10.0f, "velocidad mal convertida");
    require04(aos[1].state == 0, "Moving no puede descartarse automaticamente");
    require04(aos[2].state == 2, "Broken debe descartarse");
    require04(aos[1].xMeters != aos[0].xMeters, "Movimiento sin interpolacion");
    const auto informe = compararLayoutRuntime04(conf, aos);
    require04(informe.couriers == 4 && informe.disponibles == 2,
              "contabilizacion de disponibles incorrecta");
    require04(std::isfinite(informe.sumaSegundos), "resultado invalido");

    auto vacio = compararLayoutRuntime04(conf, {});
    require04(vacio.couriers == 0 && vacio.disponibles == 0, "vacio invalido");

    auto err = moving;
    err.motion->progress = 1.25;
    try {
      (void)prepararSnapshots04(conf, {err});
      throw std::runtime_error("no valido progreso fuera de rango");
    } catch (const std::invalid_argument&) {}
    err = idle;
    err.currentNode = "inexistente";
    try {
      (void)prepararSnapshots04(conf, {err});
      throw std::runtime_error("no valido nodo desconocido");
    } catch (const std::invalid_argument&) {}

    std::cout << "LAYOUT04_RUNTIME_CPU_OK,couriers=4,disponibles=2,"
                 "AOS_SOA_EQUIVALENCIA=OK,FLEET_INTACTO=SI\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "LAYOUT04_RUNTIME_CPU_ERROR," << error.what() << '\n';
    return 1;
  }
}
