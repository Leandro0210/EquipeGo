# Feature 04 — Performance: AoS vs SoA — Etapa 1 CPU

**Repositorio:** EquipeGo, rama `feat/bloque2-cuda-03-09`, base `4beac07`.

## Objetivo

Comparar dos distribuciones de memoria de los **mismos campos** de una instantánea de repartidor:

- **AoS (Array of Structures):** vector de `CourierAoS04` de 24 bytes con posición, velocidad, pedidos reservados, capacidad y estado.
- **SoA (Structure of Arrays):** seis vectores contiguos de los mismos tipos; nominalmente 24 bytes por registro (sin contar overhead por vector).

Ambas versiones calculan el mismo **tiempo aproximado en línea recta** desde la posición del courier hasta el restaurante, más un tramo fijo hacia el cliente. No se trata de la distancia vial oficial ni de un cambio de selección del dispatcher. En la Etapa 1 la salida es exclusivamente experimental.

## Datos y alcance

`preparar_datos_layout04.py` toma 3.516 coordenadas de nodos reales OSM del escenario SCC y simula 5.000 instantáneas de repartidores. **Las ubicaciones son coordenadas reales de los nodos; velocidad, estado y capacidad son sintéticos.** No crea 5.000 hilos, no modifica `Courier`, `Fleet`, las calles ni los pedidos.

El dataset y su hash de origen se guardan fuera del repositorio, en `Resultados_Feature04/Entradas`.

## Ensayos

- `test_courier_layout04`: 11 tamaños (incluyendo 0, 1, 2000, 5000), 5 configuraciones de hilos, validación exacta AoS/SoA, tamaños SoA inválidos y estado no disponible. Esperado **20/20 CTest** con `USE_CUDA=OFF`.
- `bench_courier_layout04`: 5 tamaños x 5 configuraciones de hilos x 2 layouts x 10 repeticiones = **500 filas**. Cada repetición incluye aproximadamente 500.000 evaluaciones distribuidas en múltiples barridos, con objetivo ligeramente desplazado; se acumulan los resultados para evitar eliminar el trabajo intermedio. El benchmark normaliza tiempo por barrido. Hilos: 1,2,4,8,16.
- `resumir_benchmark04.py` comprueba integridad del CSV y compara medianas por barrido para cada configuración. Menor tiempo es mejor; aceleración SoA = mediana AoS / mediana SoA.

Los ensayos se ejecutan en el Ryzen 7 5825U / WSL y la GPU se aborda **después**, en la Etapa 2. No afirmar aceleración GPU a partir de estos datos.

## Riesgos y límites

- La proyección de coordenadas usa aproximación equirectangular local para medir una distancia aproximada, NO rutas ni ETA real de EquipeGo.
- Los hilos trabajan sobre **copias inmutables** y regiones de salida disjuntas; el simulador concurrente permanece intacto.
- La medición de CPU incluye el costo de lanzar/juntar hilos, pero excluye creación de datos y conversión AoS→SoA. Medir conversión y H2D/D2H por separado en la etapa GPU.
- El resultado AoS/SoA depende de hardware, cachés, selección de campos y tamaño; ninguna organización es siempre mejor.


## Etapa 2: GPU AoS vs SoA (experimental)

`src/analytics/CourierLayoutCuda04.cu` implementa dos kernels sobre los campos
identicos del snapshot de repartidores, sin alterar Fleet ni Dispatcher.
Resultados: valores por repartidor, tiempos kernel (CUDA events), H2D, D2H
y total (incluye asignacion/liberacion de buffers, NO conversion host AoS->SoA).
GPU se habilita solamente con `USE_CUDA=ON`. El mismo experimento
abarca N=64, 256, 512, 2000, 5000; 10 reps por cada layout (100 filas GPU).
Se compara contra CPU con tolerancia de punto flotante; se anotan costes
H2D y D2H (SoA realiza seis copias frente a una en AoS).
Evidencia CPU (WSL) y CUDA (Colab T4) guardada fuera del repositorio.
NO es ETA de carreteras real y NO crea 5000 threads del simulador.
