# EquipeGo — Simulación concurrente de delivery en Equipetrol

**Trabajo final de Concurrencia en C++17 · Zona Equipetrol, Santa Cruz de la Sierra, Bolivia**

EquipeGo simula una flota de repartidores independientes, un generador de pedidos por ráfagas, un despachador con cálculo concurrente de ETA, recursos de recogida limitados, averías y rescate de pedidos. La interfaz se dibuja con OpenCV sobre una imagen del mapa de Equipetrol. La corrección se contrasta con un registro JSONL y un resumen final.

> **Alcance de este README:** describe la implementación inspeccionada después de DP.11. La rúbrica exige que la ejecución de evaluación siempre abra una ventana y respete `simulation.timeScale` del JSON. La implementación actual tiene opciones auxiliares de pruebas `--headless`, `--gui`, `--scale` y `--duration`, y ajusta automáticamente el valor gráfico de `timeScale` a 3 cuando no se pasa `--scale`. Estas discrepancias **deben corregirse antes de declarar cumplimiento estricto**. Las opciones auxiliares no forman parte de la interfaz obligatoria de la sección 5.

## 1. Requisitos, dependencias y compilación (DP.0)

- **Estándar:** C++17, con `std::thread`, `std::mutex`, `std::condition_variable`, `std::future`, `std::promise` y otros componentes de la biblioteca estándar.
- **Construcción:** CMake 3.11 o superior, `make` y compilador compatible con C++17 (GCC 7+ o Clang equivalente).
- **Visualización:** OpenCV 4.1 o superior; se requiere un servidor gráfico operativo para ver la ventana (Linux/WSLg/X11).
- **JSON:** nlohmann/json, encontrado por CMake o descargado mediante `FetchContent` (alternativa configurada: v3.11.3).
- **Hilos:** `Threads::Threads` de CMake; no se utilizan OpenMP, TBB, Boost.Asio, Boost.Thread ni hilos nativos directos.

**Versiones registradas en el entorno de pruebas:** GCC 15.2.0 y OpenCV 4.10.0. Las versiones exactas de CMake, sistema operativo y bibliotecas de sanitización se encuentran en [`evidencias/dp11/`](evidencias/dp11/informe_final.md), dentro de cada `versiones_herramientas.json`.

Desde la raíz del repositorio, el comando exigido por la rúbrica es:

```bash
mkdir build && cd build
cmake ..
make
./delivery_sim ../config/equipetrol.json
```

Para indicar otra ruta del registro de eventos:

```bash
./delivery_sim ../config/equipetrol.json --log run1.log
```

Para compilar con ThreadSanitizer, en un directorio de compilación independiente:

```bash
mkdir build-tsan && cd build-tsan
cmake -DENABLE_TSAN=ON ..
make
```

Las advertencias del compilador deben evaluarse con `-Wall -Wextra`, opciones ya aplicadas desde `CMakeLists.txt`. Las pruebas unitarias/integración disponibles se ejecutan con `ctest --output-on-failure` desde un directorio de compilación configurado.

## 2. Mapa, escenario y entrada (DP.1)

La configuración principal está en [`config/equipetrol.json`](config/equipetrol.json). Incluye **30 intersecciones, 42 segmentos de calle y 6 restaurantes**, y de esos segmentos **7 están marcados como unidireccionales**. El fondo configurado es [`data/equipetrol.png`](data/equipetrol.png), cuyo origen y correspondencia geográfica deben comprobarse visualmente contra OpenStreetMap antes de entregar.

Límites registrados (grados decimales):

| Norte | Sur | Oeste | Este |
|---:|---:|---:|---:|
| -17.75147 | -17.77655 | -63.21443 | -63.18718 |

La conversión latitud/longitud → píxel está en `src/config/ConfigLoader.cpp`, función `latLonToPixel`; `src/map/RoadNetwork.cpp` construye el grafo y calcula rutas mínimas por Dijkstra respetando `oneWay`. Los repartidores no planifican trayectos fuera de este grafo. Los restaurantes y nodos se dibujan en `src/visualization/MapViewer.cpp`.

**Atribución requerida por la licencia ODbL:** © OpenStreetMap contributors. La atribución se encuentra en `config/equipetrol.json` y debe ser **visible además en la ventana**. La revisión del código actual no confirmó que `MapViewer::renderNetwork` la dibuje explícitamente; verificar/corregir antes de entregar.

La sección 5 define como interfaz pública la ruta al JSON y la bandera opcional `--log`. La validación de campos y referencias se encuentra en `src/config/ConfigLoader.cpp::loadAndValidateConfig`; los archivos inválidos deben generar un error legible en `stderr` y salir con código distinto de cero, sin comenzar la simulación. Los revisores pueden suministrar configuraciones propias; las muestras incluidas en este repositorio no reemplazan esa validación.

## 3. Arquitectura y diagrama completo de hilos (DP.12)

Se crean **1 hilo principal + 1 productor + 1 despachador + N repartidores + 1 coordinador de rescate + W trabajadores ETA**, donde `N = fleet.couriers` y `W = min(N, hardware_concurrency limitado a 8)` (con al menos un trabajador). El hilo principal ejecuta la interfaz OpenCV; **no** existe un hilo separado para la interfaz.

```mermaid
flowchart TB
    MAIN["Hilo principal: main() / OpenCV / reloj / cierre"]
    PROD["Hilo productor: OrderProducer::run()\nintervalo y ráfaga"]
    BOOK["Monitor OrderBook\ncola acotada + mutex + ordersCv"]
    DIS["Hilo despachador: Dispatcher::run()\nretirar, cotizar, esperar capacidad, vencer"]
    Q["Cola ETA de QuoteExecutor\nmutex + queueCv"]
    WORK["W hilos ETA: workerLoop()\ncalcular Dijkstra + cache"]
    CACHE["SharedDistanceCache\nmutex + promise / shared_future"]
    FLEET["N hilos Courier::run()\nrecoger, viajar, entregar o esperar"]
    REST["Monitores PickupGate por restaurante\ntickets FIFO + cv + Permit RAII"]
    RESCUE["Hilo de rescate: Fleet::rescueLoop()\nexaminar averías y mover bolsas"]
    CAP["CapacitySignal\nversión + mutex + cv"]
    LOGGER["EventLogger\nmutex + JSONL + contadores"]
    SIGNAL["SIGINT/SIGTERM\natomic + self-pipe POSIX"]
    MAIN -->|"arranca, detiene y join"| PROD
    MAIN -->|"arranca, detiene y join"| DIS
    MAIN -->|"Fleet.start / requestStop / join"| FLEET
    MAIN -->|"Fleet.startRescue / join"| RESCUE
    MAIN -->|"crea / destruye Dispatcher"| WORK
    SIGNAL -->|"despertar al hilo principal"| MAIN
    PROD -->|"tryAdd / notify_one"| BOOK
    BOOK -->|"waitAndTake / tryTake"| DIS
    DIS -->|"submit"| Q
    Q -->|"trabajos"| WORK
    WORK <-->|"coordinación de ruta común"| CACHE
    WORK -->|"future.get / timeout"| DIS
    DIS -->|"tryAssignOrder / workCv.notify_one"| FLEET
    FLEET <-->|"acquire / release"| REST
    FLEET -->|"libera capacidad / avería"| CAP
    CAP -->|"waitUntilChange"| DIS
    CAP -->|"waitForChange"| RESCUE
    RESCUE -->|"transferAllTo con scoped_lock"| FLEET
    PROD -->|"eventos"| LOGGER
    DIS -->|"eventos"| LOGGER
    FLEET -->|"eventos"| LOGGER
    RESCUE -->|"eventos vía transferAllTo"| LOGGER
    LOGGER -->|"snapshotStats"| MAIN
    FLEET -->|"getSnapshot bajo stateMutex"| MAIN
```

### 3.1 Inventario de hilos y destino

| Hilo | Creación real | Ciclo / trabajo | Coordinación | Cómo termina |
|---|---|---|---|---|
| Principal / GUI | `src/main.cpp::main` | `SimulationDashboard::frame`, recibe señales o vence `durationS`; crea, observa y finaliza componentes | `getSnapshot()`, `EventLogger::snapshotStats()`, señal atómica + self-pipe; `cv::waitKey` limita cuadros | Detiene en orden y llama `join` a los hilos creados; publica resumen |
| Productor (1) | `src/main.cpp`: `std::thread(&OrderProducer::run, &producer)` | `OrderProducer::run`: espera intervalo aleatorio, genera hasta `burstMax` y pone pedidos | `OrderBook::tryAdd`; `stopCv` del productor | `producer.requestStop(); producerThread.join()` |
| Despachador (1) | `src/main.cpp`: `std::thread(&Dispatcher::run, &dispatcher, std::ref(...))` | `DispatchLoop.cpp::run`: toma pedidos, compara ETA, asigna, vence plazos | `OrderBook::ordersCv`, `CapacitySignal::signalCv`, futures ETA, `Courier::tryAssignOrder` | `dispatcher.requestStop(orderBook); dispatcherThread.join()` |
| Repartidores (N) | `src/fleet/Fleet.cpp::start`: `threads.emplace_back(&Courier::run, courier.get())` | `Courier::run`: espera trabajo, prepara, ocupa cupo, viaja por segmentos y entrega | `workCv` + `stateMutex`; `PickupGate::acquire` FIFO | `fleet.requestStop(); fleet.join()` |
| Rescate (1) | `src/fleet/Fleet.cpp::startRescue`: `std::thread(&Fleet::rescueLoop, this)` | Examina motos averiadas; intenta transferir la bolsa completa a una moto con capacidad | `CapacitySignal::waitForChange`; `Courier::transferAllTo` con dos mutex | `rescueStopRequested`, señal y `rescueThread.join()` |
| Trabajadores ETA (W) | `src/dispatch/Dispatcher.cpp`: constructor de `QuoteExecutor` | `workerLoop`: saca trabajos ETA, calcula rutas y satisface `future` | `queueCv` de trabajos; `SharedDistanceCache::get` / `shared_future` | Destructor de `QuoteExecutor`: establece `stopping`, notifica y hace `join` |

### 3.2 Canales de comunicación, esperas y propiedad

| Canal o recurso | Emisor → receptor | Protección / evento que despierta | Archivo / lugar |
|---|---|---|---|
| Pedidos `OrderBook` | Productor → despachador | `bookMutex`; `ordersCv.notify_one`; predicado contra despertares espurios; cola acotada | `src/orders/OrderBook.cpp::tryAdd/waitAndTake` |
| Notificación de capacidad o avería | Repartidor / rescate / libro → despachador y rescate | `CapacitySignal`: generación + `signalMutex` + `signalCv`, `notifyChange`; despertar al parar | `src/fleet/CapacitySignal.hpp` |
| Bolsa de cada repartidor | Despachador y rescate → repartidor | `stateMutex`, `workCv.notify_one`; espera predicada en `Courier::run` | `src/fleet/Courier.cpp` |
| Plazas de restaurante | Repartidores → `PickupGate` | mutex, tickets FIFO, `cv.wait(predicado)`; liberación RAII | `src/restaurants/PickupGate.hpp` |
| Cotizaciones ETA | Despachador → trabajadores ETA → despachador | cola protegida, `queueCv`, `packaged_task<TimedQuote>`, `future::wait_until/get` y propagación de `exception_ptr` | `src/dispatch/Dispatcher.cpp` |
| Distancia compartida entre candidatos | Trabajadores ETA entre sí | mutex corto para mapa y `std::promise/std::shared_future` para completar una vez cada par de nodos | `src/dispatch/SharedDistanceCache.hpp::get` |
| Traspaso de toda la bolsa | Rescate → dos repartidores | `std::scoped_lock` sobre ambos `stateMutex`, verifica capacidad e IDs; notifica tras publicar la transferencia | `src/fleet/Courier.cpp::transferAllTo` |
| Bitácora y contadores | Productor, despacho y motos → registro → GUI/main | `EventLogger::logMutex`, instantánea bajo bloqueo | `src/core/Logger.cpp` |
| Solicitud de apagado | SO → principal → productor / dispatcher / flota / ETA | `sigaction`, `atomic<bool>` y self-pipe; `requestStop`, notificaciones, `join()` | `src/main.cpp::SignalStopController/main` |

### 3.3 Argumento escrito sobre ausencia de condiciones de carrera (carpeta 05)

La validez de cada acceso compartido depende de una regla explícita; **tener un mutex en algún lugar no basta**. A continuación se indica la regla de acceso del código:

| Estado compartido | Regla concreta que evita la carrera | Sin esa regla fallaría… |
|---|---|---|
| Cola, `inFlight`, límite y estado de cierre de `OrderBook` | Todos los accesos mutables pasan por `bookMutex`; la `condition_variable` espera con predicado protegido. | Lecturas corruptas, sobrecapacidad, pedidos perdidos y despertares perdidos. |
| Bolsas, pedido activo, posición por nodo y estado de cada moto | `stateMutex` protege modificaciones y `getSnapshot`; `transferAllTo` toma los **dos** mutex con `scoped_lock`. | Un mismo pedido podría observarse en dos bolsas, o la GUI leería estados incoherentes. |
| Plazas, tickets, contador de espera y bandera de cierre de restaurante | `PickupGate::mutex_`, FIFO `issuedTicket_/nextToEnter_`, CV; `Permit` libera en su destructor. | Exceder `pickupSlots`, violar orden de llegada o dejar una plaza bloqueada. |
| Cola y cierre de los workers ETA | `QuoteExecutor::queueMutex` + `queueCv`; los futures transfieren resultado/excepción. | Dos workers podrían modificar la cola simultáneamente o perder un trabajo. |
| Distancias ya calculadas o en cálculo | `SharedDistanceCache::mutex_` protege el mapa; cada par tiene un `shared_future`; el cálculo ocurre fuera del mutex. | Cálculos duplicados o escritura/lectura concurrente del mapa. |
| Eventos, archivo, contadores y tiempos por pedido | `EventLogger::logMutex` rodea las escrituras y las instantáneas. | Líneas JSONL intercaladas, contadores inconsistentes y tiempos incoherentes. |
| Cambios de disponibilidad y apagado | `CapacitySignal` usa mutex + generación; `Dispatcher`, rescate y señales usan atomics; el productor usa mutex para `stopRequested`. | Carreras de cierre, consumo innecesario de CPU o esperas que nunca terminan. |
| Configuración, mapa de calles y reloj tras iniciar | La configuración/red se comparten por referencia **solo de lectura** durante la ejecución; el reloj se inicia antes de crear los hilos. | Lecturas sobre datos mutados sin sincronización o tiempos inconsistentes. |
| Interfaz OpenCV | La GUI solo se dibuja y se cierra desde el hilo principal; recibe instantáneas copiadas. | Acceso concurrente inseguro a la ventana y mezcla de estados de distintas motos. |

Estas reglas están respaldadas por pruebas, pero no representan una prueba matemática universal: los experimentos instrumentados de DP.11 cubren los escenarios consignados en sus registros.

## 4. Mapa de conceptos obligatorio (carpetas 01 a 07)

Esta tabla **completa todas las filas obligatorias de la sección 10**. Cada referencia apunta a código funcional; la columna de estudio propio de la rúbrica no es evaluable ni obligatoria y por eso se omite.

| Carpeta | Concepto obligatorio (sección 10) | Archivo/lugar real, justificación y defecto sin él |
|:--:|---|---|
| **01** | Creación explícita de hilos y destino deliberado: unidos, nunca abandonados. | `src/main.cpp::main` crea productor y despachador, `src/fleet/Fleet.cpp::start/startRescue` crea N repartidores y el rescate, y `src/dispatch/Dispatcher.cpp::QuoteExecutor` crea W workers. `main` y los destructores solicitan parada y esperan mediante `join()`; sin ello se destruirían objetos mientras un hilo sigue usándolos o acabaría la aplicación con hilos activos. |
| **01** | Decisión dependiente del paralelismo disponible del hardware. | `src/dispatch/Dispatcher.cpp::Dispatcher` usa `std::thread::hardware_concurrency()` para dimensionar el pool ETA, acotado por la cantidad de repartidores y por ocho. Sin ese límite, flotas grandes podrían crear demasiados trabajadores para el equipo y perder rendimiento en cambios de contexto. |
| **02** | Datos entregados por valor, referencia y movimiento, en el caso correcto. | **Valor:** `QuoteExecutor::submit` captura `startNode`, `courierId` e índices por valor en el trabajo que consumirá otro hilo; también se pasa por valor el puntero no propietario `courier.get()` al crear `std::thread` en `Fleet::start`. **Referencia:** `main` usa `std::ref(orderBook)`, `std::ref(logger)` y `std::ref(clock)` para el hilo despachador. **Movimiento:** `OrderProducer` inserta con `std::move(order)` en `OrderBook::tryAdd`; `Courier::run` y `transferAllTo` mueven pedidos entre contenedores. Referencias a temporales colgarían; copiar pedidos vulneraría su propiedad única; no moverlos haría imposible transferir un `Order` no copiable. |
| **02** | Pedidos transferidos por movimiento, con un solo propietario. | `src/orders/Order.hpp` elimina constructor y asignación de copia, pero habilita operaciones `noexcept` de movimiento. `OrderBook::tryAdd/waitAndTake` y `Courier::tryAssignOrder/transferAllTo` mueven el pedido entre el libro y las bolsas bajo sincronización; sin esta disciplina podría duplicarse o procesarse desde varios dueños. |
| **03** | Funciones miembro ejecutadas por hilos y contenedores de hilos. | `src/fleet/Fleet.cpp::start` crea `threads.emplace_back(&Courier::run, courier.get())` y mantiene `std::vector<std::thread>`. `main.cpp` invoca `Dispatcher::run` y `OrderProducer::run` desde hilos; sin agrupar y unir los hilos, no se podría supervisar el ciclo de vida de toda la flota. |
| **03** | Propiedad compartida que garantiza tiempos de vida; ningún puntero crudo con propiedad. | `Fleet` usa `std::shared_ptr<CapacitySignal>` y `std::shared_ptr<RestaurantManager>`; este mantiene un `shared_ptr<PickupGate>` por restaurante (`src/restaurants/RestaurantManager.hpp`). `Courier` conserva copias de esos punteros; `std::unique_ptr<Courier>` da propiedad exclusiva a Fleet. Los punteros crudos (`courier.get()`, `eventLogger`) son observadores, no propietarios, con vida coordinada por `join()`. Sin esas garantías podría usarse un monitor destruido. |
| **04** | Tareas con resultado mediante future y política de lanzamiento declarada. | `src/dispatch/Dispatcher.cpp::QuoteExecutor::submit/workerLoop` usa `std::packaged_task<TimedQuote()>` y `std::future<TimedQuote>`, con política **explícita de planificación en pool fijo**: los trabajos se encolan y los W workers los ejecutan sin lanzar un hilo por cotización. Si el despacho ejecutase secuencialmente Dijkstra, excedería más fácilmente `quoteTimeoutMs` y bloquearía el despachador. **Nota de interpretación:** no se usa literalmente `std::async(std::launch::async, ...)`; confirmar con el docente si exige esa sintaxis y no acepta la política explícita de pool. |
| **04** | Canal de un solo uso promise/future entre dos hilos. | `src/dispatch/SharedDistanceCache.hpp::SharedDistanceCache::get`: el hilo dueño publica `std::promise<double>` y los interesados esperan su `std::shared_future<double>`. Se completa una sola vez por par origen/destino, compartiendo incluso excepciones; sin el canal, habría duplicación de Dijkstra o espera activa. |
| **04** | Espera limitada y excepciones propagadas entre hilos. | `src/dispatch/Dispatcher.cpp::quoteCandidates` usa `future.wait_until(deadline)` y `TimedQuote::error` (`std::exception_ptr`); el despacho reconoce `quoteTimeout` e informa errores de cotización. Sin deadline podría bloquearse por un cálculo lento, y sin propagación un fallo aislado terminaría el proceso o sería ocultado. |
| **05** | Argumento escrito por cada objeto compartido sobre ausencia de carrera. | Ver la tabla de la sección **3.3**: cubre libro de pedidos, motos/bolsas, recursos de restaurantes, cola/caché ETA, logger, señal, configuración, reloj y GUI. Cada objeto tiene una regla de bloqueo, atomicidad, inmutabilidad o propiedad de hilo identificable; sin ella existirían lecturas y escrituras sin relación de sincronización. |
| **05** | Tipo seguro de copiar o mover gracias a funciones miembro especiales correctas. | `src/restaurants/PickupGate.hpp::Permit` prohíbe copia, permite movimiento `noexcept` con `std::exchange` y libera el cupo en su destructor; esto es RAII de un recurso exclusivo. `src/orders/Order.hpp` también elimina copias y define movimientos `noexcept`. Si un permiso pudiera copiarse, su destructor podría liberar la misma plaza dos veces. |
| **06** | Exclusión mutua exclusivamente con bloqueos de ámbito, secciones críticas cortas. | `src/orders/OrderBook.cpp` usa `std::lock_guard`; `src/fleet/Courier.cpp` combina `std::lock_guard` y `std::unique_lock` para soltar mutex durante esperas largas; `src/dispatch/Dispatcher.cpp::QuoteExecutor` extrae el trabajo bajo bloqueo y lo ejecuta fuera de él. Sin ámbito RAII una excepción podría dejar un mutex adquirido o serializar todo el sistema. |
| **06** | Operación con dos bloqueos simultáneos y por qué no se interbloquea. | `src/fleet/Courier.cpp::transferAllTo` usa `std::scoped_lock(stateMutex, recipient.stateMutex)` para tomar los dos mutex de origen y destino sin imponer órdenes opuestos. La transferencia se publica bajo ambos bloqueos; con adquisiciones secuenciales de orden variable dos rescates simultáneos podrían formar un ciclo de espera. |
| **07** | Monitores: datos compartidos inaccesibles sin su mutex. | `src/orders/OrderBook.hpp/.cpp` encapsula cola/estado/capacidad bajo `bookMutex`; `src/restaurants/PickupGate.hpp` encapsula tickets, plazas y CV; `src/fleet/Courier.hpp/.cpp` encapsula bolsa y estado con `stateMutex`. Acceder sin esos monitores permitiría exceder capacidad o ver estados parcialmente actualizados. |
| **07** | Espera predicada, despertar por otro hilo, tolerancia a espurios y despertar final. | `OrderBook::waitAndTake` usa `ordersCv.wait(lock, predicado)`, `Courier::run` espera en `workCv`, `PickupGate::acquire` en `cv_`, `CapacitySignal::waitForChange` comprueba generación o parada. `requestStop()` y `notify_all()` despiertan a los bloqueados durante el cierre; sin predicados los despertares espurios consumirían trabajo inexistente o el apagado quedaría colgado. |

## 5. Funcionamiento y observabilidad (DP.2–DP.9)

1. `src/orders/OrderGenerator.cpp` usa distribuciones aleatorias y una semilla configurable. `OrderProducer::run` crea pedidos y ráfagas y escribe `orderCreated` antes de enviarlos a `OrderBook`. Los rechazos `queueFull` y `unreachable` se registran explícitamente.
2. `src/dispatch/DispatchLoop.cpp::run` extrae pedidos, calcula cotizaciones concurrentes, respeta `quoteTimeoutMs` en tiempo real y vence los pedidos por `acceptTimeoutMs` simulado. Una asignación confirmada se escribe como `orderAssigned`.
3. `src/fleet/Courier.cpp::run` realiza el trabajo del repartidor y usa `PickupGate` para imponer `pickupSlots` y tickets FIFO. `travelTo` recorre segmentos de `RoadNetwork`; las averías provocan notificaciones a `Fleet::rescueLoop`, y `transferAllTo` realiza rescates completos con dos mutex.
4. `src/core/Logger.cpp::logStart` imprime exactamente `Simulation has started...` al estar listo. Los eventos permitidos son `orderCreated`, `orderAssigned`, `pickupStarted`, `orderPickedUp`, `orderDelivered`, `orderRejected`, `courierBreakdown`, `orderReassigned` y `simulationStopping`. Cada JSONL incluye `t` en milisegundos simulados y campos según el evento.
5. `src/main.cpp::main` detiene productor, despachador y flota, une los hilos, clasifica pedidos pendientes, registra `simulationStopping`, audita el archivo y escribe como última línea un JSON con `created`, `delivered`, `rejected`, `pending`, `reassigned`, `breakdowns`, `avgDeliveryMs`, `maxDeliveryMs` y `deliveredPerCourier`.

**Invariantes comprobables:** `created = delivered + rejected + pending`; las entregas por repartidor suman `delivered`; ningún pedido puede terminar en dos estados diferentes. Las pruebas automatizadas revisan además el registro completo y el cierre con señales.

**Límite de la visualización actual:** `SimulationDashboard::takeSamples` interpola la posición estimada a partir del nodo confirmado y un temporizador local. Para aprobar estrictamente la sección 2, se debe probar durante carga que este avance representa fielmente la posición física del repartidor en cada instante, incluyendo preparación, demoras, averías y escalas diferentes, sin saltos espurios. El panel actual también debe verificarse con configuraciones de 64 repartidores.

## 6. Experimentos propios y cambio de diseño (DP.10)

Los experimentos de flota y demanda se pueden consultar en [`evidencias/dp8_6/`](evidencias/dp8_6/) y los datos crudos de la comparación en [`evidencias/dp10/`](evidencias/dp10/). Script: `benchmarks/dp10_eta_before_after.py`.

**Experimento antes/después:** caché concurrente en `SharedDistanceCache.hpp` para compartir la distancia restaurante → cliente entre las cotizaciones de diferentes motos. Las condiciones se mantuvieron iguales salvo por el algoritmo del dispatcher: flotas de 8, 32 y 64 motos; 900 segundos simulados; `timeScale=60`; tres repeticiones por versión y cantidad de motos; misma configuración generadora y semilla.

| Motos | CPU promedio antes (s) | CPU promedio después (s) | Cambio aproximado en CPU | Entregados antes → después (promedio) |
|---:|---:|---:|---:|---:|
| 8 | 0.83 | 0.79 | −5.19 % | 17 → 17 |
| 32 | 1.06 | 1.14 | +7.36 % | 58 → 58 |
| 64 | 1.99 | 1.61 | −19.16 % | 93.3 → 96.7 |

**Interpretación:** la mejora medida más clara apareció con 64 motos, donde se consumió aproximadamente 19.16 % menos CPU total. Con 32 motos el coste aumentó: el efecto no es uniforme ni garantiza una mejora universal. La variabilidad de la planificación afecta las entregas y las latencias. El P95 de asignación del script incluye el tiempo que un pedido espera por capacidad y **no** es una medición aislada del algoritmo ETA.

**Qué cambió:** `src/dispatch/Dispatcher.cpp` dejó de calcular la misma ruta restaurante → cliente una vez por candidato y ahora consulta `SharedDistanceCache`, cuyo acceso está sincronizado con mutex + promise/shared_future. **Qué permaneció igual:** demanda, red, semilla, capacidades, tiempos y límite de workers en cada comparación. Las carpetas `evidencias/dp10/antes/` y `evidencias/dp10/despues/` contienen el JSON de configuración, JSONL, stdout, stderr, CSV y resultados individuales de las 18 corridas.

## 7. Análisis dinámico de memoria y carreras (DP.11)

**Informe detallado:** [`evidencias/dp11/informe_final.md`](evidencias/dp11/informe_final.md). Se realizaron **12 ejecuciones instrumentadas**: seis con ThreadSanitizer y seis con AddressSanitizer + LeakSanitizer + UndefinedBehaviorSanitizer. Todas incluyeron pedidos en ráfaga, averías y apagado por SIGINT o SIGTERM. Las copias de configuración usaron `bagCapacity=1`, `meanIntervalMs=5000`, `burstMax=12`, `maxPending=20` y `breakdownProbability=1.0`.

| Herramientas | Repeticiones | Reportes en los escenarios ensayados | Evidencias |
|---|---:|---|---|
| TSAN (carreras de datos) | 6/6 satisfactorias | Sin hallazgos | `evidencias/dp11/tsan_inicial/`, `tsan_repeticion/` |
| ASAN + LSAN + UBSAN (memoria y comportamiento indefinido) | 6/6 satisfactorias | Sin hallazgos | `evidencias/dp11/asan_inicial/`, `asan_repeticion/` |

Las 12 ejecuciones acumularon **5.430 pedidos creados y 96 averías**; todas salieron con código 0, respetaron `simulationStopping` único y terminaron en **0.518 s o menos** desde la señal. No fueron necesarias correcciones de memoria/concurrencia en DP.11 porque no se encontraron reportes; se repitieron las pruebas y volvieron a pasar. **No encontrar defectos no demuestra ausencia de todos los defectos posibles.**

**Comandos usados**, además de las versiones y variables de entorno consignadas en los registros originales:

```bash
cmake -S . -B ~/build-equipego-tsan -DENABLE_TSAN=ON
cmake --build ~/build-equipego-tsan --target delivery_sim -j4
python3 benchmarks/dp11_dynamic_analysis.py --sanitizer tsan \
  --binary ~/build-equipego-tsan/delivery_sim --max-wait 120 --repeats 2

cmake -S . -B ~/build-equipego-asan -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build ~/build-equipego-asan --target delivery_sim -j4
python3 benchmarks/dp11_dynamic_analysis.py --sanitizer asan \
  --binary ~/build-equipego-asan/delivery_sim --max-wait 120 --repeats 2
python3 benchmarks/dp11_finalize_report.py
```

Cada carpeta contiene versiones, configuración de cada prueba, `stdout.txt`, `stderr.txt`, `events.jsonl` y los comandos precisos en `metadata.json`. Las pruebas son independientes de este README y pueden reproducirse desde el código fuente.

## 8. Evidencias, tests y estructura del repositorio

| Ruta | Propósito |
|---|---|
| `src/` | Código fuente del simulador: ciudad, despacho, motos, restaurantes, pedidos, observabilidad y GUI |
| `config/equipetrol.json` | Escenario por defecto; admite validación de JSON según sección 5 |
| `data/equipetrol.png` | Imagen de fondo requerida de Equipetrol |
| `tests/` | Pruebas de logger, callbacks, incidentes, GUI y cierre; `test_shared_distance_cache.cpp` cubre 32 hilos concurrentes |
| `benchmarks/dp8_6_scalability.py` y herramientas DP.10/DP.11 | Experimentos reproducibles sin editar la configuración original |
| `evidencias/dp10/` | Tabla antes/después, configuraciones, JSONL y resúmenes crudos |
| `evidencias/dp11/` | 12 ejecuciones instrumentadas y archivos originales sin editar |

## 9. Lista de verificación antes de entregar (obligatoria)

- [ ] Confirmar la imagen original de OSM y la legibilidad del Segundo, Tercer y Cuarto Anillo y avenida San Martín, así como atribución **visible** en la ventana.
- [ ] Corregir `main.cpp` para **respetar el `timeScale` del JSON** y definir una interfaz de evaluación que siempre abra ventana, sin depender de un modo headless.
- [ ] Verificar con configuraciones externas y varias ejecuciones: 64 motos, cientos de nodos, `pickupSlots=1`, `bagCapacity=1`, ráfagas grandes, `breakdownProbability=1`, rutas inaccesibles e invalidaciones del JSON.
- [ ] Validar que la GUI represente el trayecto realmente recorrido, no solo una predicción independiente; comprobar fluidez y el mapa para 64 motos.
- [ ] Medir CPU con casi cero pedidos y repartidores inactivos; separar costo GUI del costo concurrente.
- [ ] Confirmar con el docente si la fila «política de lanzamiento declarada» admite el pool explícito de `QuoteExecutor` o exige `std::launch::async` en trabajo real.
- [ ] Probar compilación limpia de la sección 8 y versión final instrumentada, comparándolas con los ensayos de DP.11.

**No marcar DP.0–DP.12 como cumplimiento completo solo por las pruebas ya realizadas:** en la rúbrica hay verificaciones de GUI, mapa y entradas adversarias que deben comprobarse en la versión exacta de entrega.

---

**Mapa/datos:** © OpenStreetMap contributors. Datos de OpenStreetMap disponibles bajo [Open Database License (ODbL)](https://www.openstreetmap.org/copyright).
