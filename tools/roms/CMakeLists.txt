# The test-ROM harness. Never linked into the emulator itself.
add_library(rom_harness STATIC
    RomTests.cpp
    Detectors.cpp
    RomRun.cpp
)
target_link_libraries(rom_harness PUBLIC sst_harness)

add_executable(rom_runner rom_runner.cpp)
target_link_libraries(rom_runner PRIVATE rom_harness)
