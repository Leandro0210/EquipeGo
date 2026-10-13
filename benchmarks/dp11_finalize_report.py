#!/usr/bin/env python3
"""Consolidar DP.11 a partir de 12 ejecuciones reales, sin alterar sus registros.

Ejecutar en la raiz de EquipeGo:
    python3 benchmarks/dp11_finalize_report.py

Solo biblioteca estandar Python. Rechaza datos ausentes o inconsistentes.
"""
from __future__ import annotations

import argparse
import collections
import csv
import json
from pathlib import Path
import re
import statistics
import sys


GROUPS = (
    ("tsan_inicial", "TSAN", 1),
    ("tsan_repeticion", "TSAN", 2),
    ("asan_inicial", "ASAN + LSAN + UBSAN", 1),
    ("asan_repeticion", "ASAN + LSAN + UBSAN", 2),
)
ERROR_PATTERN = re.compile(
    r"ERROR: (?:AddressSanitizer|LeakSanitizer|ThreadSanitizer)"
    r"|WARNING: ThreadSanitizer|SUMMARY: .*Sanitizer"
    r"|AddressSanitizer:DEADLYSIGNAL|runtime error:|FATAL: ThreadSanitizer",
    re.I,
)
CSV_FIELDS = (
    "grupo", "herramienta", "caso", "estado", "creados", "asignados",
    "entregados", "rechazados", "pendientes", "averias", "rafaga_max",
    "cierre_s", "codigo_salida", "stderr_bytes", "evidencia",
)


def read_json(path: Path) -> dict:
    obj = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(obj, dict):
        raise ValueError(f"Se esperaba un objeto JSON: {path}")
    return obj


def check(condition: bool, reason: str) -> None:
    if not condition:
        raise ValueError(reason)


def find_version(manifest: dict, exact_cmd: str) -> str:
    for record in manifest.get("version_commands", []):
        command = record.get("command", [])
        if command and command[0] == exact_cmd:
            output = record.get("output", "").strip()
            if record.get("exit_code") == 0 and output:
                return output.splitlines()[0]
    return "No recuperada; revisar versiones_herramientas.json"


def inspect_run(group: Path, label: str, name: str, sanitizer: str) -> dict:
    folder = group / name
    for filename in ("metadata.json", "config.json", "events.jsonl", "stdout.txt", "stderr.txt"):
        check((folder / filename).is_file(), f"Falta {folder / filename}")
    metadata = read_json(folder / "metadata.json")
    config = read_json(folder / "config.json")
    lines = (folder / "events.jsonl").read_text(encoding="utf-8").splitlines()
    events = [json.loads(line) for line in lines if line.strip()]
    out = (folder / "stdout.txt").read_text(encoding="utf-8").splitlines()
    stderr = (folder / "stderr.txt").read_text(encoding="utf-8", errors="replace")

    check(metadata.get("status") == "PASS", f"{folder}: status distinto de PASS")
    check(metadata.get("sanitizer") == sanitizer, f"{folder}: tipo de herramienta incorrecto")
    expected_signal = "SIGINT" if name.startswith("sigint") else "SIGTERM"
    check(metadata.get("signal") == expected_signal, f"{folder}: senal incorrecta")
    check(metadata.get("exit_code") == 0, f"{folder}: codigo de salida distinto de cero")
    seconds = metadata.get("seconds_to_shutdown")
    check(isinstance(seconds, (float, int)) and 0 <= seconds <= 5,
          f"{folder}: apagado fuera de 5 segundos")
    check(metadata.get("max_created_same_timestamp", 0) >= 2,
          f"{folder}: no contiene rafaga documentada")
    check(metadata.get("max_burst_before_signal", 0) >= 2,
          f"{folder}: rafaga no observada antes de la senal")
    observed = metadata.get("observed_before_signal", {})
    for kind, minimum in (("orderCreated", 30), ("orderAssigned", 1), ("courierBreakdown", 1)):
        check(observed.get(kind, 0) >= minimum,
              f"{folder}: falta {kind} antes de la senal")
    check(not metadata.get("sanitizer_errors"), f"{folder}: errores sanitizador en metadata")
    check(not ERROR_PATTERN.search(stderr), f"{folder}: errores sanitizador en stderr")
    check(out and out[0] == "Simulation has started...", f"{folder}: inicio invalido")
    check(len(out) >= 3, f"{folder}: stdout incompleto")
    summary = json.loads(out[-1])
    check(summary == metadata.get("summary"), f"{folder}: resumen distinto de metadata")
    printed_events = [json.loads(line) for line in out[1:-1]]
    check(printed_events == events, f"{folder}: JSONL no corresponde con stdout")
    check(events and events[-1].get("event") == "simulationStopping",
          f"{folder}: falta simulationStopping al final")
    cnt = collections.Counter(event.get("event") for event in events)
    check(cnt["simulationStopping"] == 1, f"{folder}: simulationStopping duplicado")
    for stat, event in (("created", "orderCreated"), ("delivered", "orderDelivered"),
                        ("rejected", "orderRejected"), ("breakdowns", "courierBreakdown"),
                        ("reassigned", "orderReassigned")):
        check(summary.get(stat) == cnt[event], f"{folder}: {stat} no corresponde")
    check(summary["created"] == summary["delivered"] + summary["rejected"] + summary["pending"],
          f"{folder}: balance de pedidos inconsistente")
    check(cnt["orderAssigned"] >= 1, f"{folder}: no hubo asignaciones")
    check(summary["breakdowns"] >= 1, f"{folder}: no hubo averias")
    check(summary["created"] >= 30, f"{folder}: carga insuficiente")
    check(len(summary.get("deliveredPerCourier", {})) == config["fleet"]["couriers"],
          f"{folder}: flota distinta del resumen")
    check(sum(summary["deliveredPerCourier"].values()) == summary["delivered"],
          f"{folder}: entregas de repartidores no suman")
    command = metadata.get("exact_command_shell", "")
    if sanitizer == "tsan":
        check("TSAN_OPTIONS=" in command, f"{folder}: no registra TSAN_OPTIONS")
    else:
        check("ASAN_OPTIONS=" in command and "detect_leaks=1" in command
              and "LSAN_OPTIONS=" in command and "UBSAN_OPTIONS=" in command,
              f"{folder}: faltan las opciones de sanitizadores")
    return {
        "grupo": label, "herramienta": "TSAN" if sanitizer == "tsan" else "ASAN/LSAN/UBSAN",
        "caso": name, "estado": "PASS", "creados": summary["created"],
        "asignados": cnt["orderAssigned"], "entregados": summary["delivered"],
        "rechazados": summary["rejected"], "pendientes": summary["pending"],
        "averias": summary["breakdowns"],
        "rafaga_max": metadata["max_created_same_timestamp"],
        "cierre_s": seconds, "codigo_salida": metadata["exit_code"],
        "stderr_bytes": len(stderr.encode("utf-8")),
        "evidencia": str(folder.relative_to(group.parent)).replace("\\", "/"),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dir", type=Path, default=Path("evidencias/dp11"))
    args = parser.parse_args()
    base = args.dir.expanduser().resolve()
    rows = []
    groups = []
    for name, tool, repeats in GROUPS:
        folder = base / name
        check(folder.is_dir(), f"No existe {folder}: copia la evidencia primero")
        for filename in ("versiones_herramientas.json", "parametros.json", "informe.md"):
            check((folder / filename).is_file(), f"Falta {folder / filename}")
        manifest = read_json(folder / "versiones_herramientas.json")
        params = read_json(folder / "parametros.json")
        sanitizer = "tsan" if tool == "TSAN" else "asan"
        check(params.get("tool") == sanitizer, f"{folder}: herramienta incorrecta")
        check(params.get("repeats") == repeats, f"{folder}: numero de repeticiones inesperado")
        check(manifest.get("sanitizer") == sanitizer, f"{folder}: manifiesto incorrecto")
        check(bool(manifest.get("binary_sha256")), f"{folder}: falta hash ejecutable")
        versions = manifest.get("version_commands", [])
        check(isinstance(versions, list) and bool(versions), f"{folder}: faltan versiones")
        groups.append((name, tool, manifest, params))
        for prefix in ("sigint", "sigterm"):
            for repetition in range(1, repeats + 1):
                case = f"{prefix}_rep_{repetition:02d}"
                rows.append(inspect_run(folder, name, case, sanitizer))
    check(len(rows) == 12, "Se esperaban 12 ejecuciones")

    with (base / "resumen_ejecuciones.csv").open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    total = len(rows)
    closes = [r["cierre_s"] for r in rows]
    breakdowns = sum(r["averias"] for r in rows)
    created = sum(r["creados"] for r in rows)
    lines = [
        "# EquipeGo — DP.11 | Informe final de análisis dinámico",
        "",
        "## 1. Objetivo y alcance",
        "",
        "Evaluar una compilación instrumentada del simulador completo en Linux/WSL, "
        "con productor, dispatcher, flota, rescate, ráfagas de pedidos, "
        "averías y apagado coordinado por SIGINT/SIGTERM.",
        "",
        "Las pruebas usan **copias** de `config/equipetrol.json` con "
        "`bagCapacity=1`, `meanIntervalMs=5000`, `burstMax=12`, "
        "`maxPending=20` y `breakdownProbability=1.0`. "
        "No modifican el JSON original.",
        "",
        "## 2. Herramientas y versiones",
        "",
        "- **ThreadSanitizer (TSAN):** detecta carreras de datos entre hilos.",
        "- **AddressSanitizer (ASAN):** detecta accesos ilegales a memoria.",
        "- **LeakSanitizer (LSAN):** comprueba fugas al terminar el proceso "
        "(`ASAN_OPTIONS=detect_leaks=1`).",
        "- **UndefinedBehaviorSanitizer (UBSAN):** detecta comportamientos indefinidos instrumentados.",
        "",
        "| Grupo | Herramienta | GCC (g++) | SHA256 ejecutable (12 primeros) |",
        "|---|---|---|---|",
    ]
    for name, tool, manifest, _ in groups:
        gcc = find_version(manifest, "g++")
        lines.append(f"| `{name}` | {tool} | {gcc.replace('|', '/')} | "
                     f"`{manifest['binary_sha256'][:12]}` |")
    lines += [
        "",
        "Las versiones completas de GCC, CMake, Linux, `libtsan`, "
        "`libasan`, `liblsan` y el listado de bibliotecas enlazadas "
        "están en el `versiones_herramientas.json` **original** de cada grupo.",
        "",
        "**Configuraciones de compilación usadas:**",
        "",
        "```bash",
        "cmake -S . -B ~/build-equipego-tsan -DENABLE_TSAN=ON",
        "cmake --build ~/build-equipego-tsan --target delivery_sim -j4",
        "",
        "cmake -S . -B ~/build-equipego-asan \\",
        "  -DCMAKE_BUILD_TYPE=Debug \\",
        "  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1' \\",
        "  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'",
        "cmake --build ~/build-equipego-asan --target delivery_sim -j4",
        "```",
        "",
        "Cada `metadata.json` conserva el **comando exacto**, las variables de entorno "
        "del sanitizador, la señal enviada y los tiempos. Los `parametros.json` "
        "conservan rutas, hashes, duración, escala y límites del ensayo.",
        "",
        "## 3. Resultados completos",
        "",
        "| Grupo | Señal | Pedidos | Asignados | Averías | Ráfaga máxima | Cierre (s) | Estado |",
        "|---|---|---:|---:|---:|---:|---:|---|",
    ]
    for r in rows:
        lines.append(f"| `{r['grupo']}` | {r['caso'].split('_')[0].upper()} | "
                     f"{r['creados']} | {r['asignados']} | {r['averias']} | "
                     f"{r['rafaga_max']} | {r['cierre_s']:.3f} | **PASS** |")
    lines += [
        "",
        f"**Total:** {total}/12 ejecuciones válidas; {created} pedidos creados, "
        f"{breakdowns} averías acumuladas, cierre máximo {max(closes):.3f} s "
        f"y tiempo medio de cierre {statistics.mean(closes):.3f} s.",
        "",
        "Todos los procesos terminaron con código 0; cada JSONL termina en "
        "`simulationStopping` exactamente una vez. Los contadores de pedidos "
        "coinciden con los eventos y la igualdad "
        "`creados = entregados + rechazados + pendientes` se cumple.",
        "",
        "## 4. Hallazgos, correcciones y reejecución",
        "",
        "**Hallazgos de los sanitizadores en estas pruebas:** ninguno. "
        "No se reportaron carreras de datos (TSAN), accesos inválidos (ASAN), "
        "fugas de memoria (LSAN habilitado) ni errores instrumentados "
        "de comportamiento indefinido (UBSAN).",
        "",
        "**Correcciones de código derivadas de estas pruebas:** ninguna, "
        "porque los ensayos iniciales no presentaron fallos. "
        "No se inventaron hallazgos ni correcciones.",
        "",
        "**Reejecución:** tras las primeras dos corridas por herramienta "
        "(una SIGINT y una SIGTERM), se ejecutaron otras cuatro corridas "
        "por herramienta (dos por señal), todas satisfactorias.",
        "",
        "Las pruebas con instrumentación representan los escenarios ejecutados; "
        "no prueban ausencia universal de fallos ni sustituyen otros casos límite.",
        "",
        "## 5. Referencias a evidencias originales",
        "",
    ]
    for name, tool, _, _ in groups:
        lines.append(f"- `{name}/`: registros completos de {tool}: "
                     "`informe.md`, `versiones_herramientas.json`, `parametros.json`, "
                     "`sigint_rep_*/` y `sigterm_rep_*/` con config, stdout, stderr, "
                     "JSONL y metadata por ejecución.")
    lines += [
        "- `resumen_ejecuciones.csv`: tabla generada verificando las doce corridas.",
        "- `benchmarks/dp11_dynamic_analysis.py`: reproduce cada experimento instrumentado.",
        "- `benchmarks/dp11_finalize_report.py`: valida y consolida las evidencias sin "
        "alterar los archivos originales.",
        "",
        "## 6. Comandos de reproducción",
        "",
        "```bash",
        "python3 benchmarks/dp11_dynamic_analysis.py --sanitizer tsan \\",
        "  --binary ~/build-equipego-tsan/delivery_sim --max-wait 120 --repeats 2",
        "python3 benchmarks/dp11_dynamic_analysis.py --sanitizer asan \\",
        "  --binary ~/build-equipego-asan/delivery_sim --max-wait 120 --repeats 2",
        "python3 benchmarks/dp11_finalize_report.py",
        "```",
        "",
    ]
    (base / "informe_final.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"DP11 informe: {total}/12 ejecuciones verificadas")
    print(f"  Pedidos creados: {created} | Averias: {breakdowns}")
    print(f"  Cierre maximo: {max(closes):.3f}s")
    print(f"  Informe: {base / 'informe_final.md'}")
    print(f"  CSV: {base / 'resumen_ejecuciones.csv'}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
        print(f"ERROR al consolidar DP11: {exc}", file=sys.stderr)
        sys.exit(1)
