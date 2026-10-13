# DP.11 — Analisis dinamico

Herramienta: **ThreadSanitizer**.
Version: ver `versiones_herramientas.json` con g++, biblioteca runtime y ldd.
Configuracion: copia de `config/equipetrol.json` con rafagas y averias intensificadas; original intacto.
Cada corrida contiene un evento `simulationStopping` si el cierre termino correctamente.

| Corrida | Estado | Pedidos | Averias | Rafaga maxima | Cierre real (s) |
|---|---|---:|---:|---:|---:|
| SIGINT | PASS | 129 | 6 | 12 | 0.1644 |

### SIGINT — PASS

Comando completo:

```bash
TSAN_OPTIONS=halt_on_error=1:exitcode=66:history_size=7 /tmp/equipego-dp12-y8kKGm/build-tsan/delivery_sim /home/leandro/equipego_benchmarks/dp11/tsan_20261013T151134Z/sigint_rep_01/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/tsan_20261013T151134Z/sigint_rep_01/events.jsonl
```

Datos crudos: `sigint_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGINT | PASS | 490 | 8 | 12 | 0.1657 |

### SIGINT — PASS

Comando completo:

```bash
TSAN_OPTIONS=halt_on_error=1:exitcode=66:history_size=7 /tmp/equipego-dp12-y8kKGm/build-tsan/delivery_sim /home/leandro/equipego_benchmarks/dp11/tsan_20261013T151134Z/sigint_rep_02/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/tsan_20261013T151134Z/sigint_rep_02/events.jsonl
```

Datos crudos: `sigint_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGINT | PASS | 459 | 8 | 12 | 0.115 |

### SIGINT — PASS

Comando completo:

```bash
TSAN_OPTIONS=halt_on_error=1:exitcode=66:history_size=7 /tmp/equipego-dp12-y8kKGm/build-tsan/delivery_sim /home/leandro/equipego_benchmarks/dp11/tsan_20261013T151134Z/sigint_rep_03/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/tsan_20261013T151134Z/sigint_rep_03/events.jsonl
```

Datos crudos: `sigint_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGTERM | PASS | 443 | 8 | 12 | 0.2146 |

### SIGTERM — PASS

Comando completo:

```bash
TSAN_OPTIONS=halt_on_error=1:exitcode=66:history_size=7 /tmp/equipego-dp12-y8kKGm/build-tsan/delivery_sim /home/leandro/equipego_benchmarks/dp11/tsan_20261013T151134Z/sigterm_rep_01/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/tsan_20261013T151134Z/sigterm_rep_01/events.jsonl
```

Datos crudos: `sigterm_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGTERM | PASS | 281 | 8 | 12 | 0.115 |

### SIGTERM — PASS

Comando completo:

```bash
TSAN_OPTIONS=halt_on_error=1:exitcode=66:history_size=7 /tmp/equipego-dp12-y8kKGm/build-tsan/delivery_sim /home/leandro/equipego_benchmarks/dp11/tsan_20261013T151134Z/sigterm_rep_02/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/tsan_20261013T151134Z/sigterm_rep_02/events.jsonl
```

Datos crudos: `sigterm_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGTERM | PASS | 459 | 8 | 12 | 0.1145 |

### SIGTERM — PASS

Comando completo:

```bash
TSAN_OPTIONS=halt_on_error=1:exitcode=66:history_size=7 /tmp/equipego-dp12-y8kKGm/build-tsan/delivery_sim /home/leandro/equipego_benchmarks/dp11/tsan_20261013T151134Z/sigterm_rep_03/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/tsan_20261013T151134Z/sigterm_rep_03/events.jsonl
```

Datos crudos: `sigterm_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

## Interpretacion y reejecucion

Anotar las anomalias, cambios realizados en el codigo y un nuevo ensayo con esta misma herramienta en otro directorio. Si no se detectaron errores, indicar 'sin cambios por hallazgos' y repetir para corroborar. No borrar reportes originales.
