# An ordinary installed SDK/UX consumer; open this project in Qt Creator. MIT.
cmake_minimum_required(VERSION 3.21)
project(TelemetryOwnDynamicQml LANGUAGES CXX)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Qml Quick QuickControls2)
find_package(Telemetry 0.1.0 CONFIG REQUIRED COMPONENTS core workbench encoded_backend encoded_server)
find_package(TelemetryUX 0.1.0 CONFIG REQUIRED COMPONENTS dynamic_configurator)
qt_standard_project_setup(REQUIRES 6.4)
qt_add_executable(telemetry_dynamic_example main.cpp Device.hpp)
target_compile_features(telemetry_dynamic_example PRIVATE cxx_std_20)
target_link_libraries(telemetry_dynamic_example PRIVATE TelemetryUX::dynamic_configurator
    Telemetry::core Telemetry::encoded_backend Telemetry::encoded_server)
qt_add_resources(telemetry_dynamic_example "example" PREFIX "/" FILES Main.qml)
