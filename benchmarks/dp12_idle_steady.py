#!/usr/bin/env python3
"""DP12.3.1: mide CPU de EQUIPETROL durante reposo estable (Linux/WSL).

Compara periodo de medicion POSTERIOR al arranque con 8/64 motos y GUI/headless.
No modifica el JSON original ni el codigo C++ y conserva evidencias crudas.
La prueba 'headless' es solo diagnostica; la rubrica de entrega exige GUI.
"""

from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import subprocess
import sys
import time


def positive_float(raw: str) -> float:
    try:
        value = float(raw)
    except ValueError as exc:
        raise argparse.ArgumentTypeError('debe ser numerico') from exc
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError('debe ser positivo y finito')
    return value


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def proc_cpu_seconds(pid: int, clock_ticks: int) -> float:
    raw = Path(f'/proc/{pid}/stat').read_text(encoding='ascii')
    right = raw.rfind(')')
    if right == -1:
        raise RuntimeError('formato inesperado en /proc/<pid>/stat')
    rest = raw[right+2:].split()
    return (int(rest[11]) + int(rest[12])) / clock_ticks


def wait_alive(proc: subprocess.Popen, seconds: float) -> None:
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        if proc.poll() is not None:
            raise RuntimeError(f'el simulador salio antes de la medicion: {proc.returncode}')
        time.sleep(min(0.1, until-time.monotonic()))
    if proc.poll() is not None:
        raise RuntimeError(f'el simulador termino antes de tiempo: {proc.returncode}')


def wait_start(proc: subprocess.Popen, out: Path, timeout: float=20.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if out.is_file():
            try:
                with out.open('r', encoding='utf-8', errors='replace') as f:
                    if f.readline().strip() == 'Simulation has started...':
                        return
            except OSError:
                pass
        if proc.poll() is not None:
            raise RuntimeError(f'salida temprana exit={proc.returncode}; revisar stderr')
        time.sleep(0.07)
    raise RuntimeError('no aparecio la linea inicial dentro de 20s')


def trial(binary: Path, config: Path, root: Path, couriers: int, mode: str,
          warmup: float, measurement: float, cooldown: float,
          clk: int) -> dict:
    directory = root / f'motos_{couriers:03}' / mode
    directory.mkdir(parents=True, exist_ok=False)
    data = json.loads(config.read_text(encoding='utf-8'))
    image = Path(data['map']['image'])
    data['map']['image'] = str((config.parent / image).resolve() if not image.is_absolute() else image)
    data['fleet']['couriers'] = couriers
    data['orders']['meanIntervalMs'] = 20000000
    data['simulation']['timeScale'] = 1.0
    data['simulation']['durationS'] = math.ceil(warmup + measurement + cooldown + 2)
    path = directory / 'config.json'
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    events = directory / 'events.jsonl'
    stdout = directory / 'stdout.txt'
    stderr = directory / 'stderr.txt'
    cmd = [str(binary), str(path), '--gui' if mode == 'gui' else '--headless', '--log', str(events)]
    meta = {'command': cmd, 'warmup_s': warmup, 'measure_s': measurement,
            'cooldown_s': cooldown, 'mode': mode, 'couriers': couriers,
            'binary_sha256': sha256(binary), 'config_path': str(path)}
    print(f'  -> {couriers} motos / {mode}: calentando {warmup:g}s, luego midiendo {measurement:g}s...', flush=True)
    with stdout.open('wb') as out, stderr.open('wb') as err:
        proc = subprocess.Popen(cmd, stdout=out, stderr=err)
        try:
            wait_start(proc, stdout)
            wait_alive(proc, warmup)
            before = proc_cpu_seconds(proc.pid, clk)
            t0 = time.monotonic()
            wait_alive(proc, measurement)
            dt = time.monotonic() - t0
            after = proc_cpu_seconds(proc.pid, clk)
            meta['measured_cpu_s'] = round(after-before, 6)
            meta['measured_wall_s'] = round(dt, 6)
            meta['measured_cpu_pct_one_core'] = round((after-before)*100/dt, 3)
            meta['startup_excluded'] = True
            try:
                code = proc.wait(timeout=cooldown+6)
            except subprocess.TimeoutExpired:
                raise RuntimeError('el simulador no termino al cumplir durationS')
            meta['exit_code'] = code
            if code != 0:
                raise RuntimeError(f'simulador exit={code}')
        except Exception as exc:
            meta['error'] = str(exc)
            raise
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=5)
            (directory/'metadata.json').write_text(json.dumps(meta, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    output_lines = stdout.read_text(encoding='utf-8').splitlines()
    event_lines = [json.loads(line) for line in events.read_text(encoding='utf-8').splitlines() if line.strip()]
    if not output_lines or output_lines[0] != 'Simulation has started...':
        raise RuntimeError('falta mensaje inicial')
    if not event_lines or event_lines[-1].get('event') != 'simulationStopping':
        raise RuntimeError('falta simulationStopping final')
    if event_lines != [json.loads(line) for line in output_lines[1:-1]]:
        raise RuntimeError('stdout no coincide con events.jsonl')
    summary = json.loads(output_lines[-1])
    if summary.get('created') != 0:
        raise RuntimeError(f'no fue ocio puro: {summary.get("created")} pedidos creados')
    if any(summary.get(k) != 0 for k in ('rejected','delivered','pending')):
        raise RuntimeError('resumen inesperado sin pedidos')
    meta['verified_idle'] = True
    (directory/'metadata.json').write_text(json.dumps(meta, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    print(f'     OK: CPU estable {meta["measured_cpu_s"]:.3f}s / {dt:.2f}s '
          f'= {meta["measured_cpu_pct_one_core"]:.2f}% de un nucleo | 0 pedidos', flush=True)
    return meta


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    repo = Path(__file__).resolve().parents[1]
    parser.add_argument('--binary', type=Path, default=Path.home()/'build-equipego/delivery_sim_bench')
    parser.add_argument('--config', type=Path, default=repo/'config/equipetrol.json')
    parser.add_argument('--couriers', default='8,64')
    parser.add_argument('--modes', default='headless,gui')
    parser.add_argument('--warmup', type=positive_float, default=5.0)
    parser.add_argument('--measure', type=positive_float, default=20.0)
    parser.add_argument('--cooldown', type=positive_float, default=3.0)
    parser.add_argument('--output', type=Path, default=Path.home()/'equipego_benchmarks/dp12_idle_steady')
    args = parser.parse_args()
    if not sys.platform.startswith('linux'):
        parser.error('solo Linux/WSL: requiere /proc/PID/stat')
    try:
        fleets = [int(c.strip()) for c in args.couriers.split(',')]
        if not fleets or any(c < 1 for c in fleets) or len(set(fleets)) != len(fleets):
            raise ValueError('las flotas deben ser enteros positivos unicos')
    except ValueError as exc:
        parser.error(f'--couriers: {exc}')
    modes = [m.strip() for m in args.modes.split(',')]
    if not modes or any(m not in ('gui','headless') for m in modes) or len(set(modes)) != len(modes):
        parser.error('--modes debe ser gui, headless o headless,gui')
    binary = args.binary.expanduser().resolve()
    config = args.config.expanduser().resolve()
    if not binary.is_file() or not config.is_file():
        parser.error('binario o configuracion inexistente')
    dest = args.output.expanduser().resolve()/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    dest.mkdir(parents=True, exist_ok=False)
    (dest/'parametros.json').write_text(json.dumps({'binary':str(binary), 'binary_sha256':sha256(binary),
        'original_config':str(config), 'config_sha256':sha256(config),
        'system':platform.platform(), 'warmup':args.warmup,'measure':args.measure,'cooldown':args.cooldown,
        'fleets':fleets,'modes':modes},indent=2)+'\n',encoding='utf-8')
    clk = int(os.sysconf('SC_CLK_TCK'))
    rows = []
    print(f'DP12.3.1 CPU ESTABLE | evidencias: {dest}',flush=True)
    try:
        for couriers in fleets:
            for mode in modes:
                rows.append(trial(binary,config,dest,couriers,mode,
                                  args.warmup,args.measure,args.cooldown,clk))
    except Exception as exc:
        (dest/'ERROR.txt').write_text(str(exc)+'\n',encoding='utf-8')
        print(f'ERROR: {exc}\nRevisar: {dest}',file=sys.stderr)
        return 1
    cols = ['couriers','mode','measured_cpu_s','measured_wall_s','measured_cpu_pct_one_core','exit_code','verified_idle']
    with (dest/'resultados.csv').open('w',newline='',encoding='utf-8') as output:
        writer = csv.DictWriter(output,fieldnames=cols,extrasaction='ignore')
        writer.writeheader()
        writer.writerows(rows)
    lines = ['# DP12.3.1 — CPU ociosa tras calentamiento','',
             'La medicion excluye el arranque y se obtiene de /proc/PID/stat (CPU de todos los hilos).',
             '100% representa un nucleo completo; la rubrica no define un umbral numerico.','',
             '| Motos | Modo | CPU estable (%) | CPU estable (s) | Pared medida (s) |',
             '|---:|---|---:|---:|---:|']
    for m in rows:
        lines.append(f'| {m["couriers"]} | {m["mode"]} | {m["measured_cpu_pct_one_core"]:.3f} | '
                     f'{m["measured_cpu_s"]:.3f} | {m["measured_wall_s"]:.2f} |')
    lines+=['','El modo headless es solo diagnostico. La rubrica exige que la aplicacion evaluada abra ventana.',
            'Esta medicion no demuestra por si sola ausencia de spinning; revisar codigo y TSAN.','']
    (dest/'informe.md').write_text('\n'.join(lines),encoding='utf-8')
    print(f'DP12.3.1: {len(rows)}/{len(rows)} mediciones verificadas. Informe: {dest/"informe.md"}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
