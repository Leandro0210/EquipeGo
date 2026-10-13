# DP12.5 — Pruebas adversarias reproducibles

Fase: `valid`. Interfaz: `gui`. Ejecutable: `/home/leandro/build-equipego/delivery_sim`.

**Resultado: 7/7 PASS.**

| Caso | Estado | Exit | Pedidos | Averias | Cierre tras señal (s) |
|---|---|---:|---:|---:|---:|
| `baseline` | PASS | 0 | 207 | 0 | - |
| `slots_one` | PASS | 0 | 207 | 0 | - |
| `bag_one` | PASS | 0 | 207 | 0 | - |
| `burst_heavy` | PASS | 0 | 2016 | 0 | - |
| `breakdowns_one` | PASS | 0 | 687 | 8 | - |
| `unreachable` | PASS | 0 | 639 | 0 | - |
| `large_64` | PASS | 0 | 59 | 0 | - |

## Interpretacion

- Cada carpeta conserva el JSON exacto, stdout, stderr, JSONL y metadatos. Los fallos permanecen sin ocultarse.
- Las pruebas invalidas verifican salida distinta de cero, stderr y ausencia de inicio.
- Las pruebas validas comprueban transiciones, unicidad, tiempos, salida JSONL y balance final.
- No se puede inferir FIFO de llegada al restaurante a partir del JSONL sin un evento de llegada; requiere prueba de PickupGate.
- La evidencia de senales usa SIGINT o SIGTERM reales y mide el cierre despues de enviarlas.
- Ejecutar --mode headless solo con fines diagnosticos; no sustituye pruebas GUI de la rubrica.
- Ninguna bateria finita prueba ausencia universal de carreras o errores de memoria. Complementar con TSAN y ASAN.
