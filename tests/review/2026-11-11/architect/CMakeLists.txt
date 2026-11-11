# Review probe (architect, 2026-11-11): Static Watchlist edit vs Trend selection.
# Configure against an installed TelemetryUX (static_configurator component).
cmake_minimum_required(VERSION 3.21)
project(telemetry_review_architect_probe LANGUAGES CXX)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Qml Quick Test)
find_package(TelemetryUX 0.1.0 EXACT CONFIG REQUIRED COMPONENTS static_configurator)
qt_standard_project_setup(REQUIRES 6.4)
qt_add_executable(static_trend_reset static_trend_reset.cpp)
target_compile_features(static_trend_reset PRIVATE cxx_std_20)
target_link_libraries(static_trend_reset PRIVATE TelemetryUX::static_configurator Qt6::Test)
