# Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.21)
project(TelemetryWidgetsPlayground LANGUAGES CXX)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS core resource resource_adapter resource_protocol)
qt_standard_project_setup(REQUIRES 6.4)

# Application composition, not a second SDK abstraction. Generic facade users
# inherit only Resource headers; the native Model implementation stays private.
add_library(widgets_demo_resources STATIC demo/DemoCatalog.cpp resources/DeviceResources.cpp)
set_target_properties(widgets_demo_resources PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_compile_features(widgets_demo_resources PUBLIC cxx_std_20)
target_include_directories(widgets_demo_resources PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(widgets_demo_resources PUBLIC Telemetry::resource
    PRIVATE Telemetry::core Telemetry::resource_adapter Telemetry::resource_protocol)

qt_add_executable(telemetry_playground main.cpp mainwindow.cpp mainwindow.h mainwindow.ui)
target_compile_features(telemetry_playground PRIVATE cxx_std_20)
target_link_libraries(telemetry_playground PRIVATE widgets_demo_resources Telemetry::core Qt6::Widgets)
set_target_properties(telemetry_playground PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
