# One provider-neutral renderer, linked directly without a runtime plugin. MIT.
# The older resource prefix is a default import path on Qt 6.4 as well as newer Qt.
qt_add_qml_module(telemetry_ux_configurator STATIC NO_PLUGIN NO_CACHEGEN
    URI Telemetry.UX VERSION 1.0 RESOURCE_PREFIX /qt-project.org/imports
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    QML_FILES Configurator.qml OUTPUT_TARGETS _telemetry_ux_qml_objects)
add_library(TelemetryUX::configurator ALIAS telemetry_ux_configurator)
set_target_properties(telemetry_ux_configurator PROPERTIES EXPORT_NAME configurator)
target_compile_features(telemetry_ux_configurator PUBLIC cxx_std_17)
target_link_libraries(telemetry_ux_configurator PUBLIC Qt6::Quick Qt6::QuickControls2)

# Qt supplies resource object targets through the backing target's link interface.
# Install their objects as well as the archive so a relocated consumer needs no
# source tree, plugin, engine import-path setup or QML import-scanner workaround.
install(TARGETS telemetry_ux_configurator ${_telemetry_ux_qml_objects}
    EXPORT TelemetryUXTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    OBJECTS DESTINATION "${CMAKE_INSTALL_LIBDIR}/telemetry-ux/objects")
install(FILES Configurator.qml
    "${CMAKE_CURRENT_BINARY_DIR}/qmldir"
    "${CMAKE_CURRENT_BINARY_DIR}/telemetry_ux_configurator.qmltypes"
    DESTINATION "${TELEMETRY_UX_QML_INSTALL_DIR}/Telemetry/UX")
