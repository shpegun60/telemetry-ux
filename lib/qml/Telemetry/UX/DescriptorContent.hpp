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
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace telemetry::ux {
// Fields, Commands and Services, their groups, their endpoints, and the members
// of a struct type below its endpoint.
class DescriptorTreeModel final : public QAbstractItemModel {
	Q_OBJECT
public:
	enum Role {
		NameRole = Qt::UserRole + 1,
		KindRole,   // "category", "group", "endpoint", "member"
		TypeRole,   // the type in words: "u32", "f32[8]", "struct, 3 members"
		AccessRole, // "read, write" for a Field; "request → response" for a call
		CountRole,   // endpoints below a category or group
		CategoryRole // 0 Fields, 1 Commands, 2 Services
	};
	Q_ENUM(Role)

	struct Node {
		// path: the names below the category joined by "." ("measure.Voltage").
		QString name, kind, type, access, path;
		int count = 0;
		// 0 Fields, 1 Commands, 2 Services: the word a count is in.
		int category = 0;
		Node* parent = nullptr;
		std::vector<std::unique_ptr<Node>> children;
		int row() const noexcept;
	};

	explicit DescriptorTreeModel(QObject* parent = nullptr);
	~DescriptorTreeModel() override;
	void setRoot(std::unique_ptr<Node> root);
	const Node* nodeAt(const QModelIndex& index) const noexcept { return node(index); }

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
	Q_PROPERTY(QAbstractItemModel* endpoints READ endpoints CONSTANT)
public:
	// Parses bytes with the SDK's RuntimeSchema; invalid() with error() when
	// they are not a descriptor this SDK reads.
	explicit DescriptorContent(const QByteArray& bytes, QObject* parent = nullptr);

	bool valid() const noexcept { return valid_; }
	QString error() const { return error_; }
	QString fingerprint() const { return fingerprint_; }
	QString fingerprintGroups() const;
	QVariantList regions() const { return regions_; }
	// Find: only what matches (by name or path, ignoring case) and its parents;
	// empty shows everything again. Returns the matches.
	Q_INVOKABLE int setFilter(const QString& text);
	// A node's path, its row (name, type, access with tabs), or a node with
	// everything below it - the whole tree shown for an invalid index - as
	// "text" (indented), "json" (nested by name) or "tsv" (path, kind, type,
	// access).
	Q_INVOKABLE QString pathOf(const QModelIndex& index) const;
	Q_INVOKABLE QString rowOf(const QModelIndex& index) const;
	Q_INVOKABLE QString textOf(const QModelIndex& index, const QString& format) const;
	int fieldCount() const noexcept { return counts_[0]; }
	int commandCount() const noexcept { return counts_[1]; }
	int serviceCount() const noexcept { return counts_[2]; }
	int groupCount() const noexcept { return groups_; }
	int typeCount() const noexcept { return types_; }
	QString summary() const;
	QAbstractItemModel* endpoints() { return &tree_; }

	// Whether a device path names the descriptor: the protocol's own file.
	static bool names(const QString& devicePath);
	// The header's fields and the sections after it, from the bytes alone.
	static QVariantList headerRegions(std::span<const std::byte> bytes);

private:
	bool valid_ = false;
	QString error_, fingerprint_;
	QVariantList regions_;
	// Everything parsed; the model shows it, or what a filter keeps of it.
	std::unique_ptr<DescriptorTreeModel::Node> full_;
	int counts_[3] = {0, 0, 0};
	int groups_ = 0, types_ = 0;
	DescriptorTreeModel tree_;
};
} // namespace telemetry::ux
#endif
