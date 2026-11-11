# Application-owned descriptor export; no Qt or SDK checkout is needed. MIT.
cmake_minimum_required(VERSION 3.21)
project(TelemetryQmlDemoFixture LANGUAGES CXX)
find_package(Telemetry 0.1.0 CONFIG REQUIRED COMPONENTS core)

add_library(telemetry_qml_demo_fixture INTERFACE)
target_compile_features(telemetry_qml_demo_fixture INTERFACE cxx_std_20)
target_include_directories(telemetry_qml_demo_fixture INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(telemetry_qml_demo_fixture INTERFACE Telemetry::core)

add_executable(telemetry_qml_fixture_producer EXCLUDE_FROM_ALL Producer.cpp)
target_link_libraries(telemetry_qml_fixture_producer PRIVATE telemetry_qml_demo_fixture)
set(TELEMETRY_QML_FIXTURE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/native" CACHE PATH
    "Descriptor and canonical payload output directory for this demo")
add_custom_target(telemetry_qml_fixture_export
    COMMAND $<TARGET_FILE:telemetry_qml_fixture_producer> "${TELEMETRY_QML_FIXTURE_OUTPUT}"
    DEPENDS telemetry_qml_fixture_producer
    BYPRODUCTS
        "${TELEMETRY_QML_FIXTURE_OUTPUT}/descriptor.bin"
        "${TELEMETRY_QML_FIXTURE_OUTPUT}/config.bin"
        "${TELEMETRY_QML_FIXTURE_OUTPUT}/request.bin"
        "${TELEMETRY_QML_FIXTURE_OUTPUT}/response.bin"
        "${TELEMETRY_QML_FIXTURE_OUTPUT}/scalars.bin"
        "${TELEMETRY_QML_FIXTURE_OUTPUT}/empty.bin"
    USES_TERMINAL VERBATIM)
