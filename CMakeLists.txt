
cmake_minimum_required(VERSION 3.11)

project(equipetrol_delivery LANGUAGES CXX)

# =====================================================
# CONFIGURACION GENERAL
# =====================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
    ENABLE_TSAN
    "Enable ThreadSanitizer"
    OFF
)

# =====================================================
# DEPENDENCIAS
# =====================================================

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

# =====================================================
# CONFIGURACION COMPARTIDA DE TARGETS
# =====================================================

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

    target_compile_options(
        ${TARGET_NAME}
        PRIVATE
        -Wall
        -Wextra
    )

    if(ENABLE_TSAN)

        target_compile_options(
            ${TARGET_NAME}
            PRIVATE
            -fsanitize=thread
            -g
            -O1
        )

        target_link_options(
            ${TARGET_NAME}
            PRIVATE
            -fsanitize=thread
        )

    endif()

endfunction()

# =====================================================
# SIMULADOR PRINCIPAL
# =====================================================

set(SOURCES

    src/main.cpp

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

add_executable(
    delivery_sim
    ${SOURCES}
)

configure_equipego_target(delivery_sim)

# =====================================================
# DP8.1 - VISOR DEL MAPA ORIGINAL
# =====================================================

add_executable(
    map_preview

    src/visualization/map_preview.cpp
    src/visualization/MapViewer.cpp
    src/config/ConfigLoader.cpp
)

configure_equipego_target(map_preview)

# =====================================================
# TESTS AUTOMATICOS
# =====================================================

enable_testing()

set(DP_TEST_NAMES)

# -----------------------------------------------------
# TEST 1: LOGGER JSONL CONCURRENTE
# -----------------------------------------------------

add_executable(
    test_event_logger

    tests/test_event_logger.cpp
    src/core/Logger.cpp
)

configure_equipego_target(test_event_logger)

add_test(
    NAME test_event_logger
    COMMAND test_event_logger
)

list(APPEND DP_TEST_NAMES test_event_logger)

# =====================================================
# FUENTES COMPARTIDAS DE TESTS DE COURIER
# =====================================================

set(DP7_COURIER_TEST_SOURCES

    src/config/ConfigLoader.cpp
    src/core/Logger.cpp
    src/map/RoadNetwork.cpp
    src/fleet/Courier.cpp

)

# -----------------------------------------------------
# TEST 2: ASIGNACION SEGURA
# -----------------------------------------------------

add_executable(
    test_assignment_callback

    tests/test_assignment_callback.cpp
    ${DP7_COURIER_TEST_SOURCES}
)

configure_equipego_target(test_assignment_callback)

add_test(
    NAME test_assignment_callback
    COMMAND test_assignment_callback
            ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json
)

list(APPEND DP_TEST_NAMES test_assignment_callback)

# -----------------------------------------------------
# TEST 3: CICLO DE VIDA
# -----------------------------------------------------

add_executable(
    test_courier_lifecycle_events

    tests/test_courier_lifecycle_events.cpp
    ${DP7_COURIER_TEST_SOURCES}
)

configure_equipego_target(test_courier_lifecycle_events)

add_test(
    NAME test_courier_lifecycle_events
    COMMAND test_courier_lifecycle_events
            ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json
)

list(APPEND DP_TEST_NAMES test_courier_lifecycle_events)

# -----------------------------------------------------
# TEST 4: AVERIAS Y REASIGNACIONES
# -----------------------------------------------------

add_executable(
    test_incident_events

    tests/test_incident_events.cpp
    ${DP7_COURIER_TEST_SOURCES}
    src/fleet/Fleet.cpp
)

configure_equipego_target(test_incident_events)

add_test(
    NAME test_incident_events
    COMMAND test_incident_events
            ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json
)

list(APPEND DP_TEST_NAMES test_incident_events)

# -----------------------------------------------------
# TEST 5: ERROR DE ESCRITURA EN LINUX
# -----------------------------------------------------

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")

    add_executable(
        test_logger_write_failure

        tests/test_logger_write_failure.cpp
        src/core/Logger.cpp
    )

    configure_equipego_target(test_logger_write_failure)

    add_test(
        NAME test_logger_write_failure
        COMMAND test_logger_write_failure
    )

    list(APPEND DP_TEST_NAMES test_logger_write_failure)

endif()

# -----------------------------------------------------
# TEST 6: MAPA ORIGINAL EQUIPETROL
# -----------------------------------------------------

add_executable(
    test_map_viewer

    tests/test_map_viewer.cpp
    src/visualization/MapViewer.cpp
    src/config/ConfigLoader.cpp
)

configure_equipego_target(test_map_viewer)

add_test(
    NAME test_map_viewer
    COMMAND test_map_viewer
            ${CMAKE_CURRENT_SOURCE_DIR}/config/equipetrol.json
)

list(APPEND DP_TEST_NAMES test_map_viewer)

# =====================================================
# LIMITES DE TIEMPO DE TESTS
# =====================================================

set_tests_properties(
    ${DP_TEST_NAMES}

    PROPERTIES
    TIMEOUT 30
)
