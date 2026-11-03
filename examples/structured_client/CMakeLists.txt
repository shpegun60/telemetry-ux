# Standalone application fixture; consume only installed public SDK targets. MIT.
cmake_minimum_required(VERSION 3.21)
project(telemetry_structured_payload_tools LANGUAGES CXX)
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS core resource_adapter)
add_executable(telemetry_structured_device Device.cpp)
target_compile_features(telemetry_structured_device PRIVATE cxx_std_20)
target_link_libraries(telemetry_structured_device PRIVATE Telemetry::core Telemetry::resource_adapter)
