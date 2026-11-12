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
 * The tree is built once from the descriptor. A newer snapshot of the same
 * file (Live) changes the values in place, so the tree keeps its rows, its
 * expansion and its current row; the values that differ from the snapshot
 * shown before are marked. Hold keeps the shown snapshot while newer ones
 * keep arriving, and shows the newest when released.
 */
#ifndef TELEMETRY_UX_VALUES_CONTENT_HPP
#define TELEMETRY_UX_VALUES_CONTENT_HPP
#pragma once

#include "TreeFilter.hpp"
#include <QtCore/QAbstractItemModel>
#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QObject>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace telemetry::ux {
// Groups, their Fields, and the members and elements of a Field's value. Its
// shape never changes once set; only values, marks and availability do.
class ValuesTreeModel final : public QAbstractItemModel {
	Q_OBJECT
public:
	enum Role {
		NameRole = Qt::UserRole + 1,
		KindRole,        // "group", "field", "member", "element"
		TypeRole,        // the type in words: "u32", "f32[3]", "struct, 4 members"
		ValueRole,       // the value as text; empty for a struct or a group
		UnavailableRole, // the device served this Field without a value
		ChangedRole,     // differs from the snapshot shown before
		CountRole,       // Fields in a group
		PathRole,        // "group.Field.member[2]"
		NumericRole      // a number: shown right-aligned
	};
	Q_ENUM(Role)

	struct Node {
		QString name, kind, type, value, path;
		int count = 0, row = 0;
		bool unavailable = false, changed = false;
		// How the value goes into JSON: 0 text, 1 number, 2 true/false; and
		// what it writes when that is not the shown text (an enum's code).
		std::uint8_t json = 0;
		QString jsonText;
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
	Q_PROPERTY(QVariantList regions READ regions NOTIFY shownChanged)
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

private:
	struct Schema;
	// Parses descriptor, builds the tree and shows the first snapshot.
	QString adopt(const QByteArray& descriptor);
	QString show(const QByteArray& bytes);
	const ValuesTreeModel::Node* nodeOf(const QModelIndex& index) const;

	bool valid_ = false, held_ = false, needsDescriptor_ = false;
	quint64 fingerprint_ = 0;
	QString error_, fingerprintGroups_, connectedGroups_;
	QVariantList regions_;
	// The file as opened, the shown snapshot, and the newest one waiting while held.
	QByteArray original_, last_, pending_;
	std::unique_ptr<Schema> schema_;
	// Every Field's node in file order, for a snapshot's entries.
	std::vector<ValuesTreeModel::Node*> fieldNodes_;
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
