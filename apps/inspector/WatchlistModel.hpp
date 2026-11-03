/*
 * @file WatchlistModel.hpp
 * @brief Qt-owned, bounded scalar watch rows.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_INSPECTOR_QML_WATCHLIST_MODEL_HPP
#define TELEMETRY_EXAMPLES_INSPECTOR_QML_WATCHLIST_MODEL_HPP
#pragma once

#include "Models.hpp"
#include <telemetry/runtime/DynamicValueView.hpp>
#include <QtCore/QAbstractListModel>
#include <array>

namespace inspector_qml {
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
	static constexpr int maxRows = 32;
	explicit WatchlistModel(QObject* parent = nullptr);
	void bind(const Workbench& owner) noexcept;
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

	std::array<Row, maxRows> rows_{};
	int count_ = 0;
	quintptr nextToken_ = 1;
	bool changing_ = false;
	const Workbench* owner_ = nullptr;
	std::uint32_t session_ = 0;
	std::uint64_t admission_ = 0;
};
} // namespace inspector_qml
#endif
