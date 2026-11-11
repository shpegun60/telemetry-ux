/*
 * @file TreeFilter.hpp
 * @brief Find in a device-file tree: what matches by name or path, its parents, and all below it.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The tree under it is built once and never changes shape, so its nodes - and
 * the views' current row - outlive any number of finds; only this proxy's rows
 * come and go. A match keeps everything below it, so an endpoint found by
 * name still opens to its members. Qt only.
 */
#ifndef TELEMETRY_UX_TREE_FILTER_HPP
#define TELEMETRY_UX_TREE_FILTER_HPP
#pragma once

#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QString>

namespace telemetry::ux {
class TreeFilter final : public QSortFilterProxyModel {
	Q_OBJECT
public:
	// nameRole and pathRole: the source's roles a find looks in.
	TreeFilter(int nameRole, int pathRole, QObject* parent = nullptr);

	// Shows what matches text (ignoring case) and its parents; empty shows
	// everything. Returns the matches, a match below a match not counted.
	int setText(const QString& text);
	QString text() const { return text_; }
	// Whether the shown row at index matches itself, not only through a child.
	Q_INVOKABLE bool matches(const QModelIndex& index) const;

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
	bool hit(const QModelIndex& source) const;
	int count(const QModelIndex& parent, bool underMatch) const;

	int nameRole_, pathRole_;
	QString text_;
};
} // namespace telemetry::ux
#endif
