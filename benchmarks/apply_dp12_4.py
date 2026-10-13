#!/usr/bin/env python3
"""DP.12.4: la posición gráfica proviene del segmento real de Courier.

Aplicador fail-closed: exige anclajes exactos del código de avance-12,
crea copias fuera del repositorio, no modifica nada si falta un anclaje.
"""
import argparse
from datetime import datetime
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]


def replace_one(text: str, old: str, new: str, where: str) -> str:
    count = text.count(old)
    if count != 1:
        raise ValueError(f"{where}: se esperaba un único anclaje, encontrados {count}")
    return text.replace(old, new, 1)


def patch_hpp(text):
    text = replace_one(text, '#include <condition_variable>\n',
                       '#include <algorithm>\n#include <chrono>\n#include <condition_variable>\n', 'Courier.hpp: includes')
    text = replace_one(text, 'struct CourierSnapshot {', '''// Progreso sobre el segmento que el hilo del repartidor esta recorriendo.
// Se publica como copia; la GUI nunca accede a datos mutables de Courier.
struct CourierMotionSnapshot {
  std::string from;
  std::string to;
  double progress = 0.0;  // fraccion [0, 1] del segmento real
};

struct CourierSnapshot {''', 'Courier.hpp: snapshot struct')
    text = replace_one(text, '  std::optional<CourierOrderSnapshot> nextOrder;\n  std::size_t reservedCount = 0;',
                       '  std::optional<CourierOrderSnapshot> nextOrder;\n  std::optional<CourierMotionSnapshot> motion;\n  std::size_t reservedCount = 0;', 'Courier.hpp: snapshot motion')
    text = replace_one(text, '  bool everBroken = false;\n\n  std::optional<std::string> destination;', '''  bool everBroken = false;

  // Exclusivamente bajo stateMutex: misma fuente de verdad para simulacion y GUI.
  struct MotionInterval {
    std::string from;
    std::string to;
    std::chrono::steady_clock::time_point started;
    double wholeSegmentSeconds = 0.0;
    double stopFraction = 1.0;
    bool frozen = false;
  };
  std::optional<MotionInterval> motionInterval;

  std::optional<std::string> destination;''', 'Courier.hpp: interval')
    text = replace_one(text, '    snapshot.state = state;\n    snapshot.reservedCount =', '''    snapshot.state = state;
    if (motionInterval) {
      const MotionInterval &m = *motionInterval;
      const double elapsed = std::chrono::duration<double>(
          std::chrono::steady_clock::now() - m.started).count();
      const double fraction = m.frozen || m.wholeSegmentSeconds <= 0.0
                                  ? m.stopFraction
                                  : std::clamp(elapsed / m.wholeSegmentSeconds,
                                               0.0, m.stopFraction);
      snapshot.motion = CourierMotionSnapshot{m.from, m.to, fraction};
    }
    snapshot.reservedCount =''', 'Courier.hpp: getter')
    return text


def patch_cpp(text):
    anchor = '''    {
      std::unique_lock<std::mutex> lock(stateMutex);

      const bool interrupted =
          workCv.wait_for(lock, std::chrono::duration<double>(realSeconds),
                          [this] { return stopRequested; });

      if (interrupted || stopRequested) {
        return false;
      }

      if (breakdownHere) {'''
    replacement = '''    {
      std::unique_lock<std::mutex> lock(stateMutex);

      // El intervalo comienza inmediatamente antes de liberar el mutex
      // durante la espera real. La GUI ve exactamente ese instante.
      const double entireRealSeconds =
          (segment.distanceMeters / speedMetersPerSecond) /
          config.simulation.timeScale;
      const double stopFraction = segment.distanceMeters > 0.0
          ? std::clamp(distanceToTravel / segment.distanceMeters, 0.0, 1.0)
          : 1.0;
      motionInterval = MotionInterval{segment.from, segment.to,
                                      std::chrono::steady_clock::now(),
                                      entireRealSeconds, stopFraction, false};

      const bool interrupted =
          workCv.wait_for(lock, std::chrono::duration<double>(realSeconds),
                          [this] { return stopRequested; });

      if (interrupted || stopRequested) {
        motionInterval.reset();
        return false;
      }

      if (breakdownHere) {
        // Congelar en el punto REAL de la averia, incluso entre nodos.
        motionInterval->frozen = true;'''
    text = replace_one(text,anchor,replacement,'Courier.cpp: travel begin')
    text = replace_one(text, '''      } else {
        currentNode = segment.to;
      }
    }

    if (breakdownHere) {''', '''      } else {
        currentNode = segment.to;
        motionInterval.reset();
      }
    }

    if (breakdownHere) {''','Courier.cpp: travel end')
    return text


def patch_dash_hpp(text):
    text=replace_one(text,'#include <chrono>\n','', 'Dashboard.hpp: chrono')
    old='''  using Clock = std::chrono::steady_clock;

  struct MotionTracker {
    std::string node;
    std::string target;
    std::string next;
    Clock::time_point since{};
    bool active = false;
  };

'''
    text=replace_one(text,old,'', 'Dashboard.hpp: tracker')
    text=replace_one(text,'  std::vector<MotionTracker> trackers;\n','', 'Dashboard.hpp: trackers')
    return text


def patch_dash_cpp(text):
    start='''std::vector<SimulationDashboard::VisualCourier>
SimulationDashboard::takeSamples(Fleet &fleet) {'''
    end='''cv::Mat SimulationDashboard::drawFrame('''
    if text.count(start)!=1 or text.count(end)!=1:
        raise ValueError('Dashboard.cpp: limites de funcion ausentes/duplicados')
    before, rest=text.split(start,1)
    middle, after=rest.split(end,1)
    if 'tracker' not in middle or 'network.shortestRoute' not in middle:
        raise ValueError('Dashboard.cpp: takeSamples no coincide con version esperada')
    new_func='''std::vector<SimulationDashboard::VisualCourier>
SimulationDashboard::takeSamples(Fleet &fleet) {
  std::vector<VisualCourier> result;
  result.reserve(fleet.size());

  for (std::size_t i = 0; i < fleet.size(); ++i) {
    VisualCourier item;
    item.courier = fleet.at(i).getSnapshot();
    const auto &s = item.courier;

    if (s.state == CourierState::Broken) {
      item.phase = "AVERIADA";
    } else if (s.active) {
      const auto &o = *s.active;
      if (o.state == OrderState::PickedUp) {
        item.target = o.deliveryNodeId;
        item.phase = s.motion ? "ENTREGANDO AL CLIENTE" : "Finalizando entrega";
      } else {
        item.target = restaurantNode(o.restauranteId);
        item.phase = s.motion ? "YENDO AL RESTAURANTE" : "Esperando recogida";
      }
    } else if (s.motion) {
      item.phase = "EN RUTA";
    } else if (s.nextOrder) {
      item.phase = "Pedido en espera";
    } else {
      item.phase = "Disponible";
    }

    // Nunca usamos un cronometro del visor: el progreso procede del hilo
    // Courier y del mismo segmento dirigido recorrido en travelTo().
    item.point = nodePoint(s.currentNode);
    if (s.motion) {
      const cv::Point2d from = nodePoint(s.motion->from);
      const cv::Point2d to = nodePoint(s.motion->to);
      item.point = from + (to - from) * s.motion->progress;
    }
    result.push_back(std::move(item));
  }
  return result;
}

'''
    text=before+new_func+end+after
    old='''  // Todos los identificadores se conservan, incluso si hay motos juntas.
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
'''
    new='''  // Centro REAL: desplazar un punto para separar motos lo sacaria
  // artificialmente de la calle. Dibujamos todas en su posicion autentica.
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const cv::Point2d actual = samples[i].point;
    const cv::Point center(cvRound(actual.x), cvRound(actual.y));
    cv::circle(frame, center, i == selected ? 10 : 7,
               i == selected ? cv::Scalar(0, 210, 255)
                             : cv::Scalar(255, 255, 255),
               cv::FILLED, cv::LINE_AA);
    cv::circle(frame, center, i == selected ? 7 : 5,
               colorFor(i), cv::FILLED, cv::LINE_AA);
    if (samples.size() <= 8 || i == selected) {
      drawText(frame, samples[i].courier.id,
               center.x + 8, center.y - 8, 0.33, colorFor(i));
    }
  }
'''
    text=replace_one(text,old,new,'Dashboard.cpp: draw marker at real point')
    text=replace_one(text,'constexpr double kPi = 3.14159265358979323846;\n','', 'Dashboard.cpp: unused pi')
    return text


def patch_cmake(text):
    old='set_tests_properties(${DP_TEST_NAMES} PROPERTIES TIMEOUT 30)'
    new='''# DP12.4: posicion publicada por Courier, sin reloj visual independiente.
add_executable(test_dp12_motion
    tests/test_dp12_motion.cpp
    src/core/Logger.cpp
    src/map/RoadNetwork.cpp
    src/fleet/Courier.cpp
)
configure_equipego_target(test_dp12_motion)
add_test(NAME test_dp12_motion COMMAND test_dp12_motion)
list(APPEND DP_TEST_NAMES test_dp12_motion)

set_tests_properties(${DP_TEST_NAMES} PROPERTIES TIMEOUT 30)'''
    return replace_one(text,old,new,'CMakeLists.txt: new test')


def main():
    p=argparse.ArgumentParser()
    group=p.add_mutually_exclusive_group(required=True)
    group.add_argument('--check', action='store_true')
    group.add_argument('--apply', action='store_true')
    args=p.parse_args()
    fns={
       'src/fleet/Courier.hpp':patch_hpp,
       'src/fleet/Courier.cpp':patch_cpp,
       'src/visualization/SimulationDashboard.hpp':patch_dash_hpp,
       'src/visualization/SimulationDashboard.cpp':patch_dash_cpp,
       'CMakeLists.txt':patch_cmake,
    }
    originals={}
    updates={}
    for file,fn in fns.items():
        path=ROOT/file
        if not path.is_file():
            raise FileNotFoundError(f'No existe {file}. Ejecuta desde la raiz de EquipeGo.')
        original=path.read_text(encoding='utf-8')
        transformed=fn(original)
        if transformed==original:
            raise ValueError(f'{file}: el aplicador no produjo cambios')
        originals[file]=original
        updates[file]=transformed
        print('VALIDADO:',file)
    if args.check:
        print('DP12.4 CHECK OK | 5 archivos verificados, ningun cambio en disco')
        return
    stamp=datetime.now().strftime('%Y%m%d_%H%M%S')
    backup=Path.home()/'equipego_backups'/f'dp12_4_{stamp}'
    for file in originals:
        dest=backup/file
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_text(originals[file],encoding='utf-8')
    for file,text in updates.items():
        (ROOT/file).write_text(text,encoding='utf-8')
        print('ACTUALIZADO:',file)
    print('BACKUP:',backup)
    print('DP12.4 APLICADO. Ejecutar CMake/CTest antes del commit.')

if __name__=='__main__':
    try: main()
    except (ValueError,FileNotFoundError) as exc:
        print('DP12.4 NO APLICADO:',exc,file=sys.stderr)
        sys.exit(1)
