#!/usr/bin/env python3
"""DP.12.1 - Adaptador comprobado para los dos archivos completos del repo.

No borra archivos. Crea una copia exacta de seguridad en el HOME antes de
escribir. --check no modifica nada; --apply solo escribe si TODOS los anclajes
coinciden. Se usa este adaptador porque las versiones completas de C++ viven
en la rama local del usuario, y conservamos su contenido íntegro.
"""
from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path
import shutil
import sys


ROOT = Path(__file__).resolve().parents[1]
TARGETS = (
    Path('src/main.cpp'),
    Path('src/visualization/SimulationDashboard.cpp'),
)


def replace_once(source: str, old: str, new: str, path: Path) -> str:
    times = source.count(old)
    if times != 1:
        raise ValueError(f'{path}: se esperaba 1 anclaje y hay {times}. No se modifico nada.')
    return source.replace(old, new, 1)


def update_main(text: str, path: Path) -> str:
    old_gui = '''bool graphicsEnvironmentPresent() {
#if defined(__linux__)
  const char *x = std::getenv("DISPLAY");
  const char *wayland = std::getenv("WAYLAND_DISPLAY");
  return (x && *x) || (wayland && *wayland);
#else
  return true;
#endif
}

'''
    text = replace_once(text, old_gui, '', path)
    old = '''  const bool showGui =
      options.display == DisplayMode::Gui ||
      (options.display == DisplayMode::Auto && graphicsEnvironmentPresent());

  if (options.hasScale) {
    config.simulation.timeScale = options.scale;
  } else if (showGui) {
    // La simulacion visual no debe correr a 60x por defecto.
    config.simulation.timeScale = 3.0;
  }
'''
    new = '''  // El arranque normal abre la ventana incluso si falta DISPLAY:
  // en ese caso SimulationDashboard::open() informa un error legible.
  // --headless se conserva SOLO como interfaz de test mientras se migra
  // el banco de pruebas (pendiente de conformidad estricta con seccion 5).
  const bool showGui = options.display != DisplayMode::Headless;

  // Si no se indica --scale, respetamos SIEMPRE el JSON del usuario.
  if (options.hasScale) {
    config.simulation.timeScale = options.scale;
  }
'''
    text = replace_once(text, old, new, path)
    return text


def update_dashboard(text: str, path: Path) -> str:
    old = '''    drawText(frame, v.phase, x + 22, y + 33, 0.33);
  }

  return frame;
}'''
    new = '''    drawText(frame, v.phase, x + 22, y + 33, 0.33);
  }

  // DP12.1: licencia OpenStreetMap: atribucion VISIBLE en cada cuadro.
  // cv::putText utiliza Hershey, que no interpreta el simbolo UTF-8 (c).
  // Si la configuracion contiene el signo de copyright, dibujamos
  // explicitamente su glifo (C encerrada en un circulo), seguido del
  // resto del texto original. No dependemos del nombre del proveedor.
  std::string attributionText = config.map.attribution;
  const std::string copyrightUtf8 = "\\xC2\\xA9";
  const auto copyrightPosition = attributionText.find(copyrightUtf8);
  const bool containsCopyright = copyrightPosition != std::string::npos;
  if (containsCopyright) {
    attributionText.erase(copyrightPosition, copyrightUtf8.size());
    while (!attributionText.empty() && attributionText.front() == ' ') {
      attributionText.erase(attributionText.begin());
    }
  }

  if (frame.rows >= 32 && frame.cols >= base.cols + 64) {
    const int panelLeft = base.cols + 6;
    const int panelTop = frame.rows - 31;
    cv::rectangle(frame,
                  cv::Rect(panelLeft, panelTop, kPanelWidth - 12, 27),
                  cv::Scalar(239, 239, 239), cv::FILLED);
    const int baseline = frame.rows - 12;
    int textLeft = panelLeft + 10;
    if (containsCopyright) {
      const cv::Point center(textLeft + 7, baseline - 5);
      cv::circle(frame, center, 8, cv::Scalar(52, 52, 52), 1,
                 cv::LINE_AA);
      cv::putText(frame, "C", cv::Point(textLeft + 3, baseline - 1),
                  cv::FONT_HERSHEY_SIMPLEX, 0.34, cv::Scalar(52, 52, 52),
                  1, cv::LINE_AA);
      textLeft += 22;
    }
    cv::putText(frame, attributionText, cv::Point(textLeft, baseline),
                cv::FONT_HERSHEY_SIMPLEX, 0.33,
                cv::Scalar(52, 52, 52), 1, cv::LINE_AA);
  }

  return frame;
}'''
    return replace_once(text, old, new, path)


def already_applied(path: Path, original: str) -> bool:
    if path.name == 'main.cpp':
        return ('const bool showGui = options.display != DisplayMode::Headless;' in original
                and 'graphicsEnvironmentPresent()' not in original)
    return 'DP12.1: licencia OpenStreetMap' in original


def main() -> int:
    cli = argparse.ArgumentParser(description=__doc__)
    group = cli.add_mutually_exclusive_group(required=True)
    group.add_argument('--check', action='store_true')
    group.add_argument('--apply', action='store_true')
    args = cli.parse_args()

    if not (ROOT / 'CMakeLists.txt').is_file():
        print('ERROR: coloca este archivo en benchmarks/ dentro de EquipeGo.', file=sys.stderr)
        return 2

    modifications: dict[Path, str] = {}
    try:
        for rel in TARGETS:
            target = ROOT / rel
            original = target.read_text(encoding='utf-8')
            if already_applied(rel, original):
                print(f'YA APLICADO: {rel}')
                continue
            updated = (update_main(original, rel) if rel.name == 'main.cpp'
                       else update_dashboard(original, rel))
            modifications[rel] = updated
            print(f'LISTO PARA MODIFICAR: {rel}')
    except (ValueError, OSError, UnicodeError) as error:
        print(f'ERROR DP12.1: {error}', file=sys.stderr)
        return 1

    if args.check:
        print(f'DP12.1 CHECK OK | pendientes={len(modifications)}; sin cambios en disco')
        return 0
    if not modifications:
        print('DP12.1: sin cambios pendientes.')
        return 0

    root_backup = Path.home() / 'equipego_backups' / ('dp12_1_' + datetime.now().strftime('%Y%m%d_%H%M%S'))
    for rel in modifications:
        origin = ROOT / rel
        destination = root_backup / rel
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(origin, destination)
    print(f'BACKUP INTACTO: {root_backup}')
    for rel, updated in modifications.items():
        target = ROOT / rel
        target.write_text(updated, encoding='utf-8', newline='\n')
        print(f'ACTUALIZADO: {rel}')
    print('DP12.1 APLICADO. Compila y ejecuta pruebas antes de hacer commit.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
