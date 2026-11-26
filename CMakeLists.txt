cmake_minimum_required(VERSION 3.16)
project(SudokuSolver
    VERSION 1.0.0
    DESCRIPTION "Sudoku solver and generator focused on performance"
    LANGUAGES CXX)

# --------------------------------------------------------------------------
# Opciones
# --------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(SUDOKU_IS_TOP_LEVEL ON)
else()
    set(SUDOKU_IS_TOP_LEVEL OFF)
endif()

option(SUDOKU_BUILD_TESTS      "Construir el test de API"        ${SUDOKU_IS_TOP_LEVEL})
option(SUDOKU_BUILD_BENCHMARKS "Construir los benchmarks"        ${SUDOKU_IS_TOP_LEVEL})
option(SUDOKU_NATIVE_ARCH      "Compilar para la CPU nativa (popcount nativo; el binario solo corre en esta CPU)" OFF)
option(SUDOKU_WARNINGS_AS_ERRORS "Tratar los avisos como errores" OFF)

if(SUDOKU_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build" FORCE)
endif()

# --------------------------------------------------------------------------
# Opciones comunes (interface target)
# --------------------------------------------------------------------------
add_library(sudoku_options INTERFACE)
target_compile_features(sudoku_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(sudoku_options INTERFACE /W4 /permissive-)
    if(SUDOKU_WARNINGS_AS_ERRORS)
        target_compile_options(sudoku_options INTERFACE /WX)
    endif()
    if(SUDOKU_NATIVE_ARCH)
        # Equivalente MSVC de -march=native para POPCNT/AVX2.
        # Si necesitas AVX-512, cambia a /arch:AVX512 (VS 2022 17.x+).
        target_compile_options(sudoku_options INTERFACE /arch:AVX2)
    endif()
else()
    target_compile_options(sudoku_options INTERFACE -Wall -Wextra -Wpedantic)
    if(SUDOKU_WARNINGS_AS_ERRORS)
        target_compile_options(sudoku_options INTERFACE -Werror)
    endif()
    if(SUDOKU_NATIVE_ARCH)
        target_compile_options(sudoku_options INTERFACE -march=native)
    endif()
endif()

# Marca de "esto es Release" util para el aviso del benchmark en MSVC,
# ya que __OPTIMIZE__ no existe alli.
target_compile_definitions(sudoku_options INTERFACE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:SUDOKU_OPTIMIZED>)

# --------------------------------------------------------------------------
# Libreria
# --------------------------------------------------------------------------
add_library(sudoku src/Sudoku.cpp)
add_library(Sudoku::Sudoku ALIAS sudoku)

target_include_directories(sudoku
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(sudoku PRIVATE sudoku_options)

# --------------------------------------------------------------------------
# Test de API
# --------------------------------------------------------------------------
if(SUDOKU_BUILD_TESTS)
    enable_testing()
    add_executable(api_test tests/API-test.cpp)
    target_link_libraries(api_test PRIVATE sudoku sudoku_options)
    add_test(NAME api_test COMMAND api_test)
endif()

# --------------------------------------------------------------------------
# Benchmarks
# --------------------------------------------------------------------------
if(SUDOKU_BUILD_BENCHMARKS)
    foreach(bench IN ITEMS solver-benchmark generator-benchmark)
        add_executable(${bench} tests/${bench}.cpp)
        target_include_directories(${bench} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(${bench} PRIVATE sudoku_options)
    endforeach()
endif()