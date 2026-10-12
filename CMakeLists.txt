
cmake_minimum_required(VERSION 3.11)
project(equipetrol_delivery LANGUAGES CXX)

# Estandar C++17 obligatorio
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opcion obligatoria para ThreadSanitizer
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# Dependencias permitidas
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

# ============================================
# SIMULADOR PRINCIPAL
# ============================================

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

add_executable(delivery_sim ${SOURCES})

target_include_directories(delivery_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(delivery_sim PRIVATE
    Threads::Threads
    ${OpenCV_LIBS}
    nlohmann_json::nlohmann_json
)

target_compile_options(delivery_sim PRIVATE
    -Wall
    -Wextra
)

if(ENABLE_TSAN)
    target_compile_options(delivery_sim PRIVATE
        -fsanitize=thread
        -g
        -O1
    )

    set_property(
        TARGET delivery_sim
        APPEND_STRING
        PROPERTY LINK_FLAGS " -fsanitize=thread"
    )
endif()

# ============================================
# DP7.1 - TEST DE OBSERVABILIDAD
# ============================================

enable_testing()

add_executable(test_event_logger
    tests/test_event_logger.cpp
    src/core/Logger.cpp
)

target_include_directories(test_event_logger PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_event_logger PRIVATE
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_compile_options(test_event_logger PRIVATE
    -Wall
    -Wextra
)

if(ENABLE_TSAN)
    target_compile_options(test_event_logger PRIVATE
        -fsanitize=thread
        -g
        -O1
    )

    set_property(
        TARGET test_event_logger
        APPEND_STRING
        PROPERTY LINK_FLAGS " -fsanitize=thread"
    )
endif()

add_test(
    NAME test_event_logger
    COMMAND test_event_logger
)
