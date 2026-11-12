/*
 * @file FileTextModels.hpp
 * @brief A text file as numbered lines, and a CSV table sorted and filtered, for the viewer.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * Text: one row per line - its number, its text, the Find matches on it - so
 * the view makes only the lines on screen, at any length of file. A line
 * longer than maxShownChars shows its start and says how much more it has;
 * Copy takes it whole. Find counts the matches (to a cap) and Next and
 * Previous search the text itself from the current match - or from the
 * reader's line when the reader has moved - so every match is reached, past
 * the cap too. Go to takes a line number or "end".
 *
 * Table: the CSV's rows, sorted by a column - numbers as numbers, words in
 * natural order, empty cells and NaN last, equal ones in the file's order -
 * and filtered to the rows containing a text; Find walks the shown cells. A
 * reload keeps the sort and the filter. Qt only.
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
	// The widest shown line in columns (a tab to the next multiple of 8): how
	// wide the text is without wrapping.
	Q_PROPERTY(int longestLine READ longestLine NOTIFY textChanged)
	// Find: how many matches (to the cap), which one is current (1-based; 0
	// past the cap or for none), whether there are more than counted, and
	// the current match's line.
	Q_PROPERTY(int matchCount READ matchCount NOTIFY matchesChanged)
	Q_PROPERTY(int matchIndex READ matchIndex NOTIFY matchesChanged)
	Q_PROPERTY(bool matchesCapped READ matchesCapped NOTIFY matchesChanged)
	Q_PROPERTY(int matchLine READ matchLine NOTIFY matchesChanged)
public:
	enum Role {
		NumberRole = Qt::UserRole + 1, // 1-based
		TextRole,                      // what is shown of the line, without its line break
		HiddenRole,                    // characters of the line past what is shown
		MatchesRole,                   // [[start, length], …] of Find in what is shown
		CurrentRole                    // the current match's start on this line, or -1
	};
	Q_ENUM(Role)
	static constexpr int maxMatches = 100000;
	static constexpr int maxShownChars = 10000;
	static constexpr int maxDrawnPerLine = 200;
	static constexpr int tabColumns = 8;

	using QAbstractListModel::QAbstractListModel;
	// Splits at \n, \r\n and \r; a final line break makes no empty last line.
	void setText(const QString& text);
	void clear();

	int lineCount() const noexcept { return static_cast<int>(starts_.size()); }
	int longestLine() const noexcept { return longest_; }
	int matchCount() const noexcept { return static_cast<int>(positions_.size()); }
	int matchIndex() const noexcept;
	bool matchesCapped() const noexcept { return capped_; }
	int matchLine() const noexcept { return current_ >= 0 ? lineAt(current_) : -1; }

	// The next (or previous) match of text, from the current match while the
	// reader is still on its line, else from the reader's line; wrapping at
	// the ends. Empty when found without wrapping, otherwise "Wrapped to the
	// start", "Wrapped to the end" or "Not found".
	Q_INVOKABLE QString find(const QString& text, bool caseSensitive, bool forward, int fromLine);
	Q_INVOKABLE void clearFind();
	// "120" is line 120 (index 119), "end" the last; -1 when it names no line.
	Q_INVOKABLE int lineFor(const QString& spec) const;
	// Lines first..last (0-based, inclusive), whole, joined with \n.
	Q_INVOKABLE QString linesText(int first, int last) const;
	Q_INVOKABLE QString lineText(int line) const;

	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
	void textChanged();
	void matchesChanged();

private:
	int lineAt(qsizetype position) const noexcept;
	void touchLine(int line);
	void touchAll();

	QString text_;
	std::vector<qsizetype> starts_;
	std::vector<int> lengths_;
	int longest_ = 0;
	// The first matches' positions in the text, to the cap: the count.
	std::vector<qsizetype> positions_;
	QString findText_;
	bool findCase_ = false, capped_ = false;
	// The current match's position in the text, or -1.
	qsizetype current_ = -1;
};

// The table's rows as shown: sorted, filtered, searched.
class TableRowsModel final : public QSortFilterProxyModel {
	Q_OBJECT
	// The column the rows are sorted by, -1 for the file's own order.
	Q_PROPERTY(int sortedColumn READ sortedColumn NOTIFY sortChanged)
	Q_PROPERTY(bool descending READ descending NOTIFY sortChanged)
	Q_PROPERTY(int shownRows READ shownRows NOTIFY shownChanged)
	Q_PROPERTY(int totalRows READ totalRows NOTIFY shownChanged)
	Q_PROPERTY(QString filterText READ filterText NOTIFY shownChanged)
	Q_PROPERTY(int matchCount READ matchCount NOTIFY matchesChanged)
	Q_PROPERTY(int matchIndex READ matchIndex NOTIFY matchesChanged)
	Q_PROPERTY(bool matchesCapped READ matchesCapped NOTIFY matchesChanged)
public:
	static constexpr int maxMatches = 100000;

	explicit TableRowsModel(FileTableModel* source, QObject* parent = nullptr);

	int sortedColumn() const noexcept { return sorted_; }
	bool descending() const noexcept { return descending_; }
	int shownRows() const { return rowCount(); }
	int totalRows() const;
	QString filterText() const { return filter_; }
	int matchCount() const noexcept { return static_cast<int>(matches_.size()); }
	int matchIndex() const noexcept { return current_ + 1; }
	bool matchesCapped() const noexcept { return capped_; }

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
	// the current match while the reader is on it, else from the reader's
	// row and column: {row, column, message} - message as
	// TextLinesModel::find, row -1 when nothing matches.
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
	// The sorted column's cells, read once per sort: a number (NaN for none)
	// or the trimmed text, by source row.
	void readKeys();
	// Shows what the filter keeps, in one pass over the rows.
	void refilter();

	FileTableModel* table_;
	std::vector<double> numberKeys_;
	std::vector<QString> textKeys_;
	QString filter_, findText_;
	int sorted_ = -1;
	bool descending_ = false, capped_ = false;
	std::vector<std::pair<int, int>> matches_; // shown row, column
	int current_ = -1;
};
} // namespace telemetry::ux
#endif
