# DP12.5 — Pruebas adversarias reproducibles

Fase: `invalid`. Interfaz: `gui`. Ejecutable: `/home/leandro/build-equipego/delivery_sim`.

**Resultado: 12/12 PASS.**

| Caso | Estado | Exit | Pedidos | Averias | Cierre tras señal (s) |
|---|---|---:|---:|---:|---:|
| `missing_argument` | PASS | 1 | - | - | - |
| `malformed` | PASS | 1 | - | - | - |
| `missing_map` | PASS | 1 | - | - | - |
| `missing_nodes` | PASS | 1 | - | - | - |
| `dangling_street` | PASS | 1 | - | - | - |
| `dangling_restaurant` | PASS | 1 | - | - | - |
| `slots_zero` | PASS | 1 | - | - | - |
| `bounds_reversed` | PASS | 1 | - | - | - |
| `missing_image` | PASS | 1 | - | - | - |
| `probability_invalid` | PASS | 1 | - | - | - |
| `time_scale_zero` | PASS | 1 | - | - | - |
| `fleet_zero` | PASS | 1 | - | - | - |

## Interpretacion

- Cada carpeta conserva el JSON exacto, stdout, stderr, JSONL y metadatos. Los fallos permanecen sin ocultarse.
- Las pruebas invalidas verifican salida distinta de cero, stderr y ausencia de inicio.
- Las pruebas validas comprueban transiciones, unicidad, tiempos, salida JSONL y balance final.
- No se puede inferir FIFO de llegada al restaurante a partir del JSONL sin un evento de llegada; requiere prueba de PickupGate.
- La evidencia de senales usa SIGINT o SIGTERM reales y mide el cierre despues de enviarlas.
- Ejecutar --mode headless solo con fines diagnosticos; no sustituye pruebas GUI de la rubrica.
- Ninguna bateria finita prueba ausencia universal de carreras o errores de memoria. Complementar con TSAN y ASAN.
