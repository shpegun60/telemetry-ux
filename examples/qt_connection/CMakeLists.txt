# Installed Qt connection + two reusable panels. MIT.
cmake_minimum_required(VERSION 3.25)
project(TelemetryQtConnection LANGUAGES CXX)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network)
find_package(Telemetry 0.1.0 CONFIG REQUIRED COMPONENTS qt_connection)
find_package(TelemetryUX 0.1.0 CONFIG REQUIRED COMPONENTS qt_connection)
qt_standard_project_setup(REQUIRES 6.12)
qt_add_executable(telemetry-qt-connection main.cpp)
qt_add_resources(telemetry-qt-connection "entry" PREFIX "/" FILES Main.qml)
target_compile_features(telemetry-qt-connection PRIVATE cxx_std_20)
target_link_libraries(telemetry-qt-connection PRIVATE TelemetryUX::qt_connection
    Qt6::Quick Qt6::QuickControls2)
qt_import_qml_plugins(telemetry-qt-connection)
