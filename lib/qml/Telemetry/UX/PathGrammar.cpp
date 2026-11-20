/*
 * @file PathGrammar.cpp
 * @brief One path for an endpoint and the values inside it, the same in every panel.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "PathGrammar.hpp"

#include <algorithm>

namespace telemetry::ux::path_grammar {

bool special(QChar c) noexcept
{
	return c == QLatin1Char('/') || c == QLatin1Char('.') || c == QLatin1Char('[') || c == QLatin1Char(']') ||
	       c == QLatin1Char('\\');
}

QString escaped(QStringView name)
{
	QString out;
	out.reserve(name.size());
	for (const QChar c : name) {
		if (special(c))
			out.append(QLatin1Char('\\'));
		out.append(c);
	}
	return out;
}

QString unescaped(QStringView segment)
{
	QString out;
	out.reserve(segment.size());
	for (qsizetype i = 0; i < segment.size(); ++i) {
		if (segment.at(i) == QLatin1Char('\\') && i + 1 < segment.size())
			++i;
		out.append(segment.at(i));
	}
	return out;
}

QString groupPath(const QStringList& groups)
{
	QStringList out;
	out.reserve(groups.size());
	for (const auto& group : groups)
		out.append(escaped(group));
	return out.join(QLatin1Char('/'));
}

QString endpoint(const QString& groupPath, QStringView name)
{
	return groupPath.isEmpty() ? escaped(name) : groupPath + QLatin1Char('/') + escaped(name);
}

qsizetype lastSeparator(QStringView path, QChar separator)
{
	qsizetype found = -1;
	for (qsizetype i = 0; i < path.size(); ++i) {
		if (path.at(i) == QLatin1Char('\\')) {
			++i;
			continue;
		}
		if (path.at(i) == separator)
			found = i;
	}
	return found;
}

bool slashed(QStringView path)
{
	return lastSeparator(path, QLatin1Char('/')) >= 0;
}

namespace {
// The endpoint's name and its member part from the text after the group:
// the name up to its first unescaped "." or "[".
bool split(QStringView rest, Parts& parts)
{
	qsizetype end = 0;
	while (end < rest.size()) {
		const auto c = rest.at(end);
		if (c == QLatin1Char('\\')) {
			end += 2;
			continue;
		}
		if (c == QLatin1Char('.') || c == QLatin1Char('['))
			break;
		++end;
	}
	end = std::min(end, rest.size());
	parts.name = unescaped(rest.left(end));
	parts.member = rest.mid(end).toString();
	if (parts.name.isEmpty())
		return false;
	// "Config." is no member yet; "Config[" neither.
	return parts.member != QLatin1String(".") && parts.member != QLatin1String("[");
}
} // namespace

std::optional<Parts> parse(QStringView path)
{
	const auto slash = lastSeparator(path, QLatin1Char('/'));
	if (slash <= 0)
		return std::nullopt;
	Parts parts;
	// The group's segments, each unescaped; none may be empty.
	qsizetype start = 0;
	for (qsizetype i = 0; i <= slash; ++i) {
		if (i < slash && path.at(i) == QLatin1Char('\\')) {
			++i;
			continue;
		}
		if (i == slash || path.at(i) == QLatin1Char('/')) {
			if (i == start)
				return std::nullopt;
			parts.segments.append(unescaped(path.mid(start, i - start)));
			start = i + 1;
		}
	}
	parts.group = unescaped(path.left(slash));
	if (!split(path.mid(slash + 1), parts))
		return std::nullopt;
	return parts;
}

std::optional<Parts> parseDotted(QStringView text, const QSet<QString>& groups)
{
	qsizetype dot = -1;
	for (const auto& group : groups)
		if (group.size() > dot && text.size() > group.size() && text.startsWith(group) &&
		    text.at(group.size()) == QLatin1Char('.'))
			dot = group.size();
	if (dot < 0)
		dot = text.indexOf(QLatin1Char('.'));
	if (dot <= 0 || dot + 1 >= text.size())
		return std::nullopt;
	Parts parts;
	parts.group = text.left(dot).toString();
	parts.segments = {parts.group};
	const auto rest = text.mid(dot + 1);
	qsizetype end = 0;
	while (end < rest.size() && rest.at(end) != QLatin1Char('.') && rest.at(end) != QLatin1Char('['))
		++end;
	parts.name = rest.left(end).toString();
	parts.member = rest.mid(end).toString();
	if (parts.name.isEmpty() || parts.member == QLatin1String("."))
		return std::nullopt;
	return parts;
}

std::optional<Parts> parseAny(QStringView text, const QSet<QString>& groups)
{
	if (slashed(text)) {
		const auto parts = parse(text);
		if (parts && (groups.isEmpty() || groups.contains(parts->group)))
			return parts;
		// A dotted key whose group holds a "/" ("adc/channel1.Gain").
		const auto dotted = parseDotted(text, groups);
		if (dotted && groups.contains(dotted->group))
			return dotted;
		return parts;
	}
	return parseDotted(text, groups);
}

QString groupName(const Parts& parts)
{
	return parts.group;
}

QString compose(const QString& group, const QString& name, const QString& member)
{
	return endpoint(escaped(group), name) + member;
}

QString canonical(QStringView text, const QSet<QString>& groups)
{
	if (slashed(text))
		return text.toString();
	const auto parts = parseDotted(text, groups);
	if (!parts || !groups.contains(parts->group))
		return text.toString();
	return compose(parts->group, parts->name, parts->member);
}

QString canonicalPrefix(QStringView text, const QSet<QString>& groups)
{
	if (slashed(text))
		return text.toString();
	qsizetype end = -1;
	QString found;
	for (const auto& group : groups)
		if (group.size() > end && text.size() > group.size() && text.startsWith(group) &&
		    text.at(group.size()) == QLatin1Char('.')) {
			end = group.size();
			found = group;
		}
	if (end < 0)
		return text.toString();
	return escaped(found) + QLatin1Char('/') + text.mid(end + 1);
}

qsizetype leafStart(QStringView path)
{
	qsizetype start = 0;
	for (qsizetype i = 0; i < path.size(); ++i) {
		const auto c = path.at(i);
		if (c == QLatin1Char('\\')) {
			++i;
			continue;
		}
		if (c == QLatin1Char('/') || c == QLatin1Char('.'))
			start = i + 1;
		else if (c == QLatin1Char('['))
			start = i;
	}
	return start;
}

} // namespace telemetry::ux::path_grammar
