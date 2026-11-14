/*
 * @file FileTextModels.cpp
 * @brief A text file as numbered lines, and a CSV table sorted and filtered, for the viewer.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "FileTextModels.hpp"
#include "FileContent.hpp"
#include <QtCore/QStringList>
#include <QtCore/QPointer>
#include <algorithm>
#include <cmath>
#include <optional>

namespace telemetry::ux {
namespace {
struct IntegerKey {
	bool negative;
	quint64 magnitude;
};

// Recover exact 64-bit integer order only when the cached double keys tie.
// Signed magnitude also covers U64 without converting it through S64 or double.
std::optional<IntegerKey> integerKey(const QString& text)
{
	bool ok = false;
	const auto signedValue = text.toLongLong(&ok);
	if (ok)
		return IntegerKey{signedValue < 0, signedValue < 0 ? quint64(-(signedValue + 1)) + 1
		                                                : quint64(signedValue)};
	const auto unsignedValue = text.toULongLong(&ok);
	if (ok)
		return IntegerKey{false, unsignedValue};
	return std::nullopt;
}

// Natural order, the same on every platform and locale: runs of digits
// compare as numbers by their digits' values ("item 2" before "item 10", in
// any script's digits), the rest ignoring case.
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
			while (startA + 1 < endA && a.at(startA).digitValue() == 0)
				++startA;
			while (startB + 1 < endB && b.at(startB).digitValue() == 0)
				++startB;
			const auto lengthA = endA - startA, lengthB = endB - startB;
			if (lengthA != lengthB)
				return lengthA < lengthB ? -1 : 1;
			for (qsizetype k = 0; k < lengthA; ++k) {
				const int x = a.at(startA + k).digitValue(), y = b.at(startB + k).digitValue();
				if (x != y)
					return x < y ? -1 : 1;
			}
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
	positions_.clear();
	findText_.clear();
	current_ = -1;
	capped_ = false;
	const auto size = text_.size();
	const auto addLine = [&](qsizetype start, qsizetype end) {
		starts_.push_back(start);
		lengths_.push_back(static_cast<int>(end - start));
		// Its width in columns, as far as it is shown.
		int columns = 0;
		const auto shownEnd = std::min(end, start + maxShownChars);
		for (auto i = start; i < shownEnd; ++i)
			columns = text_.at(i) == QLatin1Char('\t') ? (columns / tabColumns + 1) * tabColumns : columns + 1;
		longest_ = std::max(longest_, columns);
	};
	qsizetype start = 0;
	for (qsizetype i = 0; i < size; ++i) {
		const auto c = text_.at(i);
		if (c != QLatin1Char('\n') && c != QLatin1Char('\r'))
			continue;
		addLine(start, i);
		if (c == QLatin1Char('\r') && i + 1 < size && text_.at(i + 1) == QLatin1Char('\n'))
			++i;
		start = i + 1;
	}
	// The last line, unless the text ended with a line break (or is empty).
	if (start < size || size == 0)
		addLine(start, size);
	endResetModel();
	Q_EMIT textChanged();
	Q_EMIT matchesChanged();
}

void TextLinesModel::clear()
{
	setText({});
}

int TextLinesModel::lineAt(qsizetype position) const noexcept
{
	const auto after = std::upper_bound(starts_.begin(), starts_.end(), position);
	return static_cast<int>(after - starts_.begin()) - 1;
}

int TextLinesModel::matchIndex() const noexcept
{
	if (current_ < 0)
		return 0;
	const auto at = std::lower_bound(positions_.begin(), positions_.end(), current_);
	return at != positions_.end() && *at == current_ ? static_cast<int>(at - positions_.begin()) + 1 : 0;
}

void TextLinesModel::touchLine(int line)
{
	if (line < 0 || line >= lineCount())
		return;
	const auto at = index(line);
	Q_EMIT dataChanged(at, at, {MatchesRole, CurrentRole});
}

void TextLinesModel::touchAll()
{
	if (lineCount() > 0)
		Q_EMIT dataChanged(index(0), index(lineCount() - 1), {MatchesRole, CurrentRole});
}

QString TextLinesModel::find(const QString& text, bool caseSensitive, bool forward, int fromLine)
{
	if (text.isEmpty()) {
		clearFind();
		return {};
	}
	const auto sensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
	if (text != findText_ || caseSensitive != findCase_) {
		// Enumerate non-overlapping matches once, to the cap.
		positions_.clear();
		capped_ = false;
		const auto step = std::max<qsizetype>(1, text.size());
		for (auto at = text_.indexOf(text, 0, sensitivity); at >= 0; at = text_.indexOf(text, at + step, sensitivity)) {
			if (static_cast<int>(positions_.size()) == maxMatches) {
				capped_ = true;
				break;
			}
			positions_.push_back(at);
		}
		findText_ = text;
		findCase_ = caseSensitive;
		current_ = -1;
		touchAll();
	}
	if (positions_.empty()) {
		Q_EMIT matchesChanged();
		return tr("Not found");
	}
	const auto previousLine = matchLine();
	const auto line = std::clamp(fromLine, 0, lineCount() - 1);
	// From the current match while the reader is still on its line; else from the reader's line.
	const bool fromCurrent = current_ >= 0 && lineAt(current_) == line;
	const auto step = text.size();
	// Walk the same matches that are counted/highlighted. Beyond the bounded
	// index, continue their non-overlapping enumeration without caching more.
	const auto nextMatch = [&](qsizetype from) {
		const auto at = std::lower_bound(positions_.begin(), positions_.end(), from);
		if (at != positions_.end())
			return *at;
		if (!capped_)
			return qsizetype{-1};
		auto tail = text_.indexOf(text, positions_.back() + step, sensitivity);
		while (tail >= 0 && tail < from)
			tail = text_.indexOf(text, tail + step, sensitivity);
		return tail;
	};
	const auto previousMatch = [&](qsizetype from) {
		const auto after = std::upper_bound(positions_.begin(), positions_.end(), from);
		auto found = after == positions_.begin() ? qsizetype{-1} : *(after - 1);
		if (capped_ && from > positions_.back())
			for (auto tail = text_.indexOf(text, positions_.back() + step, sensitivity); tail >= 0 && tail <= from;
			     tail = text_.indexOf(text, tail + step, sensitivity))
				found = tail;
		return found;
	};
	QString message;
	qsizetype found = -1;
	if (forward) {
		const auto from = fromCurrent ? current_ + step : starts_[static_cast<std::size_t>(line)];
		found = nextMatch(from);
		if (found < 0) {
			found = positions_.front();
			message = tr("Wrapped to the start");
		}
	} else {
		const auto lineEnd = starts_[static_cast<std::size_t>(line)] + lengths_[static_cast<std::size_t>(line)];
		const auto from = fromCurrent ? current_ - 1 : lineEnd - text.size();
		found = previousMatch(from);
		if (found < 0) {
			found = previousMatch(text_.size());
			message = tr("Wrapped to the end");
		}
	}
	current_ = found;
	touchLine(previousLine);
	touchLine(matchLine());
	Q_EMIT matchesChanged();
	return message;
}

void TextLinesModel::clearFind()
{
	const bool had = !findText_.isEmpty();
	positions_.clear();
	findText_.clear();
	current_ = -1;
	capped_ = false;
	if (had)
		touchAll();
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
	const auto start = starts_[static_cast<std::size_t>(line)];
	const auto length = lengths_[static_cast<std::size_t>(line)];
	const auto shown = std::min(length, maxShownChars);
	switch (role) {
		case NumberRole:
			return line + 1;
		case Qt::DisplayRole:
		case TextRole:
			return text_.mid(start, shown);
		case HiddenRole:
			return length - shown;
		case MatchesRole: {
			// Only the lines asked for - the ones on screen - are searched.
			QVariantList ranges;
			if (findText_.isEmpty())
				return ranges;
			const QStringView view(text_.constData() + start, shown);
			const auto sensitivity = findCase_ ? Qt::CaseSensitive : Qt::CaseInsensitive;
			const auto step = std::max<qsizetype>(1, findText_.size());
			for (auto at = view.indexOf(findText_, 0, sensitivity); at >= 0 && ranges.size() < maxDrawnPerLine;
			     at = view.indexOf(findText_, at + step, sensitivity))
				ranges.push_back(QVariantList{static_cast<int>(at), static_cast<int>(findText_.size())});
			return ranges;
		}
		case CurrentRole:
			return current_ >= start && current_ < start + shown ? static_cast<int>(current_ - start) : -1;
		default:
			return {};
	}
}

QHash<int, QByteArray> TextLinesModel::roleNames() const
{
	return {{NumberRole, "number"},
	        {TextRole, "line"},
	        {HiddenRole, "hidden"},
	        {MatchesRole, "matches"},
	        {CurrentRole, "currentMatch"}};
}

// ---- Table -----------------------------------------------------------------

TableRowsModel::TableRowsModel(FileTableModel* source, QObject* parent) : QSortFilterProxyModel(parent), table_(source)
{
	setSourceModel(source);
	setDynamicSortFilter(false);
	// The file read again (Reload): the same filter and sort, while its column is still there.
	connect(source, &QAbstractItemModel::modelReset, this, [this] {
		clearFind();
		if (sorted_ >= table_->columnCount()) {
			sorted_ = -1;
			descending_ = false;
		}
		readKeys();
		sort(sorted_, descending_ ? Qt::DescendingOrder : Qt::AscendingOrder);
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

void TableRowsModel::readKeys()
{
	numberKeys_.clear();
	textKeys_.clear();
	if (sorted_ < 0 || !table_)
		return;
	const auto rows = table_->rowCount();
	textKeys_.resize(static_cast<std::size_t>(rows));
	if (numeric(sorted_))
		numberKeys_.resize(static_cast<std::size_t>(rows));
	for (int row = 0; row < rows; ++row) {
		auto text = table_->cell(row, sorted_).trimmed();
		if (!numberKeys_.empty()) {
			bool ok = false;
			const double value = text.toDouble(&ok);
			// An empty cell or NaN is no number: NaN as the key says so.
			numberKeys_[static_cast<std::size_t>(row)] = ok ? value : std::nan("");
		}
		textKeys_[static_cast<std::size_t>(row)] = std::move(text);
	}
}

void TableRowsModel::sortColumn(int column, bool descending)
{
	sorted_ = column;
	descending_ = column >= 0 && descending;
	clearFind();
	readKeys();
	sort(sorted_, descending_ ? Qt::DescendingOrder : Qt::AscendingOrder);
	Q_EMIT sortChanged();
}

void TableRowsModel::clearSort()
{
	sortColumn(-1, false);
}

bool TableRowsModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
	const auto l = static_cast<std::size_t>(left.row()), r = static_cast<std::size_t>(right.row());
	if (r >= textKeys_.size() || l >= textKeys_.size())
		return left.row() < right.row();
	const auto& a = textKeys_[l];
	const auto& b = textKeys_[r];
	if (!numberKeys_.empty()) {
		const double x = numberKeys_[l], y = numberKeys_[r];
		// An empty cell or NaN is no number: last, whichever way.
		const bool okA = !std::isnan(x), okB = !std::isnan(y);
		if (okA != okB)
			return descending_ ? !okA : okA;
		if (okA && x != y)
			return x < y;
		if (okA && a != b) {
			const auto integerA = integerKey(a), integerB = integerKey(b);
			// Group integer spellings before other equal-double spellings so
			// mixing integers and scientific notation keeps a strict ordering.
			if (integerA.has_value() != integerB.has_value())
				return integerA.has_value();
			if (integerA && integerB) {
				if (integerA->negative != integerB->negative)
					return integerA->negative;
				if (integerA->magnitude != integerB->magnitude)
					return integerA->negative ? integerA->magnitude > integerB->magnitude
					                          : integerA->magnitude < integerB->magnitude;
			}
		}
	} else if (a.isEmpty() != b.isEmpty()) {
		return descending_ ? a.isEmpty() : b.isEmpty();
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
	if (trimmed == filter_)
		return rowCount();
	clearFind();
	filter_ = trimmed;
	refilter();
	Q_EMIT shownChanged();
	return rowCount();
}

void TableRowsModel::refilter()
{
	// The shown rows made again in one pass, the sort with them - not removed
	// and inserted interval by interval, which in a large table is slow.
	invalidate();
}

bool TableRowsModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
	if (filter_.isEmpty())
		return true;
	if (sourceParent.isValid() || !table_)
		return false;
	for (int column = 0; column < table_->columnCount(); ++column)
		if (table_->cell(sourceRow, column).contains(filter_, Qt::CaseInsensitive))
			return true;
	return false;
}

QVariantMap TableRowsModel::find(const QString& text, bool forward, int row, int column)
{
	const auto revision = ++findRevision_;
	QPointer<TableRowsModel> alive(this);
	const auto none = [](const QString& message) {
		return QVariantMap{{QStringLiteral("row"), -1}, {QStringLiteral("column"), -1}, {QStringLiteral("message"), message}};
	};
	if (text.isEmpty()) {
		clearFind();
		return none({});
	}
	if (text != findText_) {
		matches_.clear();
		capped_ = false;
		for (int r = 0; r < rowCount() && !capped_; ++r)
			for (int c = 0; c < columnCount(); ++c)
				if (index(r, c).data().toString().contains(text, Qt::CaseInsensitive)) {
					if (static_cast<int>(matches_.size()) == maxMatches) {
						capped_ = true;
						break;
					}
					matches_.emplace_back(r, c);
				}
		findText_ = text;
		current_ = -1;
	}
	if (matches_.empty()) {
		current_ = -1;
		Q_EMIT matchesChanged();
		return none(tr("Not found"));
	}
	QString message;
	const auto count = static_cast<int>(matches_.size());
	const std::pair<int, int> from{std::max(0, row), std::max(0, column)};
	// From the current match while the reader is still on it; else from the reader's cell.
	const bool fromCurrent = current_ >= 0 && matches_[static_cast<std::size_t>(current_)] == from;
	if (!fromCurrent) {
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
	// A direct receiver can clear Find, reload/filter its source, start another
	// Find or delete this model. Never inspect retired state or return old cells.
	if (!alive || revision != findRevision_)
		return none({});
	const auto& at = matches_[static_cast<std::size_t>(current_)];
	return {{QStringLiteral("row"), at.first}, {QStringLiteral("column"), at.second}, {QStringLiteral("message"), message}};
}

void TableRowsModel::clearFind()
{
	++findRevision_;
	matches_.clear();
	findText_.clear();
	current_ = -1;
	capped_ = false;
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
