# One provider-neutral renderer, linked directly without a runtime plugin. MIT.
# The older resource prefix is a default import path on Qt 6.4 as well as newer Qt.
# Theme (colour roles) and Type (the type scale) are singletons a host may set.
set_source_files_properties(Theme.qml Type.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(telemetry_ux_configurator STATIC NO_PLUGIN NO_CACHEGEN
    URI Telemetry.UX VERSION 1.0 RESOURCE_PREFIX /qt-project.org/imports
    DEPENDENCIES QtQuick
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    QML_FILES Configurator.qml FieldEditor.qml CommandPanel.qml ServicePanel.qml
              EndpointPanel.qml EndpointTreePanel.qml WatchlistPanel.qml BatchPanel.qml TrendPanel.qml PlainToolTip.qml
              Theme.qml Type.qml ProgressLine.qml LinkButton.qml DeviceFileRow.qml DeviceFilesPanel.qml FileViewer.qml
              HexPane.qml InspectorTable.qml DeviceFileMenu.qml DeviceFileProperties.qml DescriptorView.qml DescriptorRow.qml
              ValuesView.qml ValuesRow.qml FindBar.qml TextPane.qml TablePane.qml
              OUTPUT_TARGETS _telemetry_ux_qml_objects)
add_library(TelemetryUX::configurator ALIAS telemetry_ux_configurator)
set_target_properties(telemetry_ux_configurator PROPERTIES EXPORT_NAME configurator)
target_sources(telemetry_ux_configurator PRIVATE PresentationOverlay.cpp PresentationOverlay.hpp TrendHistory.cpp TrendHistory.hpp WatchSample.hpp
    FileContent.cpp FileContent.hpp FileTextModels.cpp FileTextModels.hpp HexView.cpp HexView.hpp QmlTypes.cpp QmlTypes.hpp
    Clipboard.cpp Clipboard.hpp)
target_compile_features(telemetry_ux_configurator PUBLIC cxx_std_17)
target_include_directories(telemetry_ux_configurator PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(telemetry_ux_configurator PUBLIC Qt6::Quick Qt6::QuickControls2)

# Generated bindings borrow the host's typed API; this shared Qt-only provider
# creates no transport, SDK client or Descriptor parser. It is linked explicitly.
add_library(telemetry_ux_static_configurator STATIC
    StaticConfiguratorProvider.cpp StaticConfiguratorProvider.hpp
    StaticConfiguratorBinding.hpp)
add_library(TelemetryUX::static_configurator ALIAS telemetry_ux_static_configurator)
set_target_properties(telemetry_ux_static_configurator PROPERTIES
    EXPORT_NAME static_configurator AUTOMOC ON)
target_compile_features(telemetry_ux_static_configurator PUBLIC cxx_std_17)
target_include_directories(telemetry_ux_static_configurator PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(telemetry_ux_static_configurator PUBLIC TelemetryUX::configurator)

# Qt supplies resource object targets through the backing target's link interface.
# Install their objects as well as the archive so a relocated consumer needs no
# source tree, plugin, engine import-path setup or QML import-scanner workaround.
install(TARGETS telemetry_ux_configurator telemetry_ux_static_configurator ${_telemetry_ux_qml_objects}
    EXPORT TelemetryUXTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    OBJECTS DESTINATION "${CMAKE_INSTALL_LIBDIR}/telemetry-ux/objects")
install(FILES Configurator.qml FieldEditor.qml CommandPanel.qml ServicePanel.qml
    EndpointPanel.qml EndpointTreePanel.qml WatchlistPanel.qml BatchPanel.qml TrendPanel.qml PlainToolTip.qml
    Theme.qml Type.qml ProgressLine.qml LinkButton.qml DeviceFileRow.qml DeviceFilesPanel.qml FileViewer.qml
    HexPane.qml InspectorTable.qml DeviceFileMenu.qml DeviceFileProperties.qml DescriptorView.qml DescriptorRow.qml
    ValuesView.qml ValuesRow.qml FindBar.qml TextPane.qml TablePane.qml
    "${CMAKE_CURRENT_BINARY_DIR}/qmldir"
    "${CMAKE_CURRENT_BINARY_DIR}/telemetry_ux_configurator.qmltypes"
    DESTINATION "${TELEMETRY_UX_QML_INSTALL_DIR}/Telemetry/UX")
install(FILES StaticConfiguratorBinding.hpp StaticConfiguratorProvider.hpp PresentationOverlay.hpp TrendHistory.hpp FileContent.hpp FileTextModels.hpp HexView.hpp QmlTypes.hpp Clipboard.hpp
    WatchSample.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/Telemetry/UX")

# Runtime providers are a separate optional export. Pure Qt/static consumers
# must not start discovering the native SDK or transport components.
if(TELEMETRY_UX_BUILD_DYNAMIC_CONFIGURATOR)
    if(NOT TARGET Telemetry::workbench)
        find_package(Telemetry 0.1.0 CONFIG REQUIRED COMPONENTS workbench)
    endif()
    add_library(telemetry_ux_dynamic_configurator STATIC
        DynamicModels.cpp DynamicModels.hpp DynamicConfiguratorProvider.cpp
        DynamicConfiguratorProvider.hpp DynamicConfiguratorBinding.hpp
        WatchlistModel.cpp WatchlistModel.hpp DynamicWatchlist.cpp DynamicWatchlist.hpp
        DynamicBatch.cpp DynamicBatch.hpp)
    add_library(TelemetryUX::dynamic_configurator ALIAS telemetry_ux_dynamic_configurator)
    set_target_properties(telemetry_ux_dynamic_configurator PROPERTIES
        EXPORT_NAME dynamic_configurator AUTOMOC ON)
    target_compile_features(telemetry_ux_dynamic_configurator PUBLIC cxx_std_20)
    target_include_directories(telemetry_ux_dynamic_configurator PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    target_link_libraries(telemetry_ux_dynamic_configurator PUBLIC
        TelemetryUX::configurator Telemetry::workbench)
    install(TARGETS telemetry_ux_dynamic_configurator EXPORT TelemetryUXDynamicTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(EXPORT TelemetryUXDynamicTargets NAMESPACE TelemetryUX::
        DESTINATION "${_telemetry_ux_package_dir}")
    install(FILES DynamicModels.hpp DynamicConfiguratorProvider.hpp DynamicConfiguratorBinding.hpp ScalarEditor.hpp
        WatchlistModel.hpp DynamicWatchlist.hpp DynamicBatch.hpp
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/Telemetry/UX")
endif()

if(TELEMETRY_UX_BUILD_QT_CONNECTION)
    if(NOT TARGET Telemetry::qt_connection)
        find_package(Telemetry 0.1.0 CONFIG REQUIRED COMPONENTS qt_connection)
    endif()
    add_library(telemetry_ux_qt_connection STATIC QtConnectionBinding.cpp QtConnectionBinding.hpp
        DeviceFilesModel.cpp DeviceFilesModel.hpp DeviceFileTransfers.cpp DeviceFileTransfers.hpp
        DescriptorContent.cpp DescriptorContent.hpp TelemetryCodecs.cpp TelemetryCodecs.hpp
        TreeFilter.cpp TreeFilter.hpp SchemaText.cpp SchemaText.hpp ValuesContent.cpp ValuesContent.hpp
        DeviceFileLive.cpp DeviceFileLive.hpp)
    add_library(TelemetryUX::qt_connection ALIAS telemetry_ux_qt_connection)
    set_target_properties(telemetry_ux_qt_connection PROPERTIES EXPORT_NAME qt_connection AUTOMOC ON)
    target_compile_features(telemetry_ux_qt_connection PUBLIC cxx_std_20)
    target_include_directories(telemetry_ux_qt_connection PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    target_link_libraries(telemetry_ux_qt_connection PUBLIC
        TelemetryUX::dynamic_configurator Telemetry::qt_connection)
    install(TARGETS telemetry_ux_qt_connection EXPORT TelemetryUXConnectionTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(EXPORT TelemetryUXConnectionTargets NAMESPACE TelemetryUX::
        DESTINATION "${_telemetry_ux_package_dir}")
    install(FILES QtConnectionBinding.hpp DeviceFilesModel.hpp DeviceFileTransfers.hpp DescriptorContent.hpp
        TelemetryCodecs.hpp TreeFilter.hpp ValuesContent.hpp DeviceFileLive.hpp
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/Telemetry/UX")
endif()
