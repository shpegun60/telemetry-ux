/*
 * @file WatchRows.cpp
 * @brief The Watchlist's rows as they are shown: whole paths, or a tree grouped by path.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "WatchRows.hpp"

#include <QtCore/QStringView>
#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace telemetry::ux {
namespace {

// The source's roles this model passes on, by name, in Role order from NameRole.
constexpr std::array<const char*, 19> forwarded{
    "name", "status", "text", "writable", "sampleValue", "sampleExact", "editorText", "editorKind",
    "editorChoices", "editorChoicesTruncated", "typeText", "access", "leafStart", "writeScope",
    "writeRefusal", "watchKey", "member", "shownText", "format"};

const QString valuePrefix = QStringLiteral("v\x1f");
const QString nodePrefix = QStringLiteral("p\x1f");

// A watch key's order: its Field, then the value's place inside it.
quint64 keyOrder(quint64 key) noexcept
{
	// A row without a device (bit 52, WL-4c) is numbered by its Watchlist in
	// this same order when it loses one, so its siblings keep their places.
	if (key >> 52)
		return key & ((quint64{1} << 52) - 1);
	return ((key & 0xffffffffULL) << 32) | (key >> 32);
}

} // namespace

WatchRows::WatchRows(QObject* parent) : QAbstractListModel(parent)
{
	tell_.setSingleShot(true);
	tell_.setInterval(0);
	connect(&tell_, &QTimer::timeout, this, &WatchRows::tellParents);
	shape_.nodes.append(Node{});  // the root, above every group
	shape_.nodes.first().depth = -1;
}

WatchRows::~WatchRows() = default;

QVector<WatchRows::Step> WatchRows::steps(const QString& path)
{
	QVector<Step> out;
	const int n = static_cast<int>(path.size());
	int start = 0;
	int i = 0;
	while (i < n) {
		const QChar c = path.at(i);
		if (c == QLatin1Char('.')) {
			if (i > start)
				out.append({start, i});
			start = ++i;
		} else if (c == QLatin1Char('[')) {
			if (i > start)
				out.append({start, i});
			auto close = static_cast<int>(path.indexOf(QLatin1Char(']'), i));
			if (close < 0)
				close = n - 1;
			out.append({i, close + 1});
			start = i = close + 1;
		} else if (c == QLatin1Char(' ') && QStringView(path).mid(i).startsWith(QLatin1String(" / "))) {
			if (i > start)
				out.append({start, i});
			start = i += 3;
		} else {
			++i;
		}
	}
	if (start < n)
		out.append({start, n});
	return out;
}

bool WatchRows::pathLess(const QString& a, const QString& b)
{
	const auto na = a.size();
	const auto nb = b.size();
	qsizetype i = 0;
	qsizetype j = 0;
	while (i < na && j < nb) {
		if (a.at(i).isDigit() && b.at(j).isDigit()) {
			qsizetype ei = i;
			qsizetype ej = j;
			while (ei < na && a.at(ei).isDigit()) ++ei;
			while (ej < nb && b.at(ej).isDigit()) ++ej;
			// Leading zeros aside, the longer run of digits is the larger number.
			qsizetype si = i;
			qsizetype sj = j;
			while (si + 1 < ei && a.at(si) == QLatin1Char('0')) ++si;
			while (sj + 1 < ej && b.at(sj) == QLatin1Char('0')) ++sj;
			if (ei - si != ej - sj)
				return ei - si < ej - sj;
			const auto order = QStringView(a).mid(si, ei - si).compare(QStringView(b).mid(sj, ej - sj));
			if (order != 0)
				return order < 0;
			i = ei;
			j = ej;
			continue;
		}
		const auto ca = a.at(i).toCaseFolded();
		const auto cb = b.at(j).toCaseFolded();
		if (ca != cb)
			return ca < cb;
		++i;
		++j;
	}
	if ((na - i) != (nb - j))
		return na - i < nb - j;
	return a < b;  // equal but for case: a fixed order all the same
}

void WatchRows::setSource(QAbstractItemModel* source)
{
	if (source_ == source)
		return;
	for (const auto& connection : std::as_const(connections_))
		disconnect(connection);
	connections_.clear();
	source_ = source;
	findRoles();
	if (source_) {
		const auto rowsChanged = [this] {
			if (held_ > 0)
				heldRows_ = true;
			else
				refresh();
		};
		connections_ << connect(source_, &QAbstractItemModel::dataChanged, this, &WatchRows::sourceDataChanged)
		             << connect(source_, &QAbstractItemModel::rowsInserted, this, &WatchRows::sourceRowsInserted)
		             << connect(source_, &QAbstractItemModel::rowsRemoved, this, rowsChanged)
		             << connect(source_, &QAbstractItemModel::rowsMoved, this, rowsChanged)
		             << connect(source_, &QAbstractItemModel::layoutChanged, this, rowsChanged)
		             << connect(source_, &QAbstractItemModel::modelReset, this, [this] {
			                // Every value may be another: shown again, from scratch.
			                findRoles();
			                reset();
		                })
		             << connect(source_, &QObject::destroyed, this, &WatchRows::dropSource);
	}
	reset();
	Q_EMIT sourceChanged();
}

// The source went away: its rows with it.
void WatchRows::dropSource()
{
	for (const auto& connection : std::as_const(connections_))
		disconnect(connection);
	connections_.clear();
	source_ = nullptr;
	findRoles();
	reset();
	Q_EMIT sourceChanged();
}

bool WatchRows::element(const Node& node)
{
	return node.labelStart < node.path.size() && node.path.at(node.labelStart) == QLatin1Char('[');
}

void WatchRows::setTree(bool tree)
{
	if (tree_ == tree)
		return;
	// What is selected stays: a node by its values, which in the tree are
	// shown, the nodes above them opened.
	const auto values = selectedValues();
	const auto before = selected_;
	tree_ = tree;
	bool unfolded = false;
	if (tree_ && values.size() <= unfoldForSelection)
		for (const auto& id : values) {
			const auto folds = folded_.size();
			unfoldAbove(id.mid(valuePrefix.size()));
			unfolded = unfolded || folded_.size() != folds;
		}
	selected_ = values;
	if (anchor_.startsWith(nodePrefix))
		anchor_.clear();
	reset();
	if (unfolded)
		Q_EMIT foldedChanged();
	if (selected_ != before)
		Q_EMIT selectionChanged();
	Q_EMIT treeChanged();
}

void WatchRows::setSorted(bool sorted)
{
	if (sorted_ == sorted)
		return;
	sorted_ = sorted;
	if (!tree_)
		reset();
	Q_EMIT sortedChanged();
}

QStringList WatchRows::folded() const
{
	QStringList paths(folded_.cbegin(), folded_.cend());
	std::sort(paths.begin(), paths.end(), &WatchRows::pathLess);
	return paths;
}

void WatchRows::setFolded(const QStringList& paths)
{
	QSet<QString> next(paths.cbegin(), paths.cend());
	if (next == folded_)
		return;
	folded_ = std::move(next);
	if (tree_)
		refresh();
	Q_EMIT foldedChanged();
}

bool WatchRows::setExpanded(int row, bool expanded)
{
	if (row < 0 || row >= count())
		return false;
	const auto& node = shape_.nodes.at(shape_.rows.at(row));
	if (node.children.isEmpty())
		return false;
	bool changed = false;
	if (expanded) {
		changed = folded_.remove(node.path);
	} else if (!folded_.contains(node.path)) {
		folded_.insert(node.path);
		changed = true;
	}
	if (!changed)
		return true;
	refresh();
	Q_EMIT foldedChanged();
	return true;
}

void WatchRows::expandAll()
{
	if (folded_.isEmpty())
		return;
	folded_.clear();
	refresh();
	Q_EMIT foldedChanged();
}

void WatchRows::collapseAll()
{
	if (!tree_)
		return;
	QSet<QString> all;
	for (qsizetype i = 1; i < shape_.nodes.size(); ++i)
		if (!shape_.nodes.at(i).children.isEmpty())
			all.insert(shape_.nodes.at(i).path);
	if (all == folded_)
		return;
	folded_ = std::move(all);
	refresh();
	Q_EMIT foldedChanged();
}

int WatchRows::parentRow(int row) const
{
	if (refreshing_ || row < 0 || row >= count())
		return -1;
	const auto parent = shape_.nodes.at(shape_.rows.at(row)).parent;
	return parent > 0 ? rowOfNode_.value(parent, -1) : -1;
}

void WatchRows::unfoldAbove(const QString& path)
{
	const auto at = steps(path);
	for (qsizetype k = 0; k + 1 < at.size(); ++k)
		folded_.remove(path.left(at.at(k).end));
}

int WatchRows::reveal(int sourceRow)
{
	if (!settled())
		return -1;
	const int node = shape_.nodeOfSource.value(sourceRow, -1);
	if (node < 0)
		return -1;
	if (tree_) {
		const auto before = folded_.size();
		for (int p = shape_.nodes.at(node).parent; p > 0; p = shape_.nodes.at(p).parent)
			folded_.remove(shape_.nodes.at(p).path);
		if (folded_.size() != before) {
			refresh();
			Q_EMIT foldedChanged();
		}
	}
	return rowOfNode_.value(shape_.nodeOfSource.value(sourceRow, -1), -1);
}

int WatchRows::revealPath(const QString& path)
{
	if (!settled())
		return -1;
	int found = -1;
	for (qsizetype i = 1; i < shape_.nodes.size(); ++i) {
		const auto& node = shape_.nodes.at(i);
		if (node.path != path)
			continue;
		found = static_cast<int>(i);
		if (node.source >= 0)
			break;  // a value before a node of the same path
	}
	if (found < 0)
		return -1;
	if (shape_.nodes.at(found).source >= 0)
		return reveal(shape_.nodes.at(found).source);
	const auto identity = shape_.nodes.at(found).identity;
	if (tree_) {
		const auto before = folded_.size();
		for (int p = shape_.nodes.at(found).parent; p > 0; p = shape_.nodes.at(p).parent)
			folded_.remove(shape_.nodes.at(p).path);
		if (folded_.size() != before) {
			refresh();
			Q_EMIT foldedChanged();
		}
	}
	const auto row = identities_.indexOf(identity);
	return static_cast<int>(row);
}

QVariantList WatchRows::sourceRows(int row) const
{
	QVariantList out;
	if (!settled() || row < 0 || row >= count())
		return out;
	QVector<int> stack{shape_.rows.at(row)};
	while (!stack.isEmpty()) {
		const auto& node = shape_.nodes.at(stack.takeLast());
		if (node.source >= 0)
			out.append(node.source);
		for (auto child = node.children.crbegin(); child != node.children.crend(); ++child)
			stack.append(*child);
	}
	return out;
}

QString WatchRows::lines(int row, bool paths) const
{
	return linesOf(sourceRows(row), paths);
}

QString WatchRows::linesOf(const QVariantList& sources, bool paths) const
{
	QStringList out;
	for (const auto& source : sources) {
		const auto path = sourceData(source.toInt(), NameRole).toString();
		if (paths) {
			out << path;
			continue;
		}
		auto value = sourceData(source.toInt(), ShownTextRole).toString();
		if (value.isEmpty())
			value = sourceData(source.toInt(), TextRole).toString();
		out << (value.isEmpty() ? path : path + QStringLiteral(" = ") + value);
	}
	return out.join(QLatin1Char('\n'));
}

bool WatchRows::isSelected(int row) const
{
	return settled() && row >= 0 && row < identities_.size() && selected_.contains(identities_.at(row));
}

void WatchRows::select(int row, bool toggle)
{
	if (!settled() || row < 0 || row >= identities_.size())
		return;
	const auto id = identities_.at(row);
	auto next = toggle ? selected_ : QSet<QString>{};
	if (toggle && next.contains(id))
		next.remove(id);
	else
		next.insert(id);
	setSelection(std::move(next), id);
}

void WatchRows::selectRange(int row, bool add)
{
	if (!settled() || row < 0 || row >= identities_.size())
		return;
	int from = anchorRow();
	auto anchor = anchor_;
	if (from < 0) {
		from = row;
		anchor = identities_.at(row);
	}
	auto next = add ? selected_ : QSet<QString>{};
	for (int at = std::min(from, row); at <= std::max(from, row); ++at)
		next.insert(identities_.at(at));
	setSelection(std::move(next), anchor);
}

void WatchRows::selectAll()
{
	if (!settled())
		return;
	QSet<QString> next(identities_.cbegin(), identities_.cend());
	setSelection(std::move(next), anchorRow() >= 0 || identities_.isEmpty() ? anchor_ : identities_.first());
}

void WatchRows::clearSelection()
{
	setSelection({}, anchor_);
}

int WatchRows::anchorRow() const
{
	return !settled() || anchor_.isEmpty() ? -1 : static_cast<int>(identities_.indexOf(anchor_));
}

QVariantList WatchRows::selectedRows() const
{
	QVariantList out;
	if (!settled())
		return out;
	for (qsizetype row = 0; row < identities_.size(); ++row)
		if (selected_.contains(identities_.at(row)))
			out.append(static_cast<int>(row));
	return out;
}

QVariantList WatchRows::selectedSourceRows() const
{
	QVariantList out;
	if (!settled() || selected_.isEmpty())
		return out;
	QVector<bool> seen(source_ ? source_->rowCount() : 0, false);
	for (qsizetype row = 0; row < identities_.size(); ++row) {
		if (!selected_.contains(identities_.at(row)))
			continue;
		for (const auto& source : sourceRows(static_cast<int>(row))) {
			const int at = source.toInt();
			if (at >= 0 && at < seen.size() && !seen.at(at)) {
				seen[at] = true;
				out.append(at);
			}
		}
	}
	return out;
}

QString WatchRows::selectedLines(bool paths) const
{
	return linesOf(selectedSourceRows(), paths);
}

void WatchRows::setSelection(QSet<QString> next, const QString& anchor)
{
	anchor_ = anchor;
	if (next == selected_)
		return;
	std::swap(selected_, next);  // next holds what was selected
	for (qsizetype row = 0; row < identities_.size();) {
		const auto flipped = [&](qsizetype at) {
			const auto& id = identities_.at(at);
			return selected_.contains(id) != next.contains(id);
		};
		if (!flipped(row)) {
			++row;
			continue;
		}
		const auto first = row;
		while (row < identities_.size() && flipped(row))
			++row;
		Q_EMIT dataChanged(index(static_cast<int>(first)), index(static_cast<int>(row - 1)), {SelectedRole});
	}
	Q_EMIT selectionChanged();
}

void WatchRows::pruneSelection()
{
	if (selected_.isEmpty())
		return;
	const QSet<QString> shown(identities_.cbegin(), identities_.cend());
	QSet<QString> kept, hidden;
	for (const auto& id : std::as_const(selected_))
		(shown.contains(id) ? kept : hidden).insert(id);
	if (hidden.isEmpty())
		return;
	// Folds that hid everything selected select the nodes they fold: what
	// is still in the tree but not shown, by its nearest node shown.
	QVector<int> marked;
	if (kept.isEmpty()) {
		for (qsizetype i = 1; i < shape_.nodes.size(); ++i) {
			const auto& node = shape_.nodes.at(i);
			if (!hidden.contains(node.identity))
				continue;
			for (int up = node.parent; up > 0; up = shape_.nodes.at(up).parent) {
				const int row = rowOfNode_.value(up, -1);
				if (row < 0)
					continue;
				if (!kept.contains(shape_.nodes.at(up).identity)) {
					kept.insert(shape_.nodes.at(up).identity);
					marked << row;
				}
				break;
			}
		}
		if (!marked.isEmpty() && !shown.contains(anchor_))
			anchor_ = identities_.at(*std::min_element(marked.cbegin(), marked.cend()));
	}
	selected_ = std::move(kept);
	for (const int row : std::as_const(marked))
		Q_EMIT dataChanged(index(row), index(row), {SelectedRole});
	if (refreshing_)
		selectionMoved_ = true;
	else
		Q_EMIT selectionChanged();
}

void WatchRows::valuesUnder(const Shape& shape, int node, QSet<QString>& out) const
{
	const auto& at = shape.nodes.at(node);
	if (at.source >= 0)
		out.insert(at.identity);
	for (const int child : at.children)
		valuesUnder(shape, child, out);
}

QSet<QString> WatchRows::selectedValues() const
{
	QSet<QString> out;
	for (qsizetype row = 0; row < identities_.size(); ++row)
		if (selected_.contains(identities_.at(row)))
			valuesUnder(shape_, shape_.rows.at(row), out);
	return out;
}

void WatchRows::findRoles()
{
	sourceRole_.clear();
	ownRole_.clear();
	lastSource_.clear();
	lastOwn_.clear();
	pathTypesRole_ = -1;
	if (!source_)
		return;
	const auto names = source_->roleNames();
	for (auto role = names.cbegin(); role != names.cend(); ++role) {
		if (role.value() == "pathTypes") {
			pathTypesRole_ = role.key();
			continue;
		}
		for (std::size_t i = 0; i < forwarded.size(); ++i)
			if (role.value() == forwarded[i]) {
				sourceRole_.insert(NameRole + static_cast<int>(i), role.key());
				ownRole_.insert(role.key(), NameRole + static_cast<int>(i));
			}
	}
}

WatchRows::Shape WatchRows::build() const
{
	Shape shape;
	shape.nodes.append(Node{});
	shape.nodes.first().depth = -1;
	const int n = source_ ? source_->rowCount() : 0;
	shape.nodeOfSource.fill(-1, n);
	const int nameRole = sourceRole_.value(NameRole, -1);
	const int keyRole = sourceRole_.value(WatchKeyRole, -1);

	struct Entry {
		int row = 0;
		QString path;
		quint64 order = 0;
	};
	QVector<Entry> entries;
	entries.reserve(n);
	for (int row = 0; row < n; ++row) {
		const auto index = source_->index(row, 0);
		Entry entry{row, nameRole >= 0 ? index.data(nameRole).toString() : QString{}, quint64(row)};
		if (keyRole >= 0) {
			bool ok = false;
			const auto key = index.data(keyRole).toULongLong(&ok);
			if (ok && key != 0)
				entry.order = keyOrder(key);
		}
		entries.append(std::move(entry));
	}

	auto addValue = [&shape](const Entry& entry, int parent, int labelStart) {
		Node node;
		node.path = entry.path;
		node.labelStart = labelStart;
		node.parent = parent;
		node.depth = shape.nodes.at(parent).depth + 1;
		node.source = entry.row;
		node.leaves = 1;
		node.order = entry.order;
		node.identity = valuePrefix + entry.path;
		const auto at = static_cast<int>(shape.nodes.size());
		shape.nodes.append(std::move(node));
		shape.nodes[parent].children.append(at);
		shape.nodeOfSource[entry.row] = at;
	};

	if (!tree_) {
		if (sorted_)
			std::stable_sort(entries.begin(), entries.end(),
			                 [](const Entry& a, const Entry& b) { return pathLess(a.path, b.path); });
		for (const auto& entry : std::as_const(entries)) {
			const auto at = steps(entry.path);
			addValue(entry, 0, at.isEmpty() ? 0 : at.last().start);
		}
	} else {
		QHash<QString, int> parents;
		for (const auto& entry : std::as_const(entries)) {
			const auto at = steps(entry.path);
			int parent = 0;
			for (qsizetype k = 0; k + 1 < at.size(); ++k) {
				const auto path = entry.path.left(at.at(k).end);
				const auto identity = nodePrefix + path;
				auto found = parents.constFind(identity);
				if (found == parents.cend()) {
					Node node;
					node.path = path;
					node.labelStart = at.at(k).start;
					node.parent = parent;
					node.depth = shape.nodes.at(parent).depth + 1;
					node.order = std::numeric_limits<quint64>::max();
					node.identity = identity;
					const auto made = static_cast<int>(shape.nodes.size());
					shape.nodes.append(std::move(node));
					shape.nodes[parent].children.append(made);
					found = parents.insert(identity, made);
				}
				parent = found.value();
			}
			addValue(entry, parent, at.isEmpty() ? 0 : at.last().start);
		}
		// A child is always made after its parent: from the last node back,
		// each has its whole subtree counted when its parent takes it.
		for (auto i = shape.nodes.size() - 1; i > 0; --i) {
			const auto& node = shape.nodes.at(i);
			auto& parent = shape.nodes[node.parent];
			parent.order = std::min(parent.order, node.order);
			if (node.parent > 0)
				parent.leaves += node.leaves;
		}
		for (auto& node : shape.nodes)
			std::stable_sort(node.children.begin(), node.children.end(), [&shape](int a, int b) {
				return shape.nodes.at(a).order < shape.nodes.at(b).order;
			});
	}
	walk(shape, 0, shape.rows);
	return shape;
}

void WatchRows::walk(const Shape& shape, int node, QVector<int>& rows) const
{
	for (const int child : shape.nodes.at(node).children) {
		rows.append(child);
		const auto& next = shape.nodes.at(child);
		if (!next.children.isEmpty() && !folded_.contains(next.path))
			walk(shape, child, rows);
	}
}

void WatchRows::reset()
{
	const int before = count();
	beginResetModel();
	shape_ = build();
	identities_.clear();
	identities_.reserve(shape_.rows.size());
	for (const int node : std::as_const(shape_.rows))
		identities_.append(shape_.nodes.at(node).identity);
	endResetModel();
	dirtyParents_.clear();
	rowOfNode_.fill(-1, shape_.nodes.size());
	for (qsizetype row = 0; row < shape_.rows.size(); ++row)
		rowOfNode_[shape_.rows.at(row)] = static_cast<int>(row);
	setCount(before);
	pruneSelection();
}

void WatchRows::refresh()
{
	if (refreshing_) {
		again_ = true;
		return;
	}
	refreshing_ = true;
	do {
		again_ = false;
		apply(build());
	} while (again_);
	refreshing_ = false;
	if (std::exchange(selectionMoved_, false))
		Q_EMIT selectionChanged();
}

namespace {
// Moves len items at from to before dest (dest counted before the move).
template <typename List>
void moveBlock(List& list, qsizetype from, qsizetype len, qsizetype dest)
{
	const auto block = list.mid(from, len);
	list.remove(from, len);
	const auto at = dest > from ? dest - len : dest;
	for (qsizetype i = 0; i < len; ++i)
		list.insert(at + i, block.at(i));
}
} // namespace

void WatchRows::apply(Shape next)
{
	QVector<QString> ids;
	ids.reserve(next.rows.size());
	QHash<QString, int> where;
	for (qsizetype j = 0; j < next.rows.size(); ++j) {
		ids.append(next.nodes.at(next.rows.at(j)).identity);
		where.insert(ids.last(), static_cast<int>(j));
	}
	const int before = count();
	// Where each row shown now goes; -1 when it goes away.
	QVector<int> to(identities_.size(), -1);
	QVector<bool> kept(ids.size(), false);
	for (qsizetype i = 0; i < identities_.size(); ++i) {
		const auto found = where.constFind(identities_.at(i));
		if (found == where.cend())
			continue;
		to[i] = found.value();
		kept[found.value()] = true;
	}
	// The new nodes first, the rows that stay pointing at theirs; a row about
	// to go reads as nothing until it is gone.
	QVector<int> rows(identities_.size(), -1);
	for (qsizetype i = 0; i < identities_.size(); ++i)
		if (to.at(i) >= 0)
			rows[i] = next.rows.at(to.at(i));
	shape_.nodes = std::move(next.nodes);
	shape_.nodeOfSource = std::move(next.nodeOfSource);
	shape_.rows = std::move(rows);
	dirtyParents_.clear();

	for (auto i = to.size() - 1; i >= 0;) {
		if (to.at(i) >= 0) {
			--i;
			continue;
		}
		const auto end = i;
		while (i >= 0 && to.at(i) < 0)
			--i;
		const auto first = i + 1;
		beginRemoveRows({}, static_cast<int>(first), static_cast<int>(end));
		shape_.rows.remove(first, end - first + 1);
		identities_.remove(first, end - first + 1);
		to.remove(first, end - first + 1);
		endRemoveRows();
	}
	place(to);
	for (qsizetype j = 0; j < ids.size();) {
		if (kept.at(j)) {
			++j;
			continue;
		}
		const auto first = j;
		while (j < ids.size() && !kept.at(j))
			++j;
		beginInsertRows({}, static_cast<int>(first), static_cast<int>(j - 1));
		for (auto k = first; k < j; ++k) {
			shape_.rows.insert(k, next.rows.at(k));
			identities_.insert(k, ids.at(k));
		}
		endInsertRows();
	}

	rowOfNode_.fill(-1, shape_.nodes.size());
	for (qsizetype row = 0; row < shape_.rows.size(); ++row)
		rowOfNode_[shape_.rows.at(row)] = static_cast<int>(row);
	// The rows that stayed may have moved in the source, gained or lost rows
	// under them, or been folded: their own roles, and a node's summary.
	if (count() > 0)
		Q_EMIT dataChanged(index(0), index(count() - 1),
		                   {SourceRowRole, DepthRole, BranchRole, ExpandedRole, NodeRole, LeavesRole, NestedRole,
		                    TextRole, ShownTextRole, StatusRole, TypeTextRole, AccessRole});
	setCount(before);
	// A row that went, or that a fold hid, leaves the selection.
	pruneSelection();
}

void WatchRows::place(QVector<int>& to)
{
	const auto n = to.size();
	// The longest run already in order stays where it is.
	QVector<int> tails, tailAt, previous(n, -1);
	for (qsizetype i = 0; i < n; ++i) {
		const auto at = std::lower_bound(tails.begin(), tails.end(), to.at(i)) - tails.begin();
		if (at > 0)
			previous[i] = tailAt.at(at - 1);
		if (at == tails.size()) {
			tails.append(to.at(i));
			tailAt.append(static_cast<int>(i));
		} else {
			tails[at] = to.at(i);
			tailAt[at] = static_cast<int>(i);
		}
	}
	if (tails.size() == n)
		return;
	QVector<bool> stays(n, false);
	for (int i = tailAt.isEmpty() ? -1 : tailAt.last(); i >= 0; i = previous.at(i))
		stays[i] = true;
	QVector<int> sorted = to;
	std::sort(sorted.begin(), sorted.end());
	QHash<int, int> rank;
	for (qsizetype r = 0; r < sorted.size(); ++r)
		rank.insert(sorted.at(r), static_cast<int>(r));
	QVector<int> moving;
	for (qsizetype i = 0; i < n; ++i)
		if (!stays.at(i))
			moving.append(to.at(i));
	std::sort(moving.begin(), moving.end());
	// In their new order, each run of rows that move together goes right
	// after the row that comes before it; that row is in its place already.
	for (qsizetype k = 0; k < moving.size();) {
		const auto from = to.indexOf(moving.at(k));
		qsizetype len = 1;
		while (k + len < moving.size() && from + len < n && to.at(from + len) == moving.at(k + len) &&
		       rank.value(moving.at(k + len)) == rank.value(moving.at(k + len - 1)) + 1)
			++len;
		const int r = rank.value(moving.at(k));
		const qsizetype dest = r > 0 ? to.indexOf(sorted.at(r - 1)) + 1 : 0;
		if (dest != from && dest != from + len) {
			beginMoveRows({}, static_cast<int>(from), static_cast<int>(from + len - 1), {}, static_cast<int>(dest));
			moveBlock(to, from, len, dest);
			moveBlock(shape_.rows, from, len, dest);
			moveBlock(identities_, from, len, dest);
			endMoveRows();
		}
		k += len;
	}
}

void WatchRows::setCount(int before)
{
	if (count() != before)
		Q_EMIT countChanged();
}

void WatchRows::sourceRowsInserted(const QModelIndex& parent, int first, int last)
{
	if (parent.isValid())
		return;
	// A ListModel gains its roles as rows bring them.
	findRoles();
	// What is added is shown: the nodes above it open.
	const int nameRole = sourceRole_.value(NameRole, -1);
	bool unfolded = false;
	if (tree_ && nameRole >= 0 && !folded_.isEmpty()) {
		const auto before = folded_.size();
		for (int row = first; row <= last; ++row)
			unfoldAbove(source_->index(row, 0).data(nameRole).toString());
		unfolded = folded_.size() != before;
	}
	if (held_ > 0) {
		heldRows_ = true;
		heldFolds_ = heldFolds_ || unfolded;
		return;
	}
	refresh();
	if (unfolded)
		Q_EMIT foldedChanged();
}

void WatchRows::endChanges()
{
	if (held_ == 0 || --held_ > 0)
		return;
	const bool rows = std::exchange(heldRows_, false);
	const bool data = std::exchange(heldData_, false);
	const bool folds = std::exchange(heldFolds_, false);
	// A held reading may have renamed or renumbered a row (a reconnect
	// rebinds them): the rows are rebuilt whatever came, then every role of
	// every row is told, for what a provider said about them while held.
	if (rows || data) {
		refresh();
		if (count() > 0)
			Q_EMIT dataChanged(index(0), index(count() - 1));
	}
	if (folds)
		Q_EMIT foldedChanged();
}

void WatchRows::sourceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QList<int>& roles)
{
	if (topLeft.parent().isValid() || !source_)
		return;
	if (held_ > 0) {
		heldData_ = true;
		return;
	}
	if (refreshing_) {
		again_ = true;
		return;
	}
	const int nameRole = sourceRole_.value(NameRole, -1);
	// What the reading moved: this model's roles, a node's summary (texts)
	// and a Field's outcome (status, access, type).
	bool texts = roles.isEmpty();
	bool outcome = roles.isEmpty();
	bool formats = roles.isEmpty();
	QList<int> own;
	if (!roles.isEmpty()) {
		if (roles != lastSource_) {
			lastSource_ = roles;
			lastOwn_.clear();
			lastTexts_ = lastOutcome_ = lastFormat_ = false;
			for (const int role : roles) {
				if (role == nameRole) {
					lastSource_.clear();
					refresh();  // a new path: a new place in the tree
					return;
				}
				lastOutcome_ = lastOutcome_ || role == pathTypesRole_;
				const auto found = ownRole_.constFind(role);
				if (found == ownRole_.cend())
					continue;
				lastOwn_.append(found.value());
				lastTexts_ = lastTexts_ || found.value() == TextRole || found.value() == ShownTextRole;
				lastOutcome_ = lastOutcome_ || found.value() == StatusRole || found.value() == AccessRole ||
				               found.value() == TypeTextRole;
				lastFormat_ = lastFormat_ || found.value() == FormatRole;
			}
		}
		own = lastOwn_;
		texts = lastTexts_;
		outcome = lastOutcome_;
		formats = lastFormat_;
		if (own.isEmpty() && !texts && !outcome && !formats)
			return;
	} else if (nameRole >= 0) {
		// Every role at once may hide a new path, or in the tree a new place
		// among its siblings: a row that found its device again (WL-4c).
		const int keyRole = tree_ ? sourceRole_.value(WatchKeyRole, -1) : -1;
		for (int source = topLeft.row(); source <= bottomRight.row(); ++source) {
			const int node = shape_.nodeOfSource.value(source, -1);
			const auto index = source_->index(source, 0);
			if (node < 0 || index.data(nameRole).toString() != shape_.nodes.at(node).path) {
				refresh();
				return;
			}
			const auto key = keyRole >= 0 ? index.data(keyRole).toULongLong() : 0;
			if (key != 0 && keyOrder(key) != shape_.nodes.at(node).order) {
				refresh();
				return;
			}
		}
	}
	QVector<int> shown;
	for (int source = topLeft.row(); source <= bottomRight.row(); ++source) {
		const int node = shape_.nodeOfSource.value(source, -1);
		if (node < 0)
			continue;
		const int row = rowOfNode_.value(node, -1);
		if (row >= 0)
			shown.append(row);
		if (!tree_)
			continue;
		// A folded parent shows its children's values; a node shows the
		// outcome of its first value. A group shows only its count.
		const int parent = shape_.nodes.at(node).parent;
		if (texts && parent > 0 && shape_.nodes.at(parent).depth > 0 &&
		    folded_.contains(shape_.nodes.at(parent).path))
			dirtyParents_.insert(parent);
		if (outcome)
			for (int p = parent; p > 0 && shape_.nodes.at(p).depth > 0 && firstLeaf(shape_.nodes.at(p)) == source;
			     p = shape_.nodes.at(p).parent)
				dirtyParents_.insert(p);
		// Every node above shows the format its values share.
		if (formats)
			for (int p = parent; p > 0; p = shape_.nodes.at(p).parent)
				dirtyParents_.insert(p);
	}
	if (!own.isEmpty() || roles.isEmpty()) {
		std::sort(shown.begin(), shown.end());
		for (qsizetype i = 0; i < shown.size();) {
			auto j = i + 1;
			while (j < shown.size() && shown.at(j) == shown.at(j - 1) + 1)
				++j;
			Q_EMIT dataChanged(index(shown.at(i)), index(shown.at(j - 1)), own);
			i = j;
		}
	}
	if (!dirtyParents_.isEmpty() && !tell_.isActive())
		tell_.start();
}

void WatchRows::tellParents()
{
	const auto dirty = std::exchange(dirtyParents_, {});
	for (const int node : dirty) {
		const int row = rowOfNode_.value(node, -1);
		if (row >= 0)
			Q_EMIT dataChanged(index(row), index(row),
			                   {TextRole, ShownTextRole, StatusRole, TypeTextRole, AccessRole, FormatRole});
	}
}

int WatchRows::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : count();
}

QVariant WatchRows::sourceData(int sourceRow, Role role) const
{
	const int sourceRole = sourceRole_.value(role, -1);
	QVariant value;
	if (source_ && sourceRole >= 0)
		value = source_->index(sourceRow, 0).data(sourceRole);
	if (!value.isValid() && (role == NameRole || role == StatusRole || role == TextRole))
		return QString{};
	return value;
}

int WatchRows::firstLeaf(const Node& node) const
{
	// The first value a device holds speaks for the Field: one without a device
	// (WL-4c) only when none does, so a member the device lacks never makes its
	// Field say "Not on device" or lose its type and access.
	const int keyRole = sourceRole_.value(WatchKeyRole, -1);
	int first = -1;
	QVector<const Node*> stack{&node};
	while (!stack.isEmpty()) {
		const Node* at = stack.takeLast();
		if (at->source >= 0) {
			if (first < 0)
				first = at->source;
			if (keyRole < 0 || !source_ || (source_->index(at->source, 0).data(keyRole).toULongLong() >> 52) == 0)
				return at->source;
			continue;
		}
		for (auto child = at->children.crbegin(); child != at->children.crend(); ++child)
			stack.append(&shape_.nodes.at(*child));
	}
	return first;
}

QString WatchRows::summary(const Node& node) const
{
	QStringList parts;
	for (const int child : node.children) {
		if (parts.size() == 4) {
			parts.append(QStringLiteral("…"));
			break;
		}
		const auto& at = shape_.nodes.at(child);
		QString value;
		if (at.source >= 0) {
			const auto shown = sourceData(at.source, ShownTextRole);
			value = shown.isValid() ? shown.toString() : sourceData(at.source, TextRole).toString();
			if (value.isEmpty())
				value = QStringLiteral("–");
		} else {
			const bool elements = !at.children.isEmpty() && element(shape_.nodes.at(at.children.first()));
			value = elements ? QStringLiteral("[…]") : QStringLiteral("{…}");
		}
		parts.append(at.path.mid(at.labelStart) + QLatin1Char(' ') + value);
	}
	return QLatin1Char('{') + parts.join(QStringLiteral(", ")) + QLatin1Char('}');
}

QVariant WatchRows::nodeData(const Node& node, Role role) const
{
	switch (role) {
		case NameRole:
			return node.path;
		case LeafStartRole:
			return node.labelStart;
		case TextRole:
			return node.depth > 0 && folded_.contains(node.path) ? summary(node) : QString{};
		case StatusRole: {
			// A Field's values are read together: one outcome, for the Field
			// and every node inside it (the panel words it on the Field).
			if (node.depth < 1)
				return QString{};
			const int leaf = firstLeaf(node);
			return leaf >= 0 ? sourceData(leaf, StatusRole) : QVariant(QString{});
		}
		case AccessRole: {
			if (node.depth != 1)
				return QString{};
			const int leaf = firstLeaf(node);
			return leaf >= 0 ? sourceData(leaf, AccessRole) : QVariant{};
		}
		case TypeTextRole: {
			const int leaf = firstLeaf(node);
			if (node.depth < 1 || leaf < 0 || pathTypesRole_ < 0 || !source_)
				return QString{};
			const auto types = source_->index(leaf, 0).data(pathTypesRole_).toStringList();
			return node.depth - 1 < types.size() ? types.at(node.depth - 1) : QString{};
		}
		case WritableRole:
		case SampleExactRole:
		case EditorChoicesTruncatedRole:
			return false;
		case EditorKindRole:
		case EditorTextRole:
		case MemberRole:
		case WriteRefusalRole:
			return QString{};
		case FormatRole: {
			QString shared;
			bool first = true;
			QVector<int> stack{static_cast<int>(&node - shape_.nodes.constData())};
			while (!stack.isEmpty()) {
				const auto& at = shape_.nodes.at(stack.takeLast());
				if (at.source >= 0) {
					const auto own = sourceData(at.source, FormatRole).toString();
					if (!first && own != shared)
						return QStringLiteral("mixed");
					shared = own;
					first = false;
				}
				for (const int child : at.children)
					stack.append(child);
			}
			return shared;
		}
		case ShownTextRole:
			return nodeData(node, TextRole);
		default:
			return {};
	}
}

QVariant WatchRows::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0 || index.row() >= count())
		return {};
	const int at = shape_.rows.at(index.row());
	if (at <= 0 || at >= shape_.nodes.size())
		return {};
	const auto& node = shape_.nodes.at(at);
	switch (role) {
		case Qt::DisplayRole:
			return node.source >= 0 ? sourceData(node.source, NameRole) : QVariant(node.path);
		case SourceRowRole:
			return node.source;
		case DepthRole:
			return tree_ ? node.depth : 0;
		case BranchRole:
			return !node.children.isEmpty();
		case ExpandedRole:
			return !node.children.isEmpty() && !folded_.contains(node.path);
		case NodeRole:
			if (node.source >= 0)
				return QStringLiteral("value");
			if (node.depth == 0)
				return QStringLiteral("group");
			if (!node.children.isEmpty() && element(shape_.nodes.at(node.children.first())))
				return QStringLiteral("array");
			return element(node) ? QStringLiteral("element") : QStringLiteral("struct");
		case LeavesRole:
			return node.leaves;
		case NestedRole:
			return tree_ && node.depth >= 2;
		case SelectedRole:
			return selected_.contains(node.identity);
		default:
			break;
	}
	if (role < NameRole || role > FormatRole)
		return {};
	if (node.source < 0)
		return nodeData(node, static_cast<Role>(role));
	if (role == LeafStartRole) {
		// The list keeps the source's own, where a row has one.
		const auto own = tree_ ? QVariant{} : sourceData(node.source, LeafStartRole);
		return own.isValid() ? own : QVariant(node.labelStart);
	}
	return sourceData(node.source, static_cast<Role>(role));
}

QHash<int, QByteArray> WatchRows::roleNames() const
{
	QHash<int, QByteArray> names;
	for (std::size_t i = 0; i < forwarded.size(); ++i)
		names.insert(NameRole + static_cast<int>(i), forwarded[i]);
	names.insert(SourceRowRole, "sourceRow");
	names.insert(DepthRole, "depth");
	names.insert(BranchRole, "branch");
	names.insert(ExpandedRole, "expanded");
	names.insert(NodeRole, "node");
	names.insert(LeavesRole, "leaves");
	names.insert(NestedRole, "nested");
	names.insert(SelectedRole, "selected");
	return names;
}

} // namespace telemetry::ux
