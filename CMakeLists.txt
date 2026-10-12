
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
# DP7.4 - TESTS AUTOMATICOS
# =====================================================

enable_testing()

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

# -----------------------------------------------------
# FUENTES COMPARTIDAS DE TESTS DE COURIER
# -----------------------------------------------------

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

# -----------------------------------------------------
# TEST 3: CICLO DE VIDA DE PEDIDOS
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

# -----------------------------------------------------
# TEST 4: AVERIAS Y RESCATES ATOMICOS
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

# =====================================================
# LIMITES DE TIEMPO PARA EVITAR TESTS COLGADOS
# =====================================================

set_tests_properties(
    test_event_logger
    test_assignment_callback
    test_courier_lifecycle_events
    test_incident_events

    PROPERTIES
    TIMEOUT 30
)
