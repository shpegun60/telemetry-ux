/*
 * @file EndpointPaths.hpp
 * @brief Finding a device's endpoints by a typed name or path (BA-1).
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * A typed line names one endpoint of the device's tree: by its whole path
 * ("device.Voltage"), by a value inside a Field ("device.Config.Gain" - the
 * Batch reads the whole Field), or by its name alone ("Voltage"). A name is
 * matched word by word, never by scattered letters: "con gain" finds
 * device.Config.Gain, because a wrong pick here can be a write or a Command.
 */
#ifndef TELEMETRY_UX_ENDPOINT_PATHS_HPP
#define TELEMETRY_UX_ENDPOINT_PATHS_HPP
#pragma once

#include "DynamicModels.hpp"

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>

namespace telemetry::ux::endpoint_paths {

// The words of a path: its segments, split at '.', '[' and ']', and inside
// them at '_', at a lower-to-upper case change and at digits; case-folded.
[[nodiscard]] QStringList words(const QString& path);

// How well terms (space-separated, already split) name path: 0 one term is the
// last segment, 1 the last segment starts with the last term, 2 every term
// starts a word somewhere; -1 not at all. Case does not matter.
[[nodiscard]] int nameRank(const QString& path, const QStringList& terms);

// A typed line, as a Batch reads it.
struct Pick {
	// adds, choose (several match), input (takes input), missing, error, empty
	QString status;
	QString message;
	QVector<EndpointEntry> matches;
	// The line named a value inside matches.first(), a Field read whole.
	bool member = false;
	QString path;  // the line's path, without a leading kind word
};

// category 0 when the line does not say; "read", "command" or "service" first
// chooses one. A "path = value" line is refused: writing from here is later.
[[nodiscard]] Pick resolve(const QString& line, const QVector<EndpointEntry>& list);

// The kind word for a category: "Field", "Command", "Service".
[[nodiscard]] QString kindWord(int category);

} // namespace telemetry::ux::endpoint_paths
#endif
