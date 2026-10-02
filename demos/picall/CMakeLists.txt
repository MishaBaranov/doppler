# ---------------------------------------------------------------------------
# picall — a headless, video-only Pulse caller (built for Raspberry Pi 4).
#
# Registered in the root CMakeLists.txt behind BUILD_PICALL. Needs nothing but
# Pulse and a C++17 compiler: no GLFW, OpenGL or ImGui.
#
# The PiRacer I2C drivers and the car controller sit in a small static library
# that does not depend on Pulse, so piracer_test can run on any Linux host.
# ---------------------------------------------------------------------------
add_library(piracer_hw STATIC
    src/i2c_bus.cpp
    src/pca9685.cpp
    src/ina219.cpp
    src/ssd1306.cpp
    src/piracer.cpp)
target_include_directories(piracer_hw PUBLIC src)

add_executable(picall
    src/main.cpp)

target_link_libraries(picall PRIVATE
    piracer_hw
    pexip::pulse
    Threads::Threads)

pulse_demo_rpath(picall)
pulse_demo_launcher(picall run-picall.sh)

# Run with:  ctest --test-dir <build>/demos/picall
enable_testing()
add_executable(piracer_test src/piracer_test.cpp)
target_link_libraries(piracer_test PRIVATE piracer_hw)
add_test(NAME piracer_test COMMAND piracer_test)
