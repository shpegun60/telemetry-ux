/*
 * @file PathGrammar.hpp
 * @brief One path for an endpoint and the values inside it, the same in every panel.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The grammar of the designer's review of 20 November 2026 (14.3, 15.1):
 * groups and the endpoint join with "/", members with ".", elements as
 * "[i]" - "metrology/harmonics/phaseA/Spectrum.Window", "data/Samples[3]".
 * The "/" part is what device code reads (`model.read<"adc/Config">()`), so a
 * copied path pastes into it. A group or endpoint name of a flat (v3.0)
 * descriptor may hold any of "/", ".", "[", "]" or "\"; in a path such a
 * character is escaped with "\" ("power\.meter/Voltage"). Member names are
 * identifiers and never need it.
 *
 * Before this grammar a path read "device.Config.Gain". Such a key is still
 * read: against the device's group names, the longest that it starts with
 * and that a dot follows, then the endpoint, then the members (15.1). Every
 * path this module writes is in the "/" form.
 */
#ifndef TELEMETRY_UX_PATH_GRAMMAR_HPP
#define TELEMETRY_UX_PATH_GRAMMAR_HPP
#pragma once

#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QStringView>

#include <optional>

namespace telemetry::ux::path_grammar {

// The characters a name escapes in a path.
[[nodiscard]] bool special(QChar c) noexcept;
// A name as one segment of a path: "power.meter" -> "power\.meter".
[[nodiscard]] QString escaped(QStringView name);
// A segment's name: every "\x" read as "x".
[[nodiscard]] QString unescaped(QStringView segment);
// The groups' segments joined: {"metrology", "harmonics"} -> "metrology/harmonics".
[[nodiscard]] QString groupPath(const QStringList& groups);
// An endpoint's path: its group's path (already escaped) and its own name.
// A flat descriptor's group is one name: groupPath({name}).
[[nodiscard]] QString endpoint(const QString& groupPath, QStringView name);

// A path's parts. group: the group as a flat descriptor names it - the group
// part unescaped, so "power\.meter" is "power.meter" and a nested
// "metrology/harmonics" keeps its "/"; segments: the group part's segments,
// each unescaped (part B's nested groups); name: the endpoint's name,
// unescaped; member: what follows it as written, with its leading "." or "["
// (".Gain", "[3]", ".Calib.Phase[2]"), empty for a whole endpoint.
struct Parts {
	QString group;
	QStringList segments;
	QString name;
	QString member;
};
// The index of every unescaped separator of this kind; backslashes skip the
// character after them.
[[nodiscard]] qsizetype lastSeparator(QStringView path, QChar separator);
// Whether a typed text is in the "/" form: an unescaped "/" before any
// unescaped "." or "[".
[[nodiscard]] bool slashed(QStringView path);
// A "/" path: the group's segments before the last "/" of the group part,
// then the name up to its first unescaped "." or "[". nullopt for a text
// with no "/" or with an empty segment.
[[nodiscard]] std::optional<Parts> parse(QStringView path);
// A key of the dotted form against the device's group names (each as the
// descriptor names it, unescaped): the longest one the text starts with and
// that a "." follows, else the text up to its first ".". nullopt for a
// text that is not a path.
[[nodiscard]] std::optional<Parts> parseDotted(QStringView text, const QSet<QString>& groups);
// Either form: a "/" path whose group the device has, else the dotted key
// read against the groups, else the "/" path as it parsed. With no groups
// given, a "/" path as it parses.
[[nodiscard]] std::optional<Parts> parseAny(QStringView text, const QSet<QString>& groups);
// The group as a flat descriptor names it (Parts::group).
[[nodiscard]] QString groupName(const Parts& parts);
// A flat group's endpoint and member in the "/" form: the group's name
// escaped as one segment, the endpoint's name escaped, the member as given
// (with its leading "." or "[").
[[nodiscard]] QString compose(const QString& group, const QString& name, const QString& member);
// Any typed path in the "/" form when the device has its group: a dotted
// key is rewritten, a "/" path and anything else are kept as they are.
[[nodiscard]] QString canonical(QStringView text, const QSet<QString>& groups);
// A typed beginning of a path in the "/" form: one with no "/" that starts
// with a group the device has and a "." ("device.Con") becomes "device/Con";
// anything else is kept. For completion and filters on what is typed so far.
[[nodiscard]] QString canonicalPrefix(QStringView text, const QSet<QString>& groups);
// Where a path's last step starts: after its last unescaped "/" or ".", or
// at its last "[" - "device/Config.Gain" -> 14, "data/Samples[3]" -> 12.
[[nodiscard]] qsizetype leafStart(QStringView path);

} // namespace telemetry::ux::path_grammar
#endif
