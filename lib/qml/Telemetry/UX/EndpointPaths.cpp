// Finding a device's endpoints by a typed name or path (BA-1). MIT.
#include "EndpointPaths.hpp"
#include "PathGrammar.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>

namespace telemetry::ux::endpoint_paths {
namespace {
QString tr(const char* text)
{
	return QCoreApplication::translate("EndpointPaths", text);
}

// A typed line names a path, not a name: it has a separator of the grammar.
bool pathLike(const QString& text)
{
	return text.contains(QLatin1Char('.')) || text.contains(QLatin1Char('[')) || text.contains(QLatin1Char('/'));
}

// The device's group names, as its entries name them.
QSet<QString> groupsOf(const QVector<EndpointEntry>& list)
{
	QSet<QString> groups;
	for (const auto& entry : list)
		if (!entry.group.isEmpty())
			groups.insert(entry.group);
	return groups;
}

bool isWordStart(QChar previous, QChar at)
{
	return (previous.isLower() && at.isUpper()) || (previous.isLetter() && at.isDigit()) ||
	       (previous.isDigit() && at.isLetter());
}
} // namespace

QStringList words(const QString& path)
{
	QStringList out;
	QString word;
	const auto flush = [&] {
		if (!word.isEmpty())
			out.append(word.toCaseFolded());
		word.clear();
	};
	QChar previous;
	for (const QChar at : path) {
		if (at == QLatin1Char('.') || at == QLatin1Char('[') || at == QLatin1Char(']') || at == QLatin1Char('/') ||
		    at == QLatin1Char('\\') || at == QLatin1Char('_') || at.isSpace()) {
			flush();
			previous = QChar{};
			continue;
		}
		if (!word.isEmpty() && isWordStart(previous, at))
			flush();
		word.append(at);
		previous = at;
	}
	flush();
	return out;
}

int nameRank(const QString& path, const QStringList& terms)
{
	const auto segments = path.split(QRegularExpression(QStringLiteral("[./]")), Qt::SkipEmptyParts);
	if (segments.isEmpty() || terms.isEmpty())
		return -1;
	const auto last = segments.last().toCaseFolded();
	if (terms.size() == 1 && last == terms.first().toCaseFolded())
		return 0;
	QStringList starts;
	for (const auto& segment : segments)
		starts.append(segment.toCaseFolded());
	starts += words(path);
	for (const auto& term : terms) {
		const auto folded = term.toCaseFolded();
		bool found = false;
		for (const auto& start : std::as_const(starts))
			if (start.startsWith(folded)) {
				found = true;
				break;
			}
		if (!found)
			return -1;
	}
	return last.startsWith(terms.last().toCaseFolded()) ? 1 : 2;
}

int kindCategory(const QString& word)
{
	const auto folded = word.toCaseFolded();
	if (folded == QLatin1String("read") || folded == QLatin1String("field") || folded == QLatin1String("fields"))
		return 1;
	if (folded == QLatin1String("command") || folded == QLatin1String("commands"))
		return 2;
	if (folded == QLatin1String("service") || folded == QLatin1String("services"))
		return 3;
	return 0;
}

QString kindWord(int category)
{
	switch (category) {
		case 1: return tr("Field");
		case 2: return tr("Command");
		case 3: return tr("Service");
	}
	return {};
}

Pick resolve(const QString& line, const QVector<EndpointEntry>& list)
{
	Pick pick;
	auto text = line.trimmed();
	if (text.isEmpty()) {
		pick.status = QStringLiteral("empty");
		return pick;
	}
	// A leading kind word chooses among endpoints that share a path.
	int category = 0;
	const auto space = text.indexOf(QLatin1Char(' '));
	if (space > 0) {
		category = kindCategory(text.left(space));
		if (category)
			text = text.mid(space + 1).trimmed();
	}
	// Words after a Command's or a Service's path or name: the first word
	// names it when it names a Command or a Service and nothing else; the
	// rest are values, which are set on the item, not here (BA-4b).
	if (const auto gap = text.indexOf(QRegularExpression(QStringLiteral("\\s"))); gap > 0) {
		const auto target = path_grammar::canonical(text.left(gap), groupsOf(list));
		bool callable = false, other = false;
		const bool targetIsPath = pathLike(target);
		for (const auto& entry : list) {
			if (category != 0 && entry.category != category)
				continue;
			const bool named = targetIsPath ? entry.path.compare(target, Qt::CaseInsensitive) == 0
			                                : entry.name.compare(target, Qt::CaseInsensitive) == 0;
			if (named)
				(entry.category == 1 ? other : callable) = true;
		}
		if (callable && !other) {
			pick.tail = text.mid(gap + 1).trimmed();
			pick.hasTail = true;
			text = target;
		}
	}
	pick.path = text;
	if (text.contains(QLatin1Char('='))) {
		pick.status = QStringLiteral("error");
		pick.message = tr("A Batch item found by name reads. To write a value, add the Field, then make it a write on the item.");
		return pick;
	}
	const auto wanted = [category](const EndpointEntry& entry) {
		return category == 0 || entry.category == category;
	};
	const bool isPath = pathLike(text);
	// A key of an older project ("device.Config.Gain") read as the path it
	// names ("device/Config.Gain").
	if (isPath) {
		text = path_grammar::canonical(text, groupsOf(list));
		pick.path = text;
	}
	if (isPath) {
		for (const auto& entry : list)
			if (wanted(entry) && entry.path == text)
				pick.matches.push_back(entry);
		if (pick.matches.isEmpty())
			for (const auto& entry : list)
				if (wanted(entry) && entry.path.compare(text, Qt::CaseInsensitive) == 0)
					pick.matches.push_back(entry);
		if (pick.matches.isEmpty()) {
			// A value inside a Field: the Batch reads the whole Field.
			const EndpointEntry* field = nullptr;
			for (const auto& entry : list)
				if (entry.category == 1 && (category == 0 || category == 1) &&
				    (text.startsWith(entry.path + QLatin1Char('.')) || text.startsWith(entry.path + QLatin1Char('['))) &&
				    (!field || entry.path.size() > field->path.size()))
					field = &entry;
			if (field) {
				pick.matches.push_back(*field);
				pick.member = true;
			}
		}
	} else {
		for (const auto& entry : list)
			if (wanted(entry) && entry.name.compare(text, Qt::CaseInsensitive) == 0)
				pick.matches.push_back(entry);
	}
	if (pick.matches.isEmpty()) {
		pick.status = QStringLiteral("missing");
		pick.message = isPath ? tr("%1 is not on this device.").arg(text)
		                      : tr("Nothing on this device is named %1.").arg(text);
		return pick;
	}
	if (pick.matches.size() > 1) {
		pick.status = QStringLiteral("choose");
		pick.message = tr("%L1 endpoints are named %2. Down to choose.").arg(pick.matches.size()).arg(text);
		return pick;
	}
	const auto& entry = pick.matches.first();
	if (entry.category != 1 && entry.requestBytes == 0 && pick.hasTail && !pick.tail.isEmpty()) {
		pick.status = QStringLiteral("error");
		pick.message = tr("%1 takes no input; remove \"%2\".").arg(entry.path, pick.tail);
		return pick;
	}
	// Values are set on the item, where they start at zero (BA-4b).
	if (entry.category != 1 && pick.hasTail && !pick.tail.isEmpty()) {
		pick.status = QStringLiteral("error");
		pick.message = tr("Set values on the item, not here: remove \"%1\".").arg(pick.tail);
		return pick;
	}
	pick.status = QStringLiteral("adds");
	return pick;
}

} // namespace telemetry::ux::endpoint_paths
