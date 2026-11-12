/*
 * @file ValuesContent.hpp
 * @brief A values.bin read against its descriptor for the viewer: a tree of every Field with its value.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The values codec of the Device files viewer. The file names its descriptor
 * by fingerprint only; the codec finds that descriptor (beside the file, the
 * live connection's, or one the user chooses) and the SDK's readValues reads
 * the file against it. Every value is formatted through DynamicValueView, the
 * reader a connection uses.
 *
 * Groups and Fields are made when the file is read; everything below a
 * Field - a struct's members, an array's elements, 256 at a time - is made
 * when its row is first opened, so a values file of 10 MB opens at once and
 * a snapshot formats only the rows that exist. A struct's or an array's own
 * row is summarised from the data, not from its rows. A newer snapshot of
 * the same file (Live) changes the values in place, so the tree keeps its
 * rows, its expansion and its current row; the values that differ from the
 * snapshot shown before are marked. Hold keeps the shown snapshot while
 * newer ones keep arriving, and shows the newest when released. Find and
 * Copy read the data, so a row not opened yet is neither missed nor lost.
 */
#ifndef TELEMETRY_UX_VALUES_CONTENT_HPP
#define TELEMETRY_UX_VALUES_CONTENT_HPP
#pragma once

#include "TreeFilter.hpp"
#include <QtCore/QAbstractItemModel>
#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QObject>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace telemetry::runtime {
class DynamicValueView;
}

namespace telemetry::ux {
namespace schema_text {
struct RowBudget;
}

// Groups, their Fields, and the members and elements of a Field's value. What
// is below a Field is made when its row is first opened; once made, a row
// stays, and only values, marks and availability change.
class ValuesTreeModel final : public QAbstractItemModel {
	Q_OBJECT
public:
	enum Role {
		NameRole = Qt::UserRole + 1,
		KindRole,        // "group", "field", "member", "element", "range"
		TypeRole,        // the type in words: "u32", "f32[3]", "struct, 4 members"
		ValueRole,       // the value as text; empty for a struct or a group
		UnavailableRole, // the device served this Field without a value
		ChangedRole,     // differs from the snapshot shown before
		CountRole,       // Fields in a group
		PathRole,        // "group.Field.member[2]"
		NumericRole,     // a number: shown right-aligned
		FindRole,        // the name Find looks in: none for a range
		ChangedFieldsRole // a group's Fields whose bytes the latest snapshot changed
	};
	Q_ENUM(Role)

	struct Node {
		QString name, kind, type, value, path;
		int count = 0, row = 0, changedFields = 0;
		bool unavailable = false, changed = false;
		// How the value goes into JSON: 0 text, 1 number, 2 true/false; and
		// what it writes when that is not the shown text (an enum's code).
		std::uint8_t json = 0;
		QString jsonText;
		// Below a Field: its type, its place in its parent (member or element
		// index), how many members or elements it has, how many are made.
		// A range's first element; a range's typeId is its array's.
		std::uint32_t typeId = 0, slot = 0, total = 0, first = 0;
		// The Field it belongs to, its index in the file; -1 for a group.
		int field = -1;
		Node* parent = nullptr;
		std::vector<std::unique_ptr<Node>> children;
	};

	explicit ValuesTreeModel(QObject* parent = nullptr);
	~ValuesTreeModel() override;
	// Once, when the descriptor is known.
	void setRoot(std::unique_ptr<Node> root);
	const Node* nodeAt(const QModelIndex& index) const noexcept;
	// Tells the views that these nodes' value, mark or availability changed.
	void touched(const std::vector<const Node*>& nodes);
	// Makes a row's children the first time it is opened.
	std::function<void(Node&)> populate;
	// Appends made rows below a node.
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

class ValuesContent final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool valid READ valid NOTIFY shownChanged)
	Q_PROPERTY(QString error READ error NOTIFY shownChanged)
	// A values file whose descriptor was not found: useDescriptor() supplies it.
	Q_PROPERTY(bool needsDescriptor READ needsDescriptor NOTIFY shownChanged)
	// While it needs one: the connected device's descriptor fingerprint in
	// groups of four (another than this file's), or empty when not connected.
	Q_PROPERTY(QString connectedFingerprintGroups READ connectedFingerprintGroups CONSTANT)
	// The descriptor the file needs, in groups of four: "3F2A 91C0 7BE4 0D11".
	Q_PROPERTY(QString fingerprintGroups READ fingerprintGroups CONSTANT)
	// "4 Fields in 2 groups · 1 unavailable"
	Q_PROPERTY(QString summary READ summary NOTIFY shownChanged)
	// The header's fields and every Field's status and value bytes, for the hex view.
	Q_PROPERTY(QVariantList regions READ regions NOTIFY regionsChanged)
	// The tree as shown: everything, or what Find keeps. Its indexes are the
	// ones the methods below take.
	Q_PROPERTY(QAbstractItemModel* fields READ fields CONSTANT)
	// Keeps the shown snapshot; newer ones wait, and the newest is shown on release.
	Q_PROPERTY(bool held READ held WRITE setHeld NOTIFY heldChanged)
	// Snapshots taken since opened, the shown one's time, how many values it changed.
	Q_PROPERTY(int snapshots READ snapshots NOTIFY snapshotsChanged)
	Q_PROPERTY(QDateTime shownAt READ shownAt NOTIFY shownChanged)
	Q_PROPERTY(int changedCount READ changedCount NOTIFY shownChanged)
	// Snapshots that arrived while held and are not shown yet.
	Q_PROPERTY(int waiting READ waiting NOTIFY snapshotsChanged)
public:
	// Reads bytes against descriptor (the descriptor.bin bytes whose
	// fingerprint the values header names). error() when the bytes are not a
	// values file, or not a snapshot of that descriptor. An empty descriptor
	// leaves it needing one, the fingerprint known.
	ValuesContent(const QByteArray& bytes, const QByteArray& descriptor, QObject* parent = nullptr);
	~ValuesContent() override;

	bool valid() const noexcept { return valid_; }
	bool needsDescriptor() const noexcept { return needsDescriptor_; }
	QString error() const { return error_; }
	quint64 fingerprint() const noexcept { return fingerprint_; }
	QString connectedFingerprintGroups() const { return connectedGroups_; }
	// Set by the codec when it is made; the view's words depend on it.
	void setConnectedFingerprint(quint64 fingerprint);
	QString fingerprintGroups() const { return fingerprintGroups_; }
	QString summary() const;
	QVariantList regions() const { return regions_; }
	QAbstractItemModel* fields() { return &shown_; }
	bool held() const noexcept { return held_; }
	void setHeld(bool held);
	int snapshots() const noexcept { return snapshots_; }
	QDateTime shownAt() const { return shownAt_; }
	int changedCount() const noexcept { return changedCount_; }
	int waiting() const noexcept { return waiting_; }

	// Reads the file against the descriptor in a local file - only one with
	// the fingerprint this file names. Empty when shown, else why not.
	Q_INVOKABLE QString useDescriptor(const QUrl& file);
	// The same with the descriptor's bytes, for a host that has them.
	QString useDescriptorBytes(const QByteArray& descriptor);
	// A newer snapshot of the same file. Shown at once unless held; empty
	// when taken, otherwise why it is not a snapshot of this descriptor (the
	// shown values stay).
	Q_INVOKABLE QString update(const QByteArray& bytes);
	// Find: what matches (name or path, ignoring case), its parents and all
	// below it; empty shows everything again. Returns the matches.
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
	// A shown node's path, its value (empty for a struct or a group), its row
	// as "name<TAB>value<TAB>type", or the node with everything below it as
	// "text" (indented "name = value"), "json" (nested by name, numbers as
	// numbers, an unavailable Field null) or "tsv" (path, value, type). Empty
	// for an index that is not one of fields' current rows.
	Q_INVOKABLE QString pathOf(const QModelIndex& index) const;
	Q_INVOKABLE QString valueOf(const QModelIndex& index) const;
	Q_INVOKABLE QString rowOf(const QModelIndex& index) const;
	Q_INVOKABLE QString textOf(const QModelIndex& index, const QString& format) const;
	// Everything shown, the same three ways.
	Q_INVOKABLE QString textOfAll(const QString& format) const;
	// An array of more than rangeSize elements opens as ranges of that many,
	// [0 … 255], [256 … 511], …, and ranges of ranges past 65 536 - as Eclipse
	// shows a long array: any element is a few levels down.
	static constexpr std::uint32_t rangeSize = 256;
	// Bounds that keep a large file quick: the values Find looks at, the
	// matches it opens and the rows it makes on the way to them, and the
	// values a copy writes (the copy then says it was cut short).
	static constexpr std::size_t maxFindVisits = 500000;
	static constexpr std::size_t maxFindMatches = 10000;
	static constexpr std::size_t maxFindRows = 100000;
	static constexpr std::size_t maxCopiedRows = 500000;

	// Whether a device path names the values file: the protocol's own file.
	static bool names(const QString& devicePath);
	// The fingerprint a values file's header names, if it is one this program reads.
	static bool fingerprintOf(const QByteArray& bytes, quint64& fingerprint);
	// The header's fields, from the bytes alone.
	static QVariantList headerRegions(std::span<const std::byte> bytes);

Q_SIGNALS:
	void shownChanged();
	void heldChanged();
	void snapshotsChanged();
	// Only when the layout is known (a descriptor adopted): not per snapshot.
	void regionsChanged();

private:
	struct Schema;
	using Node = ValuesTreeModel::Node;
	// Parses descriptor, builds the tree and shows the first snapshot.
	QString adopt(const QByteArray& descriptor);
	QString show(const QByteArray& bytes);
	const Node* nodeOf(const QModelIndex& index) const;
	// The rows below a node, first made: a struct's members, an array's
	// elements or its ranges - filled from the snapshot shown.
	void populate(Node& node);
	void makeElements(Node& under, std::uint32_t first, std::uint32_t count);
	// The row for a member or element index below parent, made on the way
	// through any ranges; rows made are added to made.
	Node* childAt(Node& parent, std::uint32_t slot, std::size_t& made);
	// The shown snapshot's value at a node below a Field; false when it has none.
	bool viewOf(const Node& node, runtime::DynamicValueView& out) const;
	// A row at or below a match of Find, or every row with Find off: Copy
	// reads its data rather than its rows.
	bool wholeShown(const QModelIndex& index) const;
	void appendShown(QJsonArray& array, const QModelIndex& index, schema_text::RowBudget& budget) const;
	// Makes the rows Find matches, and the way to them.
	void openMatches(const QString& text);
	QString linesOf(const QModelIndex& index, int depth, bool withSelf, schema_text::RowBudget& budget) const;
	QJsonValue jsonOf(const QModelIndex& index, schema_text::RowBudget& budget) const;
	QString tsvOf(const QModelIndex& index, bool withSelf, schema_text::RowBudget& budget) const;
	// The last line of a copy that reached maxCopiedRows.
	static QString cutShort();

	bool valid_ = false, held_ = false, needsDescriptor_ = false, findLimited_ = false;
	mutable bool copyCut_ = false;
	quint64 fingerprint_ = 0;
	QString error_, fingerprintGroups_, connectedGroups_;
	QVariantList regions_;
	// The file as opened, the shown snapshot, and the newest one waiting while held.
	QByteArray original_, last_, pending_;
	std::unique_ptr<Schema> schema_;
	// Every Field's node in file order, for a snapshot's entries, and every group's.
	std::vector<ValuesTreeModel::Node*> fieldNodes_, groupNodes_;
	// The nodes the shown snapshot marked as changed, to unmark on the next.
	std::vector<ValuesTreeModel::Node*> marked_;
	int fieldCount_ = 0, groupCount_ = 0, unavailable_ = 0;
	int snapshots_ = 0, changedCount_ = 0, waiting_ = 0;
	QDateTime shownAt_;
	ValuesTreeModel tree_;
	TreeFilter shown_;
};
} // namespace telemetry::ux
#endif
