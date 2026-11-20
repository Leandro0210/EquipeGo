# Feature 06 — Stencil: cobertura espacial de couriers (etapa CPU)

## Inspiración directa en los ejemplos de EC2

- `ec2/06_stencil/02_stencil_2d.cu`: cada celda lee vecinos norte/sur/este/oeste.
- `ec2/06_stencil/03_jacobi_sweeps.cu`: iteraciones Jacobi con **dos buffers**, intercambiados al concluir cada barrido.
- `ec2/06_stencil/bikeshare_station_coverage.cu`: convertir posiciones fijas de estaciones en fuentes de cobertura, fijar la máscara y difundir con coeficientes `cC` y `cN`. Aquí reemplazamos estaciones de bicicletas por couriers sintéticos sobre nodos OSM.

## Algoritmo

Campo 2D de lado `N`; bordes con Dirichlet fijo 0. Las celdas con couriers tienen cobertura fija 1. Para cada otra celda interior y cada iteración:

`u_next[p] = 0.2 * u[p] + 0.2 * (u[N] + u[S] + u[E] + u[O])`

La expresión usa **solo el buffer anterior**. Tras completar todas las celdas se intercambian buffers. Los valores permanecen entre 0 y 1, sin barreras entre bloques CPU porque cada `std::thread` escribe filas disjuntas. `20` barridos son un escenario de medición, no una convergencia física calibrada.

## Procedencia de datos

`preparar_couriers06.py` toma nodos geográficos de `Resultados_Feature03/Ciudad03/config/ciudad03_osm_scc_smoke8.json` y genera **5000 couriers SINTÉTICOS** con selección determinista (semilla 6062026) sobre esos nodos; no son posiciones observadas ni datos históricos. Guarda un manifiesto SHA-256 de la configuración y CSV en `Resultados_Feature06/Entradas` (fuera del repositorio).

## CPU y medidas

- Referencia: CPU secuencial (1 hilo), salida exacta y determinista.
- Paralela: 1, 2, 4, 8, 16 hilos, partición por filas y doble buffer.
- Tamaños: 32, 64, 128, 256, 512 por lado; 20 barridos; 10 repeticiones.
- 300 filas de medición, verificadas contra CPU secuencial en cada configuración.
- Cada tiempo medido incluye creación/unión de hilos por barrido, asignación de buffers y cálculo. **No** es latencia del dispatcher. No usar la medición con sanitizers o Debug.

## Integración y próximos pasos

Esta etapa agrega un módulo diagnóstico aislado que compila `USE_CUDA=OFF`. **No modifica** `Fleet`, `Dispatcher`, `RoadNetwork` ni la ventana. La Etapa 2 portará la referencia a CUDA T4 usando un kernel básico y otro `jacobi5_tiled_kernel` de `bikeshare_station_coverage.cu`; se contrastará GPU contra CPU y se medirá kernel vs tiempo total H2D+kernels+D2H. Después se evaluará una integración optativa y segura en el simulador usando posiciones del Fleet.

## Validación

- `cmake ... -DUSE_CUDA=OFF -DEQUIPEGO_BUILD_BENCHMARK=ON`
- `ctest --output-on-failure`: se espera el test nuevo `test_courier_coverage06`.
- `python3 ../Feature06_Stencil_CPU_Etapa1/preparar_couriers06.py`
- `bench_courier_coverage06 <dataset> <salida.csv>`
- `python3 ../Feature06_Stencil_CPU_Etapa1/resumir_benchmark06.py`

**No se hace commit ni push antes de aprobar la etapa completa.**


## Etapa 2 — Jacobi CUDA T4 (no integrado en Fleet)

- Referencia **concreta** EC2: `ec2/06_stencil/02_stencil_2d.cu` (vecinos),
  `ec2/06_stencil/03_jacobi_sweeps.cu` (dos buffers y 20 barridos),
  `ec2/06_stencil/bikeshare_station_coverage.cu` (`jacobi5_tiled_kernel`,
  tile con halo, `__shared__`, `__syncthreads()`, mascara de celdas fijas).
- Kernel `jacobiBasico06`: bloque 16x16, un hilo/celda, lecturas globales.
- Kernel `jacobiTiled06`: bloque 16x16; tile 16x16 y salidas 14x14.
- 0 en los bordes; fuente fija 1 donde hay courier, no se difunde la mascara.
- Se validan 20 barridos para 7 tamaños; tolerancia absoluta 1e-5.
- Benchmark: grids 32,64,128,256,512, 20 barridos, 10 repeticiones para
  `basic_global` y `tiled_shared` (100 filas, semilla 6062026).
- CUDA events: H2D, kernels, D2H; steady_clock: tiempo extremo a extremo,
  incluyendo asignaciones y liberaciones. No confundir kernel con total.
- GPU T4 se ejecuta en Google Colab. CPU WSL Ryzen y T4 Colab son máquinas
  diferentes; medir también CPU dentro de Colab para control de plataforma.
- Estado: benchmark GPU **aislado**, NO altera Fleet/Dispatcher ni garantiza
  aceleración; el siguiente paso será integrar una lectura real del Fleet.


## Etapa 3 - Snapshot observacional del Fleet (integracion real CPU)

- `EQUIPEGO_COVERAGE06=1`: captura **despues de finalizar/join** los couriers
  reales de una simulacion y produce PNG, CSV de 512x512 y manifest JSON.
- `EQUIPEGO_COVERAGE06_PREFIX=...`: prefijo para la salida; nunca sobrescribe
  evidencias. Si hay error de diagnostico se registra y el resumen del simulador
  conserva su comportamiento, sin cambiar Fleet o Dispatcher.
- Utiliza posiciones por `Courier::getSnapshot()` (nodo actual o progreso en
  segmento), proyectadas a `MapBounds` y luego `construirCampo`/`evolucionar`
  con 20 iteraciones Jacobi. Se excluyen couriers Broken.
- **Dato real del programa = posicion de los couriers de la simulacion**;
  NO se confunde con las 5000 muestras sinteticas del benchmark. La captura
  post-join no equivale a un monitoreo en tiempo real.
- Cobertura diagnostica **euclidea sobre imagen**, no una medida de rutas viales
  ni una modificacion al dispatcher. La version GPU integrada con propietario
  CUDA se introducira en otra etapa y requiere su propia validacion T4.
- Referencia EC2: `06_stencil/02_stencil_2d.cu`, `03_jacobi_sweeps.cu`
  y `bikeshare_station_coverage.cu`.


## Etapa 4 — Cobertura integrada por el propietario CUDA

- `EQUIPEGO_COVERAGE06=1` habilita el diagnostico de snapshots reales del Fleet.
- `EQUIPEGO_COVERAGE06_GPU=1` solicita calcular los 20 barridos Jacobi en el
  **mismo hilo propietario CUDA** empleado por Histogram07, Reduction08,
  Scan09 y Convolution05. Si no hay CUDA, hay error o la diferencia absoluta
  supera 1e-5, conserva el resultado CPU y registra `STENCIL06_GPU_FALLBACK`.
- Se utiliza `BasicoGlobal` por defecto en runtime porque resulto mas rapido
  que `TiledShared` en todas las cuadrículas aisladas probadas en T4.
- `STENCIL06_GPU_OWNER` evidencia el uso de GPU y sus tiempos kernel/total;
  `STENCIL06_RUN,backend=GPU` confirma la salida del simulador.
- No modifica `Fleet`, `Dispatcher` ni el flujo de pedidos; el snapshot es
  **posterior a Fleet::join**. Salida diagnostica, no cobertura vial exacta.
- Se prueban referencias CPU, divergencias, excepciones y fronteras fijas.
- Referencia docente EC2: `06_stencil/02_stencil_2d.cu`,
  `03_jacobi_sweeps.cu` y `bikeshare_station_coverage.cu`.
