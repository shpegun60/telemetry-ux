# Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.21)
project(telemetry_inspector_tcp_checks LANGUAGES CXX)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Test)
qt_standard_project_setup(REQUIRES 6.4)
get_filename_component(TELEMETRY_UX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(BUILD_TESTING OFF CACHE BOOL "Build only the maintained Inspector TCP checks" FORCE)
# Import in the test directory so Qt deferred finalization can resolve every
# SDK target transitively linked by the app's shared support library.
set(_inspector_components inspector workbench read_many_workbench encoded_backend encoded_server core tcp_server qt_transport qt_connection)
if(INSPECTOR_ENABLE_SERIAL)
    list(APPEND _inspector_components qt_serial_transport)
endif()
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS ${_inspector_components})
add_subdirectory("${TELEMETRY_UX_ROOT}/examples/inspector" example)
set_target_properties(telemetry-inspector-qml PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/example")
qt_add_executable(telemetry_inspector_tcp_check main.cpp)
target_compile_features(telemetry_inspector_tcp_check PRIVATE cxx_std_20)
target_link_libraries(telemetry_inspector_tcp_check PRIVATE inspector_qml_support Qt6::Gui Qt6::Network Qt6::Test)
