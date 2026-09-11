# The SingleStepTests harness. Never linked into the emulator itself.
add_library(sst_harness STATIC
    Sha256.cpp
    Manifest.cpp
    Selection.cpp
    SstLoader.cpp
    SstCompare.cpp
    SstRun.cpp
)
target_include_directories(sst_harness PUBLIC ${CMAKE_SOURCE_DIR}/tools)
target_include_directories(sst_harness SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(sst_harness PUBLIC fourshades_core)
