# The playable window. `screen_lib` holds the pure functions (palette,
# integer-scaling math and the frame-pacing arithmetic), the AppController
# state machine and the save-file code so they can be unit-tested without SDL
# or a window; `fourshades_app` is the window itself. src/core links neither
# SDL3 nor screen_lib -- see tools/check_core_isolation.py.
add_library(screen_lib STATIC Screen.cpp AppController.cpp Input.cpp Save.cpp FramePacer.cpp AudioResampler.cpp)
target_include_directories(screen_lib PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(screen_lib PUBLIC fourshades_core)
# Headers only, deliberately not SDL3::SDL3: Input.cpp names SDL's scancodes
# but calls no SDL function, so screen_lib -- and the test executable that
# links it -- stays free of SDL3.dll while the key mapping is still written
# in SDL's own constants rather than copies of their numbers.
target_include_directories(screen_lib SYSTEM PUBLIC ${SDL3_INCLUDE_DIR})

# Audio.cpp is the only file in app/ that calls SDL, so it is the only one
# that cannot live in screen_lib. app/Audio.h holds the drift policy as
# constexpr free functions and forward-declares SDL_AudioStream, so
# fourshades_tests includes that header and links none of this.
add_executable(fourshades_app main.cpp Audio.cpp)
target_link_libraries(fourshades_app PRIVATE screen_lib fourshades_core SDL3::SDL3)
add_custom_command(TARGET fourshades_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL3_LIB_DIR}/SDL3.dll" "$<TARGET_FILE_DIR:fourshades_app>"
    COMMENT "Copying SDL3.dll next to fourshades_app"
)
