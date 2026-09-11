cmake_minimum_required(VERSION 3.25)

project(FourShades VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # /Zc:__cplusplus makes __cplusplus report the real standard. /FS stops
    # parallel compiles racing on one .pdb (seen in Litharia as C1041).
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /FS)
endif()

# ---------------------------------------------------------------------------
# Emulator core. No window, no files, no test code: tools/check_core_isolation.py
# enforces that in CI.
# ---------------------------------------------------------------------------
add_library(fourshades_core STATIC
    src/core/Cpu.cpp
    src/core/CpuLoads8.cpp
    src/core/CpuAlu8.cpp
    src/core/CpuWide.cpp
    src/core/CpuControl.cpp
    src/core/CpuCb.cpp
    src/core/Cartridge.cpp
    src/core/Timer.cpp
    src/core/Serial.cpp
    src/core/LcdTiming.cpp
)
target_include_directories(fourshades_core PUBLIC src)
add_subdirectory(tools/sst)

enable_testing()

# ---------------------------------------------------------------------------
# Unit tests (doctest). Every tests/*.cpp file is part of this one executable.
# ---------------------------------------------------------------------------
file(GLOB FOURSHADES_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
add_executable(fourshades_tests ${FOURSHADES_TEST_SOURCES})
target_include_directories(fourshades_tests PRIVATE src tools)
# SYSTEM: MSVC compiles vendored headers with /external:W0, so their warnings
# (doctest's C5285) don't bury ours.
target_include_directories(fourshades_tests SYSTEM PRIVATE third_party)
add_test(NAME unit COMMAND fourshades_tests)
target_link_libraries(fourshades_tests PRIVATE sst_harness)
