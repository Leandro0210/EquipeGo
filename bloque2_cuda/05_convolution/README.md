# Feature 05 — Convolution: mapa de calor espacial de pedidos

## Etapa 1: referencia CPU

- `OrderHeatmap05.hpp` incluye convolucion directa **5x5** y convolucion **separable 2 pasadas**, CPU multihilo.
- Kernel 5x5: producto exterior de `[1, 4, 6, 4, 1]` dividido por **256**.
- Borde: **zero padding**; las celdas fuera del mapa contribuyen cero (la masa total en el borde puede disminuir).
- Entradas: conteos enteros, resultados `float`; acumulacion interna en `int64_t` para equivalencia exacta entre los dos calculos.
- Fuente espacial: coordenadas de nodos OpenStreetMap de la configuracion de ciudad real de Santa Cruz, 4.000 nodos.
- Pedidos: **20.000 ubicaciones simuladas**, deterministas (semilla 20261120). **No son demanda observada**. La traza JSONL de EquipeGo no contiene el destino en el evento `orderCreated`.
- No modifica `OrderProducer`, `Fleet`, `Dispatcher` ni el flujo de pedidos; tampoco ejecuta 2.000 o 5.000 hilos de Courier.
- Benchmark: N de cuadricula 32, 64, 128, 256, 512, baseline directo 1 hilo y separable 1,2,4,8,16 hilos; **10 repeticiones por configuracion** (300 filas en total); CPU de WSL Ryzen 7.
- Etapa 2: kernel 2D CUDA con bloques, halos y memoria compartida, comparado contra esta referencia CPU; aun no implementado en E1.

Por reproducibilidad, entradas y resultados se guardan en `../Resultados_Feature05/` fuera del repositorio. Evidencias finales se copiaran a `../Entregables_Bloque2_CUDA/05_convolution/` en la etapa de cierre.

No activar CUDA en esta etapa; los tests CPU esperados aumentan de **20 a 21**. Sin commit hasta validar.


## Etapa 2 — CUDA (inspiración concreta EC2)

- `ec2/05_conv/02_conv2d_basic.cu`: baseline 2D, un hilo por píxel.
- `ec2/05_conv/03_conv2d_const_mem.cu`: máscara en `__constant__`.
- `ec2/05_conv/04_conv2d_tiled.cu` y `bikeshare_demand_heatmap.cu`:
  tile + halo y `__shared__`, barrera `__syncthreads()`.
- Se adaptó radio=2 (5×5), máscara binomial entera normalizada entre 256.
  El código del docente usa otros radios/coeficientes. Ponderar enteros y
  dividir al final garantiza equivalencia bit a bit para demanda 0..1e6.
- Kernel básico: 16×16 threads, un output por thread, 25 vecinos.
  Kernel tiled: 20×20 threads para leer un tile incluido halo, 16×16
  outputs. Zero padding idéntico a la referencia CPU.
- Cinco tamaños: 32, 64, 128, 256, 512. Diez repeticiones por algoritmo.
  CUDA events miden H2D, kernel y D2H; steady_clock mide el total incluyendo
  cudaMalloc, constant symbol copy y cudaFree. No hay integración aún en la
  ventana del simulador: es un experimento GPU aislado y reproducible.
- `USE_CUDA=OFF` mantiene el comportamiento CPU. Sin commit/push hasta probar.


## Etapa 3 - diagnostico de una simulacion (opt-in CPU)

Inspiracion EC2 especifica: `ec2/05_conv/bikeshare_demand_heatmap.cu`,
`02_conv2d_basic.cu`, `03_conv2d_const_mem.cu` y `04_conv2d_tiled.cu`.
La convolucion CPU usada aqui es el filtro binomial 5x5 de E1; la E2
implementa dos kernels CUDA equivalentes. Esta E3 conecta el concepto
con los eventos `orderCreated` reales de la EJECUCION del simulador,
pero **no son pedidos observados de personas reales**.

El evento trae `restaurant`, no el destino. Se cuenta cada `orderCreated`,
incluidos los posteriormente rechazados, y se proyecta sobre el nodo
OSM del restaurante. El resultado indica concentracion de ORIGENES.

- Por defecto: ninguna operacion ni archivo adicional (flag desactivado).
- `EQUIPEGO_HEATMAP05=1`: despues de la auditoria, generar PNG de
  superposicion, CSV por celda y JSON de procedencia (sin GUI ni CUDA).
- `EQUIPEGO_HEATMAP05_PREFIX=../Resultados_Feature05/Integracion05/corrida1`
  fija un prefijo UNICO para evitar sobrescribir evidencias.
- El diagnostico es no critico: si falla genera `HEATMAP05_RUN,enabled=0,error=...`
  y mantiene intactos los contadores de la simulacion.
- No agrega hilos ni usa CUDA durante el despacho. El backend GPU se
  integrara con el propietario CUDA en la siguiente etapa independiente.

Ejemplo en WSL, ejecutado desde `EquipeGo`:

```bash
EQUIPEGO_HEATMAP05=1 \
EQUIPEGO_HEATMAP05_PREFIX=../Resultados_Feature05/Integracion05/heatmap05_e3_smoke \
"$HOME/equipego05-build/delivery_sim_bench" config/equipetrol.json \
  --headless --scale 120 --duration 600 \
  --log ../Resultados_Feature05/Integracion05/sim05_e3_smoke.jsonl
```

Esperado: `HEATMAP05_RUN,enabled=1,backend=CPU` y JSON final normal.
La prueba automatizada `test_heatmap_runtime05` comprueba conteos,
equivalencia, PNG/CSV/JSON y proteccion contra sobrescritura.


## Etapa 4 — GPU real a traves del propietario CUDA (opt-in)

Inspiracion exacta del docente: `ec2/05_conv/bikeshare_demand_heatmap.cu`,
`03_conv2d_const_mem.cu` y `04_conv2d_tiled.cu`: filtro en constant memory,
entrada con halo y tile en shared memory.

Con EQUIPEGO_HEATMAP05=1 (datos del evento orderCreated agrupados por origen
restaurante) y EQUIPEGO_HEATMAP05_GPU=1, el hilo principal envia el grid
inmutable al propietario CUDA que ya atiende Histogram 07, Reduction 08,
Scan 09. Nunca se llama CUDA desde un hilo de courier o despacho.
El resultado se compara celda a celda con la version CPU. Si falla,
se conserva CPU, se escribe HEATMAP05_GPU_FALLBACK y sigue la simulacion.

- USE_CUDA=OFF: 23 CTests CPU, opt-in GPU reporta fallback USE_CUDA_OFF.
- USE_CUDA=ON: 31 CTests CPU + CUDA, incluidos owner real GPU.
- Default sin flags: comportamiento anterior intacto, sin mapa/transferencias.
- Origen de los datos: restaurantes de pedidos de SIMULACION, no destinos,
  no mediciones reales de actividad comercial.
- No se modifica despacho, ETA, callbacks de ordenes ni GUI.
- png/csv/json se generan al finalizar, en rutas prefijadas por el usuario.
- HEATMAP05_RUN,backend=GPU solo se emite tras equivalencia CPU/GPU exacta.
