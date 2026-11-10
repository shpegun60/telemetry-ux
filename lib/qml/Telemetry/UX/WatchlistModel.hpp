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
#include <telemetry/runtime/DynamicValueView.hpp>
#include <QtCore/QAbstractListModel>
#include <QtCore/QVector>
#include <limits>
#include <functional>

namespace telemetry::ux {
class WatchlistModel final : public QAbstractListModel {
	Q_OBJECT
public:
	enum Role {
		EndpointIdRole = Qt::UserRole + 1,
		NameRole,
		StatusRole,
		TextRole
	};
	Q_ENUM(Role)
	static constexpr int maxRows = std::numeric_limits<int>::max();
	explicit WatchlistModel(QObject* parent = nullptr);
	void bind(const Workbench& owner) noexcept;
	void bind(std::function<bool()> current);
	bool add(quint32 id, QString name);
	bool remove(int row);
	void clear();
	void invalidateValues();
	bool setResult(int row, QString status, QString text);
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

private:
	struct Row {
		quint32 id = 0;
		quintptr token = 0;
		QString name, status, text;
	};

	QVector<Row> rows_;
	int count_ = 0;
	quintptr nextToken_ = 1;
	bool changing_ = false;
	const Workbench* owner_ = nullptr;
	std::function<bool()> current_;
	std::uint32_t session_ = 0;
	std::uint64_t admission_ = 0;
};
} // namespace telemetry::ux
#endif
