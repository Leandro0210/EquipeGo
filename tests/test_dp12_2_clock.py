#!/usr/bin/env python3
"""Test DP12.2. Comprueba origen comun; opcionalmente ejecuta simulador.

Prueba estatica:
    python3 tests/test_dp12_2_clock.py
Prueba real (WSL; modo auxiliar headless aun disponible):
    python3 tests/test_dp12_2_clock.py --run --binary ~/build-equipego/delivery_sim
"""
from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def check_source() -> None:
    src = (ROOT / 'src/main.cpp').read_text(encoding='utf-8')
    def position(needle: str) -> int:
        if src.count(needle) != 1:
            raise AssertionError(f'Se esperaba un anclaje: {needle}')
        return src.index(needle)

    p_open = position('dashboard->open();')
    p_rescue = position('fleet.startRescue();')
    p_start = position('const auto startTime = std::chrono::steady_clock::now();')
    p_clock = position('clock.start(config.simulation.timeScale);')
    p_dispatch = position('std::thread dispatcherThread(')
    p_producer = position('std::thread producerThread(')
    p_deadline = position('const auto deadline = config.simulation.durationS > 0')
    assert p_open < p_rescue < p_start < p_clock < p_dispatch < p_producer < p_deadline
    assert 'startTime + std::chrono::duration_cast' in src
    print('OK DP12.2 fuente: HighGUI y flota antes del reloj; epoch compartido con deadline')


def run_real(binary: Path) -> None:
    binary = binary.expanduser().resolve()
    assert binary.is_file(), f'No existe simulador: {binary}'
    config = (ROOT / 'config/equipetrol.json').resolve()
    assert config.is_file()
    with tempfile.TemporaryDirectory(prefix='equipego_dp122_') as d:
        log = Path(d) / 'eventos.jsonl'
        duration_sim_s = 300
        scale = 60
        args = [str(binary), str(config), '--headless', '--duration', str(duration_sim_s),
                '--scale', str(scale), '--log', str(log)]
        t0 = time.monotonic()
        process = subprocess.run(args, cwd=ROOT, capture_output=True,
                                 text=True, timeout=30, check=False)
        elapsed = time.monotonic() - t0
        if process.returncode:
            raise AssertionError(f'Error exit={process.returncode}: {process.stderr[-2000:]}')
        lines = process.stdout.splitlines()
        assert lines and lines[0] == 'Simulation has started...', 'Falta inicio'
        summary = json.loads(lines[-1])
        events = [json.loads(line) for line in log.read_text().splitlines() if line.strip()]
        assert events[-1]['event'] == 'simulationStopping'
        assert len([e for e in events if e['event'] == 'simulationStopping']) == 1
        assert [json.loads(line) for line in lines[1:-1]] == events
        assert summary['created'] == summary['delivered'] + summary['rejected'] + summary['pending']
        counts = collections.Counter(e['event'] for e in events)
        for k, v in [('created','orderCreated'),('delivered','orderDelivered'),('rejected','orderRejected')]:
            assert summary[k] == counts[v]
        created = [e['t'] for e in events if e['event'] == 'orderCreated']
        assert created, 'No se observaron pedidos durante 300 segundos simulados'
        latest_creation = max(created)
        limit_ms = duration_sim_s * 1000
        assert latest_creation <= limit_ms + 10000, (
            f'Pedidos creados tarde: t={latest_creation}ms > {limit_ms+10000}ms'
        )
        assert 4.0 <= elapsed <= 15.0, f'Duracion real anomala: {elapsed:.3f}s'
        print(f'OK DP12.2 runtime: {elapsed:.2f}s reales, '
              f'ultimo pedido t={latest_creation}ms, corte={limit_ms}ms, '
              f'creados={summary["created"]}, balance exacto')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--binary', type=Path,
                        default=Path.home() / 'build-equipego/delivery_sim')
    args = parser.parse_args()
    try:
        check_source()
        if args.run:
            run_real(args.binary)
    except (AssertionError, OSError, ValueError, subprocess.TimeoutExpired) as exc:
        print('FALLO DP12.2: ' + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
