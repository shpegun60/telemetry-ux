/*
 * @file EventLog.cpp
 * @brief The window's Log and Problems: what happened, and what is wrong now.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "EventLog.hpp"
#include <QtCore/QDir>
#include <QtCore/QSaveFile>
#include <algorithm>

namespace telemetry::ux {

namespace {
int clampedSeverity(int severity) noexcept
{
	return std::clamp(severity, static_cast<int>(Severity::Info), static_cast<int>(Severity::Error));
}

QString timeText(const QDateTime& time)
{
	return time.toString(QStringLiteral("hh:mm:ss"));
}
} // namespace

LogModel::LogModel(QObject* parent) : QAbstractListModel(parent) {}

QString LogModel::severityWord(int severity)
{
	switch (clampedSeverity(severity)) {
		case Severity::Error:
			return tr("Error");
		case Severity::Warning:
			return tr("Warning");
		default:
			return tr("Info");
	}
}

void LogModel::append(int severity, const QString& source, const QString& text)
{
	const QPointer<LogModel> alive(this);
	const int level = clampedSeverity(severity);
	// The oldest leaves first, before the newest comes in as row 0.
	if (static_cast<int>(entries_.size()) >= maxEntries) {
		const int last = static_cast<int>(entries_.size()) - 1;
		beginRemoveRows({}, last, last);
		if (!alive)
			return;
		errors_ -= entries_.back().severity == Severity::Error ? 1 : 0;
		entries_.pop_back();
		endRemoveRows();
		if (!alive)
			return;
	}
	beginInsertRows({}, 0, 0);
	if (!alive)
		return;
	entries_.push_front(Entry{level, source, text, QDateTime::currentDateTime()});
	errors_ += level == Severity::Error ? 1 : 0;
	endInsertRows();
	if (!alive)
		return;
	Q_EMIT countChanged();
	if (!alive)
		return;
	Q_EMIT appended(level, source, text);
}

void LogModel::clear()
{
	if (entries_.empty())
		return;
	const QPointer<LogModel> alive(this);
	beginResetModel();
	if (!alive)
		return;
	entries_.clear();
	errors_ = 0;
	endResetModel();
	if (!alive)
		return;
	Q_EMIT countChanged();
}

QString LogModel::newest() const
{
	return entries_.empty() ? QString{} : entries_.front().text;
}

QString LogModel::text(const QVariantList& rows) const
{
	std::vector<int> chosen;
	if (rows.isEmpty()) {
		for (int row = 0; row < count(); ++row)
			chosen.push_back(row);
	} else {
		for (const auto& row : rows)
			if (const int at = row.toInt(); at >= 0 && at < count())
				chosen.push_back(at);
	}
	// Oldest first, as a file is read.
	std::sort(chosen.begin(), chosen.end(), std::greater<>());
	QString out;
	for (const int row : chosen) {
		const auto& entry = entries_[static_cast<std::size_t>(row)];
		out += timeText(entry.time) + QStringLiteral("  ") + severityWord(entry.severity) + QStringLiteral("  ") +
		       entry.source + QStringLiteral("  ") + entry.text + QLatin1Char('\n');
	}
	return out;
}

QString LogModel::saveTo(const QString& localFile, const QVariantList& rows) const
{
	return saveText(localFile, text(rows));
}

QString LogModel::saveText(const QString& localFile, const QString& capturedText) const
{
	QSaveFile file(localFile);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return tr("Could not write %1: %2").arg(QDir::toNativeSeparators(localFile), file.errorString());
	const auto bytes = capturedText.toUtf8();
	if (file.write(bytes) != bytes.size()) {
		file.cancelWriting();
		return tr("Could not write %1: %2").arg(QDir::toNativeSeparators(localFile), file.errorString());
	}
	if (!file.commit())
		return tr("Could not write %1: %2").arg(QDir::toNativeSeparators(localFile), file.errorString());
	return {};
}

int LogModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : count();
}

QVariant LogModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() >= count())
		return {};
	const auto& entry = entries_[static_cast<std::size_t>(index.row())];
	switch (role) {
		case Qt::DisplayRole:
		case TextRole:
			return entry.text;
		case SeverityRole:
			return entry.severity;
		case SourceRole:
			return entry.source;
		case TimeRole:
			return entry.time;
		case TimeTextRole:
			return timeText(entry.time);
		case ProblemRole:
			return entry.severity >= Severity::Warning;
		default:
			return {};
	}
}

QHash<int, QByteArray> LogModel::roleNames() const
{
	return {{SeverityRole, "severity"}, {SourceRole, "source"},     {TimeRole, "time"},
	        {TimeTextRole, "timeText"}, {TextRole, "text"},         {ProblemRole, "problem"}};
}

LogFilterModel::LogFilterModel(LogModel* log, QObject* parent) : QSortFilterProxyModel(parent)
{
	setSourceModel(log);
}

void LogFilterModel::setText(const QString& text)
{
	if (text == text_)
		return;
	beginFilterChange();
	text_ = text;
	endFilterChange(QSortFilterProxyModel::Direction::Rows);
	Q_EMIT filterChanged();
}

void LogFilterModel::setMinimumSeverity(int severity)
{
	severity = clampedSeverity(severity);
	if (severity == minimumSeverity_)
		return;
	beginFilterChange();
	minimumSeverity_ = severity;
	endFilterChange(QSortFilterProxyModel::Direction::Rows);
	Q_EMIT filterChanged();
}

QVariantList LogFilterModel::sourceRows() const
{
	QVariantList rows;
	for (int row = 0; row < rowCount(); ++row)
		rows.push_back(mapToSource(index(row, 0)).row());
	return rows;
}

bool LogFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
	const auto at = sourceModel()->index(sourceRow, 0, sourceParent);
	if (at.data(LogModel::SeverityRole).toInt() < minimumSeverity_)
		return false;
	if (text_.isEmpty())
		return true;
	return at.data(LogModel::TextRole).toString().contains(text_, Qt::CaseInsensitive) ||
	       at.data(LogModel::SourceRole).toString().contains(text_, Qt::CaseInsensitive);
}

ProblemsModel::ProblemsModel(LogModel* log, QObject* parent) : QAbstractListModel(parent), log_(log) {}

int ProblemsModel::errorCount() const noexcept
{
	return static_cast<int>(std::count_if(problems_.begin(), problems_.end(),
	                                      [](const Problem& problem) { return problem.severity == Severity::Error; }));
}

int ProblemsModel::warningCount() const noexcept
{
	return count() - errorCount();
}

int ProblemsModel::rowOf(const QString& key) const
{
	for (std::size_t row = 0; row < problems_.size(); ++row)
		if (problems_[row].key == key)
			return static_cast<int>(row);
	return -1;
}

bool ProblemsModel::has(const QString& key) const
{
	return rowOf(key) >= 0;
}

QStringList ProblemsModel::keys(const QString& prefix) const
{
	QStringList found;
	for (const auto& problem : problems_)
		if (problem.key.startsWith(prefix))
			found << problem.key;
	return found;
}

QString ProblemsModel::text(const QVariantList& rows) const
{
	QString out;
	const auto line = [&](const Problem& problem) {
		out += timeText(problem.since) + QStringLiteral("  ") + LogModel::severityWord(problem.severity) +
		       QStringLiteral("  ") + problem.source + QStringLiteral("  ") + problem.text + QLatin1Char('\n');
	};
	if (rows.isEmpty()) {
		for (const auto& problem : problems_)
			line(problem);
		return out;
	}
	for (const auto& row : rows)
		if (const int at = row.toInt(); at >= 0 && at < count())
			line(problems_[static_cast<std::size_t>(at)]);
	return out;
}

void ProblemsModel::raise(const QString& key, int severity, const QString& source, const QString& text,
                          const QString& scope)
{
	const QPointer<ProblemsModel> alive(this);
	// A problem is a warning or an error; an Info is an event, not a problem.
	const int level = std::max(clampedSeverity(severity), static_cast<int>(Severity::Warning));
	if (const int row = rowOf(key); row >= 0) {
		auto& problem = problems_[static_cast<std::size_t>(row)];
		const bool moves = problem.severity != level;
		problem.text = text;
		problem.source = source;
		problem.scope = scope;
		if (!moves) {
			const auto changed = index(row);
			Q_EMIT dataChanged(changed, changed);
			return;
		}
		// Its severity changed: it moves to its own group, said nothing.
		auto kept = problem;
		kept.severity = level;
		if (!remove(row, false))
			return;
		problems_.reserve(problems_.size() + 1);
		const auto at = level == Severity::Error ? errorCount() : count();
		beginInsertRows({}, at, at);
		if (!alive)
			return;
		problems_.insert(problems_.begin() + at, std::move(kept));
		endInsertRows();
		if (!alive)
			return;
		Q_EMIT countsChanged();
		return;
	}
	// Errors first, then warnings, each in the order they came.
	const int at = level == Severity::Error ? errorCount() : count();
	beginInsertRows({}, at, at);
	if (!alive)
		return;
	problems_.insert(problems_.begin() + at, Problem{key, source, scope, text, level, QDateTime::currentDateTime()});
	endInsertRows();
	if (!alive)
		return;
	// Record the new fact before notifying its count: a direct receiver may
	// resolve it at once, whose Log line must follow the original one.
	if (log_)
		log_->append(level, source, text);
	if (alive)
		Q_EMIT countsChanged();
}

bool ProblemsModel::remove(int row, bool told)
{
	const QPointer<ProblemsModel> alive(this);
	const auto problem = problems_[static_cast<std::size_t>(row)];
	beginRemoveRows({}, row, row);
	if (!alive)
		return false;
	problems_.erase(problems_.begin() + row);
	endRemoveRows();
	if (!alive)
		return false;
	if (told && log_)
		log_->append(Severity::Info, problem.source, tr("Resolved: %1").arg(problem.text));
	return !alive.isNull();
}

void ProblemsModel::resolve(const QString& key)
{
	if (const int row = rowOf(key); row >= 0) {
		if (remove(row, true))
			Q_EMIT countsChanged();
	}
}

void ProblemsModel::resolveScope(const QString& scope)
{
	bool any = false;
	for (int row = count() - 1; row >= 0; --row)
		if (problems_[static_cast<std::size_t>(row)].scope == scope) {
			if (!remove(row, true))
				return;
			any = true;
		}
	if (any)
		Q_EMIT countsChanged();
}

int ProblemsModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : count();
}

QVariant ProblemsModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() >= count())
		return {};
	const auto& problem = problems_[static_cast<std::size_t>(index.row())];
	switch (role) {
		case Qt::DisplayRole:
		case TextRole:
			return problem.text;
		case KeyRole:
			return problem.key;
		case SeverityRole:
			return problem.severity;
		case SourceRole:
			return problem.source;
		case ScopeRole:
			return problem.scope;
		case SinceRole:
			return problem.since;
		case SinceTextRole:
			return timeText(problem.since);
		default:
			return {};
	}
}

QHash<int, QByteArray> ProblemsModel::roleNames() const
{
	return {{KeyRole, "key"},     {SeverityRole, "severity"}, {SourceRole, "source"},       {ScopeRole, "scope"},
	        {TextRole, "text"},   {SinceRole, "since"},       {SinceTextRole, "sinceText"}};
}

} // namespace telemetry::ux
