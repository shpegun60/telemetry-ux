# A downstream application consumes installed packages only. MIT.
cmake_minimum_required(VERSION 3.21)
project(telemetry_dynamic_qml_provider_checks LANGUAGES CXX)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Qml Quick QuickControls2 Test)
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS core workbench encoded_server encoded_backend)
find_package(TelemetryUX 0.1.0 EXACT CONFIG REQUIRED COMPONENTS dynamic_configurator)
qt_standard_project_setup(REQUIRES 6.4)
qt_add_executable(telemetry_dynamic_qml_provider main.cpp Fixture.hpp)
target_compile_features(telemetry_dynamic_qml_provider PRIVATE cxx_std_20)
target_compile_definitions(telemetry_dynamic_qml_provider PRIVATE QT_NO_KEYWORDS)
target_compile_definitions(telemetry_dynamic_qml_provider PRIVATE
    DYNAMIC_QML_EXAMPLE_MAIN="${CMAKE_CURRENT_SOURCE_DIR}/../../examples/dynamic_qml/Main.qml")
target_link_libraries(telemetry_dynamic_qml_provider PRIVATE TelemetryUX::dynamic_configurator
    Telemetry::core Telemetry::encoded_server Telemetry::encoded_backend Qt6::Test)
