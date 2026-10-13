#!/usr/bin/env python3
"""DP.12.10 — actualización documental verificable de EquipeGo.

Ejecutar desde la raíz del repositorio:
 python3 benchmarks/apply_dp12_10_docs.py --check
 python3 benchmarks/apply_dp12_10_docs.py --apply

Solo modifica README.md. No altera fuentes, configuraciones ni evidencias previas.
"""
from __future__ import annotations
import argparse
from datetime import datetime
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
README = ROOT / 'README.md'


def replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise RuntimeError(f'Se esperaba un anclaje único (apareció {text.count(old)} veces): {old[:90]!r}')
    return text.replace(old, new, 1)


def between_once(text: str, start: str, end: str, new: str) -> str:
    if text.count(start) != 1 or text.count(end) != 1:
        raise RuntimeError(f'Falta delimitador único: {start!r} / {end!r}')
    a = text.index(start)
    b = text.index(end, a + len(start))
    return text[:a] + new.rstrip() + '\n\n' + text[b:]


def update(text: str) -> str:
    if '## 8.2 Auditoría técnica definitiva (DP.12.9)' in text:
        raise RuntimeError('El README parece actualizado ya; se evita aplicar dos veces')

    text = replace_once(text,
        '> **DP.12.8 (interfaz oficial):** `delivery_sim` acepta únicamente `config.json` y `--log RUTA`; siempre abre la ventana OpenCV. Para ensayos instrumentados se compila por separado `delivery_sim_bench`, que admite `--headless`, `--gui`, `--scale` y `--duration` sin alterar la interfaz evaluada. Los registros anteriores corresponden al commit `6ad35d1`; volver a validar la compilación final luego del cambio.',
        '> **Versión verificada de entrega:** código `0b51082` (rama `avance-12`); evidencias finales publicadas en `6dd487b`, sin cambios de código entre ambos commits. Se comprobó compilación desde clon limpio, 30/30 pruebas CTest y 12/12 ejecuciones con sanitizadores. `delivery_sim` acepta únicamente `config.json` y `--log RUTA`, y siempre abre OpenCV; `delivery_sim_bench` es un target auxiliar opcional para los experimentos.'
    )
    text = replace_once(text,
        '**Versiones registradas en el entorno de pruebas:** GCC 15.2.0 y OpenCV 4.10.0. Las versiones exactas de CMake, sistema operativo y bibliotecas de sanitización se encuentran en [`evidencias/dp11/`](evidencias/dp11/informe_final.md), dentro de cada `versiones_herramientas.json`.',
        '**Versiones registradas en el entorno de pruebas:** GCC 15.2.0 y OpenCV 4.10.0. La versión exacta de cada herramienta, los hashes de los binarios y los comandos de las 12 corridas instrumentadas finales constan en [`evidencias/dp12/auditoria_0b51082/`](evidencias/dp12/auditoria_0b51082/) (`tsan/versiones_herramientas.json`, `asan/versiones_herramientas.json` y `metadata.json`). El informe histórico también está en [`evidencias/dp11/`](evidencias/dp11/informe_final.md).'
    )
    text = replace_once(text,
        '**Atribución requerida por la licencia ODbL:** © OpenStreetMap contributors. El valor está en `config/equipetrol.json` y DP.12.1 añadió su representación en el panel de la ventana desde `src/visualization/SimulationDashboard.cpp::drawFrame`; **falta confirmar su legibilidad mediante una captura visual**. La pantalla del simulador, y no solo el archivo JSON, debe mostrar la atribución.',
        '**Atribución requerida por ODbL:** © OpenStreetMap contributors. El valor aparece en `config/equipetrol.json` y está visible en el panel OpenCV (`src/visualization/SimulationDashboard.cpp::drawFrame`), según la [captura de validación visual](evidencias/dp12/validacion_visual_20261013.png). La captura acredita legibilidad de la atribución y los marcadores, pero no demuestra por sí sola el origen técnico ni la correspondencia geográfica exacta de la imagen base; esa procedencia debe conservarse documentada.'
    )
    text = replace_once(text,
        '**Informe detallado:** [`evidencias/dp11/informe_final.md`](evidencias/dp11/informe_final.md). Se realizaron **12 ejecuciones instrumentadas**:',
        '**Campaña histórica (anterior a DP.12.8):** [`evidencias/dp11/informe_final.md`](evidencias/dp11/informe_final.md). Se realizaron **12 ejecuciones instrumentadas**:'
    )
    text = replace_once(text,
        'Las 12 ejecuciones acumularon **5.430 pedidos creados y 96 averías**;',
        'En la **campaña histórica**, esas 12 ejecuciones acumularon **5.430 pedidos creados y 96 averías**;'
    )
    text = replace_once(text,
        '**Comandos usados**, además de las versiones y variables de entorno consignadas en los registros originales:',
        '**Comandos usados en la campaña histórica**, además de las versiones y variables de entorno consignadas en los registros originales (para la versión final, usar `delivery_sim_bench`, como se describe en la sección 8.2):'
    )

    insertion = """## 8.2 Auditoría técnica definitiva (DP.12.9)

**Referencia inmutable del código verificado:** commit `0b51082` de `avance-12`. El commit posterior `6dd487b` incorporó exclusivamente los archivos de evidencia de esa auditoría. Las pruebas anteriores en `evidencias/dp11/` siguen siendo válidas como antecedentes, pero los resultados de la versión de entrega se encuentran en [`evidencias/dp12/auditoria_0b51082/`](evidencias/dp12/auditoria_0b51082/).

| Validación de la versión final | Resultado | Evidencia |
|---|---:|---|
| Clon limpio + compilación oficial Release | OK; `delivery_sim_bench` ausente en el build oficial | `evidencias/dp12/auditoria_0b51082/ctest/release.txt` (CTest) y registro de compilación de la auditoría |
| CTest Release | 10/10 | `evidencias/dp12/auditoria_0b51082/ctest/release.txt` |
| CTest ThreadSanitizer | 10/10 | `evidencias/dp12/auditoria_0b51082/ctest/tsan.txt` |
| CTest AddressSanitizer + UndefinedBehaviorSanitizer | 10/10 | `evidencias/dp12/auditoria_0b51082/ctest/asan.txt` |
| Seis ejecuciones TSAN (SIGINT/SIGTERM) | 6/6 | `evidencias/dp12/auditoria_0b51082/tsan/` |
| Seis ejecuciones ASAN/UBSAN (SIGINT/SIGTERM) | 6/6 | `evidencias/dp12/auditoria_0b51082/asan/` |
| Pruebas adversarias (invalidaciones, carga, señales) | 22/22 | `evidencias/dp12/adversarial/` |
| Validación visual, ocho motos y destinos | Captura inspeccionada | [`evidencias/dp12/validacion_visual_20261013.png`](evidencias/dp12/validacion_visual_20261013.png) |

En las **12 ejecuciones instrumentadas definitivas** se registraron **5.878 pedidos creados**, **96 averías**, ráfagas máximas de 12 pedidos y un tiempo de cierre máximo observado de **0,265 segundos**. Las 12 salieron con estado 0; la herramienta de auditoría no identificó hallazgos de TSAN, ASAN/LSAN ni UBSAN en esas corridas. Los directorios `tsan/` y `asan/` conservan versiones, configuración, `metadata.json`, eventos, `stdout.txt`, `stderr.txt` y el informe. *La ausencia de reportes en las corridas no prueba la ausencia de todos los posibles defectos.*

Para repetir el análisis dinámico final tras compilar con instrumentos, activar expresamente `-DEQUIPEGO_BUILD_BENCHMARK=ON` y utilizar el binario auxiliar:

```bash
python3 benchmarks/dp11_dynamic_analysis.py \\
  --sanitizer tsan --binary /ruta/build-tsan/delivery_sim_bench --repeats 3
python3 benchmarks/dp11_dynamic_analysis.py \\
  --sanitizer asan --binary /ruta/build-asan/delivery_sim_bench --repeats 3
```

El ejecutable público de entrega (`delivery_sim`) permanece separado: no admite `--headless`, `--gui`, `--scale` ni `--duration`. Su apertura gráfica y el contrato de eventos se comprobaron con `tests/test_dp12_8_contract.py --run`; la captura incluida muestra el mapa, ocho motos, estados, destinos activos y la atribución visible de OpenStreetMap.

"""
    text = replace_once(text, '## 9. Lista de verificación antes de entregar (obligatoria)', insertion + '## 9. Lista de verificación antes de entregar (obligatoria)')

    checklist = """## 9. Lista de verificación antes de entregar (obligatoria)

- [x] Clon limpio del commit `0b51082`: compilación Release y CTest 10/10; compilación ThreadSanitizer y CTest 10/10; compilación ASAN/UBSAN y CTest 10/10.
- [x] 12/12 ejecuciones instrumentadas sobre `0b51082`, con registros crudos sin editar (`evidencias/dp12/auditoria_0b51082/`).
- [x] 22/22 pruebas adversarias (12 configuraciones inválidas, siete casos de carga, tres apagados por señales), bajo la versión previa con la misma lógica de simulación (`evidencias/dp12/adversarial/`).
- [x] Interfaz oficial que abre siempre OpenCV, y ejecutable `delivery_sim_bench` habilitado solo cuando se solicita explícitamente; pruebas de contrato correctas.
- [x] Inspección visual de ocho motos, estados por color, destinos activos y atribución de OpenStreetMap; [captura adjunta](evidencias/dp12/validacion_visual_20261013.png). Una captura estática no certifica por sí sola la fluidez de toda la animación.
- [x] Progreso real de las motos en segmentos del grafo y congelación ante averías validados con `test_dp12_motion` y CTest.
- [x] CPU ociosa estable medida para 8 y 64 motos; diferencia entre costo del visor y costo de hilos de repartidores documentada en `evidencias/dp12/cpu_ociosa/`.
- [ ] Verificación visual humana del **escenario de 64 motos** bajo ejecución prolongada: el escenario adversario superó la prueba automatizada, pero la captura aportada representa ocho motos.
- [ ] Conservar referencia verificable del **origen exacto y exportación** de `data/equipetrol.png` y comprobar sobre el mapa real la cobertura de los anillos y avenida San Martín. La captura visible no documenta toda esa procedencia.
- [ ] Confirmar con el docente, si fuese necesario, si la política de planificación explícita de tareas del pool `QuoteExecutor` satisface la fila sobre futuros y lanzamiento. La rúbrica menciona una política de lanzamiento declarada, no exige literalmente `std::launch::async`.

**Estado:** las evidencias de compilación, contrato, estrés, pruebas dinámicas y GUI principal están registradas. Los tres puntos sin marcar son revisiones externas o visuales; no se declaran aprobados automáticamente.
"""
    text = between_once(text, '## 9. Lista de verificación antes de entregar (obligatoria)', '\n---\n\n**Mapa/datos:**', checklist)
    if '5.878' not in text or '0b51082' not in text:
        raise RuntimeError('Faltan métricas clave después de actualizar')
    return text


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    g=p.add_mutually_exclusive_group(required=True)
    g.add_argument('--check', action='store_true')
    g.add_argument('--apply', action='store_true')
    args = p.parse_args()
    original = README.read_text(encoding='utf-8')
    modified = update(original)
    img=ROOT/'evidencias/dp12/validacion_visual_20261013.png'
    if not img.is_file():
        raise RuntimeError('Falta la captura de validación incluida en el ZIP: '+str(img))
    if args.check:
        print('DP12.10 CHECK OK | README listo para actualizar; no se modificó ningún archivo')
        print('Captura OSM:', img)
        return 0
    backup = README.with_name('README.dp12_10.backup.' + datetime.now().strftime('%Y%m%d_%H%M%S') + '.md')
    shutil.copy2(README, backup)
    README.write_text(modified, encoding='utf-8', newline='\n')
    print('DP12.10 APLICADO | README actualizado; copia de seguridad:', backup)
    print('Sin cambios en src/, config/, CMakeLists.txt ni evidencias previas')
    return 0

if __name__=='__main__':
    try: raise SystemExit(main())
    except (OSError, RuntimeError) as ex:
        print('DP12.10 FALLO:', ex, file=sys.stderr)
        raise SystemExit(1)
