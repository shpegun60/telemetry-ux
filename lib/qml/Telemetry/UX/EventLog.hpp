/*
 * @file EventLog.hpp
 * @brief The window's Log and Problems: what happened, and what is wrong now.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * Two typed models, as an IDE keeps its output and its diagnostics apart -
 * no topic bus between them (WINDOW_LAYOUT.md, "The bottom panel"):
 * - LogModel: every event, newest first, at most `capacity`; Copy and Save as
 *   write it as text.
 * - ProblemsModel: what is wrong now, keyed: raise() a problem under a key,
 *   resolve() it when its cause ends, resolveScope() all of one scope (the
 *   device's, on a disconnect). A new problem writes one Log line; the same
 *   raised again changes its words and writes none.
 * Each source of problems has one adapter (SessionProblems for the
 * connection), so nothing is reported twice. GUI thread only.
 */
#ifndef TELEMETRY_UX_EVENT_LOG_HPP
#define TELEMETRY_UX_EVENT_LOG_HPP
#pragma once

#include <QtCore/QAbstractListModel>
#include <QtCore/QDateTime>
#include <QtCore/QPointer>
#include <QtCore/QSortFilterProxyModel>
#include <deque>
#include <vector>

namespace telemetry::ux {

// How bad: an event in the Log, a problem in Problems.
class Severity {
	Q_GADGET
public:
	enum Value { Info = 0, Warning = 1, Error = 2 };
	Q_ENUM(Value)
};

class LogModel final : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY countChanged)
	Q_PROPERTY(int errorCount READ errorCount NOTIFY countChanged)
	Q_PROPERTY(int capacity READ capacity CONSTANT)
public:
	enum Role {
		SeverityRole = Qt::UserRole + 1, // Severity::Value
		SourceRole,                      // "Connection", "Write", "Transfers"
		TimeRole,                        // QDateTime
		TimeTextRole,                    // "14:02:07"
		TextRole,
		ProblemRole // Warning or Error: what the old Log called a problem
	};
	Q_ENUM(Role)

	// The most entries kept; the oldest leave first.
	static constexpr int maxEntries = 2000;

	explicit LogModel(QObject* parent = nullptr);

	int count() const noexcept { return static_cast<int>(entries_.size()); }
	int errorCount() const noexcept { return errors_; }
	static constexpr int capacity() noexcept { return maxEntries; }

	// An event, now; it becomes row 0.
	Q_INVOKABLE void append(int severity, const QString& source, const QString& text);
	Q_INVOKABLE void clear();
	// The newest event's words, or empty.
	Q_INVOKABLE QString newest() const;
	// Every row of `rows` (all when empty), oldest first, one a line:
	// "14:02:07  Error  Connection  The device stopped answering." - what
	// Copy puts on the clipboard and Save as writes.
	Q_INVOKABLE QString text(const QVariantList& rows = {}) const;
	// Writes text() to a local file, whole or not at all; empty when written,
	// else why not, in words.
	Q_INVOKABLE QString saveTo(const QString& localFile, const QVariantList& rows = {}) const;
	// Saves an already captured export. Capture text() before opening a save
	// dialog: row numbers move when new events arrive or the oldest leave.
	Q_INVOKABLE QString saveText(const QString& localFile, const QString& capturedText) const;

	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	// "Error", "Warning", "Info" - the words the Log and its text use.
	static QString severityWord(int severity);

Q_SIGNALS:
	void countChanged();
	// One event added; Problems and the status bar follow it.
	void appended(int severity, const QString& source, const QString& text);

private:
	struct Entry {
		int severity = Severity::Info;
		QString source, text;
		QDateTime time;
	};
	std::deque<Entry> entries_; // newest first
	int errors_ = 0;
};

// The Log as a view shows it: words to find (in the text or the source) and
// the least severity to show. A proxy of LogModel; the rows it maps are the
// Log's own.
class LogFilterModel final : public QSortFilterProxyModel {
	Q_OBJECT
	Q_PROPERTY(QString text READ text WRITE setText NOTIFY filterChanged)
	Q_PROPERTY(int minimumSeverity READ minimumSeverity WRITE setMinimumSeverity NOTIFY filterChanged)
public:
	explicit LogFilterModel(LogModel* log, QObject* parent = nullptr);

	QString text() const { return text_; }
	void setText(const QString& text);
	int minimumSeverity() const noexcept { return minimumSeverity_; }
	void setMinimumSeverity(int severity);
	// The rows shown, as rows of the Log: what Copy and Save as take.
	Q_INVOKABLE QVariantList sourceRows() const;

Q_SIGNALS:
	void filterChanged();

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
	QString text_;
	int minimumSeverity_ = Severity::Info;
};

class ProblemsModel final : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY countsChanged)
	Q_PROPERTY(int errorCount READ errorCount NOTIFY countsChanged)
	Q_PROPERTY(int warningCount READ warningCount NOTIFY countsChanged)
public:
	enum Role {
		KeyRole = Qt::UserRole + 1,
		SeverityRole, // Severity::Warning or Severity::Error
		SourceRole,
		ScopeRole,    // "device", or empty: what resolveScope() clears
		TextRole,
		SinceRole,    // QDateTime, when it was first raised
		SinceTextRole // "14:02:07"
	};
	Q_ENUM(Role)

	// A Log to write a new problem to, and its resolution; may be null.
	explicit ProblemsModel(LogModel* log = nullptr, QObject* parent = nullptr);

	int count() const noexcept { return static_cast<int>(problems_.size()); }
	int errorCount() const noexcept;
	int warningCount() const noexcept;

	// A problem under `key`. New: added - errors before warnings - and one
	// Log line. Raised again: its severity and words change in place, and
	// the Log hears nothing.
	Q_INVOKABLE void raise(const QString& key, int severity, const QString& source, const QString& text,
	                       const QString& scope = {});
	// Its cause ended: it leaves, and the Log says so once.
	Q_INVOKABLE void resolve(const QString& key);
	// Every problem of a scope at once ("device", on a disconnect).
	Q_INVOKABLE void resolveScope(const QString& scope);
	Q_INVOKABLE bool has(const QString& key) const;
	// The keys raised, those starting with prefix when one is given: what an
	// adapter checks against its own state.
	Q_INVOKABLE QStringList keys(const QString& prefix = {}) const;
	// Rows as the Log writes its own, one a line, in the order shown:
	// "12:04:31  Error  Transfers  words" - Copy and Copy all.
	Q_INVOKABLE QString text(const QVariantList& rows = {}) const;

	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
	void countsChanged();

private:
	struct Problem {
		QString key, source, scope, text;
		int severity = Severity::Warning;
		QDateTime since;
	};
	int rowOf(const QString& key) const;
	bool remove(int row, bool told);

	QPointer<LogModel> log_;
	std::vector<Problem> problems_;
};

} // namespace telemetry::ux
#endif
