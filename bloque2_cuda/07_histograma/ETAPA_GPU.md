# Feature 07, etapa GPU: histograma de tiempos de entrega

## Procedencia de los ejemplos de JJ

- `ec2/07_histogram/04_private_shared.cu`: contadores privados por bloque en memoria compartida, `atomicAdd` y `__syncthreads()`.
- `ec2/07_histogram/bikeshare_trip_durations.cu`: histogramas de duraciones, comparación global vs compartida.

**La aplicación es una adaptación original para entregas de EquipeGo**, no una copia del sistema de bicicletas. En `src/main.cpp`, `auditEventLog()` reúne las duraciones simuladas de cada pedido entregado (desde `orderCreated` hasta `orderDelivered`).

## Arquitectura

- Un solo hilo CPU (`PropietarioGpuHistograma`) recibe solicitudes en una cola protegida por `mutex` y `condition_variable`.
- Otros hilos envían vectores de duraciones y reciben `std::future<ResultadoHistogramaGpu>`.
- Dos variantes GPU del **mismo histograma**: atómicos globales y privatización por bloque en `__shared__` (no cuentan como dos features).
- El resultado se compara bin a bin contra la referencia CPU y se usa en el resumen final.
- El tiempo GPU total incluye creación de contexto eventual, asignaciones, copias y liberaciones realizadas por llamada; el tiempo de kernel usa eventos CUDA.

## Compilación

Sin CUDA (en WSL):

```bash
cmake -S . -B "$HOME/equipego-b2-cpu-build" -DCMAKE_BUILD_TYPE=Release -DUSE_CUDA=OFF -DEQUIPEGO_BUILD_BENCHMARK=ON
cmake --build "$HOME/equipego-b2-cpu-build" -j4
ctest --test-dir "$HOME/equipego-b2-cpu-build" --output-on-failure
```

Con CUDA (en Colab T4, requiere instalar dependencias OpenCV/nlohmann JSON):

```bash
cmake -S . -B build-colab-gpu -DCMAKE_BUILD_TYPE=Release -DUSE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=75 -DEQUIPEGO_BUILD_BENCHMARK=ON
cmake --build build-colab-gpu -j4
ctest --test-dir build-colab-gpu -R 'test_delivery_histogram' --output-on-failure
```

## Estado

Esta etapa **requiere compilación y pruebas reales en Google Colab** antes de crear el commit. La CPU aislada y el proyecto completo ya se verificaron en la etapa anterior. Ningún tiempo GPU de esta etapa está medido hasta que ejecutes Colab.

El modo CSV de benchmarking, las mediciones con muchos tamaños, `compute-sanitizer`, la comparación con CPU multihilo de laptop y el ensayo de integración en una simulación completa se abordarán en etapa 3, antes del commit de la feature.
