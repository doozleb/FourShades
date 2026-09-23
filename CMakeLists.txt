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
    src/core/mbc/MbcNone.cpp
    src/core/mbc/Mbc1.cpp
    src/core/mbc/Mbc2.cpp
    src/core/mbc/Mbc3.cpp
    src/core/mbc/Mbc5.cpp
    src/core/Timer.cpp
    src/core/Serial.cpp
    src/core/Joypad.cpp
    src/core/Ppu.cpp
    src/core/PixelPipeline.cpp
    src/core/GameBoy.cpp
)
target_include_directories(fourshades_core PUBLIC src)
add_subdirectory(tools/sst)
add_subdirectory(tools/roms)

# ---------------------------------------------------------------------------
# SDL3 (vendored binary, gitignored, never committed). Fetched on demand by
# third_party/sdl/fetch_sdl.py; see third_party/sdl/README.md for the pinned
# version and hash. src/core never includes or links this — the emulator
# core stays free of SDL, same as it stays free of the test harness.
#
# Only x64 is wired in: tools\dev.cmd always sets up the x64 developer
# environment, so that is the only architecture this build ever targets.
# ---------------------------------------------------------------------------
set(SDL3_VERSION 3.4.16)
set(SDL3_ROOT ${CMAKE_SOURCE_DIR}/third_party/sdl/SDL3-${SDL3_VERSION})
set(SDL3_INCLUDE_DIR ${SDL3_ROOT}/include)
set(SDL3_LIB_DIR ${SDL3_ROOT}/lib/x64)
if(NOT EXISTS ${SDL3_INCLUDE_DIR}/SDL3/SDL.h)
    message(FATAL_ERROR
        "SDL3 is not vendored (expected ${SDL3_INCLUDE_DIR}/SDL3/SDL.h). "
        "Run: python third_party/sdl/fetch_sdl.py")
endif()
add_library(SDL3::SDL3 SHARED IMPORTED)
set_target_properties(SDL3::SDL3 PROPERTIES
    IMPORTED_LOCATION "${SDL3_LIB_DIR}/SDL3.dll"
    IMPORTED_IMPLIB "${SDL3_LIB_DIR}/SDL3.lib"
)
# SYSTEM: MSVC compiles vendored headers with /external:W0, so SDL3's
# warnings don't bury ours (same reasoning as the third_party include below).
target_include_directories(SDL3::SDL3 SYSTEM INTERFACE ${SDL3_INCLUDE_DIR})

# Trivial proof that SDL3 links: calls SDL_GetVersion() and prints it.
add_executable(sdl_probe tools/sdl_probe/sdl_probe.cpp)
target_link_libraries(sdl_probe PRIVATE SDL3::SDL3)
add_custom_command(TARGET sdl_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL3_LIB_DIR}/SDL3.dll" "$<TARGET_FILE_DIR:sdl_probe>"
    COMMENT "Copying SDL3.dll next to sdl_probe"
)

# ---------------------------------------------------------------------------
# The playable window (piece 3b). Never linked into src/core.
# ---------------------------------------------------------------------------
add_subdirectory(app)

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
target_link_libraries(fourshades_tests PRIVATE sst_harness rom_harness screen_lib)
