/*
 * @file WatchRows.hpp
 * @brief The Watchlist's rows as they are shown: whole paths, or a tree grouped by path.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * A list model over any Watchlist model, for the ListView that draws it. In
 * the list a row is a source row, in the source's order or by path A-Z. In the
 * tree the rows are the nodes of the paths: a group, the Field, its struct
 * members and array elements, each value under the node it belongs to, in
 * descriptor order (by watch key) and only above something watched. The
 * tree's nodes fold by path, so a fold outlives the rows under it.
 *
 * A change of the source's rows changes only the rows it touches: the rows
 * shown before and after are compared by identity, the ones that went are
 * removed, the ones that stay and changed places are moved (the longest run
 * already in order stays put) and the new ones inserted, so a row being
 * edited keeps its delegate while the Watchlist changes around it. Only a
 * switch of view or sort, or a reset of the source, resets the rows. Reading
 * a value moves no row; the nodes it shows in are told once per event-loop
 * turn.
 *
 * The source's roles are found by name, so the Static provider's model and a
 * QML ListModel work as the dynamic one does; a role a source does not have
 * reads as nothing. Qt only.
 */
#ifndef TELEMETRY_UX_WATCH_ROWS_HPP
#define TELEMETRY_UX_WATCH_ROWS_HPP
#pragma once

#include <QtCore/QAbstractListModel>
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtQml/qqmlregistration.h>

namespace telemetry::ux {

// Not final: QML derives its own element from a creatable type.
class WatchRows : public QAbstractListModel {
	Q_OBJECT
	QML_ELEMENT
	// The Watchlist model it shows. A provider sets it once: the panel calls
	// the provider with sourceRow, which another source would not match.
	Q_PROPERTY(QAbstractItemModel* source READ source WRITE setSource NOTIFY sourceChanged)
	// false: one row per value, its whole path; true: grouped by path.
	Q_PROPERTY(bool tree READ tree WRITE setTree NOTIFY treeChanged)
	// The list by path A-Z (numbers in order: [9] before [10]); else as added.
	Q_PROPERTY(bool sorted READ sorted WRITE setSorted NOTIFY sortedChanged)
	// The paths of the tree's folded nodes.
	Q_PROPERTY(QStringList folded READ folded WRITE setFolded NOTIFY foldedChanged)
	Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
	enum Role {
		// The source's roles, read by name.
		NameRole = Qt::UserRole + 1,
		StatusRole,
		TextRole,
		WritableRole,
		SampleValueRole,
		SampleExactRole,
		EditorTextRole,
		EditorKindRole,
		EditorChoicesRole,
		EditorChoicesTruncatedRole,
		TypeTextRole,
		AccessRole,
		LeafStartRole,
		WriteScopeRole,
		WriteRefusalRole,
		WatchKeyRole,
		MemberRole,
		// This model's own.
		SourceRowRole,  // the source row, -1 for a node above values
		DepthRole,      // 0 in the list; the tree's depth
		BranchRole,     // a node with rows under it
		ExpandedRole,   // a branch shown open
		NodeRole,       // "value", "group", "struct", "array", "element"
		LeavesRole,     // the values at and under the row
		NestedRole      // in the tree, a row inside a Field (its member or element)
	};
	Q_ENUM(Role)

	explicit WatchRows(QObject* parent = nullptr);
	~WatchRows() override;

	QAbstractItemModel* source() const { return source_; }
	void setSource(QAbstractItemModel* source);
	bool tree() const noexcept { return tree_; }
	void setTree(bool tree);
	bool sorted() const noexcept { return sorted_; }
	void setSorted(bool sorted);
	QStringList folded() const;
	void setFolded(const QStringList& paths);
	int count() const noexcept { return static_cast<int>(shape_.rows.size()); }

	// Folds or opens the branch at row; false when row is not a branch.
	Q_INVOKABLE bool setExpanded(int row, bool expanded);
	Q_INVOKABLE void expandAll();
	Q_INVOKABLE void collapseAll();
	// The row of the node above row, or -1 at the top.
	Q_INVOKABLE int parentRow(int row) const;
	// The row showing source row sourceRow, opening the nodes above it; -1
	// when the source has no such row.
	Q_INVOKABLE int reveal(int sourceRow);
	// The row of path ("device.Config" names a node, "device.Config.Gain" a
	// value), opening the nodes above it; -1 when nothing has that path.
	Q_INVOKABLE int revealPath(const QString& path);
	// The source rows at and under row, in the order shown.
	Q_INVOKABLE QVariantList sourceRows(int row) const;

	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	// A path's steps as the tree shows them: "device.Calib.Phase[2].Offset"
	// is device, Calib, Phase, [2], Offset; the Static provider's
	// "Catalog / Name" is Catalog, Name. Each step is its [start, end).
	struct Step {
		int start = 0;
		int end = 0;
	};
	static QVector<Step> steps(const QString& path);
	// Path order for people: case-insensitive, numbers by value.
	static bool pathLess(const QString& a, const QString& b);

Q_SIGNALS:
	void sourceChanged();
	void treeChanged();
	void sortedChanged();
	void foldedChanged();
	void countChanged();

private:
	struct Node {
		QString path;
		int labelStart = 0;  // where the node's own step starts in path
		int parent = -1;     // node
		int depth = 0;
		int source = -1;     // the source row of a value; -1 above values
		int leaves = 0;      // values at and under the node
		quint64 order = 0;   // the smallest order at and under the node
		QVector<int> children;
		QString identity;
	};
	struct Shape {
		QVector<Node> nodes;
		QVector<int> rows;        // nodes, in the order shown
		QVector<int> nodeOfSource;
	};
	Shape build() const;
	void walk(const Shape& shape, int node, QVector<int>& rows) const;
	// Brings the shown rows to the source's present shape, row by row. A
	// change asked for while it is under way is made right after it.
	void refresh();
	void apply(Shape next);
	// Puts the rows that stay (their new places in `to`) in order with the
	// fewest moves.
	void place(QVector<int>& to);
	void reset();
	void findRoles();
	void sourceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QList<int>& roles);
	void sourceRowsInserted(const QModelIndex& parent, int first, int last);
	void tellParents();
	QVariant sourceData(int sourceRow, Role role) const;
	QVariant nodeData(const Node& node, Role role) const;
	QString summary(const Node& node) const;
	int firstLeaf(const Node& node) const;
	void unfoldAbove(const QString& path);
	void setCount(int before);
	void dropSource();
	// Whether the node's own step is an element, "[3]".
	static bool element(const Node& node);

	QPointer<QAbstractItemModel> source_;
	QVector<QMetaObject::Connection> connections_;
	QHash<int, int> sourceRole_;  // this model's role -> the source's
	QHash<int, int> ownRole_;     // the source's role -> this model's
	int pathTypesRole_ = -1;
	// The last roles a reading came with, translated: a poll repeats them.
	QList<int> lastSource_, lastOwn_;
	bool lastTexts_ = false, lastOutcome_ = false;
	bool refreshing_ = false, again_ = false;
	bool tree_ = false;
	bool sorted_ = false;
	QSet<QString> folded_;
	Shape shape_;
	QVector<int> rowOfNode_;
	QVector<QString> identities_;  // of the rows shown
	QSet<int> dirtyParents_;       // nodes whose summary or status moved
	QTimer tell_;
};

} // namespace telemetry::ux
#endif
