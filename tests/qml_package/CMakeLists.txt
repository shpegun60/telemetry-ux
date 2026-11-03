# Independent consumer of the installed, pure Qt view. MIT.
cmake_minimum_required(VERSION 3.21)
project(TelemetryUXPackageConsumer LANGUAGES CXX)
find_package(Qt6 6.4 REQUIRED COMPONENTS Gui Qml Quick QuickControls2)
find_package(TelemetryUX 0.1.0 EXACT CONFIG REQUIRED COMPONENTS configurator)
qt_standard_project_setup(REQUIRES 6.4)
qt_add_executable(telemetry_ux_package_check Consumer.cpp)
target_link_libraries(telemetry_ux_package_check PRIVATE TelemetryUX::configurator)
target_compile_features(telemetry_ux_package_check PRIVATE cxx_std_17)
