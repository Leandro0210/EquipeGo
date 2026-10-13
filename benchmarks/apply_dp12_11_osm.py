#!/usr/bin/env python3
"""Documenta el origen de la captura OSM y la prueba de 64 motos. No modifica C++."""
from __future__ import annotations
import argparse
from datetime import datetime
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
README = ROOT / 'README.md'
EVIDENCE = ROOT / 'evidencias/dp12/validacion_visual_64_motos_20261013.png'
MAP = ROOT / 'data/equipetrol.png'
DOC = ROOT / 'evidencias/dp12/procedencia_mapa_osm.md'

ORIGINAL = 'El fondo configurado es [`data/equipetrol.png`](data/equipetrol.png), cuyo origen y correspondencia geográfica deben comprobarse visualmente contra OpenStreetMap antes de entregar.'
REPLACEMENT = 'El fondo configurado es [`data/equipetrol.png`](data/equipetrol.png), elaborado mediante una **captura manual de la página oficial de OpenStreetMap**, recortada por el autor para mostrar la zona de Equipetrol (Santa Cruz de la Sierra, Bolivia). La procedencia está documentada en [`evidencias/dp12/procedencia_mapa_osm.md`](evidencias/dp12/procedencia_mapa_osm.md). Los límites declarados deben contrastarse con la imagen base; disponer de la captura no certifica por sí solo la precisión de esa calibración.'
OLD64 = '- [ ] Verificación visual humana del **escenario de 64 motos** bajo ejecución prolongada: el escenario adversario superó la prueba automatizada, pero la captura aportada representa ocho motos.'
NEW64 = '- [x] Verificación visual del **escenario de 64 motos** mediante [captura de 64 repartidores](evidencias/dp12/validacion_visual_64_motos_20261013.png), prueba de 600 s simulados con 120 pedidos creados, 59 entregados y 61 pendientes, sin rechazos; captura estática no demuestra por sí sola la fluidez cuadro a cuadro.'
OLDMAP = '- [ ] Conservar referencia verificable del **origen exacto y exportación** de `data/equipetrol.png` y comprobar sobre el mapa real la cobertura de los anillos y avenida San Martín. La captura visible no documenta toda esa procedencia.'
NEWMAP = '- [x] Documentada la **procedencia de la captura manual de OpenStreetMap** y el recorte realizado por el autor ([detalle](evidencias/dp12/procedencia_mapa_osm.md)); atribución OSM visible. [ ] Comprobar que el cuadro geográfico registrado corresponda con exactitud al recorte y que los anillos y la avenida San Martín resulten reconocibles; no consta enlace a la vista/fecha exactos de captura.'

DOC_CONTENT = '''# Procedencia de `data/equipetrol.png` (DP.1)

- **Fuente cartográfica:** [OpenStreetMap](https://www.openstreetmap.org/), página oficial.
- **Lugar representado:** Zona Equipetrol, Santa Cruz de la Sierra, Bolivia.
- **Procedimiento de obtención:** el autor abrió OpenStreetMap, realizó una captura de pantalla de la vista del mapa y la recortó manualmente para usarla como imagen de fondo `data/equipetrol.png`. No se trata de un mapa creado con otro proveedor.
- **Enlace exacto a la vista y fecha de captura:** no conservados; no se atribuye una fecha o URL de vista que no se haya podido verificar.
- **Límites geográficos declarados en `config/equipetrol.json`:** norte -17.75147, sur -17.77655, oeste -63.21443, este -63.18718. Son valores de configuración, **no constancia de una medición independiente** del recorte.
- **Atribución:** © OpenStreetMap contributors. Se muestra dentro de la ventana OpenCV y se incluye en `README.md` y en el JSON del escenario. [Licencia y atribución oficiales](https://www.openstreetmap.org/copyright). Los **datos cartográficos** de OpenStreetMap se distribuyen bajo Open Database License (ODbL).
- **Evidencia de uso en la simulación:** [`validacion_visual_20261013.png`](validacion_visual_20261013.png) (ocho motos) y [`validacion_visual_64_motos_20261013.png`](validacion_visual_64_motos_20261013.png) (64 motos).

La captura confirma visualmente que el fondo procede del estilo cartográfico de OSM, pero la precisión de la conversión entre coordenadas geográficas y píxeles del recorte requiere validación geométrica específica. Esta nota documenta fielmente el procedimiento descrito por el autor, sin inventar una descarga, exportación o URL geográfica exacta.
'''

def run(apply: bool) -> None:
    for p in (README, EVIDENCE, MAP):
        if not p.is_file():
            raise RuntimeError(f'Falta archivo exigido: {p}')
    s = README.read_text(encoding='utf-8')
    if ORIGINAL in s:
        s = s.replace(ORIGINAL, REPLACEMENT, 1)
    elif REPLACEMENT not in s:
        raise RuntimeError('No reconozco la frase sobre origen del mapa; detente y revisa README')
    if OLD64 in s:
        s = s.replace(OLD64, NEW64, 1)
    elif NEW64 not in s:
        raise RuntimeError('No reconozco el pendiente de 64 motos; detente y revisa README')
    if OLDMAP in s:
        s = s.replace(OLDMAP, NEWMAP, 1)
    elif NEWMAP not in s:
        raise RuntimeError('No reconozco el pendiente del mapa; detente y revisa README')
    if DOC.exists() and DOC.read_text(encoding='utf-8') != DOC_CONTENT:
        raise RuntimeError('Ya existe procedencia_mapa_osm.md con contenido distinto; no sobrescribiré')
    if apply:
        backup_dir = Path.home() / 'equipego_backups'
        backup_dir.mkdir(parents=True, exist_ok=True)
        backup = backup_dir / ('README_dp12_11_' + datetime.now().strftime('%Y%m%d_%H%M%S_%f') + '.md')
        shutil.copy2(README, backup)
        print('Backup README:', backup)
        README.write_text(s, encoding='utf-8')
        DOC.parent.mkdir(parents=True, exist_ok=True)
        DOC.write_text(DOC_CONTENT, encoding='utf-8')
    print(f'DP12.11 {"APLICADO" if apply else "CHECK OK"}: solo README y documentacion de procedencia OSM; codigo no modificado')
    print('Captura 64 motos:', EVIDENCE)

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--check', action='store_true')
    g.add_argument('--apply', action='store_true')
    args = ap.parse_args()
    try:
        run(args.apply)
    except (RuntimeError, OSError) as e:
        print('ERROR:', e, file=sys.stderr)
        raise SystemExit(1)
