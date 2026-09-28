cmake_minimum_required(VERSION 3.11)
project(equipetrol_delivery LANGUAGES CXX)

# Estándar C++17 obligatorio
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opción obligatoria para ThreadSanitizer (DP.0 / Sección 8)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# Banderas de advertencia estrictas obligatorias
add_compile_options(-Wall -Wextra)

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
endif()

# Dependencias permitidas: Hilos estándar C++, OpenCV y JSON
find_package(Threads REQUIRED)
find_package(OpenCV 4.1 REQUIRED)

# Buscar nlohmann_json en el sistema; si no está en la máquina limpia, CMake lo provee
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# Archivos fuente del proyecto
set(SOURCES
    src/main.cpp
)

# Target principal obligatorio: delivery_sim
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