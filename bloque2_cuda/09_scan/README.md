# Feature 09 — Scan: agrupación estable de pedidos por restaurante (etapa 1 CPU)

## Objetivo de la rúbrica
La rúbrica de GPU/EC2 propone «Orders grouped by restaurant: a counting sort whose exclusive scan gives where each restaurant's orders start». La agrupación debe coincidir exactamente con una ordenación estable de referencia en CPU.

## Referencias del material docente
- `ec2/09_scan/01_sequential_scan.cu`: prefijo inclusivo y exclusivo.
- `ec2/09_scan/04_brent_kung.cu`: Scan en memoria compartida.
- `ec2/09_scan/06_hierarchical_scan.cu`: Scan por bloques para tamaños arbitrarios.
- `ec2/09_scan/bikeshare_group_by_station.cu`: agrupación estable mediante posiciones calculadas con Scan.

## Archivos de esta etapa
- `src/analytics/RestaurantOrderScan.hpp`: API libre de CUDA para conteo, scan exclusivo y dispersión estable; incluye una versión paralela CPU y una referencia independiente `std::stable_sort`.
- `tests/test_restaurant_order_scan.cpp`: verifica que los resultados coincidan exactamente con el orden estable esperado. Incluye grupos vacíos, tamaños no múltiplos de 256 y entradas inválidas.
- `CMakeLists.txt`: registra `test_restaurant_order_scan` en CTest.

## Diseño y limitación explícita
Esta primera etapa NO altera `OrderBook`, la política de orden del despacho ni la cola CUDA. Solo valida la primitiva de agrupación independiente. En la siguiente etapa se integrará de forma segura sobre una instantánea de los pedidos pendientes y se conservará el orden de llegada dentro de cada restaurante; después vendrá CUDA opcional con el propietario compartido.

## Validación
```bash
cmake -S . -B "$HOME/equipego09-build" -DCMAKE_BUILD_TYPE=Release -DUSE_CUDA=OFF -DENABLE_TSAN=OFF
cmake --build "$HOME/equipego09-build" -j 4
ctest --test-dir "$HOME/equipego09-build" --output-on-failure
```

Esperado en Ubuntu WSL: 15 tests, si el árbol ya contenía los 14 de Feature 08. La primera etapa no habilita CUDA. No crear commit/push antes de validar la aplicación integrada.

## Etapa 2 — integración CPU observacional (sin cambiar FIFO)

- `src/analytics/RestaurantOrderLive09.hpp` mapea los `restauranteId` configurados a índices y toma una vista de los pedidos **en espera del Dispatcher**.
- `DispatchLoop.cpp` aplica `agruparPedidosScanCPU09` a cada lote real de espera y muestra `SCAN09_CPU,batches=...,observaciones=...,grupos=...,max_grupo=...` al terminar.
- `observaciones` son apariciones de pedidos a lo largo de los ciclos, **no pedidos únicos**.
- `test_restaurant_order_live09` prueba agrupación estable, índices originales, pedidos `Order` no copiables/no movidos, casos vacíos y restaurante desconocido.
- Esta etapa aún no cambia el orden de atención de pedidos ni usa CUDA. La salida es un diagnóstico de agrupación real para validar la integración antes de implementar GPU.

## Etapa 3 - backend CUDA y cola propietaria

- `RestaurantOrderScanGpu.hpp` y `RestaurantOrderScanCuda.cu`: histogramas por bloque,
  prefijos por restaurante, scan exclusivo jerarquico de los totales y
  dispersion **estable** de indices de llegada, sin mover objetos `Order`.
- La llamada `PropietarioGpuHistograma::enviarScan09` comparte el mismo hilo
  CUDA que Histogram07 y Reduction08.
- `test_restaurant_order_scan_gpu` comprueba contra `std::stable_sort` CPU
  multiples tamanos (incluyendo no multiplos de 256), restaurantes vacios,
  validez, mezcla de tareas 07/08/09, y la estabilidad exacta.
- `USE_CUDA=OFF` no crea dependencias CUDA. No se conecta aun el backend GPU
  con el dispatcher real: eso requiere validar previamente en Colab/T4.
- Limitacion deliberada: se materializa una matriz (bloques x restaurantes),
  acotada a 50 millones de celdas para evitar reservas excesivas. El rendimiento
  depende tanto del numero de pedidos como de restaurantes. La dispersion
  estable tiene un coste local O(256) por elemento; no se puede asumir speedup.

## Fuentes docentes para Scan

Conceptos de `ec2/09_scan/04_brent_kung.cu`, `06_hierarchical_scan.cu` y
`bikeshare_group_by_station.cu`. El algoritmo concreto es una adaptacion
independiente para la interfaz de EquipeGo.

## Etapa 4 - Integracion observacional CUDA en Dispatcher (validada)

- `src/analytics/RestaurantOrderGpuDispatch09.hpp` compara la agrupacion GPU
  (`starts`, `indices`) con la referencia CPU. Usa fallback por timeout (25 ms),
  excepcion, futuro invalido y diferencia; no modifica el orden de `waiting`.
- `Dispatcher::run` observa la cola real y registra `SCAN09_CPU` y `SCAN09_GPU`
  sin usar el orden agrupado para seleccionar couriers ni tocar `OrderBook`.
  Los contadores son observaciones por lote, no pedidos unicos.
- `main.cpp` conecta `PropietarioGpuHistograma::enviarScan09` al mismo propietario
  que Histogram07 y Reduction08 solamente si `USE_CUDA=ON`.
- Por defecto, `EQUIPEGO_GPU09_MIN_ORDERS=16`; para pruebas reales T4 puede
  fijarse a 1. Con `USE_CUDA=OFF` conserva la vista CPU, y no usa NVCC.
- `test_restaurant_order_gpu_dispatch09` (CPU sin GPU) comprueba fallback,
  timeouts, desempate de ubicaciones, errores y FIFO intacto.
- Con un plazo de 25ms, una GPU ocupada puede agotar el tiempo y recurrir a
  CPU. Las tareas encoladas siguen siendo propiedad del propietario CUDA;
  el uso de GPU no garantiza mejoras de latencia ni afecta asignaciones.
- La integracion GPU del simulador fue validada posteriormente en la Etapa 5.


## Etapa 5 - Validacion integral

- Compilacion CUDA, CTest y simulacion integral en Google Colab: aprobadas.
- Solicitudes Scan GPU: 43; usos efectivos: 40; timeouts: 3.
- Errores GPU: 0; diferencias CPU/GPU: 0.
- Balance global de pedidos: correcto.
- El dispatcher conserva FIFO y utiliza la agrupacion como observacion.
- Evidencias externas: `../Entregables_Bloque2_CUDA/09_scan/`.

## Etapa 6 - Resultados experimentales

- 24 configuraciones, cinco repeticiones: 120 mediciones.
- Lotes de 8 a 1.000.000 pedidos, 6 y 64 restaurantes.
- Distribuciones uniforme y concentrado80.
- Equivalencia exacta CPU/GPU confirmada.
- El tiempo total GPU supero al CPU secuencial en 1 de 24 configuraciones.
- Caso excepcional: 100.000 pedidos, 64 restaurantes, uniforme:
  CPU 6,591129 ms; GPU total 1,117216 ms; speedup 5,8996x.
  Debe interpretarse con cautela por el tiempo CPU inusualmente alto.
- La mayoria de configuraciones favorece CPU secuencial en tiempo total.
- El tiempo del kernel por si solo no demuestra aceleracion integral.
- Estas mediciones corresponden al backend aislado, no a la latencia
  end-to-end del despacho.

Los CSV, graficos, logs y manifiestos SHA256 se conservan fuera del repositorio.
