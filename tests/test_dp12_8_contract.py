#!/usr/bin/env python3
"""DP12.8: contrato de interfaz publica y humo funcional. No modifica el repo.

Uso:
 python3 tests/test_dp12_8_contract.py
 python3 tests/test_dp12_8_contract.py --run
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def check_static() -> None:
    main=(ROOT/'src/main.cpp').read_text(encoding='utf-8')
    cmake=(ROOT/'CMakeLists.txt').read_text(encoding='utf-8')
    dash=(ROOT/'src/visualization/SimulationDashboard.cpp').read_text(encoding='utf-8')
    assert '#ifdef EQUIPEGO_BENCHMARK_CLI' in main, 'faltan guards de opciones de prueba'
    assert 'const bool showGui = true;' in main, 'sin GUI obligatoria'
    assert 'Uso: delivery_sim config.json [--log RUTA]' in main, 'CLI oficial incorrecto'
    assert 'target_compile_definitions(delivery_sim_bench PRIVATE EQUIPEGO_BENCHMARK_CLI=1)' in cmake
    assert 'option(EQUIPEGO_BUILD_BENCHMARK' in cmake and 'OFF)' in cmake
    assert 'add_executable(delivery_sim ${SOURCES})' in cmake
    assert 'cv::MARKER_DIAMOND' in dash and 's.active->deliveryNodeId' in dash
    assert 'stateColor(samples[i].courier)' in dash
    assert 'stateColor(s)' in dash
    assert 'config.map.attribution' in dash, 'falta atribucion mapa'
    for p in (ROOT/'benchmarks/dp12_idle_cpu.py', ROOT/'benchmarks/dp12_idle_steady.py', ROOT/'benchmarks/dp12_adversarial.py'):
        if p.exists():
            script=p.read_text(encoding='utf-8')
            assert 'build-equipego/delivery_sim_bench' in script, f'default de {p.name} no migrado'
    print('OK DP12.8 estatico: API publica, benchmark separado, estado, destino, OSM')


def examine(stdout: str, logfile: Path, expected_created=None) -> None:
    lines=stdout.splitlines()
    assert lines and lines[0]=='Simulation has started...', 'no inicio'
    assert len(lines)>=3, 'falta resumen'
    summary=json.loads(lines[-1])
    events=[json.loads(line) for line in lines[1:-1]]
    recorded=[json.loads(line) for line in logfile.read_text(encoding='utf-8').splitlines()]
    assert events==recorded, 'stdout y JSONL diferentes'
    assert events[-1]['event']=='simulationStopping', 'falta evento final'
    assert summary['created']==summary['delivered']+summary['rejected']+summary['pending'], 'balance inconsistente'
    if expected_created is not None:
        assert summary['created']==expected_created, 'creados no esperados'


def check_runtime(official: Path, bench: Path) -> None:
    for name,p in [('delivery_sim',official),('delivery_sim_bench',bench)]:
        if not p.is_file(): raise RuntimeError(f'binario {name} inexistente: {p}')
    config=ROOT/'config/equipetrol.json'
    for opt in ('--headless','--gui','--scale','--duration'):
        extra=(opt,'3') if opt in ('--scale','--duration') else (opt,)
        proc=subprocess.run([str(official),str(config),*extra],capture_output=True,text=True,timeout=10)
        assert proc.returncode!=0 and 'Simulation has started...' not in proc.stdout, f'delivery_sim acepto {opt}'
    print('OK DP12.8 oficial rechaza --headless, --gui, --scale, --duration')
    with tempfile.TemporaryDirectory(prefix='dp12_8_cli_') as directory:
        base=Path(directory)
        with (base/'benchmark.json').open('w',encoding='utf-8') as output:
            data=json.loads(config.read_text(encoding='utf-8'))
            image=Path(data['map']['image'])
            data['map']['image']=str((image if image.is_absolute() else config.parent/image).resolve())
            data['simulation']['durationS']=120
            data['simulation']['timeScale']=60
            json.dump(data,output)
        logfile=base/'bench.jsonl'
        proc=subprocess.run([str(bench),str(base/'benchmark.json'),'--headless','--log',str(logfile)],
                            capture_output=True,text=True,timeout=25)
        assert proc.returncode==0, f'bench fallo: {proc.stderr[-1500:]}'
        examine(proc.stdout,logfile)
        print('OK DP12.8 benchmark ejecuta headless con duracion JSON')
        if os.getenv('DISPLAY') or os.getenv('WAYLAND_DISPLAY'):
            logfile=base/'official.jsonl'
            proc=subprocess.run([str(official),str(base/'benchmark.json'),'--log',str(logfile)],
                                capture_output=True,text=True,timeout=25)
            assert proc.returncode==0, f'oficial fallo con ventana: {proc.stderr[-2000:]}'
            examine(proc.stdout,logfile)
            print('OK DP12.8 oficial ejecuto con GUI y contrato JSONL')
        else:
            print('AVISO: entorno sin DISPLAY; validar GUI de forma presencial')


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',action='store_true')
    parser.add_argument('--official',type=Path,default=Path.home()/'build-equipego/delivery_sim')
    parser.add_argument('--bench',type=Path,default=Path.home()/'build-equipego/delivery_sim_bench')
    args=parser.parse_args()
    check_static()
    if args.run:
        check_runtime(args.official.expanduser().resolve(),args.bench.expanduser().resolve())
    return 0

if __name__=='__main__':
    try: raise SystemExit(main())
    except (AssertionError, RuntimeError, subprocess.TimeoutExpired) as e:
        print('FAIL DP12.8:',e,file=sys.stderr)
        raise SystemExit(1)
