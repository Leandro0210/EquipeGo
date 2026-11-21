# Feature 07 — Histograma de tiempos de entrega (etapa CPU)

**Estado:** version CPU implementada; GPU y benchmark integrado pendientes.
**Referencia del docente:** `ec2/07_histogram/bikeshare_trip_durations.cu` y
`ec2/07_histogram/04_private_shared.cu`.

La duracion se calcula como `t(orderDelivered) - t(orderCreated)` en milisegundos
simulados, reutilizando la auditoria de eventos existente en `src/main.cpp`.
Hay 61 bins: 0..59 minutos, y el bin 60 incluye todas las duraciones >=60 minutos.
La suma de los contadores debe coincidir exactamente con el numero de entregas.

El camino CPU normal recorre las duraciones una vez. La version CPU multihilo usa
histogramas privados por hilo y una fusion final sin condiciones de carrera.
La siguiente etapa sera anadir CUDA (`atomicAdd` y bins privados en `__shared__`)
y un unico hilo propietario de GPU con cola/futures. No se creara el commit de la
feature hasta terminar y verificar CPU/GPU, CMake OFF/ON y el benchmark.
