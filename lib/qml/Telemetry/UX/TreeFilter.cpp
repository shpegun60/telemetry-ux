/*
 * @file TreeFilter.cpp
 * @brief Find in a device-file tree: what matches by name or path, its parents, and all below it.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "TreeFilter.hpp"

namespace telemetry::ux {
TreeFilter::TreeFilter(int nameRole, int pathRole, QObject* parent)
    : QSortFilterProxyModel(parent), nameRole_(nameRole), pathRole_(pathRole)
{
	setRecursiveFilteringEnabled(true);
	setAutoAcceptChildRows(true);
	// Names never change while shown: a value changing below is no reason to filter again.
	setDynamicSortFilter(false);
}

bool TreeFilter::hit(const QModelIndex& source) const
{
	if (text_.isEmpty() || !source.isValid())
		return false;
	return source.data(nameRole_).toString().contains(text_, Qt::CaseInsensitive) ||
	       source.data(pathRole_).toString().contains(text_, Qt::CaseInsensitive);
}

bool TreeFilter::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
	if (text_.isEmpty())
		return true;
	return hit(sourceModel()->index(sourceRow, 0, sourceParent));
}

int TreeFilter::count(const QModelIndex& parent, bool underMatch) const
{
	int found = 0;
	const auto* source = sourceModel();
	for (int row = 0; row < source->rowCount(parent); ++row) {
		const auto child = source->index(row, 0, parent);
		const bool match = hit(child);
		if (match && !underMatch)
			++found;
		found += count(child, underMatch || match);
	}
	return found;
}

int TreeFilter::setText(const QString& text)
{
	const auto trimmed = text.trimmed();
	// The same text again: the rows shown stay as they are, only counted.
	if (trimmed == text_)
		return text_.isEmpty() || !sourceModel() ? 0 : count({}, false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
	beginFilterChange();
	text_ = trimmed;
	endFilterChange(Direction::Rows);
#else
	text_ = trimmed;
	invalidateFilter();
#endif
	return text_.isEmpty() || !sourceModel() ? 0 : count({}, false);
}

bool TreeFilter::matches(const QModelIndex& index) const
{
	return index.isValid() && index.model() == this && hit(mapToSource(index));
}
} // namespace telemetry::ux
