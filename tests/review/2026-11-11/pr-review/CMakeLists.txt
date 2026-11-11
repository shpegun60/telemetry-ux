# PR #48 review probe. Configure with -DUX_SOURCE=<telemetry-ux checkout> and
# CMAKE_PREFIX_PATH holding Qt 6 and an installed Telemetry SDK. MIT.
cmake_minimum_required(VERSION 3.21)
project(telemetry_pr_review_probe LANGUAGES CXX)
if(NOT UX_SOURCE)
    message(FATAL_ERROR "Set UX_SOURCE to the telemetry-ux checkout under review")
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Qml Quick QuickControls2)
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS workbench core)
set(TELEMETRY_UX_BUILD_DYNAMIC_CONFIGURATOR ON)
add_subdirectory("${UX_SOURCE}/lib/qml" ux-qml)
qt_add_executable(pr48_trend_probe pr48_trend_probe.cpp)
target_compile_features(pr48_trend_probe PRIVATE cxx_std_20)
target_link_libraries(pr48_trend_probe PRIVATE TelemetryUX::dynamic_configurator Qt6::Core)
