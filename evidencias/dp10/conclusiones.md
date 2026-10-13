# DP.10 — Optimización del cálculo ETA

## Objetivo
Reducir cálculos repetidos de rutas en el despachador concurrente.

## Cambio de diseño
ANTES: cada cotización calcula por separado las rutas
moto-restaurante y restaurante-cliente.

DESPUÉS: una caché concurrente permite reutilizar las
distancias de rutas repetidas. Los cálculos compartidos
se coordinan mediante mutex, promise y shared_future.

## Condiciones experimentales
- Flotas: 8, 32 y 64 motos.
- Duración: 900 segundos simulados.
- Escala: 60x.
- Repeticiones: 3 por escenario y versión.
- Mismo mapa, generador, semilla y configuración,
  excepto la cantidad de motos en cada escenario.
- Se cambia el algoritmo del dispatcher entre versiones.

## Resultados

| Motos | CPU antes (s) | CPU después (s) | Reducción de CPU |
| ---: | ---: | ---: | ---: |
| 8 | 0.83 | 0.79 | 5.19% |
| 32 | 1.06 | 1.14 | -7.36% |
| 64 | 1.99 | 1.61 | 19.16% |

## Conclusión
La optimización redujo aproximadamente 19.16% el tiempo
de CPU total con 64 motos. No hubo mejora uniforme:
con 32 motos se observó un aumento del 7.36%.

Los resultados sugieren un beneficio de reutilizar
cálculos de rutas en escenarios de flotas grandes.

Las mediciones tienen variabilidad entre ejecuciones.
Los pedidos asignados y entregados también pueden variar
por el intercalado de los hilos.

El P95 medido corresponde al tiempo hasta la asignación,
no exclusivamente al cálculo ETA.

## Reproducibilidad
Los archivos originales de las ejecuciones están en
antes/ y despues/, incluyendo configuración, eventos,
salidas y resultados. Las configuraciones generadas
contienen rutas locales del equipo de ejecución;
el script benchmarks/dp10_eta_before_after.py permite
generar nuevas configuraciones en otro entorno.

Se conserva el código previo en el historial de Git,
antes de incorporar SharedDistanceCache.
