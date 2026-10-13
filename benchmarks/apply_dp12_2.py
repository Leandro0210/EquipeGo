#!/usr/bin/env python3
"""DP12.2: unifica el origen temporal del reloj y el deadline.

Uso desde la raiz de EquipeGo:
  python3 benchmarks/apply_dp12_2.py --check
  python3 benchmarks/apply_dp12_2.py --apply

No elimina ni restaura archivos. Hace copia de seguridad en HOME antes de escribir.
El parche exige anclajes exactos y no modifica otros componentes del proyecto.
"""

from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
REL = Path('src/main.cpp')
MARKER = 'DP12.2: reloj y deadline comparten origen temporal'


def replace_one(text: str, old: str, new: str) -> str:
    n = text.count(old)
    if n != 1:
        raise ValueError(f'Anclaje inesperado: {n} coincidencias para {old!r}')
    return text.replace(old, new, 1)


def modify(code: str) -> str:
    if MARKER in code:
        if code.count(MARKER) != 1 or code.count('clock.start(config.simulation.timeScale);') != 1:
            raise ValueError('DP12.2 aparece aplicado parcialmente o duplicado')
        return code

    # 1. La construccion de componentes y la apertura de HighGUI pueden tardar.
    #    No debemos acumular tiempo simulado ANTES de que arranque la operacion.
    code = replace_one(
        code,
        '    SimulationClock clock;\n    clock.start(config.simulation.timeScale);\n    EventLogger logger(',
        '    SimulationClock clock;\n    EventLogger logger(',
    )

    # 2. El limite actual empieza despues del productor; esto desincroniza
    #    fecha final y milisegundos del reloj (y el GUI intensifica el problema).
    code = replace_one(
        code,
        '    const auto startTime = std::chrono::steady_clock::now();\n    const auto deadline =',
        '    const auto deadline =',
    )

    # 3. Comenzar justo antes del despachador, cuando la GUI y la flota YA
    #    estan listas, y reutilizar ese mismo instante para el deadline.
    code = replace_one(
        code,
        '''    fleet.startRescue();

    // =========================================
    // 3. DESPACHADOR + MENSAJE INICIAL
''',
        '''    fleet.startRescue();

    // DP12.2: reloj y deadline comparten origen temporal.
    // La GUI ya esta abierta y toda la flota se ha inicializado.
    // Los threads del despacho y del productor aun no han empezado;
    // esto evita carreras al iniciar el SimulationClock.
    const auto startTime = std::chrono::steady_clock::now();
    clock.start(config.simulation.timeScale);

    // =========================================
    // 3. DESPACHADOR + MENSAJE INICIAL
''',
    )

    if code.count('clock.start(config.simulation.timeScale);') != 1:
        raise ValueError('Inicializacion duplicada del reloj')
    if code.count('const auto startTime = std::chrono::steady_clock::now();') != 1:
        raise ValueError('Origen de deadline duplicado')
    return code


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--check', action='store_true')
    group.add_argument('--apply', action='store_true')
    args = parser.parse_args()

    filename = ROOT / REL
    if not filename.is_file() or not (ROOT / 'CMakeLists.txt').exists():
        print('ERROR: instala este script en benchmarks/ en la raiz de EquipeGo', file=sys.stderr)
        return 2
    try:
        original = filename.read_text(encoding='utf-8')
        updated = modify(original)
    except (ValueError, OSError) as ex:
        print(f'ERROR DP12.2: {ex}. No se cambio ningun archivo.', file=sys.stderr)
        return 1

    if updated == original:
        print('DP12.2: YA APLICADO. Ninguna modificacion.')
        return 0
    if args.check:
        print('DP12.2 CHECK OK | cambio seguro disponible; sin escritura')
        return 0

    destination = (Path.home() / 'equipego_backups' /
                   ('dp12_2_' + datetime.now().strftime('%Y%m%d_%H%M%S')) / REL)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(filename, destination)
    print(f'BACKUP INTACTO: {destination}')
    filename.write_text(updated, encoding='utf-8', newline='\n')
    print('DP12.2 APLICADO: src/main.cpp. No se tocaron otros archivos.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
