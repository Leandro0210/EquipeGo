#!/usr/bin/env python3
"""EquipeGo DP8.6 - Demanda baja, media, alta y extrema (8 motos fijas).

Colocar junto a dp8_6_scalability.py en benchmarks/ y ejecutar desde
la raiz del repositorio:

  python3 benchmarks/dp8_6_demand.py --scenarios baja,extrema --duration 180
  python3 benchmarks/dp8_6_demand.py --duration 900 --scale 60 --repeats 3

Utiliza el ejecutor/validador del miniavance anterior; no cambia C++ ni JSON
originales. Python 3.9+ y biblioteca estandar solamente.
"""

import argparse
import copy
import csv
from datetime import datetime
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys
from xml.sax.saxutils import escape

import dp8_6_scalability as base

# Cada escenario modifica EXCLUSIVAMENTE estos dos parametros de orders.
# La semilla seed, las 8 motos, las calles, los restaurantes y el resto del
# archivo JSON permanecen iguales.
SCENARIOS = {
    "baja": {"meanIntervalMs": 120000, "burstMax": 1},
    "media": {"meanIntervalMs": 45000, "burstMax": 2},
    "alta": {"meanIntervalMs": 20000, "burstMax": 4},
    "extrema": {"meanIntervalMs": 20000, "burstMax": 12},
}

COLUMNS = [
    "scenario", "mean_interval_ms", "burst_max", *base.TRIAL_FIELDS
]


def parse_scenarios(raw):
    names = [s.strip().lower() for s in raw.split(",")]
    if not names or any(s not in SCENARIOS for s in names):
        raise argparse.ArgumentTypeError(
            "Escenarios validos: baja,media,alta,extrema"
        )
    if len(set(names)) != len(names):
        raise argparse.ArgumentTypeError("No repitas escenarios")
    return names


def get_args():
    parser = argparse.ArgumentParser(
        description="DP8.6: comparar niveles de demanda manteniendo 8 motos"
    )
    parser.add_argument("--scenarios", type=parse_scenarios,
                        default=list(SCENARIOS))
    parser.add_argument("--duration", type=int, default=900,
                        help="Duracion en segundos simulados (900 por defecto)")
    parser.add_argument("--scale", type=float, default=60.0,
                        help="Escala de reloj (60 por defecto)")
    parser.add_argument("--repeats", type=int, default=1,
                        help="Repeticiones por escenario (1 por defecto)")
    parser.add_argument("--couriers", type=int, default=8,
                        help="Flota fija durante este experimento (8 por defecto)")
    parser.add_argument("--config", type=Path,
                        default=Path("config/equipetrol.json"))
    parser.add_argument("--binary", type=Path,
                        default=Path.home() / "build-equipego/delivery_sim")
    parser.add_argument("--output", type=Path,
                        default=Path.home() / "equipego_benchmarks/dp8_6")
    parser.add_argument("--timeout", type=int, default=0,
                        help="Timeout por ejecucion (0 = automatico)")
    args = parser.parse_args()
    if not 1 <= args.duration <= 86400:
        parser.error("--duration debe estar entre 1 y 86400")
    if not math.isfinite(args.scale) or not 0.25 <= args.scale <= 120:
        parser.error("--scale debe estar entre 0.25 y 120")
    if not 1 <= args.repeats <= 20:
        parser.error("--repeats debe estar entre 1 y 20")
    if not 1 <= args.couriers <= 128:
        parser.error("--couriers debe estar entre 1 y 128")
    if args.timeout < 0:
        parser.error("--timeout no puede ser negativo")
    return args


def load_original_config(config_path):
    with config_path.open("r", encoding="utf-8") as stream:
        config = json.load(stream)
    for section in ("fleet", "orders", "map"):
        if not isinstance(config.get(section), dict):
            raise ValueError(f"Configuracion sin seccion {section}")
    if "image" not in config["map"]:
        raise ValueError("La seccion map no contiene image")
    image = Path(config["map"]["image"])
    if not image.is_absolute():
        image = config_path.parent / image
    image = image.resolve()
    if not image.is_file():
        raise FileNotFoundError(f"No se encontro el mapa original: {image}")
    # La ruta absoluta es necesaria: el runner ejecuta cada copia desde
    # un directorio aislado por escenario y repeticion.
    config["map"]["image"] = str(image)
    return config


def safe_average(values):
    return round(statistics.mean(values), 3)


def collect_means(rows, scenario_names):
    metrics = [
        "wall_s", "created", "delivered", "rejected", "pending",
        "breakdowns", "reassigned", "queue_full", "unreachable",
        "no_courier", "delivered_pct", "rejected_pct",
        "delivery_per_sim_hour", "avg_delivery_ms", "max_delivery_ms",
    ]
    entries = []
    for name in scenario_names:
        subset = [r for r in rows if r["scenario"] == name]
        spec = SCENARIOS[name]
        entry = {
            "scenario": name,
            "mean_interval_ms": spec["meanIntervalMs"],
            "burst_max": spec["burstMax"],
            "repeats": len(subset),
        }
        for metric in metrics:
            values = [r[metric] for r in subset]
            entry[metric + "_mean"] = safe_average(values)
            entry[metric + "_stdev"] = round(statistics.stdev(values), 3) if len(values) > 1 else 0.0
        entries.append(entry)
    return entries


def write_csv(path, rows, columns):
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)


def create_chart(entries, destination):
    """Grafico SVG porcentual con barras de estados por escenario."""
    width = 980
    left = 150
    plot_width = 620
    top = 105
    step = 65
    height = top + step * len(entries) + 85
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-label="Demanda y resultados de EquipeGo">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<text x="35" y="38" font-size="22" font-family="Arial" fill="#172735" font-weight="bold">EquipeGo - DP8.6 | Demanda (8 motos)</text>',
        '<text x="35" y="65" font-size="14" font-family="Arial" fill="#607386">Distribucion promedio de pedidos al finalizar el tiempo simulado</text>',
    ]
    for i, entry in enumerate(entries):
        y = top + step * i
        created = entry["created_mean"]
        parts.append(
            f'<text x="25" y="{y+20}" font-size="16" font-family="Arial" fill="#203748">{escape(entry["scenario"].title())}</text>'
        )
        x = left
        for metric, color in (
            ("delivered_mean", "#27886b"),
            ("rejected_mean", "#d66b58"),
            ("pending_mean", "#9aabb9"),
        ):
            value = entry[metric]
            span = plot_width * value / created if created else 0.0
            parts.append(
                f'<rect x="{x:.2f}" y="{y}" width="{span:.2f}" height="30" fill="{color}"/>'
            )
            x += span
        parts.append(
            f'<text x="{left+plot_width+12}" y="{y+20}" font-size="13" font-family="Arial" fill="#203748">{created:.1f} pedidos</text>'
        )
    for i, (label, color) in enumerate([
        ("Entregados", "#27886b"),
        ("Rechazados", "#d66b58"),
        ("Pendientes", "#9aabb9"),
    ]):
        x = left + i * 210
        y = height - 36
        parts.append(f'<rect x="{x}" y="{y}" width="15" height="15" fill="{color}"/>')
        parts.append(
            f'<text x="{x+22}" y="{y+13}" font-size="13" font-family="Arial" fill="#314558">{label}</text>'
        )
    parts.append('</svg>')
    destination.write_text("\n".join(parts) + "\n", encoding="utf-8")


def write_report(root, entries, args):
    lines = [
        "# EquipeGo | DP8.6 - Escenarios de demanda", "",
        "## Condiciones del experimento", "",
        f"- Flota fija: **{args.couriers} motos**.",
        f"- Tiempo: **{args.duration} segundos simulados**.",
        f"- Escala: **{args.scale}x**.",
        f"- Repeticiones por escenario: **{args.repeats}**.",
        "- Misma semilla, mapa, restaurantes, velocidad, cola e incidencias en todas las pruebas.",
        "- Solo cambian `orders.meanIntervalMs` y `orders.burstMax` en copias del JSON.",
        "- `pendientes` significa sin terminar al corte; no implica pedidos perdidos.", "",
        "| Demanda | Intervalo medio (ms) | Rafaga max. | Creados | Entregados | Rechazados | Pendientes | % Entrega | Rechazo por cola |",
        "| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for e in entries:
        lines.append(
            f'| {e["scenario"]} | {e["mean_interval_ms"]} | {e["burst_max"]} | '
            f'{e["created_mean"]:.1f} | {e["delivered_mean"]:.1f} | '
            f'{e["rejected_mean"]:.1f} | {e["pending_mean"]:.1f} | '
            f'{e["delivered_pct_mean"]:.1f}% | {e["queue_full_mean"]:.1f} |'
        )
    lines.extend([
        "", "## Interpretacion", "",
        "- Compara `queue_full_mean` para detectar saturacion de la cola.",
        "- La cantidad de pedidos creados varia a proposito entre escenarios: no comparar solamente cantidades absolutas.",
        "- Considera % entrega, % rechazo y entregas por hora simulada junto con el volumen creado.",
        "- La fraccion entregada al corte no representa necesariamente el porcentaje final si se dejara drenar la cola.",
        "- Este ensayo caracteriza la demanda y capacidad; NO mide el speedup del calculo ETA en CPU.",
        "- La semilla fija favorece la reproducibilidad, pero no garantiza determinismo de todos los interleavings concurrentes.",
        "", "![Demanda comparada](comparacion_demanda.svg)", "",
    ])
    (root / "informe.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    args = get_args()
    source_config = args.config.expanduser().resolve()
    binary = args.binary.expanduser().resolve()
    if not source_config.is_file():
        raise FileNotFoundError(f"Falta configuracion: {source_config}")
    if not binary.is_file():
        raise FileNotFoundError(f"Falta ejecutable: {binary}. Compila primero con CMake")

    original = load_original_config(source_config)
    original["fleet"]["couriers"] = args.couriers

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_dir = args.output.expanduser().resolve()
    root = output_dir / f"demanda_{timestamp}"
    version = 1
    while root.exists():
        root = output_dir / f"demanda_{timestamp}_{version}"
        version += 1
    root.mkdir(parents=True)
    settings = {
        "couriers": args.couriers,
        "scenarios": {name: SCENARIOS[name] for name in args.scenarios},
        "duration": args.duration,
        "time_scale": args.scale,
        "repeats": args.repeats,
        "source_config": str(source_config),
        "binary": str(binary),
    }
    (root / "parametros.json").write_text(
        json.dumps(settings, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    deadline_s = args.timeout or max(90, int(args.duration / args.scale * 6 + 60))
    total = len(args.scenarios) * args.repeats
    rows = []
    print(f"DP8.6 DEMANDA | {total} simulaciones | 8 motos fijas" if args.couriers == 8
          else f"DP8.6 DEMANDA | {total} simulaciones | {args.couriers} motos fijas", flush=True)
    print(f"Salida: {root}", flush=True)
    try:
        for scenario in args.scenarios:
            config = copy.deepcopy(original)
            config["orders"].update(SCENARIOS[scenario])
            for repeat in range(1, args.repeats + 1):
                number = len(rows) + 1
                print(f"  -> [{number}/{total}] {scenario}, repeticion {repeat}...", flush=True)
                trial = base.run_trial(
                    args, root / f"escenario_{scenario}", binary,
                    config, args.couriers, repeat, deadline_s
                )
                trial = {
                    "scenario": scenario,
                    "mean_interval_ms": SCENARIOS[scenario]["meanIntervalMs"],
                    "burst_max": SCENARIOS[scenario]["burstMax"],
                    **trial,
                }
                rows.append(trial)
                print(
                    f"     creados={trial['created']} entregados={trial['delivered']} "
                    f"rechazados={trial['rejected']} pendientes={trial['pending']} "
                    f"colaLlena={trial['queue_full']} real={trial['wall_s']:.1f}s",
                    flush=True,
                )
    finally:
        if rows:
            write_csv(root / "resultados_individuales.csv", rows, COLUMNS)

    if len(rows) != total:
        raise RuntimeError("No se completaron todas las simulaciones")
    entries = collect_means(rows, args.scenarios)
    write_csv(root / "resumen_por_demanda.csv", entries, list(entries[0].keys()))
    create_chart(entries, root / "comparacion_demanda.svg")
    write_report(root, entries, args)

    print("\n=== RESUMEN POR DEMANDA (PROMEDIOS) ===")
    print("Demanda  | Creados | Entregados | Rechazados | Pendientes | %Entrega | Cola llena")
    for e in entries:
        print(
            f"{e['scenario']:<8} | {e['created_mean']:>7.1f} | "
            f"{e['delivered_mean']:>9.1f} | {e['rejected_mean']:>10.1f} | "
            f"{e['pending_mean']:>10.1f} | {e['delivered_pct_mean']:>7.1f}% | "
            f"{e['queue_full_mean']:>10.1f}"
        )
    print(f"\nRESULTADOS: {root}")
    print("ARCHIVOS: informe.md, comparacion_demanda.svg, resumen_por_demanda.csv, resultados_individuales.csv")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"ERROR DP8.6 DEMANDA: {error}", file=sys.stderr)
        sys.exit(1)
