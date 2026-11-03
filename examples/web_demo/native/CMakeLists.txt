# Native application peer consumes the installed SDK, never its source checkout. MIT.
cmake_minimum_required(VERSION 3.21)
project(TelemetryWebDemoNative LANGUAGES CXX)
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS core encoded_server batch_server)
add_executable(telemetry_web_demo_peer Peer.cpp Model.hpp)
target_compile_features(telemetry_web_demo_peer PRIVATE cxx_std_20)
target_link_libraries(telemetry_web_demo_peer PRIVATE Telemetry::core Telemetry::encoded_server Telemetry::batch_server)
