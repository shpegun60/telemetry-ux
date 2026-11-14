/*
 * @file DescriptorContent.hpp
 * @brief A descriptor.bin parsed for the viewer: its identity, its counts and its endpoint tree.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The descriptor codec of the Device files viewer: the parser is the SDK's
 * RuntimeSchema - the one a connection uses - and this is the view model the
 * viewer's Descriptor tab shows. Nothing here talks to a device; the bytes
 * come from a downloaded copy or a file on disk.
 */
#ifndef TELEMETRY_UX_DESCRIPTOR_CONTENT_HPP
#define TELEMETRY_UX_DESCRIPTOR_CONTENT_HPP
#pragma once

#include <QtCore/QAbstractItemModel>
#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include "TreeFilter.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace telemetry::ux {
// Fields, Commands and Services, their groups, their endpoints, and the members
// of a struct type below its endpoint. Members are made when their row is first
// opened: a descriptor of any size opens at once.
class DescriptorTreeModel final : public QAbstractItemModel {
	Q_OBJECT
public:
	enum Role {
		NameRole = Qt::UserRole + 1,
		KindRole,   // "category", "group", "endpoint", "member"
		TypeRole,   // the type in words: "u32", "f32[8]", "struct (3)", "(bool)"
		AccessRole, // a Field's access in words, "read, write"; empty for a call
		CountRole,    // endpoints below a category or group
		CategoryRole, // 0 Fields, 1 Commands, 2 Services
		PathRole,     // the names below the category joined by "."
		AccessMarkRole // a Field's access as the Device tree marks it: "rw", "r-", "-w", "--"
	};
	Q_ENUM(Role)

	struct Node {
		// path: the names below the category joined by "." ("measure.Voltage").
		QString name, kind, type, access, path, mark;
		int count = 0;
		// 0 Fields, 1 Commands, 2 Services: the word a count is in.
		int category = 0;
		// Its place among its parent's children.
		int row = 0;
		// The type whose members go below it, and whether they are still to be made.
		std::uint32_t typeId = 0;
		bool expandable = false;
		Node* parent = nullptr;
		std::vector<std::unique_ptr<Node>> children;
	};

	explicit DescriptorTreeModel(QObject* parent = nullptr);
	~DescriptorTreeModel() override;
	// Once: rows are only ever added after, so no index into it goes stale.
	void setRoot(std::unique_ptr<Node> root);
	const Node* nodeAt(const QModelIndex& index) const noexcept { return node(index); }
	// Makes a row's members the first time it is opened.
	std::function<void(Node&)> populate;
	void append(Node& under, std::vector<std::unique_ptr<Node>> rows);

	bool hasChildren(const QModelIndex& parent = {}) const override;
	bool canFetchMore(const QModelIndex& parent) const override;
	void fetchMore(const QModelIndex& parent) override;

	QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
	QModelIndex parent(const QModelIndex& child) const override;
	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

private:
	Node* node(const QModelIndex& index) const noexcept;
	std::unique_ptr<Node> root_;
};

class DescriptorContent final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool valid READ valid CONSTANT)
	Q_PROPERTY(QString error READ error CONSTANT)
	// The descriptor's identity, as the connection and values.bin check it.
	Q_PROPERTY(QString fingerprint READ fingerprint CONSTANT)
	// The same in groups of four, for reading: "3F2A 91C0 7BE4 0D11".
	Q_PROPERTY(QString fingerprintGroups READ fingerprintGroups CONSTANT)
	// The header's fields and the file's sections, for the hex view.
	Q_PROPERTY(QVariantList regions READ regions CONSTANT)
	Q_PROPERTY(int fieldCount READ fieldCount CONSTANT)
	Q_PROPERTY(int commandCount READ commandCount CONSTANT)
	Q_PROPERTY(int serviceCount READ serviceCount CONSTANT)
	Q_PROPERTY(int groupCount READ groupCount CONSTANT)
	Q_PROPERTY(int typeCount READ typeCount CONSTANT)
	// "4 Fields in 2 groups · 3 Commands · 1 Service · 23 types"
	Q_PROPERTY(QString summary READ summary CONSTANT)
	// The tree as shown: everything, or what Find keeps. Its indexes are the
	// ones the methods below take.
	Q_PROPERTY(QAbstractItemModel* endpoints READ endpoints CONSTANT)
public:
	// Parses bytes with the SDK's RuntimeSchema; invalid() with error() when
	// they are not a descriptor this SDK reads.
	explicit DescriptorContent(const QByteArray& bytes, QObject* parent = nullptr);
	~DescriptorContent() override;

	bool valid() const noexcept { return valid_; }
	QString error() const { return error_; }
	QString fingerprint() const { return fingerprint_; }
	QString fingerprintGroups() const;
	QVariantList regions() const { return regions_; }
	// Find: what matches (by name or path, ignoring case), its parents and
	// everything below it; empty shows everything again. Returns the matches,
	// a match inside a match not counted.
	Q_INVOKABLE int setFilter(const QString& text);
	// Whether the shown row matches Find itself, not only through a child.
	Q_INVOKABLE bool matches(const QModelIndex& index) const;
	// Whether the last Find stopped at a bound: there may be more matches
	// than it showed.
	Q_INVOKABLE bool findLimited() const noexcept { return findLimited_; }
	// The bounds Find stops at, in words, for the view to say why.
	Q_INVOKABLE QString findLimitText() const;
	// After a copy that stopped at maxCopiedRows, what the view says about it
	// (the copied text ends saying so too); empty after a whole copy.
	Q_INVOKABLE QString copyNote() const;
	// A shown node's path, its row (name, type, access with tabs), or the node
	// with everything below it as "text" (indented), "json" (nested by name)
	// or "tsv" (path, kind, type, access). Empty for an index that is not one
	// of endpoints' current rows.
	Q_INVOKABLE QString pathOf(const QModelIndex& index) const;
	Q_INVOKABLE QString rowOf(const QModelIndex& index) const;
	Q_INVOKABLE QString textOf(const QModelIndex& index, const QString& format) const;
	// Everything shown, the same three ways.
	Q_INVOKABLE QString textOfAll(const QString& format) const;
	int fieldCount() const noexcept { return counts_[0]; }
	int commandCount() const noexcept { return counts_[1]; }
	int serviceCount() const noexcept { return counts_[2]; }
	int groupCount() const noexcept { return groups_; }
	int typeCount() const noexcept { return types_; }
	QString summary() const;
	QAbstractItemModel* endpoints() { return &shown_; }

	// Bounds that keep a large descriptor quick: the members Find looks at,
	// and the rows a copy writes (the copy then says it was cut short).
	static constexpr std::size_t maxFindVisits = 500000;
	static constexpr std::size_t maxFindMatches = 10000;
	static constexpr std::size_t maxFindRows = 100000;
	static constexpr std::size_t maxCopiedRows = 500000;

	// Whether a device path names the descriptor: the protocol's own file.
	static bool names(const QString& devicePath);
	// The header's fields and the sections after it, from the bytes alone.
	static QVariantList headerRegions(std::span<const std::byte> bytes);

private:
	struct Schema;
	using Node = DescriptorTreeModel::Node;
	// The node a shown index stands for, or null for any other index.
	const Node* nodeOf(const QModelIndex& index) const;
	// A row's members, made when it is first opened.
	void populate(Node& node);
	// Makes the rows Find matches, and the way to them.
	void openMatches(const QString& text);
	// The last line of a copy that reached maxCopiedRows.
	static QString cutShort();

	bool valid_ = false, findLimited_ = false;
	mutable bool copyCut_ = false;
	QString error_, fingerprint_;
	QVariantList regions_;
	int counts_[3] = {0, 0, 0};
	int groups_ = 0, types_ = 0;
	// The parsed descriptor, kept: members are made from it as rows open.
	std::unique_ptr<Schema> schema_;
	// Categories, groups and endpoints, made once; members as they are opened.
	DescriptorTreeModel tree_;
	TreeFilter shown_;
};
} // namespace telemetry::ux
#endif
