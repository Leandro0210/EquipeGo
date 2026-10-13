# DP12.5 — Pruebas adversarias reproducibles

Fase: `signals`. Interfaz: `gui`. Ejecutable: `/home/leandro/build-equipego/delivery_sim`.

**Resultado: 3/3 PASS.**

| Caso | Estado | Exit | Pedidos | Averias | Cierre tras señal (s) |
|---|---|---:|---:|---:|---:|
| `immediate_sigint` | PASS | 0 | 0 | 0 | 0.218 |
| `burst_sigterm` | PASS | 0 | 103 | 0 | 0.116 |
| `breakdown_sigint` | PASS | 0 | 24 | 2 | 0.124 |

## Interpretacion

- Cada carpeta conserva el JSON exacto, stdout, stderr, JSONL y metadatos. Los fallos permanecen sin ocultarse.
- Las pruebas invalidas verifican salida distinta de cero, stderr y ausencia de inicio.
- Las pruebas validas comprueban transiciones, unicidad, tiempos, salida JSONL y balance final.
- No se puede inferir FIFO de llegada al restaurante a partir del JSONL sin un evento de llegada; requiere prueba de PickupGate.
- La evidencia de senales usa SIGINT o SIGTERM reales y mide el cierre despues de enviarlas.
- Ejecutar --mode headless solo con fines diagnosticos; no sustituye pruebas GUI de la rubrica.
- Ninguna bateria finita prueba ausencia universal de carreras o errores de memoria. Complementar con TSAN y ASAN.
