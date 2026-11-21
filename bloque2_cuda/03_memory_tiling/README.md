# Feature 03 - Memory Tiling: rutas minimas por bloques

## Objetivo acordado

Floyd-Warshall por bloques para una matriz **dirigida** de distancias viales entre
intersecciones de Equipetrol. La comparacion obligatoria es contra
`RoadNetwork::shortestRoute()` (Dijkstra), no contra distancias rectas.

En la configuracion `config/equipetrol.json`, `ConfigLoader.cpp` deriva la
longitud de cada calle desde las coordenadas y `RoadNetwork.cpp` utiliza
`street.lengthMeters`. La tabla de esta etapa usa esa **misma longitud**.
El campo `oneWay` se respeta de forma estricta: no agrega la arista inversa
cuando es verdadero. Conexiones inexistentes quedan en `+inf` y la consulta
externa devuelve `std::nullopt`.

## Etapa 1 - Referencia CPU (este kit)

- `src/map/FloydWarshallTiled03.hpp`: preparacion de matriz + version clasica
  y version CPU **por bloques** con tres fases (diagonal pivote, fila/columna,
  bloques restantes). Ambos usan double y comparan metros.
- `tests/test_floyd_warshall03.cpp`: casos de calles dirigidas, aristas paralelas,
  nodos aislados, bordes de tile, entradas invalidas y **todos los pares**
  de la configuracion real contra Dijkstra.
- `CMakeLists.txt`: agrega la prueba CPU `test_floyd_warshall03`, sin activar
  CUDA y sin cambiar el simulador.

No se modifica `RoadNetwork.cpp`, `Dispatcher.cpp`, ni el calculo ETA. Esta
etapa solo define y valida una tabla candidata; aun no se utiliza para
cotizaciones del dispatcher.

## Material de apoyo docente

- `ec2/03_memory_tiling/03_tiled_matmul.cu`: reutilizacion de tiles en shared.
- `ec2/03_memory_tiling/04_tiled_matmul_boundary.cu`: limites no multiplo tile.
- `ec2/03_memory_tiling/05_tile_size_occupancy.cu`: recursos por bloque y SM.

El algoritmo de Floyd-Warshall **no es** una multiplicacion de matrices;
la version CUDA posterior debera respetar las dependencias de sus tres fases
por cada bloque pivote, sin introducir carreras.

## Costo y limite deliberado

Tabla: `n*n*sizeof(double)` bytes. Floyd-Warshall: O(n^3) tiempo.
Por defecto, se rechazan configuraciones por encima de 512 intersecciones;
el limite es preventivo y no garantiza que 512 sea optimo.

Para el grafo principal de 30 nodos, una matriz de double representa
30*30*8 = 7200 bytes (solo datos, sin estructuras auxiliares).
Por su pequeno tamano es muy posible que GPU no supere al CPU en tiempo total;
esto se medira, no se supondra.

## Verificacion en WSL

```bash
cmake -S . -B "$HOME/equipego03-build" -DCMAKE_BUILD_TYPE=Release \
  -DUSE_CUDA=OFF -DENABLE_TSAN=OFF
cmake --build "$HOME/equipego03-build" -j 4
ctest --test-dir "$HOME/equipego03-build" --output-on-failure
"$HOME/equipego03-build/test_floyd_warshall03" config/equipetrol.json
```

## Plan de trabajo: Etapa 2 implementada, aun pendiente de validar

1. Integracion opcional de consultas en `RoadNetwork`/Dispatcher
   implementada en la Etapa 2, con fallback a Dijkstra; validar en WSL.
2. Tiling CUDA real en memoria compartida, con kernel de pivot/fila/columna/
   resto; `USE_CUDA=OFF` preservado.
3. CTest, GPU T4, Memcheck, Racecheck, benchmarks y perfiles.
4. Commit a `feat/bloque2-cuda-03-09` despues de validar.

Evidencias se almacenaran fuera del repo en
`../Entregables_Bloque2_CUDA/03_memory_tiling/`.


## Etapa 2 - Integracion CPU optativa (instalada; validar en WSL)

- `RoadNetwork::shortestDistanceMeters` devuelve solo metros; cuando la tabla esta
  desactivada invoca `shortestRoute` (Dijkstra original). La construccion CPU
  por bloques es optativa con `EQUIPEGO_FLOYD03_CPU=1`, por defecto desactivada.
- Antes de publicar la tabla inmutable se verifican **todos los pares** contra
  Dijkstra, manteniendo calles dirigidas, distancias y pares inalcanzables.
- `Dispatcher::quoteCandidates` conserva la formula ETA y `SharedDistanceCache`;
  solo las dos consultas escalares usan `shortestDistanceMeters`.
- `shortestRoute()` continua retornando el camino real con segmentos para el
  movimiento del courier; no hay cambios en el mapa ni en rutas geometricas.
- El opt-in se activa **antes de arrancar los hilos**. Si no puede calcularse o
  validarse, se informa y se vuelve a Dijkstra. Con 30 nodos hay 900
  verificaciones; para grafos muy grandes este costo de inicializacion debe
  medirse y la tabla no puede pasar el limite de 512 nodos definido en E1.
- `test_floyd_cache03` compara todos los pares con y sin tabla, usando el grafo
  real y otro grafo dirigido con nodo aislado. Prueba rutas completas intactas.
- El total de 19 pruebas CPU se ejecuta con `USE_CUDA=OFF`. La futura GPU real
  y los tiempos de consulta E2E todavia **no** estan validados.

Para probar un run corto (solo si las 19 pruebas pasan):

```bash
EQUIPEGO_FLOYD03_CPU=1 "$HOME/equipego03-build/delivery_sim_bench" \
  config/equipetrol.json --headless --scale 120 --duration 240 \
  --log ../Entregables_Bloque2_CUDA/03_memory_tiling/03_pruebas/sim_floyd03.jsonl
```


## Etapa 4 - Backend CUDA tiled (instalado, NO validado en T4)

- `FloydWarshallCuda03.cu`: algoritmo Floyd-Warshall dirigido FP64 en tres
  fases por pivote y tiles 16x16 con memoria compartida: diagonal, bordes,
  bloques restantes; usa barreras para evitar condiciones de carrera.
- `FloydWarshallCuda03.hpp`: interfaz para matrices densas de metros con +inf.
  Limite explicito 4096 nodos (4.000 requiere 128 MB de matriz FP64).
- `test_floyd_warshall_gpu03`: compara con referencia CPU y Dijkstra en
  grafos dirigidos, aislados, bordes no multiplo de tile y Equipetrol real.
- `USE_CUDA=OFF` debe conservar 19 CTest; con `USE_CUDA=ON` se agregara
  test 03 GPU, compartiendo la biblioteca CUDA de 07/08/09.
- La API GPU aun NO se conecta al dispatcher: se debe validar en Colab T4,
  sanitizer y rendimiento antes de crear tabla compartida y optar por GPU.
- GPU kernel only, copias y total host se miden por separado; NO asumir
  speedup con grafos de 30 nodos ni intentar automaticamente n=4000.

## Etapa 7 - Cache GPU optativa en simulador (pendiente verificar en T4)

- Solo `USE_CUDA=ON` + `EQUIPEGO_FLOYD03_GPU=1` precalcula Floyd en T4.
  Por defecto: desactivada, sigue Dijkstra. `USE_CUDA=OFF` siempre hace fallback.
- No activar a la vez `EQUIPEGO_FLOYD03_CPU=1` y `EQUIPEGO_FLOYD03_GPU=1`.
- Antes de iniciar hilos construye matriz OSM dirigida en metros, ejecuta
  Floyd CUDA, comprueba todas las distancias si n<=64 y 527 pares deterministas
  si n>64, y publica solo una tabla completamente inicializada.
- Si el precalculo o la verificacion fallan, la tabla no se publica y
  `shortestDistanceMeters` conserva Dijkstra. Las rutas geometricas de
  `shortestRoute` no se alteran, ni se cambian las features 07/08/09.
- Evitar 2.000 repartidores: el simulador crea hilos reales. Usar el smoke8.
- Repetir USE_CUDA=OFF con 19 pruebas, probar USE_CUDA=ON con 24 pruebas
  y una simulacion Equipetrol; luego una corrida de ciudad SCC con 8 couriers.
- No asumir velocidad mayor: el benchmark Etapa 6 mostro GPU mas lenta
  que Dijkstra paralelo WSL para la ciudad de 4000 nodos.
- En el modo GPU: log `FLOYD03_GPU_CACHE,enabled=1,...,pares_verificados=...`.
