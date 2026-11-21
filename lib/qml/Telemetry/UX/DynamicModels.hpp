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
#include <QtCore/QSet>
#include <QtCore/QVariantMap>

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

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

// The Device tree's groups, one tree for all three families (the designer's
// review of 20 November 2026, 14.1): in pre-order, each naming its parent's
// place in the list (-1 at the top), with the endpoints it holds of each
// family in declaration order. A flat (v3.0) descriptor gives one level, its
// same-named catalogs of different families one group (outlineOf in
// DynamicModels.cpp); a group tree (v3.1, part B) gives its group records as
// they are. The tree shows a group's subgroups first, then its Fields,
// Commands and Services.
struct GroupOutline {
	struct Group {
		QString name;
		int parent = -1;
		// Indexes into the descriptor's endpoints of each family:
		// [0] Fields, [1] Commands, [2] Services.
		std::array<std::vector<std::size_t>, 3> endpoints;
	};
	std::vector<Group> groups;
};

// All calls run on the Inspector owner thread. The Inspector object must outlive
// these models. Materialized labels and values are Qt-owned copies; the controller
// clears/resetSnapshot() before changing the displayed admission or snapshot.
class EndpointModel final : public QAbstractItemModel {
	Q_OBJECT
	// The longest type of a Field of the device ("struct (3)", "f32[256]"):
	// the Device tree's Type column is as wide as it, once per descriptor, so
	// opening a branch never moves the column. Empty until the types are known.
	Q_PROPERTY(QString widestFieldType READ widestFieldType NOTIFY widestFieldTypeChanged)
	// The same with every scope cut to "…::" ("…::Config[4]"): the widest
	// type once the column gives its scopes way (the designer's review, 14.2).
	Q_PROPERTY(QString widestShortFieldType READ widestShortFieldType NOTIFY widestFieldTypeChanged)
	// The device's Fields, Commands and Services (the descriptor's counts):
	// what the kinds filter's menu says beside each kind.
	Q_PROPERTY(int fieldTotal READ fieldTotal NOTIFY totalsChanged)
	Q_PROPERTY(int commandTotal READ commandTotal NOTIFY totalsChanged)
	Q_PROPERTY(int serviceTotal READ serviceTotal NOTIFY totalsChanged)
	// How deep the deepest group lies, the top level 0; -1 without groups.
	// The tree's indent step is chosen for a name at this depth plus 2 (a
	// Field and one member level), once per descriptor.
	Q_PROPERTY(int deepestGroup READ deepestGroup NOTIFY totalsChanged)
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
		// A group's endpoints, everything under it; 0 on other rows.
		CountRole,
		// A Field's or a value's path in the one grammar (PathGrammar.hpp):
		// "device/Config.Gain", "data/Samples[3]"; a range row's names its
		// range, "data/Samples[0 … 255]"; a group's its groups' path,
		// "metrology/harmonics".
		PathRole,
		// The row's values as Watchlist leaf ordinals of its Field:
		// [first, first + count). A scalar Field is 0, 1; groups are 0, 0.
		WatchLeafFirstRole,
		WatchLeafCountRole,
		// What a struct or array row holds, for a box's words: "members" (a
		// struct of scalars), "elements" (an array of scalars) or "values"
		// (anything deeper); empty on other rows.
		WatchLeafKindRole,
		// What the row is, for its glyph: "group", "struct", "array", "value"
		// (a scalar or an enum), "command" or "service"; a value inside a
		// Field by its own type, a range of an array "array".
		KindRole,
		// An enum's names for the row's tooltip, "Off, Low, High"; empty on
		// any other row.
		TypeNamesRole,
		// A group's Fields, Commands, Services and groups, everything under
		// it counted (the group's words: "7 Fields · 1 Command"); 0 on other rows.
		FieldCountRole,
		CommandCountRole,
		ServiceCountRole,
		GroupCountRole,
		// The row's named type, "adc::Config" (an array's element's for
		// "adc::Config[4]"), what find matches as a type; a Command's or a
		// Service's signature when it names one; empty while the descriptor
		// names no types (v3.0).
		TypeNameRole,
		// The row's type as a screen reader says it, own name first:
		// "struct Config in adc, 3 members"; empty on groups, Commands and
		// Services.
		TypeSpokenRole
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
	// Where the types come from when no previews give them: a Field's rows
	// under it (its members, as Details shows its definition) are made from
	// this schema. Previews, when set, take precedence. Nothing is retained
	// from it between calls.
	void setSchemaSource(std::function<telemetry::runtime::RuntimeSchema()> source);
	// Invalid QVariant means no demand; Field ID zero is a valid demand.
	Q_INVOKABLE QVariant previewDemandField(const QModelIndex& index) const;
	Q_INVOKABLE bool previewOnlyChange(const QList<int>& roles) const;
	Q_INVOKABLE QVariantMap endpoint(const QModelIndex& index) const;
	Q_INVOKABLE bool isGroup(const QModelIndex& index) const;
	// What PathRole holds; empty for an invalid or stale index.
	Q_INVOKABLE QString pathOf(const QModelIndex& index) const;
	// The row at a path of names (NameRole) from the root, joined by "/":
	// "device/Config", "device/Config/Gain"; the values of a Field are
	// fetched on the way. Invalid when no such row. What the Device tree
	// keeps of its open branches across a reconnect.
	Q_INVOKABLE QModelIndex indexOfPath(const QString& path);
	// The group row at a group's path in the one grammar ("metrology/harmonics",
	// "power\.meter"), or invalid: what a breadcrumb's segment reveals.
	Q_INVOKABLE QModelIndex indexOfGroupPath(const QString& path) const;
	[[nodiscard]] QString widestFieldType() const;
	[[nodiscard]] QString widestShortFieldType() const;
	[[nodiscard]] int fieldTotal() const noexcept;
	[[nodiscard]] int commandTotal() const noexcept;
	[[nodiscard]] int serviceTotal() const noexcept;
	[[nodiscard]] int deepestGroup() const noexcept;
	// The device's group names as a flat descriptor names them: what a
	// dotted key of an older project is read against (PathGrammar.hpp).
	[[nodiscard]] QSet<QString> groupNames() const;
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

Q_SIGNALS:
	void widestFieldTypeChanged();
	void totalsChanged();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
	void setWidestFieldType(const QString& type, const QString& shortType = {});
	void setTotals(int fields, int commands, int services, int deepest);
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
		UpdatedRole,
		// What the row is, for its glyph (the Device tree design, 18 Nov
		// 2026): "struct", "array" (a range of an array too) or "value".
		KindRole,
		// A quiet line under the value's closed cell (WL-9 C4): "Not applied
		// · device holds 3" on a value kept after a refused Apply; "" else.
		NoteRole
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
	// takes to keep a table as it was. Scalars only; "–" is left out. The
	// first level under the root is loaded first: a table shows it at once.
	[[nodiscard]] QHash<QString, QString> shownTexts();
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
	// The values not yet sent, by path, as typed (editorText): a draft's
	// edited values, kept when the device refuses its Apply (WL-9 C4).
	[[nodiscard]] QHash<QString, QString> unsentTexts() const;
	// Quiet lines under closed cells, by path (NoteRole); a reset drops them,
	// clearNote drops one (the user edits its value).
	void setNotes(const QHash<QString, QString>& notes);
	void clearNote(const QModelIndex& index);
	[[nodiscard]] bool hasNotes() const noexcept;
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
