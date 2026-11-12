/*
 * @file DeviceBytes.hpp
 * @brief The device's bytes as read, kept beside the local copies of its files.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * A download writes the device's file exactly; the copy may then be edited and
 * saved. To show what differs from the device and to put the device's bytes
 * back - after a restart, with no connection - the bytes as read are kept in
 * the device's own folder, under `.device-bytes`, at the copy's path within
 * that folder: `<device folder>/.device-bytes/data/capture.bin` with
 * `capture.bin.json` beside it, which says where they came from - the
 * device's path, the size, when they were read, a SHA-256 of the bytes,
 * checked whenever they are read back. Everything about a device stays in its
 * folder, on this computer, and moves with it (the owner, 12 November 2026:
 * kept locally until there is a project to keep it in).
 *
 * Nothing here reads a device: DeviceFileTransfers keeps a record when a file
 * read for the Viewer is written, and when a copy written to the device is
 * read back; a host gives OpenFiles the lookup.
 */
#ifndef TELEMETRY_UX_DEVICE_BYTES_HPP
#define TELEMETRY_UX_DEVICE_BYTES_HPP
#pragma once

#include "FileContent.hpp"
#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <optional>

namespace telemetry::ux {

class DeviceBytes final {
public:
	struct Record {
		QString bytesFile;
		QString devicePath;
		qint64 size = 0;
		QDateTime readAt;
		QByteArray sha256;
		// Read back after this copy was written to the device, not read for
		// the Viewer.
		bool written = false;
	};

	// The folder that holds the records: `.device-bytes` in a device's folder.
	static QString folderName();
	// Keeps localFile - just written from the device's devicePath, inside
	// deviceFolder - as the device's bytes for that copy, replacing an earlier
	// record. Empty when kept; otherwise why not (a copy outside the folder
	// is not kept). On Windows the `.device-bytes` folder is hidden.
	static QString keep(const QString& deviceFolder, const QString& localFile, const QString& devicePath,
	                    const QDateTime& readAt);
	// The same from bytes in memory: the device's file as just read back after
	// a write (written), which may not be the copy's bytes. keep() reads the
	// copy and comes here.
	static QString keepBytes(const QString& deviceFolder, const QString& localFile,
	                         const QString& devicePath, const QByteArray& bytes, const QDateTime& readAt,
	                         bool written = false);
	// Whether bytes - the device's devicePath read now - are what localFile's
	// record holds: false without a record, or with one of another path.
	static bool matches(const QString& localFile, const QString& devicePath, const QByteArray& bytes);
	// Whether localFile is inside deviceFolder, where its record can be kept.
	static bool inside(const QString& deviceFolder, const QString& localFile);
	// The record for localFile: looked for in the `.device-bytes` of each
	// folder above it, nearest first; only when its bytes are there at the
	// size it gives. The bytes are not read or checked here.
	static std::optional<Record> find(const QString& localFile);
	// What FileContent compares a copy against: the record's bytes, read and
	// checked against their hash, named for what they are ("the device's bytes
	// as read at …"); none without a record, when the check fails, or when the
	// record is another device path's (two names a copy's name can come from).
	static std::optional<FileContent::Reference> reference(const QString& localFile,
	                                                       const QString& devicePath = {});
	// Removes the record of localFile, if it has one: its copy was written
	// again without one being kept, so the old one would no longer be true.
	static void forget(const QString& localFile);
	// reference(), as OpenFiles takes it.
	static FileContent::ReferenceLookup lookup();
};

} // namespace telemetry::ux
#endif
