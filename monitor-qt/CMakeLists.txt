cmake_minimum_required(VERSION 3.16)
project(MonitorAntenas VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets Test)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Núcleo sin interfaz gráfica: protocolo, modelo y red. Lo usan la aplicación y las pruebas.
add_library(nucleo STATIC
    src/protocolo.cpp
    src/lectorlineas.cpp
    src/modelored.cpp
    src/conexioncliente.cpp
    src/servidortcp.cpp
)
target_include_directories(nucleo PUBLIC src)
target_link_libraries(nucleo PUBLIC Qt6::Core Qt6::Network Qt6::Gui)

add_executable(monitor-antenas
    src/main.cpp
    src/ventana.cpp
    src/grafico.cpp
)
target_link_libraries(monitor-antenas PRIVATE nucleo Qt6::Widgets)

enable_testing()
add_executable(pruebas tests/test_monitor.cpp)
target_link_libraries(pruebas PRIVATE nucleo Qt6::Test)
add_test(NAME pruebas COMMAND pruebas)
