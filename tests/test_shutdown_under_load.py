#!/usr/bin/env python3
"""DP9.2 - Pruebas de SIGINT/SIGTERM con pedidos reales y averias.

Se ejecuta desde la raiz de EquipeGo o desde cualquier directorio.
No modifica config/equipetrol.json ni los JSONL originales.
Guarda evidencia completa fuera del repositorio en ~/equipego_benchmarks/dp9_2/.
Python 3, solo biblioteca estandar, Linux/WSL.
"""

from __future__ import annotations

import argparse
import collections
import datetime as dt
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
from typing import Any


CASES = (
    {
        "name": "sigint_trabajo_real",
        "signal": signal.SIGINT,
        "wait_seconds": 5.0,
        "requirements": {"orderCreated": 15, "orderAssigned": 1,
                         "orderDelivered": 1},
        "changes": {},
    },
    {
        "name": "sigterm_estres_averias",
        "signal": signal.SIGTERM,
        "wait_seconds": 6.0,
        "requirements": {"orderCreated": 30, "orderAssigned": 1,
                         "courierBreakdown": 1},
        "changes": {
            "fleet.bagCapacity": 1,
            "orders.meanIntervalMs": 5000,
            "orders.burstMax": 12,
            "orders.maxPending": 20,
            "incidents.breakdownProbability": 1.0,
        },
    },
)


def prepare_config(src: Path, dst: Path, changes: dict[str, Any]) -> dict[str, Any]:
    config = json.loads(src.read_text(encoding="utf-8"))
    # El cargador interpreta map.image relativo al archivo JSON. Como el JSON
    # de prueba vive en otra carpeta, convertimos la ruta a absoluta.
    image = Path(config["map"]["image"])
    config["map"]["image"] = str(
        (image if image.is_absolute() else src.parent / image).resolve()
    )
    for dotted_key, value in changes.items():
        section, key = dotted_key.split(".", 1)
        config[section][key] = value
    dst.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n",
                   encoding="utf-8")
    return config


def validate_run(name: str, stdout_path: Path, stderr_path: Path,
                 events_path: Path, courier_count: int) -> dict[str, Any]:
    raw_lines = stdout_path.read_text(encoding="utf-8").splitlines()
    assert raw_lines and raw_lines[0] == "Simulation has started...", (
        f"{name}: no se encontro la linea exacta de inicio"
    )
    assert len(raw_lines) >= 3, f"{name}: faltan eventos o resumen JSON"
    try:
        summary = json.loads(raw_lines[-1])
        emitted = [json.loads(line) for line in raw_lines[1:-1]]
        recorded = [json.loads(line) for line in events_path.read_text(
            encoding="utf-8").splitlines() if line.strip()]
    except (OSError, ValueError) as exc:
        raise AssertionError(f"{name}: salida JSONL invalida: {exc}") from exc
    assert isinstance(summary, dict) and "created" in summary, (
        f"{name}: ultima linea no es el resumen"
    )
    assert emitted == recorded, f"{name}: stdout y el archivo JSONL son distintos"
    assert recorded and recorded[-1].get("event") == "simulationStopping", (
        f"{name}: simulationStopping no es el ultimo evento"
    )
    counts = collections.Counter(event.get("event") for event in recorded)
    assert counts["simulationStopping"] == 1, (
        f"{name}: simulationStopping debe aparecer exactamente una vez"
    )
    for key, event_type in (
        ("created", "orderCreated"),
        ("delivered", "orderDelivered"),
        ("rejected", "orderRejected"),
        ("reassigned", "orderReassigned"),
        ("breakdowns", "courierBreakdown"),
    ):
        assert summary[key] == counts[event_type], (
            f"{name}: contador {key}={summary[key]}, JSONL={counts[event_type]}"
        )
    assert summary["created"] == (summary["delivered"] +
                                  summary["rejected"] + summary["pending"]), (
        f"{name}: balance de pedidos incorrecto"
    )
    per_courier = summary["deliveredPerCourier"]
    assert len(per_courier) == courier_count, (
        f"{name}: repartidores esperados={courier_count}, encontrados={len(per_courier)}"
    )
    assert sum(per_courier.values()) == summary["delivered"], (
        f"{name}: suma de entregas por repartidor invalida"
    )
    created_ids = set()
    delivered_ids = set()
    rejected_ids = set()
    last_times: dict[str, int] = {}
    for event in recorded:
        assert isinstance(event.get("t"), int) and event["t"] >= 0, (
            f"{name}: tiempo del evento invalido"
        )
        order = event.get("order")
        if order is not None:
            assert event["t"] >= last_times.get(order, -1), (
                f"{name}: tiempo no monotono del pedido {order}"
            )
            last_times[order] = event["t"]
        if event["event"] == "orderCreated":
            assert order not in created_ids, f"{name}: pedido creado dos veces"
            created_ids.add(order)
        elif event["event"] == "orderDelivered":
            assert order in created_ids and order not in delivered_ids, (
                f"{name}: entrega invalida o duplicada"
            )
            delivered_ids.add(order)
        elif event["event"] == "orderRejected":
            assert order in created_ids and order not in rejected_ids, (
                f"{name}: rechazo invalido o duplicado"
            )
            rejected_ids.add(order)
    assert not (delivered_ids & rejected_ids), (
        f"{name}: pedidos entregados y rechazados simultaneamente"
    )
    return {"summary": summary, "event_counts": dict(counts),
            "stderr_bytes": stderr_path.stat().st_size}


def run_case(binary: Path, original: Path, root: Path,
             definition: dict[str, Any], scale: float,
             max_wait: float) -> dict[str, Any]:
    name = definition["name"]
    case_dir = root / name
    case_dir.mkdir(parents=True, exist_ok=True)
    config_path = case_dir / "config.json"
    out_path = case_dir / "stdout.txt"
    err_path = case_dir / "stderr.txt"
    events_path = case_dir / "events.jsonl"
    config = prepare_config(original, config_path, definition["changes"])
    command = [str(binary), str(config_path), "--headless",
               "--duration", "0", "--scale", str(scale),
               "--log", str(events_path)]
    print(f"  -> {name}: iniciando; esperando eventos reales...", flush=True)
    started = time.monotonic()
    reached = False
    observed = collections.Counter()
    startup_seen = False
    signaled_at = None
    exit_code = None
    shutdown_s = None
    error = None
    with out_path.open("w", encoding="utf-8") as out, err_path.open(
            "w", encoding="utf-8") as err:
        child = subprocess.Popen(command, stdout=out, stderr=err)
        try:
            with out_path.open("r", encoding="utf-8", errors="replace") as reader:
                carry = ""
                while time.monotonic() - started < max_wait:
                    if child.poll() is not None:
                        raise AssertionError(
                            f"{name}: termino antes de enviar la senal (exit={child.returncode})"
                        )
                    chunk = reader.read()
                    if chunk:
                        carry += chunk
                        while "\n" in carry:
                            line, carry = carry.split("\n", 1)
                            if line == "Simulation has started...":
                                startup_seen = True
                            elif line.strip():
                                try:
                                    obj = json.loads(line)
                                except ValueError as exc:
                                    raise AssertionError(
                                        f"{name}: stdout no JSON: {line[:120]}"
                                    ) from exc
                                if isinstance(obj, dict) and "event" in obj:
                                    observed[obj["event"]] += 1
                    elapsed = time.monotonic() - started
                    if startup_seen and elapsed >= definition["wait_seconds"] and all(
                        observed[event] >= needed
                        for event, needed in definition["requirements"].items()
                    ):
                        reached = True
                        break
                    time.sleep(0.05)
                if not reached:
                    raise AssertionError(
                        f"{name}: no se alcanzo la carga esperada dentro de "
                        f"{max_wait:.0f}s; observados={dict(observed)}, "
                        f"requeridos={definition['requirements']}"
                    )
            signaled_at = time.monotonic()
            child.send_signal(definition["signal"])
            try:
                exit_code = child.wait(timeout=5.0)
            except subprocess.TimeoutExpired as exc:
                raise AssertionError(
                    f"{name}: cierre excedio 5s despues de la senal"
                ) from exc
            shutdown_s = time.monotonic() - signaled_at
            assert exit_code == 0, (
                f"{name}: codigo de salida={exit_code}, se esperaba 0"
            )
            assert shutdown_s <= 5.0, (
                f"{name}: cierre tardo {shutdown_s:.3f}s (limite 5s)"
            )
        except Exception as exc:
            error = str(exc)
            raise
        finally:
            if child.poll() is None:
                child.kill()
                child.wait(timeout=5.0)
            (case_dir / "metadata.json").write_text(
                json.dumps({"command": command, "signal": definition["signal"],
                            "elapsed_before_signal_s": round(time.monotonic()-started, 3)
                            if signaled_at is None else round(signaled_at-started, 3),
                            "observed_before_signal": dict(observed),
                            "shutdown_s": shutdown_s, "exit_code": child.returncode,
                            "error": error}, indent=2) + "\n", encoding="utf-8"
            )
    verified = validate_run(name, out_path, err_path, events_path,
                            config["fleet"]["couriers"])
    result = {
        "case": name,
        "signal": "SIGINT" if definition["signal"] == signal.SIGINT else "SIGTERM",
        "elapsed_before_signal_s": round(signaled_at - started, 3),
        "shutdown_s": round(shutdown_s, 3),
        "observed_before_signal": dict(observed),
        **verified,
    }
    (case_dir / "resultado.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(f"     OK: creados={result['summary']['created']}, "
          f"entregados={result['summary']['delivered']}, "
          f"rechazados={result['summary']['rejected']}, "
          f"averias={result['summary']['breakdowns']}, "
          f"cierre={shutdown_s:.3f}s, exit=0", flush=True)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--binary", type=Path,
                        default=Path.home() / "build-equipego" / "delivery_sim")
    parser.add_argument("--config", type=Path, default=root / "config" / "equipetrol.json")
    parser.add_argument("--output", type=Path,
                        default=Path.home() / "equipego_benchmarks" / "dp9_2")
    parser.add_argument("--scale", type=float, default=60.0)
    parser.add_argument("--max-wait", type=float, default=22.0)
    parser.add_argument("--cases", choices=["all", "sigint", "sigterm"], default="all")
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        parser.error("Esta prueba requiere Linux/WSL (senales POSIX)")
    if args.scale <= 0.0:
        parser.error("--scale debe ser positivo")
    if args.max_wait < 7.0:
        parser.error("--max-wait debe ser >= 7 segundos")
    binary = args.binary.expanduser().resolve()
    config = args.config.expanduser().resolve()
    if not binary.is_file() or not config.is_file():
        parser.error(f"Falta binario ({binary}) o configuracion ({config})")
    chosen = [c for c in CASES if args.cases == "all" or
              c["name"].startswith(args.cases)]
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    destination = args.output.expanduser().resolve() / stamp
    destination.mkdir(parents=True, exist_ok=False)
    print(f"DP9.2 | {len(chosen)} pruebas con trabajo real | salida: {destination}")
    results = []
    for definition in chosen:
        try:
            results.append(run_case(binary, config, destination, definition,
                                    args.scale, args.max_wait))
        except (AssertionError, OSError, ValueError, subprocess.SubprocessError) as exc:
            print(f"FALLO DP9.2 ({definition['name']}): {exc}", file=sys.stderr)
            print(f"Conserva la carpeta para diagnostico: {destination}",
                  file=sys.stderr)
            return 1
    headings = "| Caso | Creados | Entregados | Rechazados | Averias | Cierre (s) |"
    lines = ["# DP9.2 — Apagado con trabajo real", "",
             "Duracion configurada: 0 (hasta senal); modo headless para pruebas.",
             "", headings, "|---|---:|---:|---:|---:|---:|"]
    for result in results:
        s = result["summary"]
        lines.append(f"| {result['case']} | {s['created']} | {s['delivered']} | "
                     f"{s['rejected']} | {s['breakdowns']} | {result['shutdown_s']:.3f} |")
    (destination / "informe.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"DP9.2: {len(results)}/{len(results)} pruebas superadas. "
          f"Evidencias: {destination}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
