# Programmer-review downstream project: an own device served on localhost TCP and a
# Qt Quick window built from installed Telemetry SDK + UX packages only.
# Configure with CMAKE_PREFIX_PATH="<sdk-prefix>;<ux-prefix>;<qt-prefix>".
cmake_minimum_required(VERSION 3.21)
project(MyDeviceReview LANGUAGES CXX)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2)
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS core tcp_encoded_profile qt_connection)
find_package(TelemetryUX 0.1.0 EXACT CONFIG REQUIRED COMPONENTS qt_connection)
qt_standard_project_setup(REQUIRES 6.4)

# Device side: the Model plus application-owned socket glue.
qt_add_executable(mydevice-server server/main.cpp device/MyDevice.cpp device/MyDevice.hpp)
set_target_properties(mydevice-server PROPERTIES WIN32_EXECUTABLE OFF)
target_compile_features(mydevice-server PRIVATE cxx_std_20)
target_link_libraries(mydevice-server PRIVATE Qt6::Network Telemetry::core Telemetry::tcp_encoded_profile)

# Host side: ready-made panels over one ConnectionSession.
qt_add_executable(mydevice-panel client/main.cpp)
set_target_properties(mydevice-panel PROPERTIES WIN32_EXECUTABLE OFF)
qt_add_resources(mydevice-panel "entry" PREFIX "/" BASE client FILES client/Main.qml)
target_compile_features(mydevice-panel PRIVATE cxx_std_20)
target_link_libraries(mydevice-panel PRIVATE TelemetryUX::qt_connection Qt6::Quick Qt6::QuickControls2)
qt_import_qml_plugins(mydevice-panel)
