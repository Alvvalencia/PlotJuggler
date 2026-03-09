cmake_minimum_required(VERSION 3.16)
project(pj_bench_engines CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_USE_BUNDLED_GTEST OFF CACHE BOOL "" FORCE)
set(BENCHMARK_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.8.3
)
FetchContent_MakeAvailable(benchmark)

set(PJ_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(PJ_TRANSFORMS ${PJ_ROOT}/plotjuggler_app/transforms)
set(PJ_SOL2 ${PJ_ROOT}/3rdparty/sol2-3.5.0/include)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${PJ_ROOT} ${CMAKE_BINARY_DIR}/pj_build EXCLUDE_FROM_ALL)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Xml)
find_package(Threads REQUIRED)
find_package(Lua REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Development Development.Embed)

add_executable(pj_bench_engines
  ${CMAKE_CURRENT_LIST_DIR}/bench_engines.cpp

  ${PJ_TRANSFORMS}/custom_function.cpp
  ${PJ_TRANSFORMS}/lua_custom_function.cpp
  ${PJ_TRANSFORMS}/python_custom_funcion.cpp
)

target_include_directories(pj_bench_engines PRIVATE
  ${PJ_ROOT}
  ${PJ_ROOT}/plotjuggler_base/include
  ${PJ_ROOT}/plotjuggler_app
  ${PJ_TRANSFORMS}
  ${PJ_SOL2}
  ${LUA_INCLUDE_DIR}
  ${Python3_INCLUDE_DIRS}
)

target_compile_definitions(pj_bench_engines PRIVATE
  PJ_HAS_PYTHON
)

target_link_libraries(pj_bench_engines PRIVATE
  benchmark::benchmark
  Threads::Threads

  plotjuggler_base

  Qt5::Core
  Qt5::Gui
  Qt5::Widgets
  Qt5::Xml

  ${LUA_LIBRARIES}
  Python3::Python
)
