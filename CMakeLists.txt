cmake_minimum_required(VERSION 3.16)
project(sq80_fm1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The engine: portable C++17, no OS dependencies, so it builds for the FM-1 unchanged.
add_library(sq80 STATIC src/sq80/patch.cpp src/sq80/synth.cpp)
target_include_directories(sq80 PUBLIC src)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sq80 PRIVATE -Wall -Wextra -Wshadow -Wfloat-conversion)
endif()

add_executable(sq80_render tools/sq80_render.cpp)
target_link_libraries(sq80_render PRIVATE sq80)

enable_testing()
add_executable(sq80_tests tests/test_main.cpp tests/synthetic_rom.cpp)
target_link_libraries(sq80_tests PRIVATE sq80)
add_test(NAME sq80_tests COMMAND sq80_tests)
