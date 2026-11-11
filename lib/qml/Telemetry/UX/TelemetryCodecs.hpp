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
 */
#ifndef TELEMETRY_UX_TELEMETRY_CODECS_HPP
#define TELEMETRY_UX_TELEMETRY_CODECS_HPP
#pragma once

namespace telemetry::ux {
class OpenFiles;
// Registers the Telemetry formats' codecs, after any the host added before.
void installTelemetryCodecs(OpenFiles& files);
} // namespace telemetry::ux
#endif
