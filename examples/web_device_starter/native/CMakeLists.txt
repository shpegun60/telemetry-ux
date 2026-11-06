# Installed-package consumer: no source-tree include or copied protocol engine. MIT.
cmake_minimum_required(VERSION 3.21)
project(TelemetryOwnDeviceStarter LANGUAGES CXX)
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS core encoded_server batch_server)
add_executable(telemetry_device_starter Peer.cpp Device.hpp)
target_compile_features(telemetry_device_starter PRIVATE cxx_std_20)
target_link_libraries(telemetry_device_starter PRIVATE Telemetry::core Telemetry::encoded_server Telemetry::batch_server)
