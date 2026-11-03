# Pure Qt view packaging; no Telemetry SDK or transport discovery. MIT.
if(TARGET TelemetryUX::configurator)
    return()
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.4)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" TELEMETRY_UX_VERSION LIMIT_COUNT 1)
set(TELEMETRY_UX_QML_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/qml")
set(_telemetry_ux_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/TelemetryUX")

add_subdirectory(Telemetry/UX)
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/TelemetryUXConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/TelemetryUXConfig.cmake"
    INSTALL_DESTINATION "${_telemetry_ux_package_dir}"
    PATH_VARS TELEMETRY_UX_QML_INSTALL_DIR)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/TelemetryUXConfigVersion.cmake"
    VERSION "${TELEMETRY_UX_VERSION}" COMPATIBILITY ExactVersion)
install(EXPORT TelemetryUXTargets NAMESPACE TelemetryUX::
    DESTINATION "${_telemetry_ux_package_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/TelemetryUXConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/TelemetryUXConfigVersion.cmake"
    DESTINATION "${_telemetry_ux_package_dir}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/telemetry-ux")
