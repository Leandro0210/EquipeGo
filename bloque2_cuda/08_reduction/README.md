# Feature 08 — Reduction: argmin de ETA (CPU + CUDA + despacho real)

## Propósito y fuente docente

Implementa la selección del repartidor con menor ETA. Según la rúbrica del Bloque 2, el
criterio determinista es la clave **(eta, courierId)**: ETA ascendente y, en empate,
identificador de repartidor en orden lexicográfico.

Material EC2 de referencia conceptual: `ec2/08_reduction/01_sequential_reduction.cu`,
`03_convergent_kernel.cu`, `04_shared_memory_kernel.cu`, `12_any_length_kernel.cu`
y `bikeshare_nearest_truck.cu`. La implementación de EquipeGo es propia.

## Código

- `src/analytics/EtaReduction.hpp`: CPU secuencial y multihilo; filtra ETA no finita,
  ETA negativa y candidatos no disponibles. Su primitiva general compara la pareja
  `(ETA, indice numerico)` y es también la referencia del benchmark aislado.
- `src/analytics/EtaReductionCuda.cu` y `EtaReductionGpu.hpp`: reducción CUDA en dos
  kernels (`reducirBloques` y `reducirFinal`), cada bloque utiliza memoria compartida;
  misma regla numérica en CPU y GPU.
- `src/analytics/EtaDispatchOrder.hpp`: transforma cada `courierId` en su **rango
  lexicográfico** dentro de la lista de cotizaciones; la reducción numérica elige
  entonces exactamente el menor `(ETA, courierId)` y se recupera el `courierIndex`
  original. Se conserva `courierIndex` como desempate defensivo si hay IDs iguales.
- `src/analytics/EtaGpuDispatch.hpp`: comparte ese mapeo con el kernel; comprueba el
  resultado frente a CPU, tiene deadline y fallback a CPU ante timeout o excepción,
  y conserva los candidatos restantes ordenados para reintentos.
- `src/analytics/DeliveryHistogramGpu.hpp/.cpp`: cola propietaria CUDA compartida
  con Feature 07; se evita usar CUDA simultáneamente desde distintos hilos.
- `src/dispatch/DispatchLoop.cpp`: la ruta de despacho real; después de elegir al
  candidato, `Courier::tryAssignOrder` vuelve a validar el estado y la capacidad.
  `Dispatcher::assignBestCourier` utiliza también el orden `(ETA, courierId)`.

**Decisión de diseño:** el paso de rangos es `O(n log n)` en CPU. Por eso, los benchmarks
aislados del backend CUDA (que reciben índices numéricos) no representan por sí solos
el costo end-to-end del dispatcher cuando hay miles de IDs. No debe afirmarse un speedup
integral a partir de los tiempos del kernel.

## Verificaciones ejecutadas (noviembre 2026)

- CPU sin CUDA: `14/14` pruebas CTest antes de la corrección del desempate.
  Las pruebas del nuevo desempate deben ejecutarse de nuevo después del ajuste.
- CUDA T4 en Colab: prueba de referencia CPU/GPU correcta; Memcheck sin errores y
  Racecheck sin alertas en la ejecución probada.
- Simulación integral CUDA: `intentos=101, usos=101, timeouts=0, errores=0,
  diferencias=0`, con `EQUIPEGO_GPU08_MIN_QUOTES=1`, en una hora simulada.
- Benchmark: cinco tamaños (1.000, 10.000, 100.000, 1.000.000 y 10.000.000),
  diez repeticiones, CPU secuencial y multihilo contra GPU kernel/backend/solicitud.
  El tiempo de solicitud completa a GPU fue mayor que CPU en los cinco tamaños
  ensayados. No confundir la mejora del kernel con una mejora integral.
- Nsight Systems y Nsight Compute: reportes almacenados para los dos kernels.
  En la ejecución perfilada de un millón de entradas, Nsys observó ~0,118 ms de
  kernels y ~5,343 ms de H2D. Son mediciones perfiladas, no medianas del benchmark.
- ThreadSanitizer: simulación CPU instrumentada finalizó con `EXIT_TSAN=0`, sin
  avisos en esa ejecución. No sustituye a la verificación concurrente de CUDA.

## Cómo repetir las pruebas CPU mínimas

```bash
cmake -S . -B "$HOME/equipego08-build" -DCMAKE_BUILD_TYPE=Release \
  -DUSE_CUDA=OFF -DENABLE_TSAN=OFF
cmake --build "$HOME/equipego08-build" -j 4
ctest --test-dir "$HOME/equipego08-build" --output-on-failure
```

Pruebas críticas de la corrección de desempate: `test_eta_dispatch_order` y
`test_eta_gpu_dispatch`, que incluyen empates `c10` versus `c2`.

## Evidencias

El código reside en la rama `feat/bloque2-cuda-03-09`; los archivos originales,
CSV, gráficas, logs, sanitizers y perfiles están fuera del repositorio, en:

`../Entregables_Bloque2_CUDA/08_reduction/`

Revisar el manifiesto SHA256 allí. La extracción de ocupación y métricas de memoria
de los `.ncu-rep`, la comparación roofline y la evaluación urbana global quedan
como análisis complementarios; no se deben inventar datos ausentes.
