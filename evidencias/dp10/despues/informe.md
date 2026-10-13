# EquipeGo DP10 — DESPUES (ETA / Dispatcher)

- Codigo Git al ejecutar: `3d3a9afbe2da4effa710a4175b3cf12a1edc4e70`.
- Hash SHA256 del ejecutable: `c3bbfa4e075f0c6e047bc481fd9fa742e9c0a7150c2e4390d4a715127d7b356e`.
- Flotas: 8, 32, 64; repeticiones: 3.
- Tiempo: 900 segundos simulados a escala 60.0x.
- Solo cambia fleet.couriers entre flotas; la configuracion original y semilla se mantienen.
- Para ANTES/DESPUES, comparar siempre la misma flota, carga, mapa, semilla y duracion.
- CPU(s) mide TODO el proceso, no solo el dispatcher.
- P95 asignacion es tiempo simulado desde orderCreated hasta orderAssigned; incluye esperas por capacidad.
- Porcentaje de CPU refiere a un nucleo (puede superar 100% por paralelismo).

| Motos | Creados | Asignados | Entregados | Rechazados | Pendientes | CPU(s) | CPU % | P95 asignacion (ms sim) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 337.0 | 41.0 | 17.0 | 268.3 | 51.7 | 0.79 | 5.02% | 578049.67 |
| 32 | 337.0 | 154.0 | 58.0 | 139.0 | 140.0 | 1.14 | 7.24% | 519804.62 |
| 64 | 337.0 | 285.7 | 96.7 | 11.0 | 229.3 | 1.61 | 10.44% | 204061.97 |

## Interpretacion

La duracion real debe ser aproximadamente duracion_simulada / escala.
Ese tiempo de pared NO representa un speedup del algoritmo ETA.
Los pedidos pendientes al corte NO equivalen a pedidos perdidos.
Los tiempos de asignacion tambien dependen de saturacion de bolsa y cola.
Los resultados de repeticiones pueden variar por intercalado de hilos.
No concluir mejoria causal sin comparacion con el codigo DESPUES.
