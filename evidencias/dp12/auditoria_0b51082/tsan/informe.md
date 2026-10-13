# DP.11 — Analisis dinamico

Herramienta: **ThreadSanitizer**.
Version: ver `versiones_herramientas.json` con g++, biblioteca runtime y ldd.
Configuracion: copia de `config/equipetrol.json` con rafagas y averias intensificadas; original intacto.
Cada corrida contiene un evento `simulationStopping` si el cierre termino correctamente.

| Corrida | Estado | Pedidos | Averias | Rafaga maxima | Cierre real (s) |
|---|---|---:|---:|---:|---:|
| SIGINT | PASS | 490 | 8 | 12 | 0.1147 |

### SIGINT — PASS

Comando completo:

```bash
TSAN_OPTIONS=halt_on_error=1:exitcode=66:history_size=7 /tmp/equipego-final-ixOAuy/build-tsan/delivery_sim_bench /home/leandro/equipego_benchmarks/dp11/tsan_20261013T171846Z/sigint_rep_01/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/tsan_20261013T171846Z/sigint_rep_01/events.jsonl
```

Datos crudos: `sigint_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGINT | PASS | 508 | 8 | 12 | 0.1147 |

### SIGINT — PASS

Comando completo:

```bash
TSAN_OPTIONS=halt_on_error=1:exitcode=66:history_size=7 /tmp/equipego-final-ixOAuy/build-tsan/delivery_sim_bench /home/leandro/equipego_benchmarks/dp11/tsan_20261013T171846Z/sigint_rep_02/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/tsan_20261013T171846Z/sigint_rep_02/events.jsonl
```

Datos crudos: `sigint_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGINT | PASS | 502 | 8 | 12 | 0.1139 |

### SIGINT — PASS

Comando completo:

```bash
TSAN_OPTIONS=halt_on_error=1:exitcode=66:history_size=7 /tmp/equipego-final-ixOAuy/build-tsan/delivery_sim_bench /home/leandro/equipego_benchmarks/dp11/tsan_20261013T171846Z/sigint_rep_03/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/tsan_20261013T171846Z/sigint_rep_03/events.jsonl
```

Datos crudos: `sigint_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGTERM | PASS | 517 | 8 | 12 | 0.1143 |

### SIGTERM — PASS

Comando completo:

```bash
TSAN_OPTIONS=halt_on_error=1:exitcode=66:history_size=7 /tmp/equipego-final-ixOAuy/build-tsan/delivery_sim_bench /home/leandro/equipego_benchmarks/dp11/tsan_20261013T171846Z/sigterm_rep_01/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/tsan_20261013T171846Z/sigterm_rep_01/events.jsonl
```

Datos crudos: `sigterm_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGTERM | PASS | 348 | 8 | 12 | 0.064 |

### SIGTERM — PASS

Comando completo:

```bash
TSAN_OPTIONS=halt_on_error=1:exitcode=66:history_size=7 /tmp/equipego-final-ixOAuy/build-tsan/delivery_sim_bench /home/leandro/equipego_benchmarks/dp11/tsan_20261013T171846Z/sigterm_rep_02/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/tsan_20261013T171846Z/sigterm_rep_02/events.jsonl
```

Datos crudos: `sigterm_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGTERM | PASS | 517 | 8 | 12 | 0.1145 |

### SIGTERM — PASS

Comando completo:

```bash
TSAN_OPTIONS=halt_on_error=1:exitcode=66:history_size=7 /tmp/equipego-final-ixOAuy/build-tsan/delivery_sim_bench /home/leandro/equipego_benchmarks/dp11/tsan_20261013T171846Z/sigterm_rep_03/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/tsan_20261013T171846Z/sigterm_rep_03/events.jsonl
```

Datos crudos: `sigterm_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

## Interpretacion y reejecucion

Anotar las anomalias, cambios realizados en el codigo y un nuevo ensayo con esta misma herramienta en otro directorio. Si no se detectaron errores, indicar 'sin cambios por hallazgos' y repetir para corroborar. No borrar reportes originales.
