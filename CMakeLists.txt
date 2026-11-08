cmake_minimum_required(VERSION 3.16)
project(sq80_fm1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The engine: portable C99, no OS dependencies: the same source builds as FM-1 firmware (fm1/).
add_library(sq80 STATIC src/sq80/sq80_patch.c src/sq80/sq80_synth.c)
target_include_directories(sq80 PUBLIC src)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  # -Wdouble-promotion: the FM-1's FPU is single precision, a double would be emulated
  target_compile_options(sq80 PRIVATE -Wall -Wextra -Wshadow -Wfloat-conversion -Wdouble-promotion -pedantic)
endif()
target_link_libraries(sq80 PUBLIC m)

add_executable(sq80_render tools/sq80_render.cpp)
target_link_libraries(sq80_render PRIVATE sq80)

enable_testing()
add_executable(sq80_tests tests/test_main.cpp tests/synthetic_rom.cpp)
target_link_libraries(sq80_tests PRIVATE sq80)
add_test(NAME sq80_tests COMMAND sq80_tests)
