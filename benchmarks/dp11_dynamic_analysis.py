#!/usr/bin/env python3
"""EquipeGo DP11.1: ejecuciones reales instrumentadas y evidencia cruda.

SOLO Python stdlib; Linux/WSL. Nunca modifica config/equipetrol.json.
Cada ejecución exige: inicio exacto, rafaga >=2, pedidos, asignacion,
averia, señal POSIX, cierre <=5s, contabilidad y salida sanitizada limpia.

Ejemplos:
  python3 benchmarks/dp11_dynamic_analysis.py --sanitizer tsan \
      --binary ~/build-equipego-tsan/delivery_sim
  python3 benchmarks/dp11_dynamic_analysis.py --sanitizer asan \
      --binary ~/build-equipego-asan/delivery_sim
"""

from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import signal
import subprocess
import sys
import time
from typing import Any


CHANGES: dict[str, Any] = {
    "fleet.bagCapacity": 1,
    "orders.meanIntervalMs": 5000,
    "orders.burstMax": 12,
    "orders.maxPending": 20,
    "incidents.breakdownProbability": 1.0,
}
MIN_EVENTS = {"orderCreated": 30, "orderAssigned": 1,
              "courierBreakdown": 1}
TOOL_ERROR = re.compile(
    r"ERROR: (?:AddressSanitizer|LeakSanitizer|ThreadSanitizer)"
    r"|WARNING: ThreadSanitizer|SUMMARY: .*Sanitizer"
    r"|AddressSanitizer:DEADLYSIGNAL|runtime error:|FATAL: ThreadSanitizer",
    re.I,
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def capture(command: list[str]) -> dict[str, Any]:
    try:
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=12,
                                check=False)
        return {"command": command, "exit_code": result.returncode,
                "output": result.stdout}
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"command": command, "error": str(exc)}


def tool_manifest(binary: Path, sanitizer: str) -> dict[str, Any]:
    runtime = "libtsan.so" if sanitizer == "tsan" else "libasan.so"
    commands = [
        ["g++", "--version"], ["g++", "-dumpfullversion", "-dumpversion"],
        ["g++", "-print-file-name=" + runtime],
        ["g++", "-print-file-name=liblsan.so"],
        ["cmake", "--version"], ["uname", "-a"],
        ["ldd", str(binary)],
        ["git", "rev-parse", "HEAD"],
    ]
    return {
        "tool": "ThreadSanitizer" if sanitizer == "tsan" else
                "AddressSanitizer + LeakSanitizer + UndefinedBehaviorSanitizer",
        "sanitizer": sanitizer,
        "binary": str(binary), "binary_sha256": sha256(binary),
        "platform": platform.platform(),
        "version_commands": [capture(cmd) for cmd in commands],
    }



def verify_instrumentation(manifest: dict[str, Any], sanitizer: str) -> None:
    """Evita etiquetar como sanitizada una compilacion sin runtime asociado.

    En Linux/WSL verificamos que el ejecutable este enlazado dinamicamente
    contra la biblioteca instrumentadora que corresponda.
    """
    entry = next((item for item in manifest["version_commands"]
                  if item.get("command", [""])[0] == "ldd"), None)
    output = (entry or {}).get("output", "")
    expected = ("libtsan", "libclang_rt.tsan") if sanitizer == "tsan" else (
        "libasan", "libclang_rt.asan")
    if not any(library in output for library in expected):
        raise RuntimeError(
            "El ejecutable NO muestra enlazado "
            f"{sanitizer.upper()} en ldd. Compila el binario instrumentado "
            "antes de ejecutar el experimento. Salida ldd guardada en "
            "versiones_herramientas.json."
        )


def configuration(source: Path, dest: Path) -> dict[str, Any]:
    original = json.loads(source.read_text(encoding="utf-8"))
    if not isinstance(original, dict):
        raise ValueError("Configuracion JSON invalida")
    config = json.loads(json.dumps(original))
    # Imagen respecto del JSON original; NO se modifica el mapa ni sus datos.
    image = Path(config["map"]["image"])
    config["map"]["image"] = str((image if image.is_absolute()
                                  else source.parent / image).resolve())
    if not Path(config["map"]["image"]).is_file():
        raise FileNotFoundError("No se encuentra la imagen del mapa")
    for dotted, value in CHANGES.items():
        part, field = dotted.split(".", 1)
        config[part][field] = value
    dest.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")
    return config


def environment_for(sanitizer: str) -> dict[str, str]:
    # No se ocultan ni filtran reportes. El codigo 66 identifica fallos.
    if sanitizer == "tsan":
        return {"TSAN_OPTIONS": "halt_on_error=1:exitcode=66:history_size=7"}
    return {
        "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1:exitcode=66",
        "LSAN_OPTIONS": "exitcode=66:report_objects=1",
        "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1",
    }


def analyze(name: str, stdout_path: Path, stderr_path: Path,
            event_path: Path, config: dict[str, Any]) -> dict[str, Any]:
    out = stdout_path.read_text(encoding="utf-8", errors="replace").splitlines()
    if not out or out[0] != "Simulation has started...":
        raise AssertionError(f"{name}: falta la linea exacta de inicio")
    if len(out) < 3:
        raise AssertionError(f"{name}: falta el resumen final")
    try:
        summary = json.loads(out[-1])
        emitted = [json.loads(line) for line in out[1:-1]]
        recorded = [json.loads(line) for line in event_path.read_text(
            encoding="utf-8").splitlines() if line.strip()]
    except (OSError, ValueError) as exc:
        raise AssertionError(f"{name}: JSONL incorrecto: {exc}") from exc
    if emitted != recorded:
        raise AssertionError(f"{name}: stdout y events.jsonl difieren")
    if not recorded or recorded[-1].get("event") != "simulationStopping":
        raise AssertionError(f"{name}: simulationStopping no es el ultimo")
    counts = Counter(ev.get("event") for ev in recorded)
    if counts["simulationStopping"] != 1:
        raise AssertionError(f"{name}: simulationStopping repetido")
    for summary_key, event_key in (
        ("created", "orderCreated"), ("delivered", "orderDelivered"),
        ("rejected", "orderRejected"), ("breakdowns", "courierBreakdown"),
        ("reassigned", "orderReassigned"),
    ):
        if summary.get(summary_key) != counts[event_key]:
            raise AssertionError(f"{name}: {summary_key} no coincide")
    if summary["created"] != (summary["delivered"] +
                              summary["rejected"] + summary["pending"]):
        raise AssertionError(f"{name}: pedidos perdidos en el balance")
    delivered_by = summary.get("deliveredPerCourier", {})
    if len(delivered_by) != config["fleet"]["couriers"] or \
            sum(delivered_by.values()) != summary["delivered"]:
        raise AssertionError(f"{name}: entregas por moto inconsistentes")

    creation_times = Counter()
    seen_created, seen_delivered, seen_rejected = set(), set(), set()
    previous_time: dict[str, int] = {}
    for ev in recorded:
        if not isinstance(ev.get("t"), int) or ev["t"] < 0:
            raise AssertionError(f"{name}: tiempo invalido")
        order = ev.get("order")
        if order is not None:
            if ev["t"] < previous_time.get(order, -1):
                raise AssertionError(f"{name}: reloj del pedido retrocedio")
            previous_time[order] = ev["t"]
        kind = ev.get("event")
        if kind == "orderCreated":
            if order in seen_created:
                raise AssertionError(f"{name}: pedido creado dos veces")
            seen_created.add(order)
            creation_times[ev["t"]] += 1
        elif kind == "orderDelivered":
            if order not in seen_created or order in seen_delivered:
                raise AssertionError(f"{name}: entrega duplicada o desconocida")
            seen_delivered.add(order)
        elif kind == "orderRejected":
            if order not in seen_created or order in seen_rejected:
                raise AssertionError(f"{name}: rechazo duplicado o desconocido")
            seen_rejected.add(order)
    if seen_delivered & seen_rejected:
        raise AssertionError(f"{name}: pedido rechazado Y entregado")
    peak_burst = max(creation_times.values(), default=0)
    if peak_burst < 2:
        raise AssertionError(f"{name}: no hay evidencia de rafaga")
    for kind, threshold in MIN_EVENTS.items():
        if counts[kind] < threshold:
            raise AssertionError(f"{name}: se esperaba {kind}>={threshold}")
    stderr = stderr_path.read_text(encoding="utf-8", errors="replace")
    errors = TOOL_ERROR.findall(stderr)
    if errors:
        raise AssertionError(f"{name}: reporte de sanitizador en stderr; revisarlo")
    return {"summary": summary, "event_counts": dict(counts),
            "max_created_same_timestamp": peak_burst,
            "stderr_bytes": len(stderr.encode("utf-8")),
            "sanitizer_errors": errors}


def trial(binary: Path, source_config: Path, root: Path, mode: str,
          signame: str, number: int, scale: float, max_wait: float,
          min_runtime: float, override_env: dict[str, str]) -> dict[str, Any]:
    name = f"{signame.lower()}_rep_{number:02d}"
    directory = root / name
    directory.mkdir(parents=True)
    config_path = directory / "config.json"
    config = configuration(source_config, config_path)
    stdout_path = directory / "stdout.txt"
    stderr_path = directory / "stderr.txt"
    events_path = directory / "events.jsonl"
    command = [str(binary), str(config_path), "--headless",
               "--duration", "0", "--scale", str(scale),
               "--log", str(events_path)]
    sig = getattr(signal, signame)
    env = os.environ.copy()
    env.update(override_env)
    version = {
        "command": command,
        "exact_command_shell": " ".join(
            [*(f"{key}={shlex.quote(val)}" for key, val in override_env.items()),
             shlex.join(command)]),
        "source_config": str(source_config),
        "source_config_sha256": sha256(source_config),
        "config_overrides": CHANGES,
        "sanitizer": mode, "signal": signame,
        "signal_number": int(sig),
        "scale": scale, "minimum_seconds_before_signal": min_runtime,
        "minimum_events_before_signal": MIN_EVENTS,
        "maximum_wait_for_events_s": max_wait,
        "exit_code": None, "seconds_to_shutdown": None,
        "observed_before_signal": {}, "max_burst_before_signal": 0,
        "error": None,
    }
    print(f"  -> {name}: esperando pedidos, rafaga y averia...", flush=True)
    started = time.monotonic()
    sig_time: float | None = None
    process: subprocess.Popen | None = None
    counts: Counter[str] = Counter()
    created_timestamps: Counter[int] = Counter()
    try:
        with stdout_path.open("w", encoding="utf-8") as out, \
                stderr_path.open("w", encoding="utf-8") as err:
            process = subprocess.Popen(command, stdout=out, stderr=err,
                                       cwd=directory, env=env,
                                       start_new_session=True)
            with stdout_path.open("r", encoding="utf-8", errors="replace") as reader:
                started_seen = False
                buffer = ""
                while time.monotonic() - started < max_wait:
                    if process.poll() is not None:
                        raise AssertionError(
                            f"Proceso termino antes de recibir {signame} "
                            f"(exit={process.returncode}); ver stderr.txt")
                    chunk = reader.read()
                    if chunk:
                        buffer += chunk
                        while "\n" in buffer:
                            line, buffer = buffer.split("\n", 1)
                            if line == "Simulation has started...":
                                started_seen = True
                            elif line.strip():
                                try:
                                    event = json.loads(line)
                                except ValueError as exc:
                                    raise AssertionError("stdout no es JSONL") from exc
                                if isinstance(event, dict) and "event" in event:
                                    counts[event["event"]] += 1
                                    if event["event"] == "orderCreated":
                                        created_timestamps[event["t"]] += 1
                    if (started_seen and time.monotonic() - started >= min_runtime
                            and all(counts[k] >= minimum
                                    for k, minimum in MIN_EVENTS.items())
                            and max(created_timestamps.values(), default=0) >= 2):
                        break
                    time.sleep(0.05)
                else:
                    raise AssertionError(
                        f"No se observo la carga exigida antes de {max_wait}s; "
                        f"eventos={dict(counts)}; "
                        f"max_rafaga={max(created_timestamps.values(), default=0)}")
            version["observed_before_signal"] = dict(counts)
            version["max_burst_before_signal"] = max(created_timestamps.values())
            sig_time = time.monotonic()
            process.send_signal(sig)
            try:
                code = process.wait(timeout=5.0)
            except subprocess.TimeoutExpired as exc:
                raise AssertionError("Apagado demoro mas de 5s") from exc
            version["exit_code"] = code
            version["seconds_to_shutdown"] = round(time.monotonic() - sig_time, 4)
            if code != 0:
                raise AssertionError(f"Codigo de salida={code}; revisar stderr.txt")
            if version["seconds_to_shutdown"] > 5:
                raise AssertionError("Cierre fuera del limite de 5s")
        result = analyze(name, stdout_path, stderr_path, events_path, config)
        version.update(result)
        version["status"] = "PASS"
        print(f"     OK: creados={result['summary']['created']} "
              f"averias={result['summary']['breakdowns']} "
              f"rafaga_max={result['max_created_same_timestamp']} "
              f"cierre={version['seconds_to_shutdown']:.3f}s", flush=True)
        return version
    except Exception as exc:
        version["status"] = "FAIL"
        version["error"] = str(exc)
        print(f"     FALLO: {exc}; guarda {directory}", file=sys.stderr, flush=True)
        return version
    finally:
        if process is not None:
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
            version["exit_code"] = process.returncode
        version["elapsed_before_signal_s"] = round(
            (sig_time if sig_time is not None else time.monotonic()) - started, 4)
        (directory / "metadata.json").write_text(
            json.dumps(version, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")


def generate_report(output: Path, mode: str, results: list[dict[str, Any]],
                    manifest: dict[str, Any]) -> None:
    lines = ["# DP.11 — Analisis dinamico", "",
             f"Herramienta: **{manifest['tool']}**.",
             "Version: ver `versiones_herramientas.json` con g++, biblioteca runtime y ldd.",
             "Configuracion: copia de `config/equipetrol.json` con "
             "rafagas y averias intensificadas; original intacto.",
             "Cada corrida contiene un evento `simulationStopping` si el cierre "
             "termino correctamente.", "",
             "| Corrida | Estado | Pedidos | Averias | Rafaga maxima | Cierre real (s) |",
             "|---|---|---:|---:|---:|---:|" ]
    for r in results:
        s = r.get("summary", {})
        lines.append(f"| {r['signal']} | {r['status']} | "
                     f"{s.get('created', '-')} | {s.get('breakdowns', '-')} | "
                     f"{r.get('max_created_same_timestamp','-')} | "
                     f"{r.get('seconds_to_shutdown','-')} |")
        lines.extend(["", f"### {r['signal']} — {r['status']}", "",
                      "Comando completo:", "",
                      "```bash", r["exact_command_shell"], "```", "",
                      f"Datos crudos: `{r['signal'].lower()}_rep_"
                      "NN/stdout.txt`, `stderr.txt`, `events.jsonl`, "
                      "`config.json`, `metadata.json`.", ""])
        if r.get("error"):
            lines.append("Hallazgo/error: " + r["error"] + "\n")
    lines += ["## Interpretacion y reejecucion", "",
              "Anotar las anomalias, cambios realizados en el codigo y "
              "un nuevo ensayo con esta misma herramienta en otro directorio. "
              "Si no se detectaron errores, indicar 'sin cambios por hallazgos' "
              "y repetir para corroborar. No borrar reportes originales.", ""]
    (output / "informe.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitizer", required=True, choices=("tsan", "asan"))
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--config", type=Path,
                        default=Path("config/equipetrol.json"))
    parser.add_argument("--output", type=Path,
                        default=Path.home() / "equipego_benchmarks/dp11")
    parser.add_argument("--scale", type=float, default=60)
    parser.add_argument("--max-wait", type=float, default=120)
    parser.add_argument("--min-runtime", type=float, default=6)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--case", choices=("all", "sigint", "sigterm"),
                        default="all")
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        parser.error("Requiere Linux/WSL para SIGINT y SIGTERM")
    if not .25 <= args.scale <= 120:
        parser.error("La escala debe estar entre 0.25 y 120")
    if args.min_runtime < 0 or args.max_wait <= args.min_runtime:
        parser.error("--max-wait debe superar --min-runtime")
    if not 1 <= args.repeats <= 10:
        parser.error("--repeats entre 1 y 10")
    binary = args.binary.expanduser().resolve()
    config_path = args.config.expanduser().resolve()
    if not binary.is_file() or not config_path.is_file():
        parser.error(f"No existe ejecutable {binary} o configuracion {config_path}")
    if not os.access(binary, os.X_OK):
        parser.error(f"El ejecutable carece de permisos: {binary}")
    root = args.output.expanduser().resolve()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    destination = root / f"{args.sanitizer}_{stamp}"
    i = 1
    while destination.exists():
        destination = root / f"{args.sanitizer}_{stamp}_{i}"
        i += 1
    destination.mkdir(parents=True)
    manifest = tool_manifest(binary, args.sanitizer)
    (destination / "versiones_herramientas.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    verify_instrumentation(manifest, args.sanitizer)
    (destination / "parametros.json").write_text(json.dumps({
        "tool": args.sanitizer, "source_config": str(config_path),
        "source_config_sha256": sha256(config_path), "changes": CHANGES,
        "case": args.case, "repeats": args.repeats, "scale": args.scale,
        "min_runtime": args.min_runtime, "max_wait": args.max_wait,
        "binary": str(binary), "binary_sha256": manifest["binary_sha256"],
    }, indent=2) + "\n", encoding="utf-8")
    chosen = (["SIGINT", "SIGTERM"] if args.case == "all"
              else ["SIGINT" if args.case == "sigint" else "SIGTERM"])
    print(f"DP11.1 {args.sanitizer.upper()} | {len(chosen)*args.repeats} "
          f"ejecuciones instrumentadas | evidencias: {destination}", flush=True)
    env = environment_for(args.sanitizer)
    results = []
    for signame in chosen:
        for number in range(1, args.repeats + 1):
            result = trial(binary, config_path, destination, args.sanitizer,
                           signame, number, args.scale, args.max_wait,
                           args.min_runtime, env)
            results.append(result)
            generate_report(destination, args.sanitizer, results, manifest)
            if result["status"] == "FAIL":
                print("DP11.1: FAIL; conservados reportes originales en "
                      f"{destination}", file=sys.stderr)
                return 1
    print(f"DP11.1: {len(results)}/{len(results)} pruebas PASARON; "
          f"informes completos: {destination}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"FALLO DP11.1: {exc}", file=sys.stderr)
        sys.exit(1)
