#!/usr/bin/env python3
"""DP12.3: medir CPU de una flota ociosa con/sin OpenCV (Linux/WSL).

Nunca modifica config/equipetrol.json; genera copias temporales con cero
pedidos esperados y guarda stdout, stderr, JSONL, configuraciones y CSV.
Modo --headless SOLO sirve para separar el costo de los hilos del dibujo:
la evaluacion principal de la rubrica se realiza con la ventana abierta.
"""

from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import subprocess
import sys
import time


def positive_int(value: str) -> int:
    result = int(value)
    if result < 1:
        raise argparse.ArgumentTypeError("debe ser >= 1")
    return result


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as file:
        while chunk := file.read(1 << 20):
            h.update(chunk)
    return h.hexdigest()


def resolve_config_path(original: Path, config: dict) -> None:
    image = Path(config["map"]["image"])
    config["map"]["image"] = str((original.parent / image).resolve()) if not image.is_absolute() else str(image)


def cpu_times() -> tuple[float, float]:
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime, usage.ru_stime


def run_trial(binary: Path, original: Path, destination: Path,
              couriers: int, mode: str, repeat: int, duration: int,
              scale: float, interval: int) -> dict:
    folder = destination / f"motos_{couriers:03d}" / mode / f"rep_{repeat:02d}"
    folder.mkdir(parents=True)
    conf = json.loads(original.read_text(encoding="utf-8"))
    resolve_config_path(original, conf)
    conf["fleet"]["couriers"] = couriers
    conf["orders"]["meanIntervalMs"] = interval
    conf["simulation"]["durationS"] = duration
    conf["simulation"]["timeScale"] = scale
    (folder / "config.json").write_text(json.dumps(conf, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    log = folder / "events.jsonl"
    cmd = [str(binary), str(folder / "config.json"),
           "--gui" if mode == "gui" else "--headless", "--log", str(log)]
    print(f"  -> {couriers} motos / {mode} / rep {repeat}: midiendo CPU durante {duration/scale:.1f}s...", flush=True)

    start_cpu = cpu_times()
    start = time.monotonic()
    with (folder / "stdout.txt").open("wb") as out, (folder / "stderr.txt").open("wb") as err:
        proc = subprocess.Popen(cmd, stdout=out, stderr=err)
        try:
            code = proc.wait(timeout=duration / scale + 30)
        except subprocess.TimeoutExpired as exc:
            proc.kill()
            proc.wait()
            raise RuntimeError(f"{mode}/{couriers}: timeout; los logs quedan en {folder}") from exc
    elapsed = time.monotonic() - start
    user, system = cpu_times()
    user -= start_cpu[0]
    system -= start_cpu[1]
    cpu = user + system

    meta = {"couriers": couriers, "mode": mode, "repeat": repeat,
            "duration_sim_s": duration, "scale": scale, "idle_interval_ms": interval,
            "wall_s": round(elapsed, 4), "cpu_user_s": round(user, 4),
            "cpu_system_s": round(system, 4), "cpu_total_s": round(cpu, 4),
            "cpu_pct_one_core": round(100 * cpu / elapsed, 2), "exit_code": code,
            "command": cmd, "binary_sha256": sha256(binary)}
    (folder / "metadata.json").write_text(json.dumps(meta, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    if code != 0:
        raise RuntimeError(f"{mode}/{couriers}: exit={code}; lee {folder/'stderr.txt'}")
    lines = (folder / "stdout.txt").read_text(encoding="utf-8").splitlines()
    if len(lines) < 3 or lines[0] != "Simulation has started...":
        raise RuntimeError(f"{mode}/{couriers}: linea inicial o resumen JSON ausente")
    try:
        summary = json.loads(lines[-1])
        events = [json.loads(x) for x in log.read_text(encoding="utf-8").splitlines() if x.strip()]
        reported = [json.loads(x) for x in lines[1:-1]]
    except (ValueError, OSError) as exc:
        raise RuntimeError(f"{mode}/{couriers}: formato JSONL incorrecto: {exc}") from exc
    if reported != events or not events or events[-1].get("event") != "simulationStopping":
        raise RuntimeError(f"{mode}/{couriers}: eventos inconsistentes o falta apagado final")
    if sum(e.get("event") == "simulationStopping" for e in events) != 1:
        raise RuntimeError(f"{mode}/{couriers}: simulationStopping duplicado")
    if summary["created"] != 0:
        raise RuntimeError(f"{mode}/{couriers}: aparecieron {summary['created']} pedidos; "
                           f"aumenta --idle-interval-ms y repite (no es una medida de reposo puro)")
    if summary["delivered"] != 0 or summary["rejected"] != 0 or summary["pending"] != 0:
        raise RuntimeError(f"{mode}/{couriers}: resumen de flota ociosa incongruente: {summary}")
    print(f"     OK: CPU {cpu:.3f}s / wall {elapsed:.2f}s = "
          f"{meta['cpu_pct_one_core']:.2f}% de un nucleo | sin pedidos", flush=True)
    return meta


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path.home() / "build-equipego/delivery_sim_bench")
    parser.add_argument("--config", type=Path, default=root / "config/equipetrol.json")
    parser.add_argument("--output", type=Path, default=Path.home() / "equipego_benchmarks/dp12_idle")
    parser.add_argument("--couriers", default="8,64", help="lista separada por comas")
    parser.add_argument("--modes", default="headless,gui", help="headless,gui o uno de ellos")
    parser.add_argument("--repeats", type=positive_int, default=1)
    parser.add_argument("--duration", type=positive_int, default=12, help="segundos simulados")
    parser.add_argument("--scale", type=float, default=1.0)
    parser.add_argument("--idle-interval-ms", type=positive_int, default=20000000)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        parser.error("necesitas Linux o WSL")
    if not 0 < args.scale <= 120:
        parser.error("--scale debe estar entre 0 y 120")
    try:
        couriers = [positive_int(p.strip()) for p in args.couriers.split(",")]
    except (ValueError, argparse.ArgumentTypeError) as exc:
        parser.error(f"--couriers invalido: {exc}")
    modes = [p.strip() for p in args.modes.split(",")]
    if not modes or any(x not in ("headless", "gui") for x in modes) or len(set(modes)) != len(modes):
        parser.error("--modes debe ser gui, headless o headless,gui")
    binary = args.binary.expanduser().resolve()
    config = args.config.expanduser().resolve()
    if not binary.is_file() or not config.is_file():
        parser.error(f"faltan binario ({binary}) o configuracion ({config})")
    dest = args.output.expanduser().resolve() / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    dest.mkdir(parents=True, exist_ok=False)
    setup = {"binary": str(binary), "binary_sha256": sha256(binary),
             "original_config": str(config), "original_config_sha256": sha256(config),
             "host": platform.platform(), "python": sys.version,
             "couriers": couriers, "modes": modes, "repeats": args.repeats,
             "duration": args.duration, "scale": args.scale,
             "idle_interval_ms": args.idle_interval_ms}
    (dest / "parametros.json").write_text(json.dumps(setup, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"DP12.3 CPU OCIO | evidencia: {dest}", flush=True)
    results = []
    try:
        for cour in couriers:
            for mode in modes:
                for rep in range(1, args.repeats + 1):
                    results.append(run_trial(binary, config, dest, cour, mode, rep,
                                             args.duration, args.scale, args.idle_interval_ms))
    except Exception as exc:
        (dest / "ERROR.txt").write_text(str(exc) + "\n", encoding="utf-8")
        print(f"ERROR: {exc}\nEvidencias conservadas: {dest}", file=sys.stderr)
        return 1
    columns = ["couriers", "mode", "repeat", "cpu_user_s", "cpu_system_s",
               "cpu_total_s", "wall_s", "cpu_pct_one_core", "exit_code"]
    with (dest / "resultados.csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(results)
    lines = ["# DP.12.3 — CPU de flota sin pedidos", "",
             "Ejecutado con configuraciones COPIADAS del JSON original; los logs se conservan intactos.",
             "La CPU se calcula como tiempo de CPU de todos los hilos / tiempo real transcurrido.",
             "100% equivale a un nucleo completo; 5% equivale a 0.05 nucleos ocupados.",
             "El modo headless es una instrumentacion auxiliar **no compatible** con el modo obligatorio de entrega; ",
             "sirve para separar aproximadamente el coste de simulacion del coste de la ventana.",
             "La rubrica no especifica un umbral numerico: no inferir aprobado solo por este informe.",
             "", "| Motos | Modo | CPU (s) | Pared (s) | CPU (% de nucleo) |",
             "|---:|---|---:|---:|---:|"]
    for row in results:
        lines.append(f"| {row['couriers']} | {row['mode']} | {row['cpu_total_s']:.3f} | "
                     f"{row['wall_s']:.2f} | {row['cpu_pct_one_core']:.2f}% |")
    lines += ["", "Observaciones visuales (completar manualmente):",
              "- ¿Aparece © OpenStreetMap contributors en la ventana? **Pendiente**.",
              "- ¿Las motos ociosas permanecen en su sitio? **Pendiente**.",
              "- ¿La GUI sigue fluida para 64 motos? **Pendiente**.", ""]
    (dest / "informe.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"DP12.3: {len(results)}/{len(results)} mediciones completas. "
          f"Resultados: {dest / 'informe.md'}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
