# EquipeGo — DP.11 | Informe final de análisis dinámico

## 1. Objetivo y alcance

Evaluar una compilación instrumentada del simulador completo en Linux/WSL, con productor, dispatcher, flota, rescate, ráfagas de pedidos, averías y apagado coordinado por SIGINT/SIGTERM.

Las pruebas usan **copias** de `config/equipetrol.json` con `bagCapacity=1`, `meanIntervalMs=5000`, `burstMax=12`, `maxPending=20` y `breakdownProbability=1.0`. No modifican el JSON original.

## 2. Herramientas y versiones

- **ThreadSanitizer (TSAN):** detecta carreras de datos entre hilos.
- **AddressSanitizer (ASAN):** detecta accesos ilegales a memoria.
- **LeakSanitizer (LSAN):** comprueba fugas al terminar el proceso (`ASAN_OPTIONS=detect_leaks=1`).
- **UndefinedBehaviorSanitizer (UBSAN):** detecta comportamientos indefinidos instrumentados.

| Grupo | Herramienta | GCC (g++) | SHA256 ejecutable (12 primeros) |
|---|---|---|---|
| `tsan_inicial` | TSAN | g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0 | `b1f678cdaae7` |
| `tsan_repeticion` | TSAN | g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0 | `b1f678cdaae7` |
| `asan_inicial` | ASAN + LSAN + UBSAN | g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0 | `ac7910b9e971` |
| `asan_repeticion` | ASAN + LSAN + UBSAN | g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0 | `ac7910b9e971` |

Las versiones completas de GCC, CMake, Linux, `libtsan`, `libasan`, `liblsan` y el listado de bibliotecas enlazadas están en el `versiones_herramientas.json` **original** de cada grupo.

**Configuraciones de compilación usadas:**

```bash
cmake -S . -B ~/build-equipego-tsan -DENABLE_TSAN=ON
cmake --build ~/build-equipego-tsan --target delivery_sim -j4

cmake -S . -B ~/build-equipego-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build ~/build-equipego-asan --target delivery_sim -j4
```

Cada `metadata.json` conserva el **comando exacto**, las variables de entorno del sanitizador, la señal enviada y los tiempos. Los `parametros.json` conservan rutas, hashes, duración, escala y límites del ensayo.

## 3. Resultados completos

| Grupo | Señal | Pedidos | Asignados | Averías | Ráfaga máxima | Cierre (s) | Estado |
|---|---|---:|---:|---:|---:|---:|---|
| `tsan_inicial` | SIGINT | 469 | 8 | 8 | 12 | 0.114 | **PASS** |
| `tsan_inicial` | SIGTERM | 478 | 8 | 8 | 12 | 0.114 | **PASS** |
| `tsan_repeticion` | SIGINT | 287 | 8 | 8 | 12 | 0.319 | **PASS** |
| `tsan_repeticion` | SIGINT | 498 | 8 | 8 | 12 | 0.165 | **PASS** |
| `tsan_repeticion` | SIGTERM | 478 | 8 | 8 | 12 | 0.114 | **PASS** |
| `tsan_repeticion` | SIGTERM | 478 | 8 | 8 | 12 | 0.115 | **PASS** |
| `asan_inicial` | SIGINT | 305 | 8 | 8 | 12 | 0.366 | **PASS** |
| `asan_inicial` | SIGTERM | 517 | 8 | 8 | 12 | 0.164 | **PASS** |
| `asan_repeticion` | SIGINT | 443 | 8 | 8 | 12 | 0.518 | **PASS** |
| `asan_repeticion` | SIGINT | 478 | 8 | 8 | 12 | 0.320 | **PASS** |
| `asan_repeticion` | SIGTERM | 501 | 8 | 8 | 12 | 0.416 | **PASS** |
| `asan_repeticion` | SIGTERM | 498 | 8 | 8 | 12 | 0.315 | **PASS** |

**Total:** 12/12 ejecuciones válidas; 5430 pedidos creados, 96 averías acumuladas, cierre máximo 0.518 s y tiempo medio de cierre 0.253 s.

Todos los procesos terminaron con código 0; cada JSONL termina en `simulationStopping` exactamente una vez. Los contadores de pedidos coinciden con los eventos y la igualdad `creados = entregados + rechazados + pendientes` se cumple.

## 4. Hallazgos, correcciones y reejecución

**Hallazgos de los sanitizadores en estas pruebas:** ninguno. No se reportaron carreras de datos (TSAN), accesos inválidos (ASAN), fugas de memoria (LSAN habilitado) ni errores instrumentados de comportamiento indefinido (UBSAN).

**Correcciones de código derivadas de estas pruebas:** ninguna, porque los ensayos iniciales no presentaron fallos. No se inventaron hallazgos ni correcciones.

**Reejecución:** tras las primeras dos corridas por herramienta (una SIGINT y una SIGTERM), se ejecutaron otras cuatro corridas por herramienta (dos por señal), todas satisfactorias.

Las pruebas con instrumentación representan los escenarios ejecutados; no prueban ausencia universal de fallos ni sustituyen otros casos límite.

## 5. Referencias a evidencias originales

- `tsan_inicial/`: registros completos de TSAN: `informe.md`, `versiones_herramientas.json`, `parametros.json`, `sigint_rep_*/` y `sigterm_rep_*/` con config, stdout, stderr, JSONL y metadata por ejecución.
- `tsan_repeticion/`: registros completos de TSAN: `informe.md`, `versiones_herramientas.json`, `parametros.json`, `sigint_rep_*/` y `sigterm_rep_*/` con config, stdout, stderr, JSONL y metadata por ejecución.
- `asan_inicial/`: registros completos de ASAN + LSAN + UBSAN: `informe.md`, `versiones_herramientas.json`, `parametros.json`, `sigint_rep_*/` y `sigterm_rep_*/` con config, stdout, stderr, JSONL y metadata por ejecución.
- `asan_repeticion/`: registros completos de ASAN + LSAN + UBSAN: `informe.md`, `versiones_herramientas.json`, `parametros.json`, `sigint_rep_*/` y `sigterm_rep_*/` con config, stdout, stderr, JSONL y metadata por ejecución.
- `resumen_ejecuciones.csv`: tabla generada verificando las doce corridas.
- `benchmarks/dp11_dynamic_analysis.py`: reproduce cada experimento instrumentado.
- `benchmarks/dp11_finalize_report.py`: valida y consolida las evidencias sin alterar los archivos originales.

## 6. Comandos de reproducción

```bash
python3 benchmarks/dp11_dynamic_analysis.py --sanitizer tsan \
  --binary ~/build-equipego-tsan/delivery_sim --max-wait 120 --repeats 2
python3 benchmarks/dp11_dynamic_analysis.py --sanitizer asan \
  --binary ~/build-equipego-asan/delivery_sim --max-wait 120 --repeats 2
python3 benchmarks/dp11_finalize_report.py
```
