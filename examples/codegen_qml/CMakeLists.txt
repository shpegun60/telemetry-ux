# Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.21)
project(telemetry_codegen_qml_example LANGUAGES CXX)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.4)
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
find_package(Telemetry 0.1.0 CONFIG REQUIRED COMPONENTS
    core encoded_server encoded_backend rpc_backend rpc_sessions)
if(NOT GENERATED_SDK)
    message(FATAL_ERROR "Set GENERATED_SDK to the external SDK generated from fixture/native/descriptor.bin")
endif()
add_subdirectory(fixture)
add_subdirectory("${GENERATED_SDK}/qml" generated)
qt_add_executable(telemetry_qml_example main.cpp)
qt_add_qml_module(telemetry_qml_example URI Telemetry.Example VERSION 1.0
    RESOURCE_PREFIX /qt/qml QML_FILES Main.qml)
target_compile_features(telemetry_qml_example PRIVATE cxx_std_20)
target_compile_definitions(telemetry_qml_example PRIVATE QT_NO_KEYWORDS QT_NO_FOREACH)
target_link_libraries(telemetry_qml_example PRIVATE telemetry_qml_device_api
    telemetry_qml_device_apiplugin telemetry_qml_demo_fixture
    Telemetry::encoded_server Telemetry::encoded_backend Telemetry::rpc_backend
    Telemetry::rpc_sessions Qt6::Quick Qt6::QuickControls2)
qt_import_qml_plugins(telemetry_qml_example)
