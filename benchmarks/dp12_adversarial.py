#!/usr/bin/env python3
"""DP12.5: escenarios de evaluador, sin modificar codigo C++ o JSON original.

Guarda entradas, stdout/stderr crudos, JSONL, metadatos y un informe. Requiere
Linux/WSL para las senales POSIX. `--mode headless` es SOLO diagnostico:
la entrega oficial debe abrir ventana. No afirma ausencia universal de defectos.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import copy
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys
import threading
import time
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
REQUIRED_SUMMARY = {"created", "delivered", "rejected", "pending", "reassigned", "breakdowns",
                    "avgDeliveryMs", "maxDeliveryMs", "deliveredPerCourier"}
EVENT_NAMES = {"orderCreated", "orderAssigned", "pickupStarted", "orderPickedUp", "orderDelivered",
               "orderRejected", "courierBreakdown", "orderReassigned", "simulationStopping"}


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as file:
        for chunk in iter(lambda: file.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def save_json(path: Path, payload: Any) -> None:
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def base_config(source: Path) -> dict:
    c = json.loads(source.read_text(encoding='utf-8'))
    image = Path(c['map']['image'])
    c['map']['image'] = str((image if image.is_absolute() else source.parent / image).resolve())
    if not Path(c['map']['image']).is_file():
        raise RuntimeError(f"Falta la imagen de OSM: {c['map']['image']}")
    return c


def valid_config(source: dict, name: str) -> dict:
    c = copy.deepcopy(source)
    c['simulation'].update(durationS=240, timeScale=60)
    c['incidents']['breakdownProbability'] = 0.0
    c['orders'].update(meanIntervalMs=9000, burstMax=12, maxPending=40)
    if name == 'baseline':
        pass
    elif name == 'slots_one':
        for r in c['restaurants']:
            r['pickupSlots'] = 1
    elif name == 'bag_one':
        c['fleet']['bagCapacity'] = 1
    elif name == 'burst_heavy':
        c['orders'].update(meanIntervalMs=2500, burstMax=60, maxPending=20)
    elif name == 'breakdowns_one':
        c['fleet'].update(speedKmh=70, bagCapacity=1)
        c['orders'].update(meanIntervalMs=4500, burstMax=12, maxPending=20)
        c['incidents']['breakdownProbability'] = 1.0
        c['simulation']['durationS'] = 420
    elif name == 'unreachable':
        n0 = c['nodes'][0]
        for i in range(35):
            c['nodes'].append(dict(id=f'isolated_dp125_{i}', lat=n0['lat'], lon=n0['lon']))
        c['orders'].update(meanIntervalMs=3500, burstMax=16, maxPending=20)
    elif name == 'large_64':
        c['fleet'].update(couriers=64, bagCapacity=1)
        c['orders'].update(meanIntervalMs=20000, burstMax=7, maxPending=70)
        c['simulation']['durationS'] = 300
        streets = copy.deepcopy(c['streets'])
        nodes = {n['id']: n for n in c['nodes']}
        for i in range(210):
            street = streets[i % len(streets)]
            a, b = nodes[street['from']], nodes[street['to']]
            fraction = .2 + .6 * ((i % 11) / 10)
            new = dict(id=f'dp125_extra_{i}',
                       lat=a['lat'] + (b['lat'] - a['lat']) * fraction,
                       lon=a['lon'] + (b['lon'] - a['lon']) * fraction)
            c['nodes'].append(new)
            for suffix, origin, destination in [('a', street['from'], new['id']),
                                                ('b', new['id'], street['to'])]:
                c['streets'].append(dict(id=f'dp125_{i}_{suffix}', **{'from': origin, 'to': destination},
                                         oneWay=street['oneWay']))
    else:
        raise ValueError(name)
    return c


def invalid_config(source: dict, name: str) -> str:
    c = copy.deepcopy(source)
    if name == 'malformed':
        return '{ "map": [ }\n'
    if name == 'missing_map':
        del c['map']
    elif name == 'missing_nodes':
        del c['nodes']
    elif name == 'dangling_street':
        c['streets'][0]['from'] = 'unlisted_node_dp125'
    elif name == 'dangling_restaurant':
        c['restaurants'][0]['node'] = 'unlisted_node_dp125'
    elif name == 'slots_zero':
        c['restaurants'][0]['pickupSlots'] = 0
    elif name == 'bounds_reversed':
        c['map']['bounds']['north'] = c['map']['bounds']['south'] - .001
    elif name == 'missing_image':
        c['map']['image'] = '/this/file/never/exists/dp125.png'
    elif name == 'probability_invalid':
        c['incidents']['breakdownProbability'] = 1.1
    elif name == 'time_scale_zero':
        c['simulation']['timeScale'] = 0
    elif name == 'fleet_zero':
        c['fleet']['couriers'] = 0
    else:
        raise ValueError(name)
    return json.dumps(c, ensure_ascii=False, indent=2) + '\n'


class StreamReader:
    def __init__(self, pipe, destination: Path):
        self.pipe = pipe
        self.destination = destination
        self.started = threading.Event()
        self.finished = threading.Event()
        self.lines: list[str] = []
        self.counts: Counter = Counter()
        self.bursts: Counter = Counter()
        self.max_burst = 0
        self.mutex = threading.Lock()
        self.thread = threading.Thread(target=self._run, name='stdout-recorder', daemon=True)
        self.thread.start()

    def _run(self):
        try:
            with self.destination.open('w', encoding='utf-8') as output:
                for line in self.pipe:
                    output.write(line)
                    output.flush()
                    self.lines.append(line.rstrip('\n'))
                    if line.strip() == 'Simulation has started...':
                        self.started.set()
                    try:
                        event = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if isinstance(event, dict) and 'event' in event:
                        with self.mutex:
                            self.counts[event['event']] += 1
                            if event['event'] == 'orderCreated':
                                self.bursts[event['t']] += 1
                                self.max_burst = max(self.max_burst, self.bursts[event['t']])
        finally:
            self.finished.set()

    def snapshot(self):
        with self.mutex:
            return dict(self.counts), self.max_burst

    def join(self):
        self.thread.join(timeout=3)


def analyze(stdout: Path, events: Path, meta: dict, expectations: dict) -> dict:
    lines = stdout.read_text(encoding='utf-8').splitlines()
    if not lines or lines[0] != 'Simulation has started...':
        raise AssertionError('primera linea no es Simulation has started...')
    if len(lines) < 2:
        raise AssertionError('falta resumen final')
    try:
        summary = json.loads(lines[-1])
        stdout_events = [json.loads(x) for x in lines[1:-1]]
        journal = [json.loads(x) for x in events.read_text(encoding='utf-8').splitlines()]
    except (ValueError, OSError) as exc:
        raise AssertionError(f'JSON/JSONL invalido: {exc}') from exc
    if stdout_events != journal:
        raise AssertionError('JSONL no coincide con stdout')
    if not journal or journal[-1].get('event') != 'simulationStopping':
        raise AssertionError('simulationStopping no es el ultimo evento')
    if set(summary) != REQUIRED_SUMMARY:
        raise AssertionError('resumen final con campos faltantes o adicionales')
    counts = Counter()
    created = set()
    last_t = {}
    states = {}
    per_courier = Counter()
    times = defaultdict(int)
    for ev in journal:
        if not isinstance(ev, dict) or ev.get('event') not in EVENT_NAMES or not isinstance(ev.get('t'), int) or ev['t'] < 0:
            raise AssertionError(f'evento mal formado: {ev}')
        kind = ev['event']
        counts[kind] += 1
        if kind == 'simulationStopping':
            continue
        if kind == 'courierBreakdown':
            if not ev.get('courier'):
                raise AssertionError('averia sin courier')
            continue
        order = ev.get('order')
        if not isinstance(order, str) or not order:
            raise AssertionError(f'{kind} sin order')
        if order in last_t and ev['t'] < last_t[order]:
            raise AssertionError(f'tiempo retrocede en {order}')
        last_t[order] = ev['t']
        state = states.get(order)
        if kind == 'orderCreated':
            if order in created or not ev.get('restaurant'):
                raise AssertionError(f'ID duplicado o restaurante vacio: {order}')
            created.add(order)
            states[order] = 'created'
            times[order] = ev['t']
        elif kind == 'orderAssigned':
            if state != 'created' or not ev.get('courier'):
                raise AssertionError(f'asignacion invalida: {order} ({state})')
            states[order] = 'assigned'
        elif kind == 'pickupStarted':
            if state != 'assigned' or not ev.get('restaurant') or not ev.get('courier'):
                raise AssertionError(f'pickupStarted invalido: {order} ({state})')
        elif kind == 'orderPickedUp':
            if state != 'assigned' or not ev.get('restaurant') or not ev.get('courier'):
                raise AssertionError(f'orderPickedUp invalido: {order} ({state})')
            states[order] = 'pickedUp'
        elif kind == 'orderDelivered':
            if state != 'pickedUp' or not ev.get('courier'):
                raise AssertionError(f'entrega duplicada o prematura: {order} ({state})')
            states[order] = 'delivered'
            per_courier[ev['courier']] += 1
        elif kind == 'orderRejected':
            if state != 'created' or ev.get('reason') not in ('queueFull', 'unreachable', 'noCourier'):
                raise AssertionError(f'rechazo invalido: {order} ({state})')
            states[order] = 'rejected'
        elif kind == 'orderReassigned':
            if state not in ('assigned', 'pickedUp') or not ev.get('from') or not ev.get('to') or ev['from'] == ev['to']:
                raise AssertionError(f'reasignacion invalida: {order} ({state})')
    if counts['simulationStopping'] != 1:
        raise AssertionError('simulationStopping no ocurre exactamente una vez')
    if summary['created'] != len(created) or summary['created'] != counts['orderCreated']:
        raise AssertionError('created incorrecto')
    if summary['delivered'] != counts['orderDelivered'] or summary['rejected'] != counts['orderRejected']:
        raise AssertionError('delivered/rejected incorrectos')
    if summary['breakdowns'] != counts['courierBreakdown'] or summary['reassigned'] != counts['orderReassigned']:
        raise AssertionError('breakdowns/reassigned incorrectos')
    if summary['created'] != summary['delivered'] + summary['rejected'] + summary['pending']:
        raise AssertionError('balance global incorrecto')
    if sum(summary['deliveredPerCourier'].values()) != summary['delivered']:
        raise AssertionError('deliveredPerCourier incorrecto')
    if dict(per_courier) != {k:v for k,v in summary['deliveredPerCourier'].items() if v}:
        raise AssertionError('entregas por repartidor incorrectas')
    if (sum(s == 'delivered' for s in states.values()) != summary['delivered'] or
        sum(s == 'rejected' for s in states.values()) != summary['rejected']):
        raise AssertionError('estados finales inconsistentes')
    bursts = Counter(ev['t'] for ev in journal if ev['event'] == 'orderCreated')
    maximum_burst = max(bursts.values(), default=0)
    if counts['orderCreated'] < expectations.get('min_created', 0):
        raise AssertionError(f"se crearon {counts['orderCreated']} pedidos; esperado al menos {expectations['min_created']}")
    if maximum_burst < expectations.get('min_burst', 0):
        raise AssertionError(f"rafaga maxima {maximum_burst}; esperado al menos {expectations['min_burst']}")
    if counts['courierBreakdown'] < expectations.get('min_breakdowns', 0):
        raise AssertionError(f"averias insuficientes: {counts['courierBreakdown']}")
    if sum(ev.get('reason')=='unreachable' for ev in journal) < expectations.get('min_unreachable', 0):
        raise AssertionError('no ocurrieron los rechazos unreachable esperados')
    return {"counts":dict(counts),"summary":summary,"max_burst":maximum_burst,"events":len(journal)}


def run_one(binary: Path, directory: Path, config: dict | None, label: str, *,
            mode: str, kind: str, expectations: dict | None = None, signal_mode: str = '') -> dict:
    directory.mkdir(parents=True, exist_ok=False)
    output = directory/'stdout.txt'
    stderr = directory/'stderr.txt'
    journal = directory/'events.jsonl'
    args = [str(binary)]
    if kind == 'invalid':
        if label != 'missing_argument':
            file = directory/'config.json'
            file.write_text(config, encoding='utf-8')
            args.append(str(file))
    else:
        file = directory/'config.json'
        save_json(file, config)
        args.append(str(file))
    if kind != 'invalid' or label != 'missing_argument':
        args.extend(['--log',str(journal)])
    if kind != 'invalid':
        args.append('--'+mode)
    metadata = {"label":label,"kind":kind,"command":args,"mode":mode if kind != 'invalid' else None,
                "started_at_utc":datetime.now(timezone.utc).isoformat()}
    with stderr.open('w',encoding='utf-8') as err:
        start = time.monotonic()
        proc = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=err,
                                encoding='utf-8',errors='replace',bufsize=1)
        assert proc.stdout is not None
        reader = StreamReader(proc.stdout,output)
        try:
            if kind == 'invalid':
                proc.wait(timeout=12)
            else:
                if not reader.started.wait(timeout=30):
                    raise RuntimeError('no aparecio Simulation has started... antes de 30 s')
                if signal_mode:
                    if signal_mode != 'immediate':
                        until = time.monotonic() + 15
                        while time.monotonic() < until and proc.poll() is None:
                            observed, peak = reader.snapshot()
                            if signal_mode == 'burst' and peak >= 12:
                                break
                            if signal_mode == 'breakdown' and observed.get('courierBreakdown',0) >= 1:
                                break
                            time.sleep(.04)
                        else:
                            raise RuntimeError(f'no se observo condicion {signal_mode} antes de 15 s')
                    metadata['signal'] = 'SIGINT' if label.endswith('sigint') else 'SIGTERM'
                    sig = signal.SIGINT if metadata['signal']=='SIGINT' else signal.SIGTERM
                    signal_sent = time.monotonic()
                    proc.send_signal(sig)
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired as exc:
                        raise RuntimeError('apagado > 5 s tras la señal') from exc
                    metadata['shutdown_s'] = round(time.monotonic()-signal_sent,3)
                else:
                    proc.wait(timeout=50)
            metadata['exit_code'] = proc.returncode
        except Exception as exc:
            metadata['runner_error'] = str(exc)
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=8)
            metadata['exit_code'] = proc.returncode
        finally:
            reader.join()
            metadata['wall_s'] = round(time.monotonic()-start,3)
    errors=[]
    if 'runner_error' in metadata:
        errors.append(metadata['runner_error'])
    if kind == 'invalid':
        if metadata['exit_code'] == 0:
            errors.append('entrada invalida tuvo exit 0')
        if reader.started.is_set():
            errors.append('entrada invalida inicio la simulacion')
        if not stderr.read_text(encoding='utf-8').strip():
            errors.append('entrada invalida sin mensaje en stderr')
        if journal.exists() and journal.stat().st_size:
            errors.append('entrada invalida escribio eventos')
    else:
        if metadata['exit_code'] != 0:
            errors.append('simulacion termino con codigo distinto de cero')
        try:
            metadata['audit'] = analyze(output,journal,metadata, expectations or {})
        except (AssertionError, ValueError, OSError, KeyError, TypeError) as exc:
            errors.append(str(exc))
        if signal_mode and metadata.get('shutdown_s',99) > 5.0:
            errors.append('incumple apagado menor que 5 segundos')
    metadata['status'] = 'PASS' if not errors else 'FAIL'
    metadata['errors'] = errors
    save_json(directory/'metadata.json',metadata)
    return metadata


INVALID = ('missing_argument','malformed','missing_map','missing_nodes','dangling_street',
           'dangling_restaurant','slots_zero','bounds_reversed','missing_image',
           'probability_invalid','time_scale_zero','fleet_zero')
VALID = ('baseline','slots_one','bag_one','burst_heavy','breakdowns_one','unreachable','large_64')
SIGNAL = ('immediate_sigint','burst_sigterm','breakdown_sigint')
EXPECTATIONS = {'baseline':{'min_created':1},'slots_one':{'min_created':1},'bag_one':{'min_created':1},
                'burst_heavy':{'min_created':30,'min_burst':12},
                'breakdowns_one':{'min_created':10,'min_breakdowns':1},
                'unreachable':{'min_created':25,'min_unreachable':1},
                'large_64':{'min_created':1},
                'burst_sigterm':{'min_created':12,'min_burst':12},
                'breakdown_sigint':{'min_breakdowns':1}}


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase',required=True, choices=('invalid','valid','signals','all'))
    parser.add_argument('--mode',default='gui',choices=('gui','headless'),
                        help='GUI por defecto; headless SOLO diagnostico, no valida requisito de ventana')
    parser.add_argument('--binary',type=Path,default=Path.home()/'build-equipego/delivery_sim_bench')
    parser.add_argument('--config',type=Path,default=ROOT/'config/equipetrol.json')
    parser.add_argument('--output',type=Path,default=Path.home()/'equipego_benchmarks/dp12_adversarial')
    args=parser.parse_args()
    if not sys.platform.startswith('linux'):
        parser.error('requiere Linux/WSL, especialmente por señales POSIX')
    binary=args.binary.expanduser().resolve()
    source=args.config.expanduser().resolve()
    if not binary.is_file() or not os.access(binary,os.X_OK) or not source.is_file():
        parser.error('binario ejecutable o configuracion inexistente')
    config=base_config(source)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    dest=args.output.expanduser().resolve()/f'{args.phase}_{args.mode}_{stamp}'
    dest.mkdir(parents=True,exist_ok=False)
    manifest={"phase":args.phase,"mode":args.mode,"timestamp_utc":stamp,
              "binary":str(binary),"binary_sha256":sha(binary),"source_config":str(source),
              "source_config_sha256":sha(source),"platform":platform.platform(),
              "git_commit":subprocess.run(['git','rev-parse','HEAD'],cwd=ROOT,
              capture_output=True,text=True,check=False).stdout.strip(),
              "note":"Modo headless solo diagnostico; la rubrica exige ventana."}
    save_json(dest/'parametros.json',manifest)
    jobs=[]
    if args.phase in ('all','invalid'): jobs += [(s,'invalid') for s in INVALID]
    if args.phase in ('all','valid'): jobs += [(s,'valid') for s in VALID]
    if args.phase in ('all','signals'): jobs += [(s,'signals') for s in SIGNAL]
    rows=[]
    print(f'DP12.5 PRUEBAS ADVERSARIAS | {len(jobs)} casos | evidencia: {dest}',flush=True)
    for number,(name,kind) in enumerate(jobs,1):
        print(f'  [{number}/{len(jobs)}] {name} ...',end='',flush=True)
        if kind=='invalid':
            raw=invalid_config(config,name) if name!='missing_argument' else None
            result=run_one(binary,dest/name,raw,name,mode=args.mode,kind=kind)
        else:
            base=valid_config(config,'burst_heavy' if name=='burst_sigterm' else
                             'breakdowns_one' if name=='breakdown_sigint' else
                             'baseline' if name=='immediate_sigint' else name)
            if kind=='signals':
                base['simulation']['durationS']=0
            mode= {'immediate_sigint':'immediate','burst_sigterm':'burst',
                   'breakdown_sigint':'breakdown'}.get(name,'')
            result=run_one(binary,dest/name,base,name,mode=args.mode,kind=kind,
                           expectations=EXPECTATIONS.get(name),signal_mode=mode)
        rows.append(result)
        print(f" {result['status']} | exit={result['exit_code']} "
              f"| {result.get('audit',{}).get('summary',{}).get('created','-')} pedidos "
              f"| {result.get('shutdown_s','-')} s cierre",flush=True)
        if result['errors']:
            for error in result['errors']:
                print(f'       ERROR: {error}',flush=True)
    with (dest/'resultados.csv').open('w',newline='',encoding='utf-8') as file:
        writer=csv.writer(file)
        writer.writerow(['caso','grupo','estado','salida','duracion_s','cierre_s','creados','entregados','rechazados','pendientes','averias','rafaga_max','errores'])
        for r in rows:
            a=r.get('audit',{}); s=a.get('summary',{})
            writer.writerow([r['label'],r['kind'],r['status'],r['exit_code'],r['wall_s'],r.get('shutdown_s',''),
                             s.get('created',''),s.get('delivered',''),s.get('rejected',''),s.get('pending',''),
                             s.get('breakdowns',''),a.get('max_burst',''),'; '.join(r['errors'])])
    passed=sum(r['status']=='PASS' for r in rows)
    lines=['# DP12.5 — Pruebas adversarias reproducibles','',
           f"Fase: `{args.phase}`. Interfaz: `{args.mode}`. Ejecutable: `{binary}`.",'',
           f'**Resultado: {passed}/{len(rows)} PASS.**','',
           '| Caso | Estado | Exit | Pedidos | Averias | Cierre tras señal (s) |',
           '|---|---|---:|---:|---:|---:|']
    for r in rows:
        s=r.get('audit',{}).get('summary',{})
        lines.append(f"| `{r['label']}` | {r['status']} | {r['exit_code']} | "
                     f"{s.get('created','-')} | {s.get('breakdowns','-')} | {r.get('shutdown_s','-')} |")
    lines+=['','## Interpretacion','',
            '- Cada carpeta conserva el JSON exacto, stdout, stderr, JSONL y metadatos. Los fallos permanecen sin ocultarse.',
            '- Las pruebas invalidas verifican salida distinta de cero, stderr y ausencia de inicio.',
            '- Las pruebas validas comprueban transiciones, unicidad, tiempos, salida JSONL y balance final.',
            '- No se puede inferir FIFO de llegada al restaurante a partir del JSONL sin un evento de llegada; requiere prueba de PickupGate.',
            '- La evidencia de senales usa SIGINT o SIGTERM reales y mide el cierre despues de enviarlas.',
            '- Ejecutar --mode headless solo con fines diagnosticos; no sustituye pruebas GUI de la rubrica.',
            '- Ninguna bateria finita prueba ausencia universal de carreras o errores de memoria. Complementar con TSAN y ASAN.', '']
    (dest/'informe.md').write_text('\n'.join(lines),encoding='utf-8')
    print(f'DP12.5: {passed}/{len(rows)} PASS | informe {dest/"informe.md"}',flush=True)
    return 0 if passed==len(rows) else 1

if __name__=='__main__':
    raise SystemExit(main())
