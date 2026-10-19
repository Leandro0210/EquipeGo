#!/usr/bin/env python3
"""EquipeGo DP10.1: experimento reproducible ANTES/DESPUES del dispatcher ETA.

Python 3.9+, Linux/WSL, sin dependencias externas. Mantiene el JSON de origen
intacto, valida la salida del simulador y conserva los datos crudos.

Ejemplo inicial (2-4 segundos):
  python3 benchmarks/dp10_eta_before_after.py --fleets 8 --duration 180 --repeats 1

Linea base:
  python3 benchmarks/dp10_eta_before_after.py --fleets 8,32,64 --duration 900 --repeats 3 --label antes

Despues de una optimizacion real:
  python3 benchmarks/dp10_eta_before_after.py --fleets 8,32,64 --duration 900 --repeats 3 --label despues --compare /ruta/a/antes_TIMESTAMP
"""

import argparse
import csv
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import resource
import statistics
import subprocess
import sys
import time

import dp8_6_scalability as previous

FIELDS = [
    "label", "couriers", "repeat", "duration_sim_s", "scale", "wall_s",
    "cpu_s", "cpu_user_s", "cpu_system_s", "cpu_pct_one_core",
    "created", "assigned", "delivered", "rejected", "pending",
    "queue_full", "no_courier", "unreachable", "breakdowns", "reassigned",
    "assignment_p50_sim_ms", "assignment_p95_sim_ms", "assignment_mean_sim_ms",
    "delivered_per_sim_hour", "trial_directory",
]
MEASURES = [
    "wall_s", "cpu_s", "cpu_pct_one_core", "created", "assigned",
    "delivered", "rejected", "pending", "queue_full", "no_courier",
    "unreachable", "breakdowns", "reassigned", "assignment_p50_sim_ms",
    "assignment_p95_sim_ms", "assignment_mean_sim_ms", "delivered_per_sim_hour",
]


def parse_fleets(value):
    try:
        result = [int(n.strip()) for n in value.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("Se esperan enteros separados por comas") from error
    if not result or len(set(result)) != len(result) or any(not 1 <= n <= 128 for n in result):
        raise argparse.ArgumentTypeError("Usa cantidades unicas de 1 a 128")
    return result


def args_from_cli():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fleets", type=parse_fleets, default=parse_fleets("8,32,64"))
    parser.add_argument("--duration", type=int, default=900)
    parser.add_argument("--scale", type=float, default=60.0)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--label", choices=("antes", "despues"), default="antes")
    parser.add_argument("--config", type=Path, default=Path("config/equipetrol.json"))
    parser.add_argument("--binary", type=Path,
                        default=Path.home() / "build-equipego/delivery_sim_bench")
    parser.add_argument("--output", type=Path,
                        default=Path.home() / "equipego_benchmarks/dp10")
    parser.add_argument("--compare", type=Path, default=None,
                        help="Directorio de una linea base ANTES previamente ejecutada")
    parser.add_argument("--timeout", type=int, default=0,
                        help="Limite real por ensayo (0 automatico)")
    args = parser.parse_args()
    if not 1 <= args.duration <= 86400:
        parser.error("--duration debe estar entre 1 y 86400")
    if not math.isfinite(args.scale) or not 0.25 <= args.scale <= 120:
        parser.error("--scale debe estar entre 0.25 y 120")
    if not 1 <= args.repeats <= 20:
        parser.error("--repeats debe estar entre 1 y 20")
    if args.timeout < 0:
        parser.error("--timeout debe ser >= 0")
    if args.compare and args.label != "despues":
        parser.error("--compare requiere --label despues")
    return args


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def config_signature(config):
    """Hash del JSON sin cambios; map.image debe estar resuelta antes."""
    raw = json.dumps(config, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    return hashlib.sha256(raw.encode("utf-8")).hexdigest()


def percentile(data, q):
    if not data:
        return None
    ordered = sorted(data)
    pos = (len(ordered) - 1) * q
    lower = int(pos)
    upper = min(lower + 1, len(ordered) - 1)
    return round(ordered[lower] + (ordered[upper] - ordered[lower]) * (pos - lower), 3)


def assignment_latencies(path):
    created = {}
    latencies = []
    assigned_ids = set()
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            event = json.loads(line)
            kind = event.get("event")
            order = event.get("order")
            if kind == "orderCreated":
                if order in created:
                    raise ValueError(f"Creacion duplicada en linea {line_number}")
                created[order] = event["t"]
            elif kind == "orderAssigned":
                if order not in created or order in assigned_ids:
                    raise ValueError(f"Asignacion invalida o repetida en linea {line_number}")
                assigned_ids.add(order)
                delay = event["t"] - created[order]
                if delay < 0:
                    raise ValueError("Asignacion anterior a creacion")
                latencies.append(delay)
    return {
        "assigned": len(latencies),
        "p50": percentile(latencies, 0.50),
        "p95": percentile(latencies, 0.95),
        "mean": round(statistics.mean(latencies), 3) if latencies else None,
    }


def get_cpu():
    ru = resource.getrusage(resource.RUSAGE_CHILDREN)
    return ru.ru_utime, ru.ru_stime


def git_revision():
    result = subprocess.run(["git", "rev-parse", "HEAD"],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                            encoding="utf-8", check=False)
    return result.stdout.strip() if result.returncode == 0 else "desconocido"


def save_csv(path, rows, fields):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def run_one(args, root, binary, original, couriers, repeat, timeout):
    folder = root / f"motos_{couriers:03d}" / f"repeticion_{repeat:02d}"
    folder.mkdir(parents=True, exist_ok=False)
    config = json.loads(json.dumps(original))
    config["fleet"]["couriers"] = couriers
    (folder / "config.json").write_text(json.dumps(config, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    command = [str(binary), str(folder / "config.json"), "--headless",
               "--scale", str(args.scale), "--duration", str(args.duration),
               "--log", str(folder / "events.log")]
    (folder / "comando.txt").write_text(" ".join(command) + "\n", encoding="utf-8")
    before_user, before_system = get_cpu()
    start = time.perf_counter()
    try:
        with (folder / "stdout.log").open("w", encoding="utf-8") as out, \
             (folder / "stderr.log").open("w", encoding="utf-8") as err:
            completed = subprocess.run(command, cwd=folder, stdout=out, stderr=err,
                                       timeout=timeout, check=False)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"Simulacion excedio {timeout}s: {folder}") from error
    wall = time.perf_counter() - start
    after_user, after_system = get_cpu()
    user = max(0., after_user - before_user)
    system = max(0., after_system - before_system)
    if completed.returncode != 0:
        raise RuntimeError(f"Exit={completed.returncode}, revisar {folder / 'stderr.log'}")

    with (folder / "stdout.log").open("r", encoding="utf-8") as out:
        lines = [line.strip() for line in out if line.strip()]
    if not lines or lines[0] != "Simulation has started...":
        raise ValueError(f"Falta mensaje de inicio en {folder}")
    try:
        summary = json.loads(lines[-1])
    except (ValueError, TypeError) as error:
        raise ValueError(f"Resumen final invalido en {folder}") from error
    events = folder / "events.log"
    reasons = previous.validate_summary(summary, events, couriers)
    lat = assignment_latencies(events)
    total_cpu = user + system
    row = {
        "label": args.label, "couriers": couriers, "repeat": repeat,
        "duration_sim_s": args.duration, "scale": args.scale,
        "wall_s": round(wall, 4), "cpu_s": round(total_cpu, 4),
        "cpu_user_s": round(user, 4), "cpu_system_s": round(system, 4),
        "cpu_pct_one_core": round(100 * total_cpu / wall, 2) if wall else 0.,
        "created": summary["created"], "assigned": lat["assigned"],
        "delivered": summary["delivered"], "rejected": summary["rejected"],
        "pending": summary["pending"],
        "queue_full": reasons["queueFull"],
        "no_courier": reasons["noCourier"],
        "unreachable": reasons["unreachable"],
        "breakdowns": summary["breakdowns"], "reassigned": summary["reassigned"],
        "assignment_p50_sim_ms": lat["p50"],
        "assignment_p95_sim_ms": lat["p95"],
        "assignment_mean_sim_ms": lat["mean"],
        "delivered_per_sim_hour": round(summary["delivered"] * 3600 / args.duration, 3),
        "trial_directory": str(folder),
    }
    (folder / "resultado.json").write_text(json.dumps({"row": row, "summary": summary, "reasons": reasons},
                                                           indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return row


def aggregate(rows, fleets):
    groups = []
    for n in fleets:
        selection = [row for row in rows if int(row["couriers"]) == n]
        entry = {"couriers": n, "repeats": len(selection)}
        for key in MEASURES:
            vals = [float(row[key]) for row in selection if row[key] is not None and row[key] != ""]
            entry[key + "_mean"] = round(statistics.mean(vals), 3) if vals else None
            entry[key + "_std"] = (round(statistics.stdev(vals), 3)
                                   if len(vals) >= 2 else 0 if vals else None)
        groups.append(entry)
    return groups


def fmt(value, digits=2):
    if value is None:
        return "N/D"
    return f"{float(value):.{digits}f}"


def report(root, entries, args, revision):
    lines = [
        f"# EquipeGo DP10 — {args.label.upper()} (ETA / Dispatcher)", "",
        f"- Codigo Git al ejecutar: `{revision}`.",
        f"- Hash SHA256 del ejecutable: `{sha256(args.binary.expanduser().resolve())}`.",
        f"- Flotas: {', '.join(map(str, args.fleets))}; repeticiones: {args.repeats}.",
        f"- Tiempo: {args.duration} segundos simulados a escala {args.scale}x.",
        "- Solo cambia fleet.couriers entre flotas; la configuracion original y semilla se mantienen.",
        "- Para ANTES/DESPUES, comparar siempre la misma flota, carga, mapa, semilla y duracion.",
        "- CPU(s) mide TODO el proceso, no solo el dispatcher.",
        "- P95 asignacion es tiempo simulado desde orderCreated hasta orderAssigned; incluye esperas por capacidad.",
        "- Porcentaje de CPU refiere a un nucleo (puede superar 100% por paralelismo).", "",
        "| Motos | Creados | Asignados | Entregados | Rechazados | Pendientes | CPU(s) | CPU % | P95 asignacion (ms sim) |",
        "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for e in entries:
        lines.append(f"| {e['couriers']} | {fmt(e['created_mean'], 1)} | {fmt(e['assigned_mean'], 1)} | "
                     f"{fmt(e['delivered_mean'],1)} | {fmt(e['rejected_mean'],1)} | "
                     f"{fmt(e['pending_mean'],1)} | {fmt(e['cpu_s_mean'])} | "
                     f"{fmt(e['cpu_pct_one_core_mean'])}% | "
                     f"{fmt(e['assignment_p95_sim_ms_mean'])} |")
    lines.extend(["", "## Interpretacion", "",
                  "La duracion real debe ser aproximadamente duracion_simulada / escala.",
                  "Ese tiempo de pared NO representa un speedup del algoritmo ETA.",
                  "Los pedidos pendientes al corte NO equivalen a pedidos perdidos.",
                  "Los tiempos de asignacion tambien dependen de saturacion de bolsa y cola.",
                  "Los resultados de repeticiones pueden variar por intercalado de hilos.",
                  "No concluir mejoria causal sin comparacion con el codigo DESPUES.", ""])
    (root / "informe.md").write_text("\n".join(lines), encoding="utf-8")


def compare_with_baseline(args, root, current, metadata):
    older = args.compare.expanduser().resolve()
    base_meta_path = older / "parametros.json"
    base_rows_path = older / "resumen_por_flota.csv"
    if not base_meta_path.is_file() or not base_rows_path.is_file():
        raise ValueError(f"La linea base carece de parametros.json o resumen_por_flota.csv: {older}")
    base_meta = json.loads(base_meta_path.read_text(encoding="utf-8"))
    for key in ("fleets", "duration", "scale", "repeats", "config_signature"):
        if base_meta.get(key) != metadata.get(key):
            raise ValueError(f"Comparacion invalida: {key} cambio entre antes y despues")
    if base_meta.get("label") != "antes":
        raise ValueError("La carpeta comparada no tiene la etiqueta antes")
    with base_rows_path.open("r", encoding="utf-8", newline="") as stream:
        baseline = {int(row["couriers"]): row for row in csv.DictReader(stream)}
    comparison = []
    lines = ["# DP10 — Comparacion ANTES/DESPUES", "",
             f"- Linea base: `{older}`", f"- Nueva version: `{root}`",
             "- El protocolo verifica flota, duracion, escala, repeticiones y configuracion identicas.",
             "- CPU mide todo el proceso. No cambiar el hardware ni ejecutar otras cargas durante las pruebas.", "",
             "| Motos | CPU antes (s) | CPU despues (s) | Reduccion CPU (%) | Entregados antes | Entregados despues | P95 antes | P95 despues |",
             "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for current_row in current:
        n = int(current_row["couriers"])
        if n not in baseline:
            raise ValueError(f"La linea base no tiene flota de {n} motos")
        before = baseline[n]
        cpu_before = float(before["cpu_s_mean"])
        cpu_after = float(current_row["cpu_s_mean"])
        reduction = 100 * (cpu_before - cpu_after) / cpu_before if cpu_before > 0 else None
        record = {"couriers": n,
                  "cpu_antes_s": cpu_before, "cpu_despues_s": cpu_after,
                  "reduccion_cpu_pct": round(reduction, 3) if reduction is not None else "",
                  "entregados_antes": float(before["delivered_mean"]),
                  "entregados_despues": float(current_row["delivered_mean"]),
                  "p95_antes_sim_ms": before["assignment_p95_sim_ms_mean"],
                  "p95_despues_sim_ms": current_row["assignment_p95_sim_ms_mean"]}
        comparison.append(record)
        lines.append(f"| {n} | {fmt(cpu_before)} | {fmt(cpu_after)} | "
                     f"{fmt(reduction)}% | {fmt(record['entregados_antes'],1)} | "
                     f"{fmt(record['entregados_despues'],1)} | "
                     f"{fmt(record['p95_antes_sim_ms'])} | "
                     f"{fmt(record['p95_despues_sim_ms'])} |")
    save_csv(root / "comparacion_antes_despues.csv", comparison, list(comparison[0]))
    lines.extend(["", "Reduccion positiva indica menos tiempo de CPU; negativa indica regresion.",
                  "Una reduccion de CPU no basta si se pierden pedidos o incumplen limites.", ""])
    (root / "comparacion_antes_despues.md").write_text("\n".join(lines), encoding="utf-8")
    print("\n=== ANTES VS DESPUES ===")
    for row in comparison:
        print(f"  {row['couriers']} motos: CPU {row['cpu_antes_s']:.2f}s -> "
              f"{row['cpu_despues_s']:.2f}s; reduccion={fmt(row['reduccion_cpu_pct'])}%")


def main():
    args = args_from_cli()
    binary = args.binary.expanduser().resolve()
    config_path = args.config.expanduser().resolve()
    if not binary.is_file():
        raise FileNotFoundError(f"No existe el ejecutable: {binary}")
    if not config_path.is_file():
        raise FileNotFoundError(f"No existe la configuracion: {config_path}")
    original = json.loads(config_path.read_text(encoding="utf-8"))
    if not isinstance(original.get("fleet"), dict) or not isinstance(original.get("map"), dict):
        raise ValueError("La configuracion carece de fleet o map")
    map_path = Path(original["map"]["image"])
    if not map_path.is_absolute():
        map_path = config_path.parent / map_path
    map_path = map_path.resolve()
    if not map_path.is_file():
        raise FileNotFoundError(f"No se encuentra el mapa: {map_path}")
    original["map"]["image"] = str(map_path)
    signature = config_signature(original)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    root = args.output.expanduser().resolve() / f"{args.label}_{timestamp}"
    counter = 2
    while root.exists():
        root = args.output.expanduser().resolve() / f"{args.label}_{timestamp}_{counter}"
        counter += 1
    root.mkdir(parents=True)
    args.binary = binary
    revision = git_revision()
    metadata = {"label": args.label, "fleets": args.fleets, "duration": args.duration,
                "scale": args.scale, "repeats": args.repeats,
                "config_signature": signature, "source_config": str(config_path),
                "original_map": str(map_path), "binary": str(binary),
                "binary_sha256": sha256(binary), "git_head": revision,
                "started_at": timestamp}
    (root / "parametros.json").write_text(json.dumps(metadata, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    timeout = args.timeout or max(90, int(args.duration / args.scale * 6 + 60))
    rows = []
    total = len(args.fleets) * args.repeats
    print(f"DP10 {args.label.upper()} | {total} simulaciones | SALIDA: {root}", flush=True)
    try:
        for n in args.fleets:
            for repeat in range(1, args.repeats + 1):
                print(f"  -> [{len(rows)+1}/{total}] {n} motos, repeticion {repeat}...", flush=True)
                row = run_one(args, root, binary, original, n, repeat, timeout)
                rows.append(row)
                print(f"     creados={row['created']} asignados={row['assigned']} "
                      f"entregados={row['delivered']} rechazados={row['rejected']} "
                      f"cpu={row['cpu_s']:.2f}s p95={fmt(row['assignment_p95_sim_ms'])}ms", flush=True)
    finally:
        if rows:
            save_csv(root / "resultados_individuales.csv", rows, FIELDS)
    if len(rows) != total:
        raise RuntimeError("No se completaron todos los ensayos")
    aggregate_rows = aggregate(rows, args.fleets)
    save_csv(root / "resumen_por_flota.csv", aggregate_rows, list(aggregate_rows[0]))
    report(root, aggregate_rows, args, revision)
    print("\n=== RESULTADOS POR FLOTA ===")
    print("Motos | CPU(s) | CPU%  | Asignados | Entregados | Rechazados | P95 Asignacion (sim ms)")
    for group in aggregate_rows:
        print(f"{group['couriers']:>5} | {fmt(group['cpu_s_mean']):>6} | "
              f"{fmt(group['cpu_pct_one_core_mean']):>5} | "
              f"{fmt(group['assigned_mean'],1):>9} | "
              f"{fmt(group['delivered_mean'],1):>10} | "
              f"{fmt(group['rejected_mean'],1):>10} | "
              f"{fmt(group['assignment_p95_sim_ms_mean']):>23}")
    if args.compare:
        compare_with_baseline(args, root, aggregate_rows, metadata)
    print(f"\nRESULTADOS: {root}", flush=True)
    print("ARCHIVOS: parametros.json, informe.md, resultados_individuales.csv, "
          "resumen_por_flota.csv, carpetas con config.json + events.log + stdout.log + stderr.log", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"ERROR DP10: {error}", file=sys.stderr)
        sys.exit(1)
