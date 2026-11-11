# Designer review driver for examples/qt_docking (2026-11-11). Not part of any build.
cmake_minimum_required(VERSION 3.21)
project(TelemetryDockingReviewDriver LANGUAGES CXX)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Test)
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS qt_connection)
find_package(TelemetryUX 0.1.0 EXACT CONFIG REQUIRED COMPONENTS qt_connection)
qt_standard_project_setup(REQUIRES 6.4)

# Use the example's own QML unchanged, under the same resource paths.
set(example "${CMAKE_CURRENT_SOURCE_DIR}/../../../../examples/qt_docking")
foreach(file Main.qml TrendContent.qml TelemetryDark/qmldir TelemetryDark/TabButton.qml)
    set_source_files_properties("${example}/${file}" PROPERTIES QT_RESOURCE_ALIAS "${file}")
    list(APPEND qml_files "${example}/${file}")
endforeach()

qt_add_executable(docking-review-driver review_driver.cpp)
qt_add_resources(docking-review-driver "entry" PREFIX "/" FILES ${qml_files})
target_compile_features(docking-review-driver PRIVATE cxx_std_20)
target_link_libraries(docking-review-driver PRIVATE TelemetryUX::qt_connection
    Qt6::Quick Qt6::QuickControls2 Qt6::Test)
qt_import_qml_plugins(docking-review-driver)
