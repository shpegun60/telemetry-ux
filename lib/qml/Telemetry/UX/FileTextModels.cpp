/*
 * @file FileTextModels.cpp
 * @brief A text file as numbered lines, and a CSV table sorted and filtered, for the viewer.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "FileTextModels.hpp"
#include "FileContent.hpp"
#include <QtCore/QStringList>
#include <algorithm>

namespace telemetry::ux {
namespace {
// Natural order, the same on every platform and locale: runs of digits
// compare as numbers ("item 2" before "item 10"), the rest ignoring case.
int naturalCompare(const QString& a, const QString& b)
{
	qsizetype i = 0, j = 0;
	while (i < a.size() && j < b.size()) {
		if (a.at(i).isDigit() && b.at(j).isDigit()) {
			auto endA = i, endB = j;
			while (endA < a.size() && a.at(endA).isDigit())
				++endA;
			while (endB < b.size() && b.at(endB).isDigit())
				++endB;
			// Leading zeros do not make a number larger.
			auto startA = i, startB = j;
			while (startA + 1 < endA && a.at(startA) == QLatin1Char('0'))
				++startA;
			while (startB + 1 < endB && b.at(startB) == QLatin1Char('0'))
				++startB;
			const auto lengthA = endA - startA, lengthB = endB - startB;
			if (lengthA != lengthB)
				return lengthA < lengthB ? -1 : 1;
			if (const int digits = QStringView(a).mid(startA, lengthA).compare(QStringView(b).mid(startB, lengthB)))
				return digits;
			i = endA;
			j = endB;
			continue;
		}
		const auto x = a.at(i).toCaseFolded(), y = b.at(j).toCaseFolded();
		if (x != y)
			return x < y ? -1 : 1;
		++i;
		++j;
	}
	return (i < a.size()) - (j < b.size());
}
} // namespace

// ---- Text ------------------------------------------------------------------

void TextLinesModel::setText(const QString& text)
{
	beginResetModel();
	text_ = text;
	starts_.clear();
	lengths_.clear();
	longest_ = 0;
	matches_.clear();
	findText_.clear();
	current_ = -1;
	capped_ = false;
	const auto size = static_cast<int>(text_.size());
	int start = 0;
	for (int i = 0; i < size; ++i) {
		const auto c = text_.at(i);
		if (c != QLatin1Char('\n') && c != QLatin1Char('\r'))
			continue;
		starts_.push_back(start);
		lengths_.push_back(i - start);
		longest_ = std::max(longest_, i - start);
		if (c == QLatin1Char('\r') && i + 1 < size && text_.at(i + 1) == QLatin1Char('\n'))
			++i;
		start = i + 1;
	}
	// The last line, unless the text ended with a line break (or is empty).
	if (start < size || size == 0) {
		starts_.push_back(start);
		lengths_.push_back(size - start);
		longest_ = std::max(longest_, size - start);
	}
	endResetModel();
	Q_EMIT textChanged();
	Q_EMIT matchesChanged();
}

void TextLinesModel::clear()
{
	setText({});
}

int TextLinesModel::matchLine() const noexcept
{
	return current_ >= 0 ? matches_[static_cast<std::size_t>(current_)].line : -1;
}

void TextLinesModel::touchLine(int line)
{
	if (line < 0 || line >= lineCount())
		return;
	const auto at = index(line);
	Q_EMIT dataChanged(at, at, {MatchesRole, CurrentRole});
}

QString TextLinesModel::find(const QString& text, bool caseSensitive, bool forward, int fromLine)
{
	if (text.isEmpty()) {
		clearFind();
		return {};
	}
	const bool fresh = text != findText_ || caseSensitive != findCase_;
	if (fresh) {
		// Every match, line by line: a match never spans a line break.
		matches_.clear();
		capped_ = false;
		const auto sensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
		for (int line = 0; line < lineCount() && !capped_; ++line) {
			const QStringView view(text_.constData() + starts_[static_cast<std::size_t>(line)],
			                       lengths_[static_cast<std::size_t>(line)]);
			for (qsizetype at = view.indexOf(text, 0, sensitivity); at >= 0;
			     at = view.indexOf(text, at + std::max<qsizetype>(1, text.size()), sensitivity)) {
				if (static_cast<int>(matches_.size()) == maxMatches) {
					capped_ = true;
					break;
				}
				matches_.push_back({line, static_cast<int>(at), static_cast<int>(text.size())});
			}
		}
		findText_ = text;
		findCase_ = caseSensitive;
		current_ = -1;
		if (lineCount() > 0)
			Q_EMIT dataChanged(index(0), index(lineCount() - 1), {MatchesRole, CurrentRole});
	}
	if (matches_.empty()) {
		Q_EMIT matchesChanged();
		return tr("Not found");
	}
	const auto previousLine = matchLine();
	QString message;
	const auto count = static_cast<int>(matches_.size());
	if (current_ < 0) {
		// The first from the reader's line: at it or after it, or before it backwards.
		const auto from = std::max(0, fromLine);
		const auto after = std::lower_bound(matches_.begin(), matches_.end(), from,
		                                    [](const Match& m, int line) { return m.line < line; });
		if (forward) {
			current_ = after == matches_.end() ? 0 : static_cast<int>(after - matches_.begin());
			if (after == matches_.end())
				message = tr("Wrapped to the start");
		} else {
			const auto before = std::upper_bound(matches_.begin(), matches_.end(), from,
			                                     [](int line, const Match& m) { return line < m.line; });
			current_ = before == matches_.begin() ? count - 1 : static_cast<int>(before - matches_.begin()) - 1;
			if (before == matches_.begin())
				message = tr("Wrapped to the end");
		}
	} else if (forward) {
		if (++current_ == count) {
			current_ = 0;
			message = tr("Wrapped to the start");
		}
	} else if (--current_ < 0) {
		current_ = count - 1;
		message = tr("Wrapped to the end");
	}
	touchLine(previousLine);
	touchLine(matchLine());
	Q_EMIT matchesChanged();
	return message;
}

void TextLinesModel::clearFind()
{
	const bool had = !matches_.empty();
	matches_.clear();
	findText_.clear();
	current_ = -1;
	capped_ = false;
	if (had && lineCount() > 0)
		Q_EMIT dataChanged(index(0), index(lineCount() - 1), {MatchesRole, CurrentRole});
	Q_EMIT matchesChanged();
}

int TextLinesModel::lineFor(const QString& spec) const
{
	const auto text = spec.trimmed();
	if (text.compare(QStringLiteral("end"), Qt::CaseInsensitive) == 0)
		return lineCount() - 1;
	bool ok = false;
	const auto number = text.toInt(&ok);
	return ok && number >= 1 && number <= lineCount() ? number - 1 : -1;
}

QString TextLinesModel::lineText(int line) const
{
	if (line < 0 || line >= lineCount())
		return {};
	return text_.mid(starts_[static_cast<std::size_t>(line)], lengths_[static_cast<std::size_t>(line)]);
}

QString TextLinesModel::linesText(int first, int last) const
{
	first = std::max(0, first);
	last = std::min(last, lineCount() - 1);
	QStringList lines;
	for (int line = first; line <= last; ++line)
		lines << lineText(line);
	return lines.join(QLatin1Char('\n'));
}

int TextLinesModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : lineCount();
}

QVariant TextLinesModel::data(const QModelIndex& index, int role) const
{
	const int line = index.row();
	if (!index.isValid() || line < 0 || line >= lineCount())
		return {};
	switch (role) {
		case NumberRole:
			return line + 1;
		case Qt::DisplayRole:
		case TextRole:
			return lineText(line);
		case MatchesRole: {
			QVariantList ranges;
			auto at = std::lower_bound(matches_.begin(), matches_.end(), line,
			                           [](const Match& m, int l) { return m.line < l; });
			for (; at != matches_.end() && at->line == line; ++at)
				ranges.push_back(QVariantList{at->column, at->length});
			return ranges;
		}
		case CurrentRole:
			return current_ >= 0 && matches_[static_cast<std::size_t>(current_)].line == line
			           ? matches_[static_cast<std::size_t>(current_)].column
			           : -1;
		default:
			return {};
	}
}

QHash<int, QByteArray> TextLinesModel::roleNames() const
{
	return {{NumberRole, "number"}, {TextRole, "line"}, {MatchesRole, "matches"}, {CurrentRole, "currentMatch"}};
}

// ---- Table -----------------------------------------------------------------

TableRowsModel::TableRowsModel(FileTableModel* source, QObject* parent) : QSortFilterProxyModel(parent), table_(source)
{
	setSourceModel(source);
	setDynamicSortFilter(false);
	// A new file: its own order, every row. The filter goes before the rows
	// are mapped again, so none of the new rows is judged by the old filter.
	connect(source, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
		filter_.clear();
		sorted_ = -1;
		descending_ = false;
	});
	connect(source, &QAbstractItemModel::modelReset, this, [this] {
		clearFind();
		sort(-1);
		Q_EMIT sortChanged();
		Q_EMIT shownChanged();
	});
}

int TableRowsModel::totalRows() const
{
	return table_ ? table_->rowCount() : 0;
}

bool TableRowsModel::numeric(int column) const
{
	return table_ && table_->numeric(column);
}

QString TableRowsModel::heading(int column) const
{
	return table_ ? table_->headerData(column, Qt::Horizontal).toString() : QString{};
}

void TableRowsModel::sortBy(int column)
{
	if (column != sorted_)
		sortColumn(column, false);
	else if (!descending_)
		sortColumn(column, true);
	else
		clearSort();
}

void TableRowsModel::sortColumn(int column, bool descending)
{
	sorted_ = column;
	descending_ = column >= 0 && descending;
	clearFind();
	sort(sorted_, descending_ ? Qt::DescendingOrder : Qt::AscendingOrder);
	Q_EMIT sortChanged();
}

void TableRowsModel::clearSort()
{
	sortColumn(-1, false);
}

bool TableRowsModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
	const auto a = left.data().toString().trimmed();
	const auto b = right.data().toString().trimmed();
	// Empty cells last, whichever way.
	if (a.isEmpty() != b.isEmpty())
		return descending_ ? a.isEmpty() : b.isEmpty();
	if (numeric(left.column())) {
		bool okA = false, okB = false;
		const double x = a.toDouble(&okA), y = b.toDouble(&okB);
		if (okA && okB && x != y)
			return x < y;
	}
	const int compared = naturalCompare(a, b);
	if (compared != 0)
		return compared < 0;
	// Equal cells keep the file's order, either way: sorted descending, the
	// proxy asks lessThan(right, left), so the rows' order is turned here too.
	return descending_ ? left.row() > right.row() : left.row() < right.row();
}

int TableRowsModel::setFilter(const QString& text)
{
	const auto trimmed = text.trimmed();
	clearFind();
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
	beginFilterChange();
	filter_ = trimmed;
	endFilterChange(Direction::Rows);
#else
	filter_ = trimmed;
	invalidateFilter();
#endif
	Q_EMIT shownChanged();
	return rowCount();
}

bool TableRowsModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
	if (filter_.isEmpty())
		return true;
	const auto* source = sourceModel();
	for (int column = 0; column < source->columnCount(sourceParent); ++column)
		if (source->index(sourceRow, column, sourceParent).data().toString().contains(filter_, Qt::CaseInsensitive))
			return true;
	return false;
}

QVariantMap TableRowsModel::find(const QString& text, bool forward, int row, int column)
{
	if (text.isEmpty()) {
		clearFind();
		return {{QStringLiteral("row"), -1}, {QStringLiteral("column"), -1}, {QStringLiteral("message"), QString{}}};
	}
	if (text != findText_) {
		matches_.clear();
		for (int r = 0; r < rowCount(); ++r)
			for (int c = 0; c < columnCount(); ++c)
				if (index(r, c).data().toString().contains(text, Qt::CaseInsensitive))
					matches_.emplace_back(r, c);
		findText_ = text;
		current_ = -1;
	}
	QString message;
	if (matches_.empty()) {
		current_ = -1;
		Q_EMIT matchesChanged();
		return {{QStringLiteral("row"), -1}, {QStringLiteral("column"), -1}, {QStringLiteral("message"), tr("Not found")}};
	}
	const auto count = static_cast<int>(matches_.size());
	const std::pair<int, int> from{std::max(0, row), std::max(0, column)};
	if (current_ < 0) {
		if (forward) {
			const auto after = std::lower_bound(matches_.begin(), matches_.end(), from);
			current_ = after == matches_.end() ? 0 : static_cast<int>(after - matches_.begin());
			if (after == matches_.end())
				message = tr("Wrapped to the start");
		} else {
			const auto before = std::upper_bound(matches_.begin(), matches_.end(), from);
			current_ = before == matches_.begin() ? count - 1 : static_cast<int>(before - matches_.begin()) - 1;
			if (before == matches_.begin())
				message = tr("Wrapped to the end");
		}
	} else if (forward) {
		if (++current_ == count) {
			current_ = 0;
			message = tr("Wrapped to the start");
		}
	} else if (--current_ < 0) {
		current_ = count - 1;
		message = tr("Wrapped to the end");
	}
	Q_EMIT matchesChanged();
	const auto& at = matches_[static_cast<std::size_t>(current_)];
	return {{QStringLiteral("row"), at.first}, {QStringLiteral("column"), at.second}, {QStringLiteral("message"), message}};
}

void TableRowsModel::clearFind()
{
	matches_.clear();
	findText_.clear();
	current_ = -1;
	Q_EMIT matchesChanged();
}

QString TableRowsModel::cellText(int row, int column) const
{
	return index(row, column).data().toString();
}

QString TableRowsModel::joined(const QStringList& cells, const QString& format) const
{
	if (format != QStringLiteral("csv"))
		return QStringList(cells).replaceInStrings(QStringLiteral("\t"), QStringLiteral(" ")).join(QLatin1Char('\t'));
	QStringList quoted;
	for (auto cell : cells) {
		if (cell.contains(QLatin1Char(',')) || cell.contains(QLatin1Char('"')) || cell.contains(QLatin1Char('\n')) ||
		    cell.contains(QLatin1Char('\r'))) {
			cell.replace(QLatin1Char('"'), QStringLiteral("\"\""));
			cell = QLatin1Char('"') + cell + QLatin1Char('"');
		}
		quoted << cell;
	}
	return quoted.join(QLatin1Char(','));
}

QString TableRowsModel::rowText(int row, const QString& format) const
{
	if (row < 0 || row >= rowCount())
		return {};
	QStringList cells;
	for (int column = 0; column < columnCount(); ++column)
		cells << cellText(row, column);
	return joined(cells, format);
}

QString TableRowsModel::shownText(const QString& format) const
{
	QStringList headings, lines;
	for (int column = 0; column < columnCount(); ++column)
		headings << heading(column);
	lines << joined(headings, format);
	for (int row = 0; row < rowCount(); ++row)
		lines << rowText(row, format);
	return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}
} // namespace telemetry::ux
