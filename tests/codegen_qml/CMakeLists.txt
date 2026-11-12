# Generated API runtime qualification using an installed SDK. MIT.
cmake_minimum_required(VERSION 3.25)
project(telemetry_codegen_qml_checks LANGUAGES CXX)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick Test)
find_package(Telemetry 0.1.0 CONFIG REQUIRED COMPONENTS
    core encoded_server encoded_backend rpc_backend rpc_sessions)
qt_standard_project_setup(REQUIRES 6.12)
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
if(NOT GENERATED_SDK)
    message(FATAL_ERROR "Set GENERATED_SDK to the generated SDK root")
endif()
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../examples/codegen_qml/fixture" fixture)
add_subdirectory("${GENERATED_SDK}/qml" generated)
qt_add_executable(telemetry_qml_check main.cpp)
qt_add_qml_module(telemetry_qml_check
    URI Telemetry.Tests VERSION 1.0 QML_FILES Smoke.qml)
target_compile_features(telemetry_qml_check PRIVATE cxx_std_20)
target_compile_definitions(telemetry_qml_check PRIVATE QT_NO_KEYWORDS)
target_link_libraries(telemetry_qml_check PRIVATE telemetry_qml_demo_fixture
    telemetry_qml_device_api telemetry_qml_device_apiplugin Qt6::Quick Qt6::Test
    Telemetry::encoded_server Telemetry::encoded_backend Telemetry::rpc_backend Telemetry::rpc_sessions)
qt_import_qml_plugins(telemetry_qml_check)
qt_add_executable(telemetry_qml_queued Queued.cpp)
target_compile_features(telemetry_qml_queued PRIVATE cxx_std_20)
target_link_libraries(telemetry_qml_queued PRIVATE telemetry_qml_demo_fixture
    telemetry_qml_device_api Qt6::Test Telemetry::encoded_server
    Telemetry::encoded_backend Telemetry::rpc_backend Telemetry::rpc_sessions)
