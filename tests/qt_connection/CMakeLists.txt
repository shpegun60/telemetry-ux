# Installed Session -> installed UX, actual TCP/QML acceptance. MIT.
cmake_minimum_required(VERSION 3.21)
project(telemetry_qt_connection_checks LANGUAGES CXX)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Test)
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS qt_connection tcp_server)
find_package(TelemetryUX 0.1.0 EXACT CONFIG REQUIRED COMPONENTS qt_connection)
qt_standard_project_setup(REQUIRES 6.4)
qt_add_executable(telemetry_qt_connection_check main.cpp
    ../../examples/inspector/DemoPeer.cpp ../../examples/inspector/TcpDemoServer.cpp)
qt_add_resources(telemetry_qt_connection_check "entry" PREFIX "/"
    BASE "../../examples/qt_connection" FILES ../../examples/qt_connection/Main.qml)
target_compile_features(telemetry_qt_connection_check PRIVATE cxx_std_20)
target_compile_definitions(telemetry_qt_connection_check PRIVATE QT_NO_KEYWORDS QT_NO_FOREACH)
target_compile_definitions(telemetry_qt_connection_check PRIVATE TELEMETRY_TEST_BULK_WRITABLE)
target_include_directories(telemetry_qt_connection_check PRIVATE ../../examples/inspector)
target_link_libraries(telemetry_qt_connection_check PRIVATE TelemetryUX::qt_connection
    Telemetry::tcp_server Qt6::Quick Qt6::QuickControls2 Qt6::Test)
qt_import_qml_plugins(telemetry_qt_connection_check)

# Keep controlled watch scheduling in the maintained installed-Session gate.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../dynamic_watchlist" watchlist-scheduling)
set_target_properties(telemetry_dynamic_watchlist_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../dynamic_batch" batch-presentation)
set_target_properties(telemetry_dynamic_batch_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# Geometry over the same installed Session/view, without opening a connection.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../qml_layout" layout)
set_target_properties(telemetry_qml_layout_check telemetry_qml_search_providers telemetry_qml_bulk_selection PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
