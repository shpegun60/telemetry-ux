# Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.21)
project(telemetry_inspector_qml_checks LANGUAGES CXX)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test)
qt_standard_project_setup(REQUIRES 6.4)
get_filename_component(TELEMETRY_UX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(BUILD_TESTING OFF CACHE BOOL "Build only the maintained Inspector checks" FORCE)
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
qt_add_executable(telemetry_inspector_qml_check main.cpp)
target_compile_features(telemetry_inspector_qml_check PRIVATE cxx_std_20)
target_link_libraries(telemetry_inspector_qml_check PRIVATE inspector_qml_support Qt6::Gui Qt6::Test)
target_include_directories(telemetry_inspector_qml_check PRIVATE
    "${TELEMETRY_UX_ROOT}/examples/inspector")
qt_add_executable(telemetry_inspector_qml_input_check QmlInput.cpp)
target_compile_features(telemetry_inspector_qml_input_check PRIVATE cxx_std_20)
target_compile_definitions(telemetry_inspector_qml_input_check PRIVATE
    INSPECTOR_QML_SOURCE_FILE="${TELEMETRY_UX_ROOT}/examples/inspector/Main.qml")
target_link_libraries(telemetry_inspector_qml_input_check PRIVATE inspector_qml_support
    Qt6::Quick Qt6::QuickControls2 Qt6::Test)
qt_import_qml_plugins(telemetry_inspector_qml_input_check)
qt_add_executable(telemetry_inspector_qml_command_check Command.cpp)
target_compile_features(telemetry_inspector_qml_command_check PRIVATE cxx_std_20)
target_compile_definitions(telemetry_inspector_qml_command_check PRIVATE
    INSPECTOR_QML_SOURCE_FILE="${TELEMETRY_UX_ROOT}/examples/inspector/Main.qml")
target_link_libraries(telemetry_inspector_qml_command_check PRIVATE inspector_qml_support
    Qt6::Quick Qt6::QuickControls2 Qt6::Test)
qt_import_qml_plugins(telemetry_inspector_qml_command_check)
qt_add_executable(telemetry_inspector_qml_service_check Service.cpp)
target_compile_features(telemetry_inspector_qml_service_check PRIVATE cxx_std_20)
target_compile_definitions(telemetry_inspector_qml_service_check PRIVATE
    INSPECTOR_QML_SOURCE_FILE="${TELEMETRY_UX_ROOT}/examples/inspector/Main.qml")
target_link_libraries(telemetry_inspector_qml_service_check PRIVATE inspector_qml_support
    Qt6::Quick Qt6::QuickControls2 Qt6::Test)
qt_import_qml_plugins(telemetry_inspector_qml_service_check)
qt_add_executable(telemetry_inspector_qml_watchlist_check Watchlist.cpp)
target_compile_features(telemetry_inspector_qml_watchlist_check PRIVATE cxx_std_20)
target_compile_definitions(telemetry_inspector_qml_watchlist_check PRIVATE
    INSPECTOR_QML_SOURCE_FILE="${TELEMETRY_UX_ROOT}/examples/inspector/Main.qml")
target_link_libraries(telemetry_inspector_qml_watchlist_check PRIVATE inspector_qml_support
    Qt6::Quick Qt6::QuickControls2 Qt6::Test)
qt_import_qml_plugins(telemetry_inspector_qml_watchlist_check)
foreach(_case Failures Scheduling)
    string(TOLOWER "${_case}" _name)
    qt_add_executable(telemetry_inspector_qml_watchlist_${_name}_check Watchlist${_case}.cpp)
    target_compile_features(telemetry_inspector_qml_watchlist_${_name}_check PRIVATE cxx_std_20)
    target_compile_definitions(telemetry_inspector_qml_watchlist_${_name}_check PRIVATE
        INSPECTOR_QML_SOURCE_FILE="${TELEMETRY_UX_ROOT}/examples/inspector/Main.qml")
    target_link_libraries(telemetry_inspector_qml_watchlist_${_name}_check PRIVATE inspector_qml_support
        Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    qt_import_qml_plugins(telemetry_inspector_qml_watchlist_${_name}_check)
endforeach()

# Different host chrome embeds the same explicit-provider view, without a context singleton.
qt_add_executable(telemetry_configurator_embedding_check Embedded.cpp)
qt_add_qml_module(telemetry_configurator_embedding_check
    URI Telemetry.EmbeddingHost VERSION 1.0 RESOURCE_PREFIX /qt/qml QML_FILES Embedded.qml)
target_compile_features(telemetry_configurator_embedding_check PRIVATE cxx_std_20)
target_link_libraries(telemetry_configurator_embedding_check PRIVATE inspector_qml_support
    Qt6::Quick Qt6::QuickControls2 Qt6::Test)
qt_import_qml_plugins(telemetry_configurator_embedding_check)
