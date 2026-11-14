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
#include "WatchFormat.hpp"
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
	// The type each step inside the Field leads to, the value's own last;
	// empty for a whole Field. The tree's nodes take their types from it.
	QVector<TypeId> steps;

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
		AccessRole,
		// The type of each step of the path below its group, as words: the
		// Field's first, the value's last ({"struct (3)", "u16[64]", "u16"}).
		PathTypesRole,
		// The value in the row's format (its own, else the Watchlist's):
		// "High", "2" or "0x02". text keeps its fixed form for recording.
		ShownTextRole,
		// The row's own format, "original", "dec" or "hex"; "" when it
		// follows the Watchlist's.
		FormatRole
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
	// The Watchlist's number format, and a row's own (none follows it).
	void setFormat(WatchNumberFormat format);
	[[nodiscard]] WatchNumberFormat format() const noexcept { return format_; }
	bool setRowFormat(int row, std::optional<WatchNumberFormat> format);
	// The formats rows have of their own, by path. Kept for a path while it is
	// not watched, so a value added again (or after a reconnect, or from a
	// saved project) comes back in its format.
	[[nodiscard]] QHash<QString, WatchNumberFormat> pathFormats() const { return pathFormats_; }
	// false, and nothing changed, for more than formatLimit paths.
	bool setPathFormats(QHash<QString, WatchNumberFormat> formats);
	static constexpr int formatLimit = 4 * maxRows;
	[[nodiscard]] std::optional<WatchNumberFormat> rowFormat(int row) const;
	// The type of the row's last reading; invalid before its first.
	[[nodiscard]] WatchValueType valueType(int row) const;
	// Rows for paths a device does not have, or not yet (WL-4c). A row whose
	// key is watch_key::unresolved keeps its path and its last value; it is
	// never read, written or recorded.
	// Every row loses its device (a disconnect, another device): unresolved
	// keys from next on, values kept and stale. Returns the next number.
	quint64 unbindAll(quint64 next);
	// row's path found on this device: the values it names, in place (several
	// for a struct typed whole); none removes the row (watched already).
	bool rebindRow(int row, const QVector<WatchEntry>& entries);
	// row's path is not on this device: it says so.
	bool markMissing(int row);
	[[nodiscard]] bool isMissing(int row) const;
	// Some row is bound to the device attached.
	[[nodiscard]] bool hasBound() const;
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
		QVector<TypeId> steps;
		std::optional<WatchNumberFormat> format;
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
	WatchNumberFormat format_ = WatchNumberFormat::Original;
	QHash<QString, WatchNumberFormat> pathFormats_;
	quint64 membership_ = 0;
};
} // namespace telemetry::ux
#endif
