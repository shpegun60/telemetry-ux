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
#include <telemetry/runtime/DynamicValueView.hpp>
#include <QtCore/QAbstractListModel>
#include <QtCore/QSet>
#include <QtCore/QVector>
#include <limits>
#include <functional>

namespace telemetry::ux {
namespace detail { class ScalarEditorMetadata; }
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
		CategoricalRole  // Boolean/enum observations use step presentation
	};
	Q_ENUM(Role)
	static constexpr int maxRows = watchlistRowLimit;
	explicit WatchlistModel(QObject* parent = nullptr);
	~WatchlistModel() override;
	// Metadata is copied before the first reading. No descriptor borrow remains.
	void setSchemaMetadata(const telemetry::runtime::RuntimeSchema& schema);
	void bind(const Workbench& owner) noexcept;
	void bind(std::function<bool()> current);
	// Asked once per row as it is added: whether the Field accepts writes.
	void setWritable(std::function<bool(quint32)> writable);
	bool add(quint32 id, QString name);
	bool addMany(const QVector<QPair<quint32, QString>>& entries);
	bool remove(int row);
	void clear();
	// A row taken out by removeIds, to be put back by restore.
	struct Removed {
		int row = 0;
		quint32 id = 0;
		QString name;
	};
	// Removes every row whose Field is in ids, one removal per run of
	// neighbouring rows so persistent indexes follow; the rows in their order.
	QVector<Removed> removeIds(const QSet<quint32>& ids);
	// Puts rows back where removeIds took them from, in its order; false, and
	// nothing changed, when that no longer fits (a Field already held, no room).
	bool restore(const QVector<Removed>& rows);
	// Moves whenever the set of Fields held changes.
	[[nodiscard]] quint64 membership() const noexcept;
	void invalidateValues();
	// The sample is required: a reading published without its typed number would
	// be shown but never plotted. Pass sampleOf() of the value formatted as text.
	bool setResult(int row, QString status, QString text, WatchSample sample,
	               WatchValueBits value = {});
	[[nodiscard]] quint32 id(int row) const noexcept;
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
		quint32 id = 0;
		quintptr token = 0;
		QString name, status, text, sampleText;
		quint64 sampleRevision = 0;
		WatchSample sample;
		bool writable = false;
		WatchValueBits value{};
	};

	std::unique_ptr<detail::ScalarEditorMetadata> metadata_;
	QVector<Row> rows_;
	int count_ = 0;
	quintptr nextToken_ = 1;
	bool changing_ = false;
	const Workbench* owner_ = nullptr;
	std::function<bool()> current_;
	std::function<bool(quint32)> writable_;
	std::uint32_t session_ = 0;
	std::uint64_t admission_ = 0;
	quint64 sampleEpoch_ = 0;
	quint64 membership_ = 0;
};
} // namespace telemetry::ux
#endif
