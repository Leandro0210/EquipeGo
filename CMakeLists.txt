cmake_minimum_required(VERSION 3.11)

project(equipetrol_delivery LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

find_package(Threads REQUIRED)
find_package(OpenCV 4.1 REQUIRED)
find_package(nlohmann_json 3.2.0 QUIET)

if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_GetProperties(nlohmann_json)
    if(NOT nlohmann_json_POPULATED)
        FetchContent_Populate(nlohmann_json)
        add_subdirectory(
            ${nlohmann_json_SOURCE_DIR}
            ${nlohmann_json_BINARY_DIR}
            EXCLUDE_FROM_ALL
        )
    endif()
endif()

function(configure_equipego_target TARGET_NAME)
    target_include_directories(
        ${TARGET_NAME}
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${OpenCV_INCLUDE_DIRS}
    )
    target_link_libraries(
        ${TARGET_NAME}
        PRIVATE
        Threads::Threads
        ${OpenCV_LIBS}
        nlohmann_json::nlohmann_json
    )
    target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra)
    if(ENABLE_TSAN)
        target_compile_options(
            ${TARGET_NAME} PRIVATE -fsanitize=thread -g -O1
        )
        target_link_options(${TARGET_NAME} PRIVATE -fsanitize=thread)
    endif()
endfunction()


set(SOURCES
    src/main.cpp
    src/visualization/SimulationDashboard.cpp
    src/visualization/MapViewer.cpp
    src/config/ConfigLoader.cpp
    src/core/Logger.cpp
    src/orders/OrderBook.cpp
    src/orders/OrderGenerator.cpp
    src/orders/OrderProducer.cpp
    src/map/RoadNetwork.cpp
    src/fleet/Courier.cpp
    src/fleet/Fleet.cpp
    src/dispatch/Dispatcher.cpp
    src/dispatch/DispatchLoop.cpp
)
add_executable(delivery_sim ${SOURCES})
configure_equipego_target(delivery_sim)

option(EQUIPEGO_BUILD_BENCHMARK "Build extra diagnostic executable" OFF)
if(EQUIPEGO_BUILD_BENCHMARK)
    add_executable(delivery_sim_bench ${SOURCES})
    configure_equipego_target(delivery_sim_bench)
    target_compile_definitions(delivery_sim_bench PRIVATE EQUIPEGO_BENCHMARK_CLI=1)
endif()


add_executable(map_preview
    src/visualization/map_preview.cpp
    src/visualization/MapViewer.cpp
    src/config/ConfigLoader.cpp
)
configure_equipego_target(map_preview)


add_executable(live_fleet_preview
    src/visualization/live_fleet_preview.cpp
    src/visualization/MapViewer.cpp
    src/config/ConfigLoader.cpp
    src/core/Logger.cpp
    src/map/RoadNetwork.cpp
    src/fleet/Courier.cpp
    src/fleet/Fleet.cpp
)
configure_equipego_target(live_fleet_preview)


add_executable(real_orders_preview
    src/visualization/real_orders_preview.cpp
    src/visualization/MapViewer.cpp
    src/config/ConfigLoader.cpp
    src/core/Logger.cpp
    src/map/RoadNetwork.cpp
    src/fleet/Courier.cpp
    src/fleet/Fleet.cpp
    src/orders/OrderBook.cpp
    src/orders/OrderGenerator.cpp
    src/orders/OrderProducer.cpp
    src/dispatch/Dispatcher.cpp
    src/dispatch/DispatchLoop.cpp
)
configure_equipego_target(real_orders_preview)


enable_testing()
set(DP_TEST_NAMES)

add_executable(test_event_logger
    tests/test_event_logger.cpp
    src/core/Logger.cpp
)
configure_equipego_target(test_event_logger)
add_test(NAME test_event_logger COMMAND test_event_logger)
list(APPEND DP_TEST_NAMES test_event_logger)

set(DP7_COURIER_TEST_SOURCES
    src/config/ConfigLoader.cpp
    src/core/Logger.cpp
    src/map/RoadNetwork.cpp
    src/fleet/Courier.cpp
)

add_executable(test_assignment_callback
    tests/test_assignment_callback.cpp
    ${DP7_COURIER_TEST_SOURCES}
)
configure_equipego_target(test_assignment_callback)
add_test(NAME test_assignment_callback
    COMMAND test_assignment_callback
    ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json
)
list(APPEND DP_TEST_NAMES test_assignment_callback)

add_executable(test_courier_lifecycle_events
    tests/test_courier_lifecycle_events.cpp
    ${DP7_COURIER_TEST_SOURCES}
)
configure_equipego_target(test_courier_lifecycle_events)
add_test(NAME test_courier_lifecycle_events
    COMMAND test_courier_lifecycle_events
    ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json
)
list(APPEND DP_TEST_NAMES test_courier_lifecycle_events)

add_executable(test_incident_events
    tests/test_incident_events.cpp
    ${DP7_COURIER_TEST_SOURCES}
    src/fleet/Fleet.cpp
)
configure_equipego_target(test_incident_events)
add_test(NAME test_incident_events
    COMMAND test_incident_events
    ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json
)
list(APPEND DP_TEST_NAMES test_incident_events)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_logger_write_failure
        tests/test_logger_write_failure.cpp
        src/core/Logger.cpp
    )
    configure_equipego_target(test_logger_write_failure)
    add_test(NAME test_logger_write_failure
        COMMAND test_logger_write_failure
    )
    list(APPEND DP_TEST_NAMES test_logger_write_failure)
endif()

add_executable(test_map_viewer
    tests/test_map_viewer.cpp
    src/visualization/MapViewer.cpp
    src/config/ConfigLoader.cpp
)
configure_equipego_target(test_map_viewer)
add_test(NAME test_map_viewer
    COMMAND test_map_viewer
    ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json
)
list(APPEND DP_TEST_NAMES test_map_viewer)

add_test(NAME test_live_fleet_preview
    COMMAND live_fleet_preview
    ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json --check
)
list(APPEND DP_TEST_NAMES test_live_fleet_preview)


add_test(NAME test_real_orders_preview
    COMMAND real_orders_preview
    ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json --check
)
list(APPEND DP_TEST_NAMES test_real_orders_preview)


add_executable(test_live_event_stats
    tests/test_live_event_stats.cpp
    src/core/Logger.cpp
)
configure_equipego_target(test_live_event_stats)
add_test(NAME test_live_event_stats COMMAND test_live_event_stats)
list(APPEND DP_TEST_NAMES test_live_event_stats)

add_executable(test_dp12_motion
    tests/test_dp12_motion.cpp
    src/core/Logger.cpp
    src/map/RoadNetwork.cpp
    src/fleet/Courier.cpp
)
configure_equipego_target(test_dp12_motion)
add_test(NAME test_dp12_motion COMMAND test_dp12_motion)
list(APPEND DP_TEST_NAMES test_dp12_motion)

set_tests_properties(${DP_TEST_NAMES} PROPERTIES TIMEOUT 30)

# Feature CUDA 07, etapa CPU: test sin dependencias CUDA ni OpenCV.
add_executable(test_delivery_histogram tests/test_delivery_histogram.cpp)
target_include_directories(test_delivery_histogram PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_delivery_histogram PRIVATE Threads::Threads)
add_test(NAME test_delivery_histogram COMMAND test_delivery_histogram)

# Feature 07: CUDA es optativo y esta DESACTIVADO por defecto.
option(USE_CUDA "Enable optional CUDA analytics" OFF)
if(USE_CUDA)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    find_package(CUDAToolkit REQUIRED)

    add_library(equipego_histogram_gpu STATIC
        src/analytics/DeliveryHistogramGpu.cpp
        src/analytics/DeliveryHistogramCuda.cu
        src/analytics/EtaReductionCuda.cu
        src/analytics/RestaurantOrderScanCuda.cu
        src/map/FloydWarshallCuda03.cu)
    target_include_directories(equipego_histogram_gpu PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(equipego_histogram_gpu PUBLIC
        Threads::Threads CUDA::cudart)
    set_target_properties(equipego_histogram_gpu PROPERTIES
        CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)

    target_link_libraries(delivery_sim PRIVATE equipego_histogram_gpu)
    target_compile_definitions(delivery_sim PRIVATE EQUIPEGO_USE_CUDA=1)
    if(EQUIPEGO_BUILD_BENCHMARK)
        target_link_libraries(delivery_sim_bench PRIVATE equipego_histogram_gpu)
        target_compile_definitions(delivery_sim_bench PRIVATE EQUIPEGO_USE_CUDA=1)
    endif()

    add_executable(test_delivery_histogram_gpu
        tests/test_delivery_histogram_gpu.cpp)
    target_link_libraries(test_delivery_histogram_gpu PRIVATE
        equipego_histogram_gpu)
    add_test(NAME test_delivery_histogram_gpu
        COMMAND test_delivery_histogram_gpu)

    # Feature 08 — Reduction comparte el propietario y el runtime CUDA.
    add_executable(test_eta_reduction_gpu tests/test_eta_reduction_gpu.cpp)
    target_link_libraries(test_eta_reduction_gpu PRIVATE equipego_histogram_gpu)
    add_test(NAME test_eta_reduction_gpu COMMAND test_eta_reduction_gpu)

    # Feature 09 - Scan estable GPU dentro del propietario CUDA.
    add_executable(test_restaurant_order_scan_gpu
        tests/test_restaurant_order_scan_gpu.cpp)
    target_link_libraries(test_restaurant_order_scan_gpu PRIVATE
        equipego_histogram_gpu)
    add_test(NAME test_restaurant_order_scan_gpu
        COMMAND test_restaurant_order_scan_gpu)
endif()

# Feature 08 - Reduction CPU (sin dependencia CUDA).
add_executable(test_eta_reduction tests/test_eta_reduction.cpp)
target_include_directories(test_eta_reduction PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_eta_reduction PRIVATE Threads::Threads)
if(ENABLE_TSAN)
    target_compile_options(test_eta_reduction PRIVATE -fsanitize=thread -g -O1)
    target_link_options(test_eta_reduction PRIVATE -fsanitize=thread)
endif()
add_test(NAME test_eta_reduction COMMAND test_eta_reduction)

# Feature 08: ruta real de eleccion de candidatos en el dispatcher.
add_executable(test_eta_dispatch_order tests/test_eta_dispatch_order.cpp)
target_include_directories(test_eta_dispatch_order PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_eta_dispatch_order PRIVATE Threads::Threads)
if(ENABLE_TSAN)
    target_compile_options(test_eta_dispatch_order PRIVATE -fsanitize=thread -g -O1)
    target_link_options(test_eta_dispatch_order PRIVATE -fsanitize=thread)
endif()
add_test(NAME test_eta_dispatch_order COMMAND test_eta_dispatch_order)

# Feature 08 — ruta de seleccion GPU con deadline y fallback CPU.
# Test CPU puro: no requiere nvcc ni GPU.
add_executable(test_eta_gpu_dispatch tests/test_eta_gpu_dispatch.cpp)
target_include_directories(test_eta_gpu_dispatch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_eta_gpu_dispatch PRIVATE Threads::Threads)
if(ENABLE_TSAN)
    target_compile_options(test_eta_gpu_dispatch PRIVATE -fsanitize=thread -g -O1)
    target_link_options(test_eta_gpu_dispatch PRIVATE -fsanitize=thread)
endif()
add_test(NAME test_eta_gpu_dispatch COMMAND test_eta_gpu_dispatch)

# Feature 09 — Scan (etapa 1 CPU): grouping stable por restaurante.
add_executable(test_restaurant_order_scan tests/test_restaurant_order_scan.cpp)
target_include_directories(test_restaurant_order_scan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_restaurant_order_scan PRIVATE Threads::Threads)
if(ENABLE_TSAN)
    target_compile_options(test_restaurant_order_scan PRIVATE -fsanitize=thread -g -O1)
    target_link_options(test_restaurant_order_scan PRIVATE -fsanitize=thread)
endif()
add_test(NAME test_restaurant_order_scan COMMAND test_restaurant_order_scan)

# Feature 09 — etapa 2: vista estable de pedidos reales sin alterar FIFO.
add_executable(test_restaurant_order_live09 tests/test_restaurant_order_live09.cpp)
target_include_directories(test_restaurant_order_live09 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_restaurant_order_live09 PRIVATE Threads::Threads)
if(ENABLE_TSAN)
    target_compile_options(test_restaurant_order_live09 PRIVATE -fsanitize=thread -g -O1)
    target_link_options(test_restaurant_order_live09 PRIVATE -fsanitize=thread)
endif()
add_test(NAME test_restaurant_order_live09 COMMAND test_restaurant_order_live09)

# Feature09 - Etapa 4: seleccion observacional GPU/CPU, sin NVCC en CPU.
add_executable(test_restaurant_order_gpu_dispatch09
    tests/test_restaurant_order_gpu_dispatch09.cpp)
target_include_directories(test_restaurant_order_gpu_dispatch09 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_restaurant_order_gpu_dispatch09 PRIVATE Threads::Threads)
if(ENABLE_TSAN)
    target_compile_options(test_restaurant_order_gpu_dispatch09 PRIVATE
        -fsanitize=thread -g -O1)
    target_link_options(test_restaurant_order_gpu_dispatch09 PRIVATE
        -fsanitize=thread)
endif()
add_test(NAME test_restaurant_order_gpu_dispatch09
    COMMAND test_restaurant_order_gpu_dispatch09)
set_tests_properties(test_restaurant_order_gpu_dispatch09 PROPERTIES TIMEOUT 30)


# Feature 03 - Memory Tiling, etapa 1: tabla Floyd-Warshall CPU.
# No sustituye Dijkstra ni altera el Dispatcher.
add_executable(test_floyd_warshall03
    tests/test_floyd_warshall03.cpp
    src/map/RoadNetwork.cpp
    src/config/ConfigLoader.cpp)
configure_equipego_target(test_floyd_warshall03)
add_test(NAME test_floyd_warshall03
    COMMAND test_floyd_warshall03
    ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json)
set_tests_properties(test_floyd_warshall03 PROPERTIES TIMEOUT 30)


# Feature 03 - Memory Tiling, etapa 2: cache optativa de distancias.
add_executable(test_floyd_cache03
    tests/test_floyd_cache03.cpp
    src/map/RoadNetwork.cpp
    src/config/ConfigLoader.cpp)
configure_equipego_target(test_floyd_cache03)
add_test(NAME test_floyd_cache03
    COMMAND test_floyd_cache03
    ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json)
set_tests_properties(test_floyd_cache03 PROPERTIES TIMEOUT 30)


# Feature 03 - Etapa 4: backend CUDA tiled; no modifica el dispatcher.
# La prueba GPU solo se registra al compilar con USE_CUDA=ON.
if(USE_CUDA)
    add_executable(test_floyd_warshall_gpu03
        tests/test_floyd_warshall_gpu03.cpp
        src/map/RoadNetwork.cpp
        src/config/ConfigLoader.cpp)
    configure_equipego_target(test_floyd_warshall_gpu03)
    target_link_libraries(test_floyd_warshall_gpu03 PRIVATE equipego_histogram_gpu)
    add_test(NAME test_floyd_warshall_gpu03
        COMMAND test_floyd_warshall_gpu03
        ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json)
    set_tests_properties(test_floyd_warshall_gpu03 PROPERTIES TIMEOUT 120)
endif()

# Feature 03 - Etapa 7: integracion optativa de la tabla GPU, precalculo al inicio.
# Sin nvcc o con USE_CUDA=OFF se conservan las 19 pruebas y Dijkstra.
if(USE_CUDA)
    target_sources(delivery_sim PRIVATE src/map/FloydWarshallGpuCache03.cpp)
    if(EQUIPEGO_BUILD_BENCHMARK)
        target_sources(delivery_sim_bench PRIVATE src/map/FloydWarshallGpuCache03.cpp)
    endif()

    add_executable(test_floyd_cache_gpu_integracion03
        tests/test_floyd_cache_gpu_integracion03.cpp
        src/map/FloydWarshallGpuCache03.cpp
        src/map/RoadNetwork.cpp
        src/config/ConfigLoader.cpp)
    configure_equipego_target(test_floyd_cache_gpu_integracion03)
    target_link_libraries(test_floyd_cache_gpu_integracion03 PRIVATE
        equipego_histogram_gpu)
    target_compile_definitions(test_floyd_cache_gpu_integracion03 PRIVATE
        EQUIPEGO_USE_CUDA=1)
    add_test(NAME test_floyd_cache_gpu_integracion03
        COMMAND test_floyd_cache_gpu_integracion03
        ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json)
    set_tests_properties(test_floyd_cache_gpu_integracion03 PROPERTIES TIMEOUT 120)
endif()

# Feature 04 - AoS vs SoA CPU, etapa 1 (sin cambios en dispatcher).
add_executable(test_courier_layout04 tests/test_courier_layout04.cpp)
target_include_directories(test_courier_layout04 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_courier_layout04 PRIVATE Threads::Threads)
if(ENABLE_TSAN)
    target_compile_options(test_courier_layout04 PRIVATE -fsanitize=thread -g -O1)
    target_link_options(test_courier_layout04 PRIVATE -fsanitize=thread)
endif()
add_test(NAME test_courier_layout04 COMMAND test_courier_layout04)
set_tests_properties(test_courier_layout04 PROPERTIES TIMEOUT 60)

if(EQUIPEGO_BUILD_BENCHMARK)
    add_executable(bench_courier_layout04 benchmarks/bench_courier_layout04.cpp)
    target_include_directories(bench_courier_layout04 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(bench_courier_layout04 PRIVATE Threads::Threads)
endif()

# Feature 04 - Etapa 2: kernels CUDA AoS / SoA de snapshot, sin mutar Fleet.
if(USE_CUDA)
    target_sources(equipego_histogram_gpu PRIVATE
        src/analytics/CourierLayoutCuda04.cu)
    add_executable(test_courier_layout_gpu04 tests/test_courier_layout_gpu04.cpp)
    target_include_directories(test_courier_layout_gpu04 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_courier_layout_gpu04 PRIVATE equipego_histogram_gpu)
    add_test(NAME test_courier_layout_gpu04 COMMAND test_courier_layout_gpu04)
    set_tests_properties(test_courier_layout_gpu04 PROPERTIES TIMEOUT 120)
    if(EQUIPEGO_BUILD_BENCHMARK)
        add_executable(bench_courier_layout_gpu04
            benchmarks/bench_courier_layout_gpu04.cpp)
        target_include_directories(bench_courier_layout_gpu04 PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(bench_courier_layout_gpu04 PRIVATE
            equipego_histogram_gpu)
    endif()
endif()
