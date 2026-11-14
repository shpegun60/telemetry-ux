# Deterministic installed-package cache and scheduling contracts. MIT.
cmake_minimum_required(VERSION 3.25)
project(telemetry_endpoint_previews_checks LANGUAGES CXX)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Test)
find_package(Telemetry 0.1.0 CONFIG REQUIRED COMPONENTS core encoded_server encoded_backend read_many_workbench)
find_package(TelemetryUX 0.1.0 CONFIG REQUIRED COMPONENTS dynamic_configurator)
qt_standard_project_setup(REQUIRES 6.12)
qt_add_executable(telemetry_endpoint_previews_check main.cpp)
target_compile_features(telemetry_endpoint_previews_check PRIVATE cxx_std_20)
target_compile_definitions(telemetry_endpoint_previews_check PRIVATE QT_NO_KEYWORDS)
target_link_libraries(telemetry_endpoint_previews_check PRIVATE TelemetryUX::dynamic_configurator
    Telemetry::core Telemetry::encoded_server Telemetry::encoded_backend)
enable_testing()
add_test(NAME endpoint_previews COMMAND telemetry_endpoint_previews_check)

# Actual installed Session + local TCP + shipped QML controls. No board access.
find_package(Qt6 6.12 REQUIRED COMPONENTS Gui Qml Quick QuickControls2 Network Test)
find_package(Telemetry 0.1.0 CONFIG REQUIRED COMPONENTS qt_connection tcp_server)
find_package(TelemetryUX 0.1.0 CONFIG REQUIRED COMPONENTS qt_connection)
qt_add_executable(telemetry_endpoint_previews_ui_check ui.cpp
    ../../examples/inspector/DemoPeer.cpp ../../examples/inspector/TcpDemoServer.cpp)
target_compile_features(telemetry_endpoint_previews_ui_check PRIVATE cxx_std_20)
target_compile_definitions(telemetry_endpoint_previews_ui_check PRIVATE QT_NO_KEYWORDS QT_NO_FOREACH)
target_include_directories(telemetry_endpoint_previews_ui_check PRIVATE ../../examples/inspector)
target_link_libraries(telemetry_endpoint_previews_ui_check PRIVATE TelemetryUX::qt_connection
    Telemetry::tcp_server Qt6::Quick Qt6::QuickControls2 Qt6::Test)
qt_import_qml_plugins(telemetry_endpoint_previews_ui_check)
add_test(NAME endpoint_previews_ui COMMAND telemetry_endpoint_previews_ui_check)

qt_add_executable(telemetry_endpoint_preview_models_check model.cpp)
target_compile_features(telemetry_endpoint_preview_models_check PRIVATE cxx_std_20)
target_compile_definitions(telemetry_endpoint_preview_models_check PRIVATE QT_NO_KEYWORDS)
target_link_libraries(telemetry_endpoint_preview_models_check PRIVATE TelemetryUX::dynamic_configurator
    Telemetry::core Telemetry::encoded_server Telemetry::encoded_backend Telemetry::read_many_workbench Qt6::Test)
add_test(NAME endpoint_preview_models COMMAND telemetry_endpoint_preview_models_check)
