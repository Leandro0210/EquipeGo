#!/usr/bin/env python3
"""DP8.6 - Ensayo reproducible de escalabilidad de flota para EquipeGo.

Ejecutar desde la raiz del repositorio en WSL:
    python3 benchmarks/dp8_6_scalability.py --fleets 2,8 --duration 120
    python3 benchmarks/dp8_6_scalability.py --fleets 1,2,8,16,32 --duration 900

Usa exclusivamente la biblioteca estandar de Python 3.9+.
No modifica el JSON original ni el codigo C++.
"""

import argparse
import csv
from datetime import datetime
import json
from pathlib import Path
import statistics
import subprocess
import sys
import time
from xml.sax.saxutils import escape

FLEETS_DEFAULT = "1,2,8,16,32"
TRIAL_FIELDS = [
    "couriers", "repeat", "duration_sim_s", "time_scale", "wall_s",
    "created", "delivered", "rejected", "pending", "breakdowns",
    "reassigned", "queue_full", "unreachable", "no_courier",
    "delivered_pct", "rejected_pct", "delivery_per_sim_hour",
    "avg_delivery_ms", "max_delivery_ms", "trial_directory",
]


def parse_fleets(raw):
    try:
        fleets = [int(v.strip()) for v in raw.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("Escribe enteros separados por comas") from error
    if not fleets or any(v < 1 or v > 128 for v in fleets):
        raise argparse.ArgumentTypeError("Cada cantidad de motos debe estar entre 1 y 128")
    if len(fleets) != len(set(fleets)):
        raise argparse.ArgumentTypeError("No repitas cantidades de motos")
    return fleets


def get_args():
    parser = argparse.ArgumentParser(
        description="DP8.6: compara distintas cantidades de motos usando delivery_sim real"
    )
    parser.add_argument("--fleets", type=parse_fleets, default=parse_fleets(FLEETS_DEFAULT))
    parser.add_argument("--duration", type=int, default=900,
                        help="Duracion en segundos simulados (predeterminado: 900)")
    parser.add_argument("--scale", type=float, default=60.0,
                        help="Aceleracion del reloj (predeterminado: 60)")
    parser.add_argument("--repeats", type=int, default=1,
                        help="Repeticiones por escenario (predeterminado: 1)")
    parser.add_argument("--config", type=Path, default=Path("config/equipetrol.json"))
    parser.add_argument("--binary", type=Path, default=Path.home() / "build-equipego/delivery_sim")
    parser.add_argument("--output", type=Path, default=Path.home() / "equipego_benchmarks/dp8_6")
    parser.add_argument("--timeout", type=int, default=0,
                        help="Timeout por simulacion en segundos reales (0 = automatico)")
    args = parser.parse_args()
    if not 1 <= args.duration <= 86400:
        parser.error("--duration debe estar entre 1 y 86400")
    if not 0.25 <= args.scale <= 120:
        parser.error("--scale debe estar entre 0.25 y 120")
    if not 1 <= args.repeats <= 20:
        parser.error("--repeats debe estar entre 1 y 20")
    if args.timeout < 0:
        parser.error("--timeout no puede ser negativo")
    return args


def validate_summary(summary, events_path, couriers):
    if not isinstance(summary, dict):
        raise ValueError("El resumen final no es un objeto JSON")
    required = ("created", "delivered", "rejected", "pending", "breakdowns",
                "reassigned", "avgDeliveryMs", "maxDeliveryMs", "deliveredPerCourier")
    if any(key not in summary for key in required):
        raise ValueError("Al resumen le faltan campos obligatorios de DP7")
    for field in ("created", "delivered", "rejected", "pending", "breakdowns", "reassigned"):
        number = summary[field]
        if type(number) is not int or number < 0:
            raise ValueError(f"Contador invalido: {field}={number}")
    if summary["created"] != summary["delivered"] + summary["rejected"] + summary["pending"]:
        raise ValueError("Balance incorrecto de pedidos")
    delivery_counts = summary["deliveredPerCourier"]
    if not isinstance(delivery_counts, dict) or len(delivery_counts) != couriers:
        raise ValueError("Cantidad de repartidores incorrecta en deliveredPerCourier")
    if sum(delivery_counts.values()) != summary["delivered"]:
        raise ValueError("Suma de entregas por repartidor incorrecta")

    counters = {name: 0 for name in (
        "orderCreated", "orderDelivered", "orderRejected",
        "courierBreakdown", "orderReassigned", "simulationStopping")}
    reasons = {"queueFull": 0, "unreachable": 0, "noCourier": 0}
    last_event = None
    with events_path.open("r", encoding="utf-8") as stream:
        for line in stream:
            event = json.loads(line)
            name = event.get("event")
            last_event = name
            if name in counters:
                counters[name] += 1
            if name == "orderRejected" and event.get("reason") in reasons:
                reasons[event["reason"]] += 1
    if last_event != "simulationStopping" or counters["simulationStopping"] != 1:
        raise ValueError("El ultimo evento no es simulationStopping")
    matches = {
        "orderCreated": "created", "orderDelivered": "delivered",
        "orderRejected": "rejected", "courierBreakdown": "breakdowns",
        "orderReassigned": "reassigned",
    }
    for event_name, summary_key in matches.items():
        if counters[event_name] != summary[summary_key]:
            raise ValueError(f"No coincide {event_name} entre JSONL y resumen")
    if sum(reasons.values()) != summary["rejected"]:
        raise ValueError("No coinciden los motivos de rechazo")
    return reasons


def run_trial(args, root, binary, original_config, number, repeat, deadline_s):
    folder = root / f"motos_{number:03d}" / f"repeticion_{repeat:02d}"
    folder.mkdir(parents=True, exist_ok=False)

    # La copia apunta a la imagen ORIGINAL mediante ruta absoluta.
    # El ConfigLoader resuelve map.image respecto del nuevo JSON.
    config = json.loads(json.dumps(original_config))
    config["fleet"]["couriers"] = number
    (folder / "config.json").write_text(
        json.dumps(config, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    command = [
        str(binary), str(folder / "config.json"),
        "--headless", "--scale", str(args.scale),
        "--duration", str(args.duration),
    ]
    (folder / "comando.txt").write_text(" ".join(command) + "\n", encoding="utf-8")
    stdout_file = folder / "stdout.log"
    stderr_file = folder / "stderr.log"

    started = time.perf_counter()
    try:
        with stdout_file.open("w", encoding="utf-8") as out, \
             stderr_file.open("w", encoding="utf-8") as err:
            result = subprocess.run(
                command, cwd=folder, stdout=out, stderr=err,
                timeout=deadline_s, check=False,
            )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"Timeout de {deadline_s}s en motos={number}, repeticion={repeat}") from error
    wall_s = time.perf_counter() - started
    if result.returncode != 0:
        raise RuntimeError(
            f"delivery_sim salio con codigo {result.returncode}; "
            f"revisa {stderr_file} y {stdout_file}"
        )
    with stdout_file.open("r", encoding="utf-8") as stream:
        nonempty = [line.strip() for line in stream if line.strip()]
    if not nonempty:
        raise ValueError("stdout vacio: no hay resumen final")
    try:
        summary = json.loads(nonempty[-1])
    except json.JSONDecodeError as error:
        raise ValueError("La ultima linea de stdout no es JSON valido") from error
    reasons = validate_summary(summary, folder / "events.log", number)
    created = summary["created"]
    row = {
        "couriers": number,
        "repeat": repeat,
        "duration_sim_s": args.duration,
        "time_scale": args.scale,
        "wall_s": round(wall_s, 3),
        "created": created,
        "delivered": summary["delivered"],
        "rejected": summary["rejected"],
        "pending": summary["pending"],
        "breakdowns": summary["breakdowns"],
        "reassigned": summary["reassigned"],
        "queue_full": reasons["queueFull"],
        "unreachable": reasons["unreachable"],
        "no_courier": reasons["noCourier"],
        "delivered_pct": round(100 * summary["delivered"] / created, 2) if created else 0,
        "rejected_pct": round(100 * summary["rejected"] / created, 2) if created else 0,
        "delivery_per_sim_hour": round(summary["delivered"] * 3600 / args.duration, 2),
        "avg_delivery_ms": round(float(summary["avgDeliveryMs"]), 2),
        "max_delivery_ms": summary["maxDeliveryMs"],
        "trial_directory": str(folder),
    }
    (folder / "resultado.json").write_text(
        json.dumps({"trial": row, "summary": summary, "rejection_reasons": reasons},
                   indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    return row


def save_csv(path, rows, fieldnames):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def aggregate(rows, fleets):
    result = []
    measures = [
        "wall_s", "created", "delivered", "rejected", "pending",
        "breakdowns", "reassigned", "queue_full", "unreachable", "no_courier",
        "delivered_pct", "rejected_pct", "delivery_per_sim_hour", "avg_delivery_ms",
    ]
    for number in fleets:
        items = [row for row in rows if row["couriers"] == number]
        entry = {"couriers": number, "repeats": len(items)}
        for key in measures:
            entry[key + "_mean"] = round(statistics.mean(item[key] for item in items), 3)
        entry["wall_s_stdev"] = round(statistics.stdev(item["wall_s"] for item in items), 3) if len(items) > 1 else 0
        result.append(entry)
    return result


def render_svg(entries, path):
    # Grafico 100% apilado: distribucion de estados finales por flota.
    width, left, right, top, row_height = 950, 118, 160, 105, 55
    plot_width = width - left - right
    height = top + len(entries) * row_height + 92
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-label="Distribucion de pedidos por cantidad de motos">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<text x="34" y="39" font-size="23" font-family="Arial" fill="#172735" font-weight="bold">EquipeGo · DP8.6 | Escalabilidad de flota</text>',
        '<text x="34" y="66" font-size="14" font-family="Arial" fill="#607386">Distribucion porcentual promedio al terminar cada simulacion</text>',
    ]
    for index, entry in enumerate(entries):
        y = top + index * row_height
        delivered = entry["delivered_mean"]
        rejected = entry["rejected_mean"]
        pending = entry["pending_mean"]
        total = delivered + rejected + pending
        parts.append(f'<text x="24" y="{y + 21}" font-size="16" font-family="Arial" font-weight="bold" fill="#243746">{entry["couriers"]} motos</text>')
        x = left
        for amount, color in ((delivered, "#248567"), (rejected, "#dc725e"), (pending, "#96a8ba")):
            w = plot_width * amount / total if total else 0
            parts.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="30" fill="{color}"/>')
            x += w
        parts.append(f'<text x="{left + plot_width + 14}" y="{y + 21}" font-size="13" font-family="Arial" fill="#314659">{entry["delivered_pct_mean"]:.1f}% entregados</text>')
    legends = [("Entregados", "#248567"), ("Rechazados", "#dc725e"), ("Pendientes", "#96a8ba")]
    for i, (name, color) in enumerate(legends):
        x = left + i * 210
        y = height - 44
        parts.append(f'<rect x="{x}" y="{y}" width="15" height="15" fill="{color}"/>')
        parts.append(f'<text x="{x+23}" y="{y+13}" font-family="Arial" font-size="14" fill="#324b60">{escape(name)}</text>')
    parts.append('</svg>')
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")


def write_report(folder, entries, args):
    lines = [
        "# EquipeGo — DP8.6 | Escalabilidad de flota", "",
        f"- Duracion por ejecucion: **{args.duration} s simulados**.",
        f"- Escala de tiempo: **{args.scale}x**.",
        f"- Repeticiones por flota: **{args.repeats}**.",
        "- Se mantiene el mismo generador, semilla, restaurantes y probabilidad de averias.",
        "- Cada escenario termina al vencer la duracion (no espera vaciar la cola).", "",
        "| Motos | Creados | Entregados | Rechazados | Pendientes | Entrega % | Rechazo % | Duracion real (s) |",
        "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for e in entries:
        lines.append(
            f"| {e['couriers']} | {e['created_mean']:.1f} | {e['delivered_mean']:.1f} | "
            f"{e['rejected_mean']:.1f} | {e['pending_mean']:.1f} | "
            f"{e['delivered_pct_mean']:.1f}% | {e['rejected_pct_mean']:.1f}% | "
            f"{e['wall_s_mean']:.2f} |"
        )
    lines += [
        "", "## Interpretacion", "",
        "- `Entregados / creados` mide la fraccion completada ANTES del corte, no la tasa final de exito de la cola.",
        "- `Pendientes` incluye trabajos en proceso al acabar la duracion; no equivale a pedidos perdidos.",
        "- Una mayor cantidad de motos no prueba por si sola speedup paralelo: para eso medir ETA con 1, 2, 4 y 8 workers.",
        "- Los porcentajes son promedios de cada repeticion; revisa `resultados_individuales.csv` para la variabilidad.",
        "", "![Comparacion de resultados](comparacion.svg)", "",
    ]
    (folder / "informe.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    args = get_args()
    config_path = args.config.expanduser().resolve()
    binary = args.binary.expanduser().resolve()
    if not config_path.is_file():
        raise FileNotFoundError(f"No existe la configuracion: {config_path}")
    if not binary.is_file():
        raise FileNotFoundError(f"No existe el ejecutable: {binary}. Primero compila con CMake.")
    with config_path.open("r", encoding="utf-8") as source:
        original_config = json.load(source)
    if not isinstance(original_config.get("fleet"), dict):
        raise ValueError("La configuracion no tiene fleet")
    if not isinstance(original_config.get("map"), dict):
        raise ValueError("La configuracion no tiene map")
    image_path = Path(original_config["map"]["image"])
    if not image_path.is_absolute():
        image_path = config_path.parent / image_path
    image_path = image_path.resolve()
    if not image_path.is_file():
        raise FileNotFoundError(f"No se encuentra el mapa original: {image_path}")
    # Solo en las COPIAS, para que la imagen cargue desde una carpeta temporal.
    original_config["map"]["image"] = str(image_path)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    base = args.output.expanduser().resolve()
    root = base / timestamp
    counter = 1
    while root.exists():
        root = base / f"{timestamp}_{counter}"
        counter += 1
    root.mkdir(parents=True)
    (root / "parametros.json").write_text(
        json.dumps({"fleets": args.fleets, "duration": args.duration,
                    "scale": args.scale, "repeats": args.repeats,
                    "source_config": str(config_path), "binary": str(binary)},
                   indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    deadline_s = args.timeout or max(90, int(args.duration / args.scale * 6 + 60))
    rows = []
    total = len(args.fleets) * args.repeats
    print(f"DP8.6 | {total} simulaciones | Salida: {root}", flush=True)
    try:
        for couriers in args.fleets:
            for repeat in range(1, args.repeats + 1):
                print(f"  -> [{len(rows)+1}/{total}] {couriers} motos, repeticion {repeat}...", flush=True)
                trial = run_trial(args, root, binary, original_config, couriers, repeat, deadline_s)
                rows.append(trial)
                print(f"     creados={trial['created']} entregados={trial['delivered']} "
                      f"rechazados={trial['rejected']} pendientes={trial['pending']} "
                      f"real={trial['wall_s']:.1f}s", flush=True)
    finally:
        if rows:
            save_csv(root / "resultados_individuales.csv", rows, TRIAL_FIELDS)
    if len(rows) != total:
        raise RuntimeError("No se completaron todas las simulaciones")
    grouped = aggregate(rows, args.fleets)
    save_csv(root / "resumen_por_flota.csv", grouped, list(grouped[0].keys()))
    render_svg(grouped, root / "comparacion.svg")
    write_report(root, grouped, args)
    print("\n=== RESUMEN POR FLOTA (PROMEDIOS) ===")
    print("Motos | Creados | Entregados | Rechazados | Pendientes | %Entrega | Real(s)")
    for entry in grouped:
        print(f"{entry['couriers']:>5} | {entry['created_mean']:>7.1f} | "
              f"{entry['delivered_mean']:>9.1f} | {entry['rejected_mean']:>10.1f} | "
              f"{entry['pending_mean']:>10.1f} | {entry['delivered_pct_mean']:>7.1f}% | "
              f"{entry['wall_s_mean']:>7.2f}")
    print(f"\nRESULTADOS: {root}")
    print("ARCHIVOS: informe.md, comparacion.svg, resumen_por_flota.csv, resultados_individuales.csv")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"ERROR DP8.6: {error}", file=sys.stderr)
        sys.exit(1)
