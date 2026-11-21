# Feature 07 — Etapa 3, benchmark a escala

Referencia de JJ: `ec2/07_histogram/04_private_shared.cu` y `bikeshare_trip_durations.cu`.

## Qué mide

5 tamaños: 10.000, 100.000, 1.000.000, 5.000.000 y 10.000.000 duraciones sintéticas deterministas (`seed=2026`) repartidas en 0..90 minutos. Son pruebas de escala, **no registros observados de la simulación**. El programa primero calcula la referencia CPU y comprueba que cada ejecución CPU o GPU coincide exactamente y suma N.

Por cada tamaño: calentamiento por implementación, 10+ repeticiones, alternando un orden barajado (semilla fija). Versiones: CPU secuencial, CPU multihilo con 1,2,4,... hilos hasta el máximo configurado, GPU atómicos globales y GPU compartida. Emitimos una fila CSV por repetición.

La GPU devuelve `kernel_ms` (cudaEvent) y `gpu_total_ms` (cudaMalloc/copies/kernel/cudaFree y costes internos de llamada, sin tiempo de espera de la cola). `request_ms` mide el tiempo alrededor de la solicitud por `future`, **incluye la copia del vector CPU hacia la cola**, y puede ser notablemente mayor. Para las gráficas `time_ms` es CPU wall-clock o GPU total interno. Esto no equivale al rendimiento de la simulación completa.

## Compilar en WSL (laptop)

```bash
g++ -O3 -std=c++17 -pthread -Isrc bloque2_cuda/07_histograma/benchmark_histograma07.cpp -o /tmp/bench07_cpu
/tmp/bench07_cpu --machine laptop --max-threads 8 --reps 10 --out /tmp/laptop07.csv
```

Sustituir 8 por el número de procesadores lógicos (`nproc` en WSL puede diferir de la topología física de Windows). Mantener compilación Release y registrar modelo CPU, núcleos y flags. Compilar sin CUDA es intencional. Para validación rápida no puntuable, añadir `--quick --reps 2`.

## Compilar en Colab T4

```bash
nvcc -O3 -std=c++17 -arch=sm_75 -Xcompiler=-pthread -DEQUIPEGO_BENCH_CUDA -Isrc \
    bloque2_cuda/07_histograma/benchmark_histograma07.cpp \
    src/analytics/DeliveryHistogramGpu.cpp \
    src/analytics/DeliveryHistogramCuda.cu -o /content/bench07_gpu
/content/bench07_gpu --machine colab --max-threads 2 --reps 10 --gpu --out /content/colab07.csv
```

Se supone que Colab contiene el checkout más reciente más archivos de la Feature 07 cargados antes, porque aún no están pusheados. `--gpu` solo se admite en el ejecutable CUDA.

## Salidas y pendientes de la rúbrica

`plot_histograma07.py` combina ambos CSV para el resumen, tabla principal y 2 imágenes. La gráfica de tiempo fuera del kernel incluye **todos** los costes no-kernel (no prueba por sí sola qué fracción específica corresponde a transferencias). La rúbrica pide además perfilar copias con `nsys`, profiling/ocupación con `ncu` si está disponible, compute-sanitizer, medir efecto sobre simulación completa, y CSV equivalente a través de `--headless --bench`. Este benchmark separado es una etapa experimental inicial, no sustituye esos requisitos.
