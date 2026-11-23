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
 *
 * A line says what happened, to what (LG-1, the designer's §7): record()
 * takes an event with its target - the kind glyph's name, the path, what
 * follows the path ("= 2", "(2, 3) → 6") - and its outcome in words, its
 * severity from the outcome's kind ("ok", "doubt", "refused", "failed"),
 * never from the words. One line per operation: a "pending" event shows only
 * once the operation has run progressThresholdMs, and its outcome then takes
 * that line's place; a later word on the same operation (what a refused
 * Field holds) does too. A grouped event holds its list (items), which the
 * current row opens: 20 lines, then "and 12 more".
 */
#ifndef TELEMETRY_UX_EVENT_LOG_HPP
#define TELEMETRY_UX_EVENT_LOG_HPP
#pragma once

#include <QtCore/QAbstractListModel>
#include <QtCore/QDateTime>
#include <QtCore/QPointer>
#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QStringList>
#include <QtCore/QVariantMap>
#include <deque>
#include <map>
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
		SourceRole,                      // "Connection", "Command", "Transfers"
		TimeRole,                        // QDateTime
		TimeTextRole,                    // "14:02:07"
		TextRole,                        // the words: the whole event, or an operation's outcome
		ProblemRole,                     // Warning or Error: what the old Log called a problem
		KindRole,   // the kind glyph before the path ("value", "struct", "array", "command", "service"), or empty
		PathRole,   // the target's path ("control/Reset", "device/Config.Gain"), or empty
		ValuesRole, // what follows the path: "= 2", "(2, 3) → 6", or empty
		DetailRole, // a grouped event's list as the current row opens it, or empty
		LineRole    // path, values and words as one line: what the row says aloud
	};
	Q_ENUM(Role)

	// The most entries kept; the oldest leave first.
	static constexpr int maxEntries = 2000;
	// How long an operation runs before the Log says it is under way (the
	// design's §7, the qt-ui-design skill's §1.1): one constant for every
	// source, the providers included.
	static constexpr int progressThresholdMs = 1000;
	// The lines of a grouped event's list the current row shows before
	// "and 12 more".
	static constexpr int detailLines = 20;

	explicit LogModel(QObject* parent = nullptr);

	int count() const noexcept { return static_cast<int>(entries_.size()); }
	int errorCount() const noexcept { return errors_; }
	static constexpr int capacity() noexcept { return maxEntries; }

	// An event, now; it becomes row 0.
	Q_INVOKABLE void append(int severity, const QString& source, const QString& text);
	// An event with its target (LG-1): {severity or result, source, kind,
	// path, values, text, items, key}. result, when given, decides the
	// severity: "ok" and "pending" are Info, "doubt" (not read back, not
	// confirmed) a Warning, "refused" and "failed" an Error. key names one
	// operation: a "pending" event under a key waits progressThresholdMs
	// before it shows, and an outcome under it before then is the only line;
	// under a key already shown, the event takes that line's place, its time
	// kept. items: a grouped event's list.
	Q_INVOKABLE void record(const QVariantMap& event);
	Q_INVOKABLE void clear();
	// The newest event's line, or empty.
	Q_INVOKABLE QString newest() const;
	// Every row of `rows` (all when empty), oldest first, one a line:
	// "14:02:07  Error  Connection  The device stopped answering." - what
	// Copy puts on the clipboard and Save as writes. An operation's line is
	// its target and its outcome, "control/Reset  executed"; a grouped
	// event's list follows it, every item, each on a line of its own.
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
	// The severity of an outcome's kind (see record); -1 for a kind unknown.
	static int severityOf(const QString& result);
	// A list as a row opens it: its first detailLines items, one a line,
	// then "and 12 more".
	static QString detailText(const QStringList& items);
	// path, values and words as one line: "device/Config.Gain = 2  written";
	// the words alone without a path.
	static QString lineText(const QString& path, const QString& values, const QString& text);

Q_SIGNALS:
	void countChanged();
	// One event added; Problems and the status bar follow it. text is its
	// line (lineText).
	void appended(int severity, const QString& source, const QString& text);

private:
	struct Entry {
		int severity = Severity::Info;
		QString source, text, kind, path, values, key;
		QStringList items;
		QDateTime time;
	};
	void insert(Entry entry);
	int rowOfKey(const QString& key) const;
	void showHeld(const QString& key, quint64 serial);

	std::deque<Entry> entries_; // newest first
	int errors_ = 0;
	// Operations under way and not yet shown, by key: each shows once it has
	// run progressThresholdMs, unless its outcome came first.
	struct Held {
		Entry entry;
		quint64 serial = 0;
	};
	std::map<QString, Held> held_;
	quint64 heldSerial_ = 0;
};

// The Log as a view shows it: words to find (in the line, the source or a
// grouped event's list) and the least severity to show. A proxy of
// LogModel; the rows it maps are the Log's own.
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
		SeverityRole,  // Severity::Warning or Severity::Error
		SourceRole,
		ScopeRole,     // "device", or empty: what resolveScope() clears
		TextRole,      // the words; a grouped problem's summary
		SinceRole,     // QDateTime, when it was first raised
		SinceTextRole, // "14:02:07"
		DetailRole     // a grouped problem's list as the current row opens it, or empty
	};
	Q_ENUM(Role)

	// A Log to write a new problem to, and its resolution; may be null.
	explicit ProblemsModel(LogModel* log = nullptr, QObject* parent = nullptr);

	int count() const noexcept { return static_cast<int>(problems_.size()); }
	int errorCount() const noexcept;
	int warningCount() const noexcept;

	// A problem under `key`. New: added - errors before warnings - and one
	// Log line. Raised again: its severity and words change in place, and
	// the Log hears nothing. items: a grouped problem's list, text its one
	// summary line ("8 watched values wait for a device"); the Log's line
	// and the current row hold the list.
	Q_INVOKABLE void raise(const QString& key, int severity, const QString& source, const QString& text,
	                       const QString& scope = {}, const QStringList& items = {});
	// Its cause ended: it leaves, and the Log says so once - "Resolved: "
	// and its summary, never its list.
	Q_INVOKABLE void resolve(const QString& key);
	// Its cause ended, and the Log says what changed in words of its own:
	// "All 8 watched values found on Emulated device". severity and items
	// are that line's (a resolution that leaves something behind is a
	// warning with the list of what is left).
	Q_INVOKABLE void resolveWith(const QString& key, const QString& words, int severity = Severity::Info,
	                             const QStringList& items = {});
	// Every problem of a scope at once ("device", on a disconnect).
	Q_INVOKABLE void resolveScope(const QString& scope);
	Q_INVOKABLE bool has(const QString& key) const;
	// The keys raised, those starting with prefix when one is given: what an
	// adapter checks against its own state.
	Q_INVOKABLE QStringList keys(const QString& prefix = {}) const;
	// Rows as the Log writes its own, one a line, in the order shown:
	// "12:04:31  Error  Transfers  words" - Copy and Copy all. A grouped
	// problem's list follows its line.
	Q_INVOKABLE QString text(const QVariantList& rows = {}) const;

	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
	void countsChanged();

private:
	struct Problem {
		QString key, source, scope, text;
		QStringList items;
		int severity = Severity::Warning;
		QDateTime since;
	};
	int rowOf(const QString& key) const;
	// Removes row; told, the Log hears words (with severity and items), or
	// "Resolved: " and its summary when words are empty.
	bool remove(int row, bool told, const QString& words = {}, int severity = Severity::Info,
	            const QStringList& items = {});

	QPointer<LogModel> log_;
	std::vector<Problem> problems_;
};

} // namespace telemetry::ux
#endif
