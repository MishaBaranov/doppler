# ---------------------------------------------------------------------------
# picall — a headless, video-only Pulse caller (built for Raspberry Pi 4).
#
# Registered in the root CMakeLists.txt behind BUILD_PICALL. Needs nothing but
# Pulse and a C++17 compiler: no GLFW, OpenGL or ImGui.
# ---------------------------------------------------------------------------
add_executable(picall
    src/main.cpp)

target_link_libraries(picall PRIVATE
    pexip::pulse
    Threads::Threads)

pulse_demo_rpath(picall)
pulse_demo_launcher(picall run-picall.sh)
