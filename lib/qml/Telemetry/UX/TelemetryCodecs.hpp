/*
 * @file TelemetryCodecs.hpp
 * @brief The codecs of the Telemetry formats, for a host's Viewer.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The Viewer (OpenFiles, FileViewer) is Qt only; the formats Telemetry itself
 * defines need the SDK's parsers, so their codecs are installed from here:
 * - Descriptor: /telemetry/descriptor.bin, "TDS3", shown by DescriptorView.qml
 *   as its fingerprint, its counts and every endpoint as a tree, with the
 *   header's fields and sections named in the hex view.
 * - Values: /telemetry/values.bin, "TVL3", shown by ValuesView.qml as every
 *   Field with its value, read against the descriptor the file names: the one
 *   beside it, the connected device's, or a downloaded one with the same
 *   fingerprint; without any, the view asks for it. Live re-reads the device's
 *   file into it.
 */
#ifndef TELEMETRY_UX_TELEMETRY_CODECS_HPP
#define TELEMETRY_UX_TELEMETRY_CODECS_HPP
#pragma once

#include <QtCore/QStringList>
#include <functional>

namespace telemetry::integration::qt {
class ConnectionSession;
}

namespace telemetry::ux {
class DeviceFilesModel;
class OpenFiles;
// Registers the Telemetry formats' codecs - descriptor, values, the read-many
// and batch capability records - and JSON's, after any the host added before.
void installTelemetryCodecs(OpenFiles& files);
// Lets the viewer reach a device: a values.bin is read against the connected
// device's descriptor, or a descriptor.bin with the same fingerprint
// downloaded into a device's folder under one of devicesRoots(); a device's
// file can be read live through session. A file that waited for its
// descriptor is read again when a device is admitted. deviceFiles names a
// file a codec takes by its path after that codec (setCodecLookup).
void connectTelemetryDevice(OpenFiles& files, integration::qt::ConnectionSession* session,
                            DeviceFilesModel* deviceFiles, std::function<QStringList()> devicesRoots);
} // namespace telemetry::ux
#endif
