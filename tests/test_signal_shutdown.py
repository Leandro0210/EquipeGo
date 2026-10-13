#!/usr/bin/env python3
"""DP9.1: integracion SIGINT/SIGTERM con el ejecutable real (Linux/WSL).

No toca config/equipetrol.json ni events.log del proyecto. Cada ejecucion
utiliza un directorio temporal propio y --log para aislar el JSONL.
"""

import argparse
import json
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def verify_case(binary: Path, config: Path, name: str, sig: int) -> None:
    with tempfile.TemporaryDirectory(prefix=f"equipego_dp91_{name.lower()}_") as folder:
        base = Path(folder)
        stdout_path = base / "stdout.txt"
        stderr_path = base / "stderr.txt"
        events_path = base / "events.jsonl"
        command = [
            str(binary), str(config), "--headless", "--scale", "60",
            "--duration", "0" if sig == signal.SIGTERM else "3600",
            "--log", str(events_path),
        ]
        with stdout_path.open("w", encoding="utf-8") as out, stderr_path.open(
            "w", encoding="utf-8"
        ) as err:
            child = subprocess.Popen(command, stdout=out, stderr=err)
            try:
                # Nunca se envia una senal antes del mensaje de inicio.
                start_deadline = time.monotonic() + 15.0
                while time.monotonic() < start_deadline:
                    if child.poll() is not None:
                        raise AssertionError(
                            f"{name}: simulador termino antes de la senal "
                            f"(exit={child.returncode}): {stderr_path.read_text()}"
                        )
                    if "Simulation has started..." in stdout_path.read_text(
                        encoding="utf-8", errors="replace"
                    ):
                        break
                    time.sleep(0.025)
                else:
                    raise AssertionError(f"{name}: no imprimio la linea de inicio")

                # Tiempo suficiente para que el productor y dispatcher hagan trabajo.
                time.sleep(0.5)
                when_sent = time.monotonic()
                child.send_signal(sig)
                try:
                    exit_code = child.wait(timeout=5.0)
                except subprocess.TimeoutExpired as exc:
                    raise AssertionError(
                        f"{name}: no finalizo dentro de los 5 segundos reales"
                    ) from exc
                duration = time.monotonic() - when_sent
                assert exit_code == 0, (
                    f"{name}: exit={exit_code}, stderr: {stderr_path.read_text()}"
                )
                assert duration <= 5.0, f"{name}: cierre tardo {duration:.3f}s"
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=5.0)

        lines = [
            line.strip()
            for line in stdout_path.read_text(encoding="utf-8").splitlines()
            if line.strip()
        ]
        assert lines and lines[0] == "Simulation has started...", (
            f"{name}: linea inicial incorrecta"
        )
        summary = json.loads(lines[-1])
        assert "event" not in summary, f"{name}: no hay resumen JSON final"

        events = [
            json.loads(line)
            for line in events_path.read_text(encoding="utf-8").splitlines()
            if line.strip()
        ]
        assert events, f"{name}: JSONL sin eventos"
        assert events[-1]["event"] == "simulationStopping", (
            f"{name}: ultimo evento distinto de simulationStopping"
        )
        assert sum(event["event"] == "simulationStopping" for event in events) == 1

        counts = {
            "created": "orderCreated",
            "delivered": "orderDelivered",
            "rejected": "orderRejected",
            "reassigned": "orderReassigned",
            "breakdowns": "courierBreakdown",
        }
        for field, event_type in counts.items():
            actual = sum(event["event"] == event_type for event in events)
            assert summary[field] == actual, (
                f"{name}: {field}={summary[field]} pero JSONL={actual}"
            )

        assert (
            summary["created"]
            == summary["delivered"] + summary["rejected"] + summary["pending"]
        ), f"{name}: balance de pedidos inconsistente"
        assert sum(summary["deliveredPerCourier"].values()) == summary["delivered"]
        assert len(summary["deliveredPerCourier"]) == 8

        # La salida serializada en stdout debe concordar con el JSONL.
        stdout_events = [json.loads(line) for line in lines[1:-1]]
        assert stdout_events == events, f"{name}: stdout y JSONL difieren"
        print(
            f"OK {name}: exit=0, cierre={duration:.3f}s, "
            f"creados={summary['created']}, entregados={summary['delivered']}, "
            f"pendientes={summary['pending']}, "
            "simulationStopping=1, JSONL consistente",
            flush=True,
        )


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--binary", type=Path, default=Path.home() / "build-equipego" / "delivery_sim"
    )
    parser.add_argument(
        "--config", type=Path, default=root / "config" / "equipetrol.json"
    )
    arguments = parser.parse_args()
    binary = arguments.binary.expanduser().resolve()
    config = arguments.config.expanduser().resolve()
    if not binary.is_file():
        parser.error(f"No existe el ejecutable: {binary}")
    if not config.is_file():
        parser.error(f"No existe la configuracion: {config}")
    for name, sig in (("SIGINT", signal.SIGINT), ("SIGTERM", signal.SIGTERM)):
        verify_case(binary, config, name, sig)
    print("DP9.1: 2/2 pruebas de apagado por senal aprobadas")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, OSError, json.JSONDecodeError) as error:
        print(f"FALLO DP9.1: {error}", file=sys.stderr)
        sys.exit(1)
