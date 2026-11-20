/*
 * @file TreeFilter.hpp
 * @brief Find in a tree: what matches by name or path, its parents, and all below it.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The tree under it keeps its nodes: rows are only added as they are opened
 * (a "Show more" row gives way to the rows it stood for), so its nodes - and
 * the views' current row - outlive any number of finds; only this proxy's rows
 * come and go. A match keeps everything below it, so an endpoint found by
 * name still opens to its members. A find counts the rows made so far: the
 * contents make the rows that lead to matches before they set the text.
 * Qt only.
 *
 * Two uses: a device file's view makes one with its source's role numbers;
 * the Device tree makes one in QML (TreeFilter { sourceModel: ... }), which
 * finds its roles by name - "name" and "path" (a model without a path role
 * is matched by the names from the root, dotted) - and leaves two kinds of
 * row out of the matches: a value inside a Field ("previewChild") and a
 * category ("kind" "category"), whose name is the noun of every row below.
 */
#ifndef TELEMETRY_UX_TREE_FILTER_HPP
#define TELEMETRY_UX_TREE_FILTER_HPP
#pragma once

#include <QtCore/QModelIndexList>
#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>

namespace telemetry::ux {
// Not final: QML derives its own element from a creatable type.
class TreeFilter : public QSortFilterProxyModel {
	Q_OBJECT
	QML_ELEMENT
	// What is found: the text set last, trimmed.
	Q_PROPERTY(QString text READ text NOTIFY textChanged)
	// The matches of that text in the rows made so far, a match below a
	// match not counted; counted again when the source's rows change.
	Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
	// For QML: the roles are found by name when the source is set.
	explicit TreeFilter(QObject* parent = nullptr);
	// nameRole and pathRole: the source's roles a find looks in.
	TreeFilter(int nameRole, int pathRole, QObject* parent = nullptr);

	// Shows what matches text (ignoring case) and its parents; empty shows
	// everything. Returns the matches, a match below a match not counted.
	// The same text again filters nothing anew: rows added since were
	// filtered as they came.
	Q_INVOKABLE int setText(const QString& text);
	QString text() const { return text_; }
	int count() const { return count_; }
	// Whether the shown row at index matches itself, not only through a child.
	Q_INVOKABLE bool matches(const QModelIndex& index) const;
	// The counted matches, as shown rows in the tree's order.
	Q_INVOKABLE QModelIndexList found() const;
	// The row's name (a shown row or a source row), from the name role.
	Q_INVOKABLE QString nameOf(const QModelIndex& index) const;
	// The names from the root to the row, joined: "Fields/device/Config".
	Q_INVOKABLE QString namePath(const QModelIndex& index, const QString& separator = QStringLiteral("/")) const;
	// The source row at such a path of names, its rows fetched on the way;
	// invalid when there is none.
	Q_INVOKABLE QModelIndex sourceAt(const QString& namePath, const QString& separator = QStringLiteral("/"));
	// A role of the row by its name ("kind", "isEndpoint"); invalid without one.
	Q_INVOKABLE QVariant value(const QModelIndex& index, const QString& role) const;
	// As the proxy's, but an index of another model maps to none instead of
	// a warning: a view may hold one across a reset.
	QModelIndex mapToSource(const QModelIndex& proxyIndex) const override;
	QModelIndex mapFromSource(const QModelIndex& sourceIndex) const override;
	void setSourceModel(QAbstractItemModel* model) override;

Q_SIGNALS:
	void textChanged();
	void countChanged();

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
	bool hit(const QModelIndex& source) const;
	int tally(const QModelIndex& parent, bool underMatch) const;
	void collect(const QModelIndex& parent, QModelIndexList& out) const;
	void findRoles();
	void recount();

	int nameRole_ = -1, pathRole_ = -1;
	// Rows never matched, found by name (QML use only): a value inside a
	// Field, a category.
	int childRole_ = -1, kindRole_ = -1;
	bool byName_ = false;
	int count_ = 0;
	QString text_;
	QList<QMetaObject::Connection> sourceConnections_;
};
} // namespace telemetry::ux
#endif
