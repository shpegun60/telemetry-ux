/*
 * @file FileTextModels.hpp
 * @brief A text file as numbered lines, and a CSV table sorted and filtered, for the viewer.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * Text: one row per line - its number, its text, the Find matches on it - so
 * the view makes only the lines on screen, at any length of file. Find runs
 * over the whole text and keeps every match (to a cap); Next and Previous
 * move from the line the reader is on. Go to takes a line number or "end".
 *
 * Table: the CSV's rows, sorted by a column - numbers as numbers, words in
 * natural order, empty cells last - and filtered to the rows containing a
 * text; Find walks the shown cells. Qt only.
 */
#ifndef TELEMETRY_UX_FILE_TEXT_MODELS_HPP
#define TELEMETRY_UX_FILE_TEXT_MODELS_HPP
#pragma once

#include <QtCore/QAbstractListModel>
#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <vector>

namespace telemetry::ux {
class FileTableModel;

class TextLinesModel final : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int lineCount READ lineCount NOTIFY textChanged)
	// The longest line in characters: how wide the text is without wrapping.
	Q_PROPERTY(int longestLine READ longestLine NOTIFY textChanged)
	// Find: how many matches, which one is current (1-based, 0 for none),
	// whether there were more than the cap, and the current match's line.
	Q_PROPERTY(int matchCount READ matchCount NOTIFY matchesChanged)
	Q_PROPERTY(int matchIndex READ matchIndex NOTIFY matchesChanged)
	Q_PROPERTY(bool matchesCapped READ matchesCapped NOTIFY matchesChanged)
	Q_PROPERTY(int matchLine READ matchLine NOTIFY matchesChanged)
public:
	enum Role {
		NumberRole = Qt::UserRole + 1, // 1-based
		TextRole,                      // without its line break
		MatchesRole,                   // [[start, length], …] of Find on this line
		CurrentRole                    // the current match's start on this line, or -1
	};
	Q_ENUM(Role)
	static constexpr int maxMatches = 100000;

	using QAbstractListModel::QAbstractListModel;
	// Splits at \n, \r\n and \r; a final line break makes no empty last line.
	void setText(const QString& text);
	void clear();

	int lineCount() const noexcept { return static_cast<int>(starts_.size()); }
	int longestLine() const noexcept { return longest_; }
	int matchCount() const noexcept { return static_cast<int>(matches_.size()); }
	int matchIndex() const noexcept { return current_ + 1; }
	bool matchesCapped() const noexcept { return capped_; }
	int matchLine() const noexcept;

	// The next (or previous) match of text from line, wrapping at the ends;
	// a new text finds every match first. Empty when found without wrapping,
	// otherwise "Wrapped to the start", "Wrapped to the end" or "Not found".
	Q_INVOKABLE QString find(const QString& text, bool caseSensitive, bool forward, int fromLine);
	Q_INVOKABLE void clearFind();
	// "120" is line 120 (index 119), "end" the last; -1 when it names no line.
	Q_INVOKABLE int lineFor(const QString& spec) const;
	// Lines first..last (0-based, inclusive) joined with \n.
	Q_INVOKABLE QString linesText(int first, int last) const;
	Q_INVOKABLE QString lineText(int line) const;

	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
	void textChanged();
	void matchesChanged();

private:
	struct Match {
		int line, column, length;
	};
	void touchLine(int line);

	QString text_;
	std::vector<int> starts_, lengths_;
	int longest_ = 0;
	std::vector<Match> matches_;
	QString findText_;
	bool findCase_ = false, capped_ = false;
	int current_ = -1;
};

// The table's rows as shown: sorted, filtered, searched.
class TableRowsModel final : public QSortFilterProxyModel {
	Q_OBJECT
	// The column the rows are sorted by, -1 for the file's own order.
	Q_PROPERTY(int sortedColumn READ sortedColumn NOTIFY sortChanged)
	Q_PROPERTY(bool descending READ descending NOTIFY sortChanged)
	Q_PROPERTY(int shownRows READ shownRows NOTIFY shownChanged)
	Q_PROPERTY(int totalRows READ totalRows NOTIFY shownChanged)
	Q_PROPERTY(int matchCount READ matchCount NOTIFY matchesChanged)
	Q_PROPERTY(int matchIndex READ matchIndex NOTIFY matchesChanged)
public:
	explicit TableRowsModel(FileTableModel* source, QObject* parent = nullptr);

	int sortedColumn() const noexcept { return sorted_; }
	bool descending() const noexcept { return descending_; }
	int shownRows() const { return rowCount(); }
	int totalRows() const;
	int matchCount() const noexcept { return static_cast<int>(matches_.size()); }
	int matchIndex() const noexcept { return current_ + 1; }

	// A heading clicked: ascending, then descending, then the file's order.
	Q_INVOKABLE void sortBy(int column);
	// The same by name, for the keyboard's menu: a column and a direction,
	// or back to the file's order.
	Q_INVOKABLE void sortColumn(int column, bool descending);
	Q_INVOKABLE void clearSort();
	// Only the rows with text in some cell (ignoring case); empty shows all.
	// Returns the rows shown.
	Q_INVOKABLE int setFilter(const QString& text);
	// The next or previous shown cell containing text (ignoring case), from
	// row and column: {row, column, message} - message as TextLinesModel::find,
	// row -1 when nothing matches.
	Q_INVOKABLE QVariantMap find(const QString& text, bool forward, int row, int column);
	Q_INVOKABLE void clearFind();
	Q_INVOKABLE bool numeric(int column) const;
	Q_INVOKABLE QString heading(int column) const;
	Q_INVOKABLE QString cellText(int row, int column) const;
	// A shown row, or everything shown with the headings, as "tsv" or "csv".
	Q_INVOKABLE QString rowText(int row, const QString& format) const;
	Q_INVOKABLE QString shownText(const QString& format) const;

Q_SIGNALS:
	void sortChanged();
	void shownChanged();
	void matchesChanged();

protected:
	bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
	bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
	QString joined(const QStringList& cells, const QString& format) const;

	FileTableModel* table_;
	QString filter_, findText_;
	int sorted_ = -1;
	bool descending_ = false;
	std::vector<std::pair<int, int>> matches_; // shown row, column
	int current_ = -1;
};
} // namespace telemetry::ux
#endif
