# DP10 — Comparacion ANTES/DESPUES

- Linea base: `/home/leandro/equipego_benchmarks/dp10/antes_20261012_201341`
- Nueva version: `/home/leandro/equipego_benchmarks/dp10/despues_20261012_202704`
- El protocolo verifica flota, duracion, escala, repeticiones y configuracion identicas.
- CPU mide todo el proceso. No cambiar el hardware ni ejecutar otras cargas durante las pruebas.

| Motos | CPU antes (s) | CPU despues (s) | Reduccion CPU (%) | Entregados antes | Entregados despues | P95 antes | P95 despues |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 0.83 | 0.79 | 5.19% | 17.0 | 17.0 | 573433.67 | 578049.67 |
| 32 | 1.06 | 1.14 | -7.36% | 58.0 | 58.0 | 519910.40 | 519804.62 |
| 64 | 1.99 | 1.61 | 19.16% | 93.3 | 96.7 | 202121.80 | 204061.97 |

Reduccion positiva indica menos tiempo de CPU; negativa indica regresion.
Una reduccion de CPU no basta si se pierden pedidos o incumplen limites.
