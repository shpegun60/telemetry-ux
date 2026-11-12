# Installed Session -> installed UX: the Device files models and panel over real TCP. MIT.
cmake_minimum_required(VERSION 3.25)
project(telemetry_device_files_checks LANGUAGES CXX)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Test)
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS qt_connection tcp_server resource_adapter)
find_package(TelemetryUX 0.1.0 EXACT CONFIG REQUIRED COMPONENTS qt_connection)
qt_standard_project_setup(REQUIRES 6.12)

qt_add_executable(telemetry_device_files_check main.cpp)
target_compile_features(telemetry_device_files_check PRIVATE cxx_std_20)
target_compile_definitions(telemetry_device_files_check PRIVATE QT_NO_KEYWORDS QT_NO_FOREACH)
# resource_adapter: values.bin written as a device serves it, for the values codec.
target_link_libraries(telemetry_device_files_check PRIVATE TelemetryUX::qt_connection
    Telemetry::tcp_server Telemetry::resource_adapter Qt6::Core Qt6::Network)

qt_add_executable(telemetry_device_files_panel panel.cpp)
qt_add_resources(telemetry_device_files_panel "panel-entry" PREFIX "/" FILES Panel.qml)
target_compile_features(telemetry_device_files_panel PRIVATE cxx_std_20)
target_compile_definitions(telemetry_device_files_panel PRIVATE QT_NO_KEYWORDS QT_NO_FOREACH)
# Test: keys sent through the window, as typed, so a window's shortcuts see them.
target_link_libraries(telemetry_device_files_panel PRIVATE TelemetryUX::qt_connection
    Telemetry::tcp_server Qt6::Quick Qt6::QuickControls2 Qt6::Test)
qt_import_qml_plugins(telemetry_device_files_panel)
