# Optional standalone consumer example; KDDockWidgets is a separate QML runtime.
# It is not built by the default library build, CI or packaging: KDDockWidgets
# is GPL-2.0-only/GPL-3.0-only or commercial (see README.md).
cmake_minimum_required(VERSION 3.21)
project(TelemetryQtDocking LANGUAGES CXX)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network)
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS qt_connection)
find_package(TelemetryUX 0.1.0 EXACT CONFIG REQUIRED COMPONENTS qt_connection)
qt_standard_project_setup(REQUIRES 6.4)

set(KDDOCKWIDGETS_QML_IMPORT_DIR "" CACHE PATH
    "Directory containing com/kdab/dockwidgets/qmldir of KDDockWidgets 2.5 (Qt Quick frontend)")
set(KDDOCKWIDGETS_BIN_DIR "" CACHE PATH
    "Directory of the KDDockWidgets shared library, put first on PATH at start (Windows)")

qt_add_executable(telemetry-qt-docking main.cpp)
qt_add_resources(telemetry-qt-docking "entry" PREFIX "/" FILES
    Main.qml TrendContent.qml TelemetryDark/qmldir TelemetryDark/TabButton.qml)
target_compile_features(telemetry-qt-docking PRIVATE cxx_std_20)
target_link_libraries(telemetry-qt-docking PRIVATE TelemetryUX::qt_connection
    Qt6::Quick Qt6::QuickControls2)
qt_import_qml_plugins(telemetry-qt-docking)

if(KDDOCKWIDGETS_QML_IMPORT_DIR)
    if(NOT EXISTS "${KDDOCKWIDGETS_QML_IMPORT_DIR}/com/kdab/dockwidgets/qmldir")
        message(FATAL_ERROR
            "KDDOCKWIDGETS_QML_IMPORT_DIR (${KDDOCKWIDGETS_QML_IMPORT_DIR}) has no "
            "com/kdab/dockwidgets/qmldir. Build KDDockWidgets 2.5 for this Qt with "
            "KDDockWidgets_FRONTENDS=qtquick and KDDockWidgets_QML_MODULE=ON; see README.md.")
    endif()
    target_compile_definitions(telemetry-qt-docking PRIVATE
        TELEMETRY_KDDW_QML_IMPORT_DIR="${KDDOCKWIDGETS_QML_IMPORT_DIR}")
else()
    message(WARNING
        "KDDOCKWIDGETS_QML_IMPORT_DIR is not set. The example builds, but at run time it "
        "needs the KDDockWidgets QML module on QML_IMPORT_PATH; see README.md.")
endif()
if(KDDOCKWIDGETS_BIN_DIR)
    target_compile_definitions(telemetry-qt-docking PRIVATE
        TELEMETRY_KDDW_BIN_DIR="${KDDOCKWIDGETS_BIN_DIR}")
endif()
