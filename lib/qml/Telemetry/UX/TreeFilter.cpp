/*
 * @file TreeFilter.cpp
 * @brief Find in a tree: what matches by name or path, its parents, and all below it.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "TreeFilter.hpp"

#include <QtCore/QStringList>

namespace telemetry::ux {
namespace {
void configure(QSortFilterProxyModel& proxy)
{
	proxy.setRecursiveFilteringEnabled(true);
	proxy.setAutoAcceptChildRows(true);
	// Names never change while shown: a value changing below is no reason to filter again.
	proxy.setDynamicSortFilter(false);
}
} // namespace

TreeFilter::TreeFilter(QObject* parent) : QSortFilterProxyModel(parent), byName_(true)
{
	configure(*this);
}

TreeFilter::TreeFilter(int nameRole, int pathRole, QObject* parent)
    : QSortFilterProxyModel(parent), nameRole_(nameRole), pathRole_(pathRole)
{
	configure(*this);
}

void TreeFilter::findRoles()
{
	if (!byName_)
		return;
	nameRole_ = pathRole_ = childRole_ = kindRole_ = -1;
	const auto* source = sourceModel();
	if (!source)
		return;
	const auto roles = source->roleNames();
	for (auto at = roles.cbegin(); at != roles.cend(); ++at) {
		if (at.value() == "name")
			nameRole_ = at.key();
		else if (at.value() == "path")
			pathRole_ = at.key();
		else if (at.value() == "previewChild")
			childRole_ = at.key();
		else if (at.value() == "kind")
			kindRole_ = at.key();
	}
}

void TreeFilter::setSourceModel(QAbstractItemModel* model)
{
	for (const auto& connection : std::as_const(sourceConnections_))
		disconnect(connection);
	sourceConnections_.clear();
	QSortFilterProxyModel::setSourceModel(model);
	findRoles();
	if (model) {
		// After the proxy's own handlers: its rows are the source's by then.
		sourceConnections_ << connect(model, &QAbstractItemModel::modelReset, this, [this] {
			findRoles();
			recount();
		});
		// A match added under a row this find hides: the proxy shows a new
		// row's parents only when they are filtered again.
		sourceConnections_ << connect(model, &QAbstractItemModel::rowsInserted, this,
		                              [this](const QModelIndex& parent, int first, int last) {
			if (!text_.isEmpty() && parent.isValid() && !mapFromSource(parent).isValid()) {
				for (int row = first; row <= last; ++row) {
					const auto added = sourceModel()->index(row, 0, parent);
					if (hit(added) || tally(added, false) > 0) {
						beginFilterChange();
						endFilterChange(Direction::Rows);
						break;
					}
				}
			}
			recount();
		});
		sourceConnections_ << connect(model, &QAbstractItemModel::rowsRemoved, this, &TreeFilter::recount);
		sourceConnections_ << connect(model, &QAbstractItemModel::layoutChanged, this, &TreeFilter::recount);
	}
	recount();
}

bool TreeFilter::hit(const QModelIndex& source) const
{
	if (text_.isEmpty() || !source.isValid() || nameRole_ < 0)
		return false;
	if (childRole_ >= 0 && source.data(childRole_).toBool())
		return false;
	if (kindRole_ >= 0 && source.data(kindRole_).toString() == QLatin1String("category"))
		return false;
	if (source.data(nameRole_).toString().contains(text_, Qt::CaseInsensitive))
		return true;
	if (pathRole_ >= 0)
		return source.data(pathRole_).toString().contains(text_, Qt::CaseInsensitive);
	// No path role: the names from the root, dotted, as the Device tree's path.
	QStringList names;
	for (auto at = source; at.isValid(); at = at.parent())
		names.prepend(at.data(nameRole_).toString());
	return names.join(QLatin1Char('.')).contains(text_, Qt::CaseInsensitive);
}

bool TreeFilter::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
	if (text_.isEmpty())
		return true;
	return hit(sourceModel()->index(sourceRow, 0, sourceParent));
}

int TreeFilter::tally(const QModelIndex& parent, bool underMatch) const
{
	int found = 0;
	const auto* source = sourceModel();
	for (int row = 0; row < source->rowCount(parent); ++row) {
		const auto child = source->index(row, 0, parent);
		const bool match = hit(child);
		if (match && !underMatch)
			++found;
		found += tally(child, underMatch || match);
	}
	return found;
}

void TreeFilter::recount()
{
	const int counted = text_.isEmpty() || !sourceModel() || nameRole_ < 0 ? 0 : tally({}, false);
	if (counted == count_)
		return;
	count_ = counted;
	Q_EMIT countChanged();
}

int TreeFilter::setText(const QString& text)
{
	const auto trimmed = text.trimmed();
	// The same text again: the rows shown stay as they are, only counted.
	if (trimmed != text_) {
		beginFilterChange();
		text_ = trimmed;
		endFilterChange(Direction::Rows);
		Q_EMIT textChanged();
	}
	recount();
	return count_;
}

bool TreeFilter::matches(const QModelIndex& index) const
{
	return index.isValid() && index.model() == this && hit(mapToSource(index));
}

void TreeFilter::collect(const QModelIndex& parent, QModelIndexList& out) const
{
	for (int row = 0; row < rowCount(parent); ++row) {
		const auto shown = index(row, 0, parent);
		// Below a match nothing more is counted.
		if (hit(mapToSource(shown)))
			out.append(shown);
		else
			collect(shown, out);
	}
}

QModelIndexList TreeFilter::found() const
{
	QModelIndexList out;
	if (!text_.isEmpty() && sourceModel())
		collect({}, out);
	return out;
}

QString TreeFilter::nameOf(const QModelIndex& index) const
{
	if (nameRole_ < 0 || !index.isValid())
		return {};
	const auto source = index.model() == this ? mapToSource(index) : index;
	return source.isValid() && source.model() == sourceModel() ? source.data(nameRole_).toString() : QString{};
}

QString TreeFilter::namePath(const QModelIndex& index, const QString& separator) const
{
	auto at = index.isValid() && index.model() == this ? mapToSource(index) : index;
	if (nameRole_ < 0 || !at.isValid() || at.model() != sourceModel())
		return {};
	QStringList names;
	for (; at.isValid(); at = at.parent())
		names.prepend(at.data(nameRole_).toString());
	return names.join(separator);
}

QModelIndex TreeFilter::sourceAt(const QString& namePath, const QString& separator)
{
	auto* source = sourceModel();
	if (!source || nameRole_ < 0 || namePath.isEmpty())
		return {};
	QModelIndex at;
	for (const auto& name : namePath.split(separator)) {
		QModelIndex next;
		for (int row = 0;; ++row) {
			if (row >= source->rowCount(at)) {
				if (!source->canFetchMore(at))
					break;
				source->fetchMore(at);
				if (row >= source->rowCount(at))
					break;
			}
			const auto child = source->index(row, 0, at);
			if (child.data(nameRole_).toString() == name) {
				next = child;
				break;
			}
		}
		if (!next.isValid())
			return {};
		at = next;
	}
	return at;
}

QVariant TreeFilter::value(const QModelIndex& index, const QString& role) const
{
	const auto source = index.isValid() && index.model() == this ? mapToSource(index) : index;
	if (!source.isValid() || source.model() != sourceModel())
		return {};
	const auto roles = sourceModel()->roleNames();
	for (auto at = roles.cbegin(); at != roles.cend(); ++at)
		if (at.value() == role.toUtf8())
			return source.data(at.key());
	return {};
}

QModelIndex TreeFilter::mapToSource(const QModelIndex& proxyIndex) const
{
	if (proxyIndex.isValid() && proxyIndex.model() != this)
		return {};
	return QSortFilterProxyModel::mapToSource(proxyIndex);
}

QModelIndex TreeFilter::mapFromSource(const QModelIndex& sourceIndex) const
{
	if (!sourceModel() || (sourceIndex.isValid() && sourceIndex.model() != sourceModel()))
		return {};
	return QSortFilterProxyModel::mapFromSource(sourceIndex);
}
} // namespace telemetry::ux
