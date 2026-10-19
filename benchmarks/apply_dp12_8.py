#!/usr/bin/env python3
"""DP12.8: interfaz oficial GUI y ejecutable auxiliar de ensayos; destinos y estados.

Ejecutar --check antes de --apply. Los cambios son atomicos a nivel de
prevalidacion de archivos, con backup antes de guardar. No se toca config/.
"""
from __future__ import annotations
import argparse
from datetime import datetime
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
BENCH_FLAG = 'EQUIPEGO_BENCHMARK_CLI'


def replace_exact(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: esperaba 1 anclaje, encontre {count}')
    return text.replace(old, new, 1)


def main_patch(source: str) -> str:
    source = replace_exact(source,
        'enum class DisplayMode { Auto, Gui, Headless };\n\nstruct RunOptions {\n'
        '  DisplayMode display = DisplayMode::Auto;\n  bool hasScale = false;\n'
        '  double scale = 0.0;\n  bool hasDuration = false;\n  int duration = 0;\n',
        f'#ifdef {BENCH_FLAG}\n'
        'enum class DisplayMode { Auto, Gui, Headless };\n'
        '#endif\n\nstruct RunOptions {\n'
        f'#ifdef {BENCH_FLAG}\n'
        '  DisplayMode display = DisplayMode::Auto;\n  bool hasScale = false;\n'
        '  double scale = 0.0;\n  bool hasDuration = false;\n  int duration = 0;\n'
        '#endif\n', 'opciones',)
    source = replace_exact(source,
        '    std::cerr << "Uso: delivery_sim config.json [--gui|--headless] "\n'
        '                 "[--scale N] [--duration SEGUNDOS] [--log RUTA]\\n";\n',
        f'#ifdef {BENCH_FLAG}\n'
        '    std::cerr << "Uso: delivery_sim_bench config.json [--gui|--headless] "\n'
        '                 "[--scale N] [--duration SEGUNDOS] [--log RUTA]\\n";\n'
        '#else\n'
        '    std::cerr << "Uso: delivery_sim config.json [--log RUTA]\\n";\n'
        '#endif\n', 'uso')
    source = replace_exact(source,
        '    if (argument == "--gui") {\n'
        '      options.display = DisplayMode::Gui;\n'
        '    } else if (argument == "--headless") {\n'
        '      options.display = DisplayMode::Headless;\n'
        '    } else if (argument == "--log" && i + 1 < argc) {',
        f'#ifdef {BENCH_FLAG}\n'
        '    if (argument == "--gui") {\n'
        '      options.display = DisplayMode::Gui;\n'
        '    } else if (argument == "--headless") {\n'
        '      options.display = DisplayMode::Headless;\n'
        '    } else\n'
        '#endif\n'
        '    if (argument == "--log" && i + 1 < argc) {', 'banderas de visor')
    source = replace_exact(source,
        '      options.hasLogPath = true;\n'
        '    } else if (argument == "--scale" && i + 1 < argc) {',
        '      options.hasLogPath = true;\n'
        f'#ifdef {BENCH_FLAG}\n'
        '    } else if (argument == "--scale" && i + 1 < argc) {', 'banderas de escala')
    source = replace_exact(source,
        '        std::cerr << "Duracion invalida (0 a 86400 segundos simulados)\\n";\n'
        '        return false;\n'
        '      }\n'
        '    } else {\n'
        '      std::cerr << "Opcion desconocida: " << argument << \'\\n\';',
        '        std::cerr << "Duracion invalida (0 a 86400 segundos simulados)\\n";\n'
        '        return false;\n'
        '      }\n'
        '#endif\n'
        '    } else {\n'
        '      std::cerr << "Opcion desconocida: " << argument << \'\\n\';', 'cierre parser')
    source = replace_exact(source,
        '  // El arranque normal abre la ventana incluso si falta DISPLAY:\n'
        '  // en ese caso SimulationDashboard::open() informa un error legible.\n'
        '  // --headless se conserva SOLO como interfaz de test mientras se migra\n'
        '  // el banco de pruebas (pendiente de conformidad estricta con seccion 5).\n'
        '  const bool showGui = options.display != DisplayMode::Headless;\n\n'
        '  // Si no se indica --scale, respetamos SIEMPRE el JSON del usuario.\n'
        '  if (options.hasScale) {\n'
        '    config.simulation.timeScale = options.scale;\n'
        '  }\n'
        '  if (options.hasDuration) {\n'
        '    config.simulation.durationS = options.duration;\n'
        '  }\n',
        '  // DP12.8: la interfaz publica SIEMPRE abre una ventana.\n'
        '  // Las opciones de diagnostico solo se compilan en delivery_sim_bench.\n'
        f'#ifdef {BENCH_FLAG}\n'
        '  const bool showGui = options.display != DisplayMode::Headless;\n'
        '  if (options.hasScale) config.simulation.timeScale = options.scale;\n'
        '  if (options.hasDuration) config.simulation.durationS = options.duration;\n'
        '#else\n'
        '  const bool showGui = true;\n'
        '#endif\n', 'arranque')
    return source


def cmake_patch(source: str) -> str:
    return replace_exact(source,
        'add_executable(delivery_sim ${SOURCES})\n'
        'configure_equipego_target(delivery_sim)\n',
        'add_executable(delivery_sim ${SOURCES})\n'
        'configure_equipego_target(delivery_sim)\n\n'
        '# Binario auxiliar OPCIONAL: no se compila en el build oficial normal.\n'
        '# Solo para reproducir ensayos, se activa con -DEQUIPEGO_BUILD_BENCHMARK=ON.\n'
        'option(EQUIPEGO_BUILD_BENCHMARK "Build extra diagnostic executable" OFF)\n'
        'if(EQUIPEGO_BUILD_BENCHMARK)\n'
        '    add_executable(delivery_sim_bench ${SOURCES})\n'
        '    configure_equipego_target(delivery_sim_bench)\n'
        f'    target_compile_definitions(delivery_sim_bench PRIVATE {BENCH_FLAG}=1)\n'
        'endif()\n',
        'nuevo target')


def dashboard_patch(source: str) -> str:
    source = replace_exact(source,
        'cv::Scalar colorFor(std::size_t i) {\n'
        '  return kColors[i % kColors.size()];\n}\n',
        'cv::Scalar colorFor(std::size_t i) {\n'
        '  return kColors[i % kColors.size()];\n}\n\n'
        '// BGR. El color interior refleja el estado, no la identidad.\n'
        'cv::Scalar stateColor(const CourierSnapshot &s) {\n'
        '  if (s.state == CourierState::Broken) return {40, 40, 215};\n'
        '  if (s.active && s.active->state == OrderState::PickedUp)\n'
        '    return {48, 165, 35};\n'
        '  if (s.active) return {35, 150, 245};\n'
        '  if (s.motion) return {210, 135, 60};\n'
        '  return {122, 122, 122};\n}\n', 'colores por estado')
    source = replace_exact(source,
        '  // Centro REAL: desplazar un punto para separar motos lo sacaria\n',
        '  // Cada pedido ACTIVO muestra su destino de entrega sobre el mapa.\n'
        '  // La marca esta fija en un nodo real, incluso antes del pickup.\n'
        '  for (std::size_t i = 0; i < samples.size(); ++i) {\n'
        '    const auto &s = samples[i].courier;\n'
        '    if (!s.active) continue;\n'
        '    const cv::Point2d p = nodePoint(s.active->deliveryNodeId);\n'
        '    const cv::Point target(cvRound(p.x), cvRound(p.y));\n'
        '    cv::drawMarker(frame, target, cv::Scalar(20, 20, 245),\n'
        '                   cv::MARKER_DIAMOND, i == selected ? 22 : 14,\n'
        '                   i == selected ? 3 : 2, cv::LINE_AA);\n'
        '    if (i == selected) {\n'
        '      drawText(frame, "DEST " + s.active->orderId,\n'
        '               target.x + 9, target.y - 11, 0.34,\n'
        '               cv::Scalar(15, 15, 210));\n'
        '    }\n'
        '  }\n\n'
        '  // Centro REAL: desplazar un punto para separar motos lo sacaria\n', 'marcadores de entrega')
    source = replace_exact(source,
        '    cv::circle(frame, center, i == selected ? 10 : 7,\n'
        '               i == selected ? cv::Scalar(0, 210, 255)\n'
        '                             : cv::Scalar(255, 255, 255),\n'
        '               cv::FILLED, cv::LINE_AA);\n'
        '    cv::circle(frame, center, i == selected ? 7 : 5,\n'
        '               colorFor(i), cv::FILLED, cv::LINE_AA);\n',
        '    cv::circle(frame, center, i == selected ? 11 : 8,\n'
        '               i == selected ? cv::Scalar(0, 210, 255)\n'
        '                             : colorFor(i),\n'
        '               cv::FILLED, cv::LINE_AA);\n'
        '    cv::circle(frame, center, i == selected ? 7 : 5,\n'
        '               stateColor(samples[i].courier), cv::FILLED, cv::LINE_AA);\n', 'motos estado')
    source = replace_exact(source,
        '    drawText(frame, v.phase, x + 22, y + 33, 0.33);\n'
        '  }\n\n'
        '  // DP12.1: licencia OpenStreetMap: atribucion VISIBLE en cada cuadro.',
        '    drawText(frame, v.phase, x + 22, y + 33, 0.33);\n'
        '  }\n\n'
        '  drawText(frame, "G=libre N=recoge V=entrega R=averia D=destino",\n'
        '           x, 143, 0.32);\n\n'
        '  // DP12.1: licencia OpenStreetMap: atribucion VISIBLE en cada cuadro.',
        'leyenda compacta')
    source = replace_exact(source,
        '    cv::circle(frame, cv::Point(x + 8, y - 4), 7,\n'
        '               colorFor(i), cv::FILLED, cv::LINE_AA);\n',
        '    cv::circle(frame, cv::Point(x + 8, y - 4), 7,\n'
        '               stateColor(s), cv::FILLED, cv::LINE_AA);\n', 'panel estado')
    return source


def readme_patch(source: str) -> str:
    start = '> **Estado tras DP.12.2 (rama `avance-12`):**'
    line = next((x for x in source.splitlines() if x.startswith(start)), None)
    if not line: raise RuntimeError('README: estado DP12 no encontrado')
    source = replace_exact(source, line,
        '> **DP.12.8 (interfaz oficial):** `delivery_sim` acepta únicamente `config.json` y '
        '`--log RUTA`; siempre abre la ventana OpenCV. Para ensayos instrumentados se '
        'compila por separado `delivery_sim_bench`, que admite `--headless`, '
        '`--gui`, `--scale` y `--duration` sin alterar la interfaz evaluada. '
        'Los registros anteriores corresponden al commit `6ad35d1`; volver a '
        'validar la compilación final luego del cambio.', 'README estado')
    section = '## 9. Lista de verificación antes de entregar (obligatoria)'
    insert = ('## 8.1 Interfaz pública y modo de pruebas (DP.12.8)\n\n'
        'Desde el directorio de compilación, **la interfaz oficial** es:\n\n'
        '```bash\n./delivery_sim ../config/equipetrol.json\n'
        './delivery_sim ../config/equipetrol.json --log run1.log\n```\n\n'
        'Este ejecutable abre OpenCV obligatoriamente y rechaza `--headless`, '
        '`--gui`, `--scale` y `--duration`. Si no hay pantalla, informa un '
        'error en lugar de ejecutar oculto.\n\n'
        '**Solo para diagnósticos**, activar el segundo target con '
        '`cmake -S . -B build-bench -DEQUIPEGO_BUILD_BENCHMARK=ON` '
        'y compilar. Por defecto solo se compila la aplicación oficial. '
        'El auxiliar `delivery_sim_bench` se ejecuta así:\n\n'
        '```bash\n./delivery_sim_bench ../config/equipetrol.json '
        '--headless --scale 60 --duration 300 --log ensayo.jsonl\n```\n\n'
        'Los informes existentes conservan sus comandos originales y SHA de '
        'ejecutables. Para reproducir un escenario con banderas de diagnóstico '
        'después de DP.12.8, apuntar `--binary` o el script a `delivery_sim_bench`.\n\n'
        'En OpenCV, las motos muestran color interior según estado (gris libre, '
        'naranja hacia restaurante, verde entrega, rojo avería); rombos rojos '
        'marcan destinos de los pedidos activos. Se conserva la atribución OSM.\n\n')
    source = replace_exact(source, section, insert+section, 'README nueva sección')
    replacements = {
        '- [ ] Resolver el modo opcional `--headless`, prohibido en sentido literal por la rúbrica, sin perder la reproducibilidad de los experimentos; validar atribución visual.':
        '- [x] DP.12.8: separar `delivery_sim` (solo interfaz oficial) y `delivery_sim_bench` (diagnóstico). [ ] Validar atribución visual con captura.',
        '- [ ] Verificar con configuraciones externas y varias ejecuciones: 64 motos, cientos de nodos, `pickupSlots=1`, `bagCapacity=1`, ráfagas grandes, `breakdownProbability=1`, rutas inaccesibles e invalidaciones del JSON.':
        '- [x] DP.12.5: 22/22 pruebas adversarias (12 inválidos, 7 válidos, 3 señales), evidencias en `evidencias/dp12/adversarial/`.',
        '- [ ] Validar que la GUI represente el trayecto realmente recorrido, no solo una predicción independiente; comprobar fluidez y el mapa para 64 motos.':
        '- [x] DP.12.4: movimiento basado en snapshots reales y test de segmento/avería. [ ] Inspección humana de la ventana con 64 motos.',
        '- [ ] Medir CPU con casi cero pedidos y repartidores inactivos; separar costo GUI del costo concurrente.':
        '- [x] DP.12.3: CPU ociosa estable medida con 8 y 64 motos, separando el costo de OpenCV (`evidencias/dp12/cpu_ociosa/`).',
        '- [ ] Probar compilación limpia de la sección 8 y versión final instrumentada, comparándolas con los ensayos de DP.11.':
        '- [x] DP.12.6-7: clon limpio `6ad35d1`, CTest 10/10 + TSAN 10/10 y 12/12 instrumentadas (6 TSAN, 6 ASAN/UBSAN). [ ] Revalidar si cambia el código oficial.'
    }
    for k,v in replacements.items():
        source=replace_exact(source,k,v,'README checklist')
    return source


def collect_changes() -> dict[Path, str]:
    patches = {'src/main.cpp':main_patch,'CMakeLists.txt':cmake_patch,
               'src/visualization/SimulationDashboard.cpp':dashboard_patch,
               'README.md':readme_patch}
    changes={}
    for rel,fn in patches.items():
        p=ROOT/rel
        if not p.is_file(): raise FileNotFoundError(rel)
        original=p.read_text(encoding='utf-8')
        if BENCH_FLAG in original and rel=='src/main.cpp':
            raise RuntimeError('DP12.8 ya parece aplicado; no duplicar')
        changes[p]=fn(original)
    benchmark_dir=ROOT/'benchmarks'
    for p in sorted(benchmark_dir.glob('*.py')):
        if p.name==Path(__file__).name: continue
        content=p.read_text(encoding='utf-8')
        updated=content.replace('build-equipego/delivery_sim', 'build-equipego/delivery_sim_bench')
        if updated!=content: changes[p]=updated
    return changes


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    g=parser.add_mutually_exclusive_group(required=True)
    g.add_argument('--check',action='store_true')
    g.add_argument('--apply',action='store_true')
    args=parser.parse_args()
    try:
        changes=collect_changes()
        print(f'DP12.8 CHECK OK | {len(changes)} archivos listos; no se tocaron config/ ni temporales')
        for p in changes: print('  ',p.relative_to(ROOT))
        if args.check: return 0
        backup=Path.home()/'equipego_backups'/('dp12_8_'+datetime.now().strftime('%Y%m%d_%H%M%S'))
        for p in changes:
            relative=p.relative_to(ROOT)
            destination=backup/relative
            destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(p,destination)
        for p,new in changes.items():
            p.write_text(new,encoding='utf-8',newline='\n')
        print('DP12.8 APLICADO | backup intacto:',backup)
        return 0
    except (RuntimeError, OSError) as exc:
        print('DP12.8 ERROR:',exc,file=sys.stderr)
        return 1

if __name__=='__main__': raise SystemExit(main())
