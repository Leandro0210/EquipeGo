# DP.11 — Analisis dinamico

Herramienta: **AddressSanitizer + LeakSanitizer + UndefinedBehaviorSanitizer**.
Version: ver `versiones_herramientas.json` con g++, biblioteca runtime y ldd.
Configuracion: copia de `config/equipetrol.json` con rafagas y averias intensificadas; original intacto.
Cada corrida contiene un evento `simulationStopping` si el cierre termino correctamente.

| Corrida | Estado | Pedidos | Averias | Rafaga maxima | Cierre real (s) |
|---|---|---:|---:|---:|---:|
| SIGINT | PASS | 443 | 8 | 12 | 0.4653 |

### SIGINT — PASS

Comando completo:

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:exitcode=66 LSAN_OPTIONS=exitcode=66:report_objects=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /tmp/equipego-dp12-y8kKGm/build-asan/delivery_sim /home/leandro/equipego_benchmarks/dp11/asan_20261013T153122Z/sigint_rep_01/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/asan_20261013T153122Z/sigint_rep_01/events.jsonl
```

Datos crudos: `sigint_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGINT | PASS | 517 | 8 | 12 | 0.2147 |

### SIGINT — PASS

Comando completo:

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:exitcode=66 LSAN_OPTIONS=exitcode=66:report_objects=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /tmp/equipego-dp12-y8kKGm/build-asan/delivery_sim /home/leandro/equipego_benchmarks/dp11/asan_20261013T153122Z/sigint_rep_02/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/asan_20261013T153122Z/sigint_rep_02/events.jsonl
```

Datos crudos: `sigint_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGINT | PASS | 523 | 8 | 12 | 0.164 |

### SIGINT — PASS

Comando completo:

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:exitcode=66 LSAN_OPTIONS=exitcode=66:report_objects=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /tmp/equipego-dp12-y8kKGm/build-asan/delivery_sim /home/leandro/equipego_benchmarks/dp11/asan_20261013T153122Z/sigint_rep_03/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/asan_20261013T153122Z/sigint_rep_03/events.jsonl
```

Datos crudos: `sigint_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGTERM | PASS | 523 | 8 | 12 | 0.1715 |

### SIGTERM — PASS

Comando completo:

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:exitcode=66 LSAN_OPTIONS=exitcode=66:report_objects=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /tmp/equipego-dp12-y8kKGm/build-asan/delivery_sim /home/leandro/equipego_benchmarks/dp11/asan_20261013T153122Z/sigterm_rep_01/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/asan_20261013T153122Z/sigterm_rep_01/events.jsonl
```

Datos crudos: `sigterm_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGTERM | PASS | 523 | 8 | 12 | 0.1642 |

### SIGTERM — PASS

Comando completo:

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:exitcode=66 LSAN_OPTIONS=exitcode=66:report_objects=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /tmp/equipego-dp12-y8kKGm/build-asan/delivery_sim /home/leandro/equipego_benchmarks/dp11/asan_20261013T153122Z/sigterm_rep_02/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/asan_20261013T153122Z/sigterm_rep_02/events.jsonl
```

Datos crudos: `sigterm_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

| SIGTERM | PASS | 523 | 8 | 12 | 0.2671 |

### SIGTERM — PASS

Comando completo:

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:exitcode=66 LSAN_OPTIONS=exitcode=66:report_objects=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /tmp/equipego-dp12-y8kKGm/build-asan/delivery_sim /home/leandro/equipego_benchmarks/dp11/asan_20261013T153122Z/sigterm_rep_03/config.json --headless --duration 0 --scale 60 --log /home/leandro/equipego_benchmarks/dp11/asan_20261013T153122Z/sigterm_rep_03/events.jsonl
```

Datos crudos: `sigterm_rep_NN/stdout.txt`, `stderr.txt`, `events.jsonl`, `config.json`, `metadata.json`.

## Interpretacion y reejecucion

Anotar las anomalias, cambios realizados en el codigo y un nuevo ensayo con esta misma herramienta en otro directorio. Si no se detectaron errores, indicar 'sin cambios por hallazgos' y repetir para corroborar. No borrar reportes originales.
