/*
 * @file DynamicModels.hpp
 * @brief Qt-owned endpoint metadata and lazy Inspector snapshot tree models.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_UX_DYNAMIC_MODELS_HPP
#define TELEMETRY_UX_DYNAMIC_MODELS_HPP
#pragma once

#include "PresentationOverlay.hpp"

#include <integration/inspector/InspectorModel.hpp>
#include <integration/workbench/WorkbenchModel.hpp>

#include <QtCore/QAbstractItemModel>
#include <QtCore/QVariantMap>

#include <cstddef>
#include <memory>
#include <optional>

namespace telemetry::ux {

class EndpointPreviews;

// One endpoint of the device as the tree holds it, for finding it by name or
// path (BA-1). category: 1 Field, 2 Command, 3 Service.
struct EndpointEntry {
	int category = 0;
	std::uint32_t id = 0;
	QString group, name, path;
	// A Field's type; a Command's or Service's signature, "(u16)", "() -> f32".
	QString typeText;
	// A Command's or Service's request bytes; 0 takes no input.
	std::uint32_t requestBytes = 0;
	bool readable = false, writable = false, aggregate = false;
};

// The largest Field value the GUI reads whole (a struct read for its
// members included) and the largest it writes back.
inline constexpr std::size_t workbenchPayloadBytes = 4096;
inline constexpr std::size_t workbenchSnapshotBytes = 65536;
using Inspector =
    telemetry::integration::inspector::InspectorModel<8, workbenchPayloadBytes, workbenchSnapshotBytes>;
// ReadMany copies its bounded binding into the existing FIFO completion slot.
// This opt-in GUI budget does not change the portable Workbench default.
using Workbench = telemetry::integration::workbench::WorkbenchModel<8, workbenchPayloadBytes,
                                                                    workbenchSnapshotBytes, 1536>;
using WorkbenchView = telemetry::integration::workbench::WorkbenchView<8, workbenchPayloadBytes,
                                                                       workbenchSnapshotBytes, 1536>;

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
		WritableRole,
		TypeTextRole,
		AccessTextRole,
		UnitRole,
		WireBytesRole,
		TypeIdRole,
		ResponseTypeTextRole,
		PreviewEligibleRole,
		PreviewFieldIdRole,
		PreviewAggregateRole,
		PreviewChildRole,
		PreviewValueRole,
		PreviewStateRole,
		PreviewAgeMsRole,
		PreviewReasonRole,
		// A category's or group's number of endpoints; 0 on other rows.
		CountRole,
		// A Field's or a value's path: "device.Config.Gain", "data.Samples[3]";
		// a range row's names its range, "data.Samples[0 … 255]"; a group's
		// or category's name.
		PathRole,
		// The row's values as Watchlist leaf ordinals of its Field:
		// [first, first + count). A scalar Field is 0, 1; groups are 0, 0.
		WatchLeafFirstRole,
		WatchLeafCountRole,
		// What a struct or array row holds, for a box's words: "members" (a
		// struct of scalars), "elements" (an array of scalars) or "values"
		// (anything deeper); empty on other rows.
		WatchLeafKindRole
	};
	Q_ENUM(Role)

	explicit EndpointModel(QObject* parent = nullptr);
	~EndpointModel() override;
	void rebuild(const Inspector& inspector);
	void rebuild(const Workbench& workbench);
	void rebuild(const WorkbenchView& view);
	void clear();
	[[nodiscard]] bool changing() const noexcept;
	void setPresentation(const PresentationOverlay& overlay, const QString& fingerprint,
	                     bool notify = true);
	// Copy labels and sizes from the currently admitted schema. No schema or
	// descriptor views are retained, and metadata publication never sends I/O.
	void setSchemaMetadata(const telemetry::runtime::RuntimeSchema& schema);
	void setPreviews(EndpointPreviews* previews);
	// Invalid QVariant means no demand; Field ID zero is a valid demand.
	Q_INVOKABLE QVariant previewDemandField(const QModelIndex& index) const;
	Q_INVOKABLE bool previewOnlyChange(const QList<int>& roles) const;
	Q_INVOKABLE QVariantMap endpoint(const QModelIndex& index) const;
	Q_INVOKABLE bool isGroup(const QModelIndex& index) const;
	// What PathRole holds; empty for an invalid or stale index.
	Q_INVOKABLE QString pathOf(const QModelIndex& index) const;
	// Resolve the current model index without retaining one across a reset.
	bool matchesEndpoint(const QModelIndex& index, int category, std::uint32_t id) const;
	// Whether the current device's Field with this id accepts writes.
	[[nodiscard]] bool fieldWritable(std::uint32_t id) const;
	// The Field's value size in bytes, and its path; 0 and empty when unknown.
	[[nodiscard]] std::uint32_t fieldWireBytes(std::uint32_t id) const;
	[[nodiscard]] QString fieldPath(std::uint32_t id) const;
	// Every endpoint, in the tree's order; empty while the tree changes.
	[[nodiscard]] QVector<EndpointEntry> endpointList() const;
	// The endpoint row with this category and id, or an invalid index.
	[[nodiscard]] QModelIndex endpointIndex(int category, std::uint32_t id) const;
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
	void rebuildSource(const Inspector* inspector, const Workbench* workbench,
	                   const WorkbenchView* view = nullptr);
	void previewSourceChanged();
	void previewValuesChanged();
};

class ValueModel final : public QAbstractItemModel {
	Q_OBJECT
	// Every value is from an earlier run (a response whose request changed
	// since), and the note that says which.
	Q_PROPERTY(bool stale READ stale NOTIFY staleChanged)
	Q_PROPERTY(QString staleNote READ staleNote NOTIFY staleChanged)
	// How many levels the deepest value lies below the root row (the root is
	// 0), counting the first element of every array: what a tree table needs
	// to choose its indent step. 0 for a scalar and while nothing is shown.
	Q_PROPERTY(int deepest READ deepest NOTIFY deepestChanged)
	// The rows are a structure-only state (resetShape): a table that is not
	// read or called yet, or one kept as it was while an action runs.
	Q_PROPERTY(bool shape READ shape NOTIFY shapeChanged)
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
		EditorTextRole,
		EditorKindRole,
		EditorChoicesRole,
		EditorChoicesTruncatedRole,
		CanonicalTextRole,
		TypeTextRole,
		// What the cell says while its text is not a value of its type:
		// "Enter a number from 0 to 255." Empty on rows that take no text.
		TakesRole,
		// Not yet sent (family spec C): a draft's value that differs from the
		// value read, or a request's value that differs from the one sent last.
		UnsentRole,
		// A value from an earlier run, shown italic and muted, and why.
		StaleRole,
		StaleNoteRole,
		// The row's dotted path from the table's first level: "" for the
		// root, "Calib", "Calib.Phase[0]" (the format of texts()).
		PathRole,
		// A folded branch's members as text, at most eight, whole parts:
		// "Gain 1.5" for a struct's member, "0" for an array's element.
		SummaryPartsRole,
		// "struct", "array" or "arrayOfAggregates"; empty on a leaf.
		SummaryKindRole,
		// How many elements or members a branch holds: an array's length
		// even when it is shown as ranges (ChildCountRole counts the ranges).
		ItemCountRole,
		// How many values under a row are not yet sent (0 or 1 on a leaf).
		UnsentCountRole,
		// The value differs from the one the table showed before the action
		// that just finished (noteUpdate()): the cell flashes once. False on
		// a branch, and on every row of a table shown for the first time.
		UpdatedRole
	};
	Q_ENUM(Role)
	// Which table a structure-only state is shown in: a Field's value (with
	// its root row), or a section's rows (a request's, a response's).
	enum class Section { Value, Request, Response };
	static constexpr int fetchBatch = 128;
	// The most rows a structure-only state holds; a larger type keeps its
	// first rows, shallowest first.
	static constexpr std::size_t shapeRowLimit = 4096;

	explicit ValueModel(QObject* parent = nullptr);
	~ValueModel() override;
	void resetSnapshot(const Inspector& inspector);
	void resetSnapshot(const Workbench& workbench);
	void resetServiceResponse(const Workbench& workbench);
	void resetSnapshot(const WorkbenchView& view);
	// Read-only projection of a caller-owned, admission-guarded value snapshot.
	void resetSnapshot(const telemetry::integration::inspector::SnapshotStore& snapshot);
	// An editable projection of a caller-owned request editor's draft (the
	// Batch's item, BA-4a): every scalar row takes text; the owner writes it
	// through the editor and updateScalar() shows it.
	void resetDraft(const telemetry::integration::workbench::RequestEditor& editor);
	void resetServiceResponse(const WorkbenchView& view);
	// A structure-only state: the rows of a type from the schema alone, no
	// bytes and nothing borrowed afterwards. Each value shows `shown`'s text
	// for its path (see shownTexts()) or "–" when it has none; nothing takes
	// text. What a table holds while nothing is read or called yet (a
	// Service's response before its call), or while an action runs (the
	// values it was sent with, "as they were").
	void resetShape(const telemetry::runtime::RuntimeSchema& schema, TypeId type, Section section,
	                const QHash<QString, QString>& shown = {});
	// Whether the rows are a structure-only state.
	[[nodiscard]] bool shape() const noexcept;
	// The table the next rows are shown in, when their source does not say
	// it: a Batch write's draft is a Field's value (with its root row), a
	// request shown from a snapshot is a section's. Kept until changed;
	// nullopt follows the source (a request editor's rows are a section's).
	void setSection(std::optional<Section> section);
	// The values as they show now, by path (PathRole): what resetShape()
	// takes to keep a table as it was. Scalars only; "–" is left out.
	[[nodiscard]] QHash<QString, QString> shownTexts() const;
	// The next reset shows the same thing again, newer (a read, a call, a
	// run that just finished): its rows say which values changed
	// (UpdatedRole). Any other reset compares nothing.
	void noteUpdate();
	// What noteUpdate() kept, forgotten when no reset took it.
	void dropUpdate();
	[[nodiscard]] telemetry::integration::inspector::NodeResult
	node(const QModelIndex& index) const;
	void updateScalar(const QModelIndex& index);
	// A Field's cells are editable before a draft exists: the first change
	// opens it (Details, BA-4). The owner says when its Field may be edited;
	// a draft or a request editor makes the cells editable on their own.
	void setEditOffer(bool offered);
	[[nodiscard]] bool editOffer() const noexcept;
	// The values as sent last, by path, for the unsent marks of a request:
	// texts() is the rows' values now ({"factor": "0.1"}), setBaseline takes
	// them as the values sent, clearBaseline drops them. A reset drops them too.
	[[nodiscard]] QHash<QString, QString> texts() const;
	void setBaseline(const QHash<QString, QString>& sent);
	void clearBaseline();
	[[nodiscard]] bool hasBaseline() const noexcept;
	// "value=2 factor=3": the baseline's top-level values, for a stale note.
	[[nodiscard]] QString baselineLine() const;
	// The rows whose value is not yet sent.
	[[nodiscard]] int unsentCount() const;
	// Every row is from an earlier run; note says which, for the tooltip.
	void setStale(bool stale, const QString& note = {});
	[[nodiscard]] bool stale() const noexcept;
	[[nodiscard]] QString staleNote() const;
	// The rows to show without the root row the model keeps: the root of a
	// struct or array, or an invalid index (a scalar has no root row to hide).
	Q_INVOKABLE QModelIndex contentRoot() const;
	// The row at a dotted path (see PathRole), fetching the batches of members
	// it lies in; invalid when the path no longer exists.
	Q_INVOKABLE QModelIndex indexOfPath(const QString& path);
	// The rows are a section's own (a request's or a response's), whose root is
	// the section's heading and not a row; false for a Field's value.
	Q_INVOKABLE bool sectioned() const;
	// What PathRole holds; empty for an invalid or stale index.
	Q_INVOKABLE QString pathOf(const QModelIndex& index) const;
	[[nodiscard]] int deepest() const noexcept;
	void clear();
	[[nodiscard]] bool changing() const noexcept;
	void setPresentation(const PresentationOverlay& overlay, const QString& fingerprint,
	                     int category, quint32 id, const QString& role, bool notify = true);
	// The host supplies its admitted schema; only Qt-owned enum choices remain.
	// Boolean choices need no schema. Unknown enum codes stay editable as text.
	void setSchemaMetadata(const telemetry::runtime::RuntimeSchema& schema);
	QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
	QModelIndex parent(const QModelIndex& index) const override;
	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;
	bool canFetchMore(const QModelIndex& parent) const override;
	void fetchMore(const QModelIndex& parent) override;
	bool hasChildren(const QModelIndex& parent = {}) const override;

Q_SIGNALS:
	void staleChanged();
	void deepestChanged();
	void shapeChanged();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
	void resetSource(const Inspector* inspector, const Workbench* workbench, bool response = false,
	                 const WorkbenchView* view = nullptr,
	                 const telemetry::integration::inspector::SnapshotStore* snapshot = nullptr,
	                 const telemetry::integration::workbench::RequestEditor* draft = nullptr);
};

} // namespace telemetry::ux
#endif // TELEMETRY_UX_DYNAMIC_MODELS_HPP
