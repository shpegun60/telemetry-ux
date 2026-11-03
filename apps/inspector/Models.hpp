/*
 * @file Models.hpp
 * @brief Qt-owned endpoint metadata and lazy Inspector snapshot tree models.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_INSPECTOR_QML_MODELS_HPP
#define TELEMETRY_EXAMPLES_INSPECTOR_QML_MODELS_HPP
#pragma once

#include <integration/inspector/InspectorModel.hpp>
#include <integration/workbench/WorkbenchModel.hpp>

#include <QtCore/QAbstractItemModel>
#include <QtCore/QVariantMap>

#include <memory>

namespace inspector_qml {

using Inspector = telemetry::integration::inspector::InspectorModel<8, 4096, 65536>;
// ReadMany copies its bounded binding into the existing FIFO completion slot.
// This opt-in GUI budget does not change the portable Workbench default.
using Workbench = telemetry::integration::workbench::WorkbenchModel<8, 4096, 65536, 768>;

// All calls run on the Inspector owner thread. The Inspector object must outlive
// these models. Materialized labels and values are Qt-owned copies; the controller
// clears/resetSnapshot() before changing the displayed admission or snapshot.
class EndpointModel final : public QAbstractItemModel {
	Q_OBJECT
public:
	enum Role {
		NameRole = Qt::UserRole + 1,
		DetailRole,
		ValueRole,
		EndpointIdRole,
		CategoryRole,
		ReadableRole,
		IsEndpointRole,
		WritableRole
	};
	Q_ENUM(Role)

	explicit EndpointModel(QObject* parent = nullptr);
	~EndpointModel() override;
	void rebuild(const Inspector& inspector);
	void rebuild(const Workbench& workbench);
	void clear();
	[[nodiscard]] bool changing() const noexcept;
	Q_INVOKABLE QVariantMap endpoint(const QModelIndex& index) const;
	QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
	QModelIndex parent(const QModelIndex& index) const override;
	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
	void rebuildSource(const Inspector* inspector, const Workbench* workbench);
};

class ValueModel final : public QAbstractItemModel {
	Q_OBJECT
public:
	enum Role {
		NameRole = Qt::UserRole + 1,
		DetailRole,
		ValueRole,
		TypeRole,
		AggregateRole,
		ChildCountRole,
		LoadedCountRole,
		EditableRole,
		EditorTextRole
	};
	Q_ENUM(Role)
	static constexpr int fetchBatch = 128;

	explicit ValueModel(QObject* parent = nullptr);
	~ValueModel() override;
	void resetSnapshot(const Inspector& inspector);
	void resetSnapshot(const Workbench& workbench);
	void resetServiceResponse(const Workbench& workbench);
	[[nodiscard]] telemetry::integration::inspector::NodeResult
	node(const QModelIndex& index) const;
	void updateScalar(const QModelIndex& index);
	void clear();
	[[nodiscard]] bool changing() const noexcept;
	QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
	QModelIndex parent(const QModelIndex& index) const override;
	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;
	bool canFetchMore(const QModelIndex& parent) const override;
	void fetchMore(const QModelIndex& parent) override;
	bool hasChildren(const QModelIndex& parent = {}) const override;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
	void resetSource(const Inspector* inspector, const Workbench* workbench, bool response = false);
};

} // namespace inspector_qml
#endif // TELEMETRY_EXAMPLES_INSPECTOR_QML_MODELS_HPP
