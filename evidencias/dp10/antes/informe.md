# EquipeGo DP10 — ANTES (ETA / Dispatcher)

- Codigo Git al ejecutar: `3d3a9afbe2da4effa710a4175b3cf12a1edc4e70`.
- Hash SHA256 del ejecutable: `f611d6fcda8bfc519d12f77416ef3df6cd4b0b0651036582b9a9aa2377299d47`.
- Flotas: 8, 32, 64; repeticiones: 3.
- Tiempo: 900 segundos simulados a escala 60.0x.
- Solo cambia fleet.couriers entre flotas; la configuracion original y semilla se mantienen.
- Para ANTES/DESPUES, comparar siempre la misma flota, carga, mapa, semilla y duracion.
- CPU(s) mide TODO el proceso, no solo el dispatcher.
- P95 asignacion es tiempo simulado desde orderCreated hasta orderAssigned; incluye esperas por capacidad.
- Porcentaje de CPU refiere a un nucleo (puede superar 100% por paralelismo).

| Motos | Creados | Asignados | Entregados | Rechazados | Pendientes | CPU(s) | CPU % | P95 asignacion (ms sim) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 337.0 | 41.0 | 17.0 | 269.0 | 51.0 | 0.83 | 5.26% | 573433.67 |
| 32 | 337.0 | 154.0 | 58.0 | 139.0 | 140.0 | 1.06 | 6.89% | 519910.40 |
| 64 | 337.0 | 282.3 | 93.3 | 14.7 | 229.0 | 1.99 | 13.02% | 202121.80 |

## Interpretacion

La duracion real debe ser aproximadamente duracion_simulada / escala.
Ese tiempo de pared NO representa un speedup del algoritmo ETA.
Los pedidos pendientes al corte NO equivalen a pedidos perdidos.
Los tiempos de asignacion tambien dependen de saturacion de bolsa y cola.
Los resultados de repeticiones pueden variar por intercalado de hilos.
No concluir mejoria causal sin comparacion con el codigo DESPUES.
