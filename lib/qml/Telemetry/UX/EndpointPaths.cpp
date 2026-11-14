// Finding a device's endpoints by a typed name or path (BA-1). MIT.
#include "EndpointPaths.hpp"

#include <QtCore/QCoreApplication>

namespace telemetry::ux::endpoint_paths {
namespace {
QString tr(const char* text)
{
	return QCoreApplication::translate("EndpointPaths", text);
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
		if (at == QLatin1Char('.') || at == QLatin1Char('[') || at == QLatin1Char(']') ||
		    at == QLatin1Char('_') || at.isSpace()) {
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
	const auto segments = path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
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
		const auto first = text.left(space).toCaseFolded();
		category = first == QLatin1String("read") ? 1 : first == QLatin1String("command") ? 2
		         : first == QLatin1String("service") ? 3 : 0;
		if (category)
			text = text.mid(space + 1).trimmed();
	}
	pick.path = text;
	if (text.contains(QLatin1Char('='))) {
		pick.status = QStringLiteral("error");
		pick.message = tr("A Batch item found by name reads. To write a value, select the Field, write it in Details, then Add selected draft.");
		return pick;
	}
	const auto wanted = [category](const EndpointEntry& entry) {
		return category == 0 || entry.category == category;
	};
	const bool isPath = text.contains(QLatin1Char('.')) || text.contains(QLatin1Char('['));
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
	if (entry.category == 1 && !entry.readable) {
		pick.status = QStringLiteral("error");
		pick.message = tr("%1 can only be written: select it, write its value in Details, then Add selected draft.").arg(entry.path);
		return pick;
	}
	if (entry.category != 1 && entry.requestBytes > 0) {
		pick.status = QStringLiteral("input");
		pick.message = tr("%1 takes input. Enter selects it: fill it in Details, then Add selected draft.").arg(entry.path);
		return pick;
	}
	pick.status = QStringLiteral("adds");
	return pick;
}

} // namespace telemetry::ux::endpoint_paths
