/*
 * @file JsonContent.hpp
 * @brief A JSON file read for the viewer: a tree of its values, made as it is opened.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The JSON codec of the Device files viewer. Qt's parser checks the file
 * once and says where it is wrong; the tree is read from the text itself, so
 * its keys keep the file's order (QJsonObject sorts them) and Copy gives a
 * value exactly as the file writes it. The tree opens with the top level
 * only; an object's members and an array's elements are made when their row
 * is first opened - a long one as ranges of 256, as Eclipse shows a long
 * array - so a file of megabytes opens at once. Find looks through the text, not the rows made so
 * far - names, paths and values - and makes the way to what it finds, up to
 * bounds it names. Qt only.
 */
#ifndef TELEMETRY_UX_JSON_CONTENT_HPP
#define TELEMETRY_UX_JSON_CONTENT_HPP
#pragma once

#include "TreeFilter.hpp"
#include <QtCore/QAbstractItemModel>
#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace telemetry::ux {
class OpenFiles;

// An object's members and an array's elements, made as they are opened. Its
// roles are the Values tree's, so the same row draws both.
class JsonTreeModel final : public QAbstractItemModel {
	Q_OBJECT
public:
	enum Role {
		NameRole = Qt::UserRole + 1,
		KindRole,        // "object", "array", "string", "number", "bool", "null", "range"
		TypeRole,        // "object, 3 keys", "array, 600 items", "string", ...
		ValueRole,       // a scalar as the file writes it; a container's first members
		UnavailableRole, // never: the Values row's role
		ChangedRole,     // never
		CountRole,       // never
		PathRole,        // "config.items[3].name"
		NumericRole,     // a number: shown right-aligned
		FindRole,        // what Find looks in besides the path: the name and a scalar's value
		ChangedFieldsRole // never
	};
	Q_ENUM(Role)

	struct Node {
		QString name, kind, value, type, path;
		// What Find looks in besides the path: the name, and a scalar's text.
		QString find;
		// The value's bytes in the text.
		qsizetype start = 0, end = 0;
		// A container's or a range's members, and a range's first.
		std::uint32_t total = 0, first = 0;
		// A container of more than rangeSize members: where every 256th
		// begins, and an object's first and last key of each block.
		std::vector<qsizetype> marks;
		QStringList blockKeys;
		int row = 0;
		Node* parent = nullptr;
		std::vector<std::unique_ptr<Node>> children;
	};

	explicit JsonTreeModel(QObject* parent = nullptr);
	~JsonTreeModel() override;
	void setRoot(std::unique_ptr<Node> root);
	const Node* nodeAt(const QModelIndex& index) const noexcept;
	Node& root() noexcept { return *root_; }
	// Makes a row's children the first time it is opened.
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
	QModelIndex indexOf(const Node& node) const;
	std::unique_ptr<Node> root_;
};

class JsonContent final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool valid READ valid CONSTANT)
	Q_PROPERTY(QString error READ error CONSTANT)
	// "object, 12 keys" or "array, 600 items".
	Q_PROPERTY(QString summary READ summary CONSTANT)
	// The top level's members, for the hex view.
	Q_PROPERTY(QVariantList regions READ regions CONSTANT)
	// The tree as shown: everything, or what Find keeps.
	Q_PROPERTY(QAbstractItemModel* nodes READ nodes CONSTANT)
public:
	explicit JsonContent(const QByteArray& bytes, QObject* parent = nullptr);
	~JsonContent() override;

	bool valid() const noexcept { return valid_; }
	QString error() const { return error_; }
	QString summary() const { return summary_; }
	QVariantList regions() const { return regions_; }
	QAbstractItemModel* nodes() { return &shown_; }

	// Find: what matches by name, path or a value (ignoring case), its parents
	// and all below it; empty shows everything again. Returns the matches.
	Q_INVOKABLE int setFilter(const QString& text);
	Q_INVOKABLE bool matches(const QModelIndex& index) const;
	Q_INVOKABLE bool findLimited() const noexcept { return findLimited_; }
	Q_INVOKABLE QString findLimitText() const;
	// A shown row's path, its value as text (a string without its quotes;
	// empty for an object or an array), its row as "name<TAB>value<TAB>type",
	// and its value exactly as the file writes it. Empty for another index.
	Q_INVOKABLE QString pathOf(const QModelIndex& index) const;
	Q_INVOKABLE QString valueOf(const QModelIndex& index) const;
	Q_INVOKABLE QString rowOf(const QModelIndex& index) const;
	Q_INVOKABLE QString jsonOf(const QModelIndex& index) const;
	// The whole file's text.
	Q_INVOKABLE QString jsonOfAll() const;
	// An array or an object of more than rangeSize members opens as ranges of
	// that many, [0 … 255], [256 … 511], …, and ranges of ranges past 65 536 -
	// as Eclipse shows a long array: any member is a few levels down.
	static constexpr std::uint32_t rangeSize = 256;
	// Bounds that keep Find quick: the values it looks at (two million: the
	// whole of a 10 MB file of small objects, in about half a second), the
	// matches it opens and the rows it makes on the way.
	static constexpr std::size_t maxFindVisits = 2000000;
	static constexpr std::size_t maxFindMatches = 10000;
	static constexpr std::size_t maxFindRows = 100000;

	// Whether a file's name says JSON: "*.json".
	static bool names(const QString& path);
	// Whether its first bytes begin as a JSON object or array.
	static bool looksLikeJson(const QByteArray& head);

private:
	using Node = JsonTreeModel::Node;
	const Node* nodeOf(const QModelIndex& index) const;
	void populate(Node& node);
	// A container's members from first, or the ranges over them.
	void makeMembers(Node& under, Node& container, std::uint32_t first, std::uint32_t count);
	void markBlocks(Node& container) const;
	// The row for a member index below parent, made on the way through any
	// ranges; rows made are added to made.
	Node* childAt(Node& parent, std::uint32_t slot, std::size_t& made);
	void openMatches(const QString& text);
	// A value at the text's offset made a row: its kind, words and members counted.
	std::unique_ptr<Node> make(const QString& name, const QString& path, qsizetype start, qsizetype end) const;

	bool valid_ = false, findLimited_ = false;
	QString error_, summary_;
	QVariantList regions_;
	QByteArray text_;
	JsonTreeModel tree_;
	TreeFilter shown_;
};

// Registers the JSON codec: by name ("*.json"), or as a guess from its first
// bytes when the name has no extension - a guess that fails says nothing.
void installJsonCodec(OpenFiles& files);
} // namespace telemetry::ux
#endif
