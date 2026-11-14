/*
 * @file WatchlistModel.hpp
 * @brief Qt-owned, bounded scalar watch rows.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_UX_WATCHLIST_MODEL_HPP
#define TELEMETRY_UX_WATCHLIST_MODEL_HPP
#pragma once

#include "DynamicModels.hpp"
#include "WatchSample.hpp"
#include "WatchFrame.hpp"
#include "WatchKey.hpp"
#include <telemetry/runtime/DynamicValueView.hpp>
#include <QtCore/QAbstractListModel>
#include <QtCore/QSet>
#include <QtCore/QVector>
#include <limits>
#include <functional>
#include <utility>

namespace telemetry::ux {
namespace detail { class ScalarEditorMetadata; }

// What a Watchlist row watches and how it reads. A whole Field leaves member
// empty and type 0 (its Field's type is used); a value inside a struct or
// array Field names itself by member ("Gain", "[3]", "Phase[2].Offset") and
// carries the value's own type.
struct WatchEntry {
	WatchKey key = 0;
	QString name;   // the row's path: "device.Config.Gain"
	QString member;
	TypeId type = 0;
	// A value's write covers its whole Field: the Field's own name
	// ("Config"), how many other values it holds, and what they are.
	QString scope;
	quint32 others = 0;
	enum class Kind : quint8 { Members, Elements, Values } kind = Kind::Members;

	// An entry for key; its write scope is the Watchlist's to fill in.
	static WatchEntry of(WatchKey key, QString name, QString member = {}, TypeId type = 0)
	{
		WatchEntry entry;
		entry.key = key;
		entry.name = std::move(name);
		entry.member = std::move(member);
		entry.type = type;
		return entry;
	}
};

class WatchlistModel final : public QAbstractListModel {
	Q_OBJECT
public:
	enum Role {
		EndpointIdRole = Qt::UserRole + 1,
		NameRole,
		StatusRole,
		TextRole,
		SampleRevisionRole,
		SampleTextRole,
		SampleEpochRole,
		SampleValueRole, // double of the last Ok reading when exact, else no value
		SampleExactRole, // whether that reading is a finite number a double holds exactly
		StatusCodeRole,  // int(WatchStatus), stable across providers
		WritableRole,    // the Field can be written from its row
		EditorTextRole,  // canonical typed value; never reconstructed from a double/display text
		EditorKindRole,
		EditorChoicesRole,
		EditorChoicesTruncatedRole,
		TypeTextRole,
		CategoricalRole, // Boolean/enum observations use step presentation
		WatchKeyRole,    // qulonglong WatchKey: the Field id, or a value inside it
		MemberRole,      // the value's member text; empty for a whole Field
		// A value's write: {field: "Config", others: 20, kind: "members" |
		// "elements" | "values"}; empty for a whole Field, and for a value
		// that is its Field's only one.
		WriteScopeRole,
		// Why the row is not written from the Watchlist, in words; empty when
		// it is writable or there is nothing to say.
		WriteRefusalRole,
		// Where the name's last segment starts: "device.Config." is drawn
		// muted before "Gain", "data.Samples" before "[3]".
		LeafStartRole,
		// The row's access as the row family marks it: "rw" or "r-".
		AccessRole
	};
	Q_ENUM(Role)
	static constexpr int maxRows = watchlistRowLimit;
	explicit WatchlistModel(QObject* parent = nullptr);
	~WatchlistModel() override;
	// Metadata is copied before the first reading. No descriptor borrow remains.
	void setSchemaMetadata(const telemetry::runtime::RuntimeSchema& schema);
	void bind(const Workbench& owner) noexcept;
	void bind(std::function<bool()> current);
	// Asked once per row as it is added: whether the row accepts writes, and
	// why not in words.
	void setWritable(std::function<bool(WatchKey)> writable);
	void setWriteRefusal(std::function<QString(WatchKey)> refusal);
	bool add(quint32 id, QString name);
	bool add(const WatchEntry& entry);
	bool addMany(const QVector<QPair<quint32, QString>>& entries);
	// All or nothing: false, and nothing added, on an empty name, a key held
	// or repeated, or too little room.
	bool addMany(const QVector<WatchEntry>& entries);
	bool remove(int row);
	void clear();
	// A row taken out by removeIds, to be put back by restore.
	struct Removed {
		int row = 0;
		WatchEntry entry;
	};
	// Removes every row whose key is in keys, one removal per run of
	// neighbouring rows so persistent indexes follow; the rows in their order.
	QVector<Removed> removeIds(const QSet<WatchKey>& keys);
	// Puts rows back where removeIds took them from, in its order; false, and
	// nothing changed, when that no longer fits (a key already held, no room).
	bool restore(const QVector<Removed>& rows);
	// Moves whenever the set of Fields held changes.
	[[nodiscard]] quint64 membership() const noexcept;
	void invalidateValues();
	// The sample is required: a reading published without its typed number would
	// be shown but never plotted. Pass sampleOf() of the value formatted as text.
	bool setResult(int row, QString status, QString text, WatchSample sample,
	               WatchValueBits value = {});
	// The row's Field id; 0 past the end.
	[[nodiscard]] quint32 id(int row) const noexcept;
	[[nodiscard]] WatchKey key(int row) const noexcept;
	[[nodiscard]] QString member(int row) const;
	// The row holding key, or -1.
	[[nodiscard]] int rowOf(WatchKey key) const;
	[[nodiscard]] int count() const noexcept;
	[[nodiscard]] bool changing() const noexcept;
	QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;
	// Convert while the borrowed view is current; the returned text owns all
	// digits. NaN payloads and signed zero retain their exact hexadecimal bits.
	[[nodiscard]] static QString formatValue(const telemetry::runtime::DynamicValueView& value);
	// The same value as a number for plotting, from the typed view, never from text.
	[[nodiscard]] static WatchSample sampleOf(const telemetry::runtime::DynamicValueView& value);

private:
	struct Row {
		WatchKey key = 0;
		quintptr token = 0;
		QString name, member, status, text, sampleText, scope, refusal;
		TypeId type = 0;
		quint32 others = 0;
		WatchEntry::Kind kind = WatchEntry::Kind::Members;
		quint64 sampleRevision = 0;
		WatchSample sample;
		bool writable = false;
		WatchValueBits value{};
	};
	Row makeRow(const WatchEntry& entry);
	bool insertable(const QVector<WatchEntry>& entries) const;

	std::unique_ptr<detail::ScalarEditorMetadata> metadata_;
	QVector<Row> rows_;
	int count_ = 0;
	quintptr nextToken_ = 1;
	bool changing_ = false;
	const Workbench* owner_ = nullptr;
	std::function<bool()> current_;
	std::function<bool(WatchKey)> writable_;
	std::function<QString(WatchKey)> refusal_;
	mutable QHash<WatchKey, int> rowOf_;
	mutable quint64 rowOfMembership_ = ~quint64{0};
	std::uint32_t session_ = 0;
	std::uint64_t admission_ = 0;
	quint64 sampleEpoch_ = 0;
	quint64 membership_ = 0;
};
} // namespace telemetry::ux
#endif
