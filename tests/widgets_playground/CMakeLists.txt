# Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.21)
project(TelemetryWidgetsPlaygroundChecks LANGUAGES CXX)
# Parent visibility also permits Qt 6.4 deferred executable finalization.
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS core resource resource_adapter resource_protocol)
get_filename_component(TELEMETRY_UX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory("${TELEMETRY_UX_ROOT}/examples/widgets_playground" playground)
add_executable(widgets_device_facade_check DeviceCheck.cpp)
set_target_properties(widgets_device_facade_check PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_compile_features(widgets_device_facade_check PRIVATE cxx_std_20)
target_link_libraries(widgets_device_facade_check PRIVATE widgets_demo_resources)
