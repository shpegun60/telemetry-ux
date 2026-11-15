// Shared native execution, independent Qt presentation. Ruslan Kovtun, codexAi. MIT.
#include "DynamicConfiguratorProvider.hpp"
#include "RequestArguments.hpp"
#include "SchemaText.hpp"
#include "EndpointPreviews.hpp"
#include "DynamicWatchlist.hpp"
#include "DynamicBatch.hpp"
#include "EndpointPaths.hpp"
#include "ScalarEditor.hpp"
#include "ScalarEditorMetadata.hpp"
#include "WatchCoverage.hpp"
#include "WatchRows.hpp"
#include "WatchTraces.hpp"
#include "WatchFormat.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QPersistentModelIndex>
#include <QtCore/QRegularExpression>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QThread>
#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace telemetry::ux {
namespace cl = telemetry::client;
namespace ins = telemetry::integration::inspector;
namespace wb = telemetry::integration::workbench;
namespace rt = telemetry::runtime;

class DynamicViewState {
public:
	// A provider may outlive its binding after owner retirement. Keep backing
	// storage alive independently, and destroy it after the native view.
	std::shared_ptr<void> storage;
	// These are panel response bytes, not a second client/transport/FIFO.
	std::array<std::byte, 65536> response{};
	WorkbenchView view;
	enum class Action {
		None,
		Read,
		Apply,
		Command,
		Service
	} action = Action::None;
	std::optional<ins::ReadResult> read;
	std::optional<wb::ApplyResult> applied;
	std::optional<ins::OperationResult> commanded;
	std::optional<wb::ServiceResult> serviced;
	bool retired = false, dirty = false, staleData = false;
	bool eventReported = true;
	std::uint32_t endpointId = 0, submittedSession = 0;
	std::uint64_t sequence = 0;

	DynamicViewState(Workbench& owner, std::shared_ptr<void> token)
	    : storage(std::move(token)), view(owner, response)
	{}

	void retire()
	{
		if (!retired) {
			retired = true;
			view.retire();
			dirty = true;
		}
	}
};

struct DynamicConfiguratorBinding::Impl {
	Workbench& owner;
	std::shared_ptr<void> storage;
	std::vector<std::shared_ptr<DynamicViewState>> views;
	std::uint64_t admission;
	std::uint32_t session;
	QString fingerprint;
	bool retired = false, allowed = false, notifying = false;

	Impl(Workbench& value, std::shared_ptr<void> token, QString identity)
	    : owner(value), storage(std::move(token)), admission(value.snapshotInfo().admission),
	      session(value.session()), fingerprint(std::move(identity))
	{}

	bool current() const
	{
		return storage && !retired && owner.isCurrent() && owner.session() == session &&
		       owner.snapshotInfo().admission == admission;
	}
};

DynamicConfiguratorBinding::DynamicConfiguratorBinding(Workbench& owner,
                                                       std::shared_ptr<void> ownerStorage,
                                                       QObject* parent, QString fingerprint)
    : QObject(parent),
      d(std::make_unique<Impl>(owner, std::move(ownerStorage), std::move(fingerprint)))
{}

DynamicConfiguratorBinding::~DynamicConfiguratorBinding()
{
	retire();
	// The host owns progress and transport retirement. Destroying a registry
	// cannot silently release a callback's native buffers before completion.
	if (!drained())
		qFatal("DynamicConfiguratorBinding must be drained before destruction");
	poll();
}

bool DynamicConfiguratorBinding::current() const
{
	return d->current();
}

QString DynamicConfiguratorBinding::fingerprint() const
{
	return current() ? d->fingerprint : QString{};
}

bool DynamicConfiguratorBinding::drained() const
{
	return std::all_of(d->views.begin(), d->views.end(), [](const auto& state) {
		return state->view.drained();
	});
}

bool DynamicConfiguratorBinding::mutationsAllowed() const
{
	return current() && d->allowed;
}

void DynamicConfiguratorBinding::setMutationsAllowed(bool allowed)
{
	Q_ASSERT(thread() == QThread::currentThread());
	if (d->allowed == allowed)
		return;
	d->allowed = allowed;
	Q_EMIT changed();
}

std::shared_ptr<DynamicViewState> DynamicConfiguratorBinding::acquire()
{
	Q_ASSERT(thread() == QThread::currentThread());
	if (!current())
		return {};
	auto state = std::make_shared<DynamicViewState>(d->owner, d->storage);
	if (state->view.synchronize() != cl::Status::Ok)
		return {};
	d->views.push_back(state);
	return state;
}

void DynamicConfiguratorBinding::retire()
{
	Q_ASSERT(thread() == QThread::currentThread());
	if (d->retired)
		return;
	d->retired = true;
	d->allowed = false;
	for (const auto& state : d->views)
		state->retire();
}

void DynamicConfiguratorBinding::poll()
{
	Q_ASSERT(thread() == QThread::currentThread());
	if (d->notifying)
		return;
	if (!current())
		retire();
	const bool dirty = std::any_of(d->views.begin(), d->views.end(), [](const auto& state) {
		return state->dirty;
	});
	std::vector<QVariantMap> completed;
	for (const auto& state : d->views) {
		if (state->eventReported)
			continue;
		const ins::OperationResult* operation = nullptr;
		if (state->read)
			operation = &state->read->operation;
		else if (state->applied)
			operation = &state->applied->write;
		else if (state->commanded)
			operation = &*state->commanded;
		else if (state->serviced)
			operation = &state->serviced->operation;
		if (!operation)
			continue;
		QVariantMap facts{
		    {QStringLiteral("operation"), static_cast<unsigned>(operation->operation)},
		    {QStringLiteral("status"), static_cast<unsigned>(operation->status)},
		    {QStringLiteral("dispatch"), operation->dispatch},
		    {QStringLiteral("endpointStatus"), operation->endpointStatus},
		    {QStringLiteral("succeeded"), operation->succeeded()},
		    {QStringLiteral("accepted"), operation->accepted()},
		    {QStringLiteral("executed"), operation->executed()},
		    {QStringLiteral("session"), state->submittedSession},
		    {QStringLiteral("endpointId"), state->endpointId},
		    {QStringLiteral("sequence"), QString::number(state->sequence)},
		    {QStringLiteral("panelRetired"), state->retired}};
		if (state->applied) {
			facts.insert(QStringLiteral("refreshAttempted"), state->applied->refreshAttempted);
			facts.insert(QStringLiteral("refreshPublished"), state->applied->refresh.published);
		}
		if (state->serviced) {
			facts.insert(QStringLiteral("published"), state->serviced->published);
			facts.insert(QStringLiteral("valueStatus"),
			             static_cast<unsigned>(state->serviced->valueStatus));
		}
		state->eventReported = true;
		completed.push_back(std::move(facts));
	}
	// Reclamation occurs after the native completion/publication stack returned.
	std::erase_if(d->views, [](const auto& state) {
		return state->retired && state->view.drained() && state.use_count() == 1;
	});
	if (dirty || !completed.empty()) {
		d->notifying = true;
		QPointer<DynamicConfiguratorBinding> alive(this);
		for (const auto& facts : completed) {
			Q_EMIT operationFinished(facts);
			if (!alive)
				return;
		}
		Q_EMIT changed();
		if (alive)
			d->notifying = false;
	}
}

void DynamicConfiguratorBinding::mutationAccepted(const std::shared_ptr<DynamicViewState>& origin)
{
	QPointer<DynamicConfiguratorBinding> alive(this);
	for (const auto& state : d->views) {
		if (state == origin || state->retired)
			continue;
		state->view.invalidateData();
		state->staleData = state->dirty = true;
	}
	if (origin)
		Q_EMIT managedMutationAccepted();
	if (alive)
		poll();
}

void DynamicConfiguratorBinding::invalidateData()
{
	mutationAccepted({});
}

void DynamicConfiguratorBinding::requestSubmitted()
{
	Q_ASSERT(thread() == QThread::currentThread());
	Q_EMIT submitted();
}

namespace {
QString operationText(const ins::OperationResult& result)
{
	if (result.status == cl::Status::OutcomeUnknown)
		return QStringLiteral("OutcomeUnknown - may have executed; no automatic retry");
	if (result.accepted())
		return QStringLiteral("Accepted - execution is not confirmed");
	if (result.executed())
		return QStringLiteral("Executed");
	return QStringLiteral("Status %1 - dispatch %2 - endpoint %3")
	    .arg(static_cast<unsigned>(result.status))
	    .arg(result.dispatch)
	    .arg(result.endpointStatus);
}

} // namespace

// One selected schema branch, reusing the endpoint model's local type traversal.
// It never asks the preview scheduler for demand and never invents a payload.
class EndpointDefinitionModel final : public QSortFilterProxyModel {
public:
	QPersistentModelIndex selected;
	void setSelection(const QModelIndex& index) {
		if (selected == index) return;
		beginFilterChange(); selected = index; endFilterChange(Direction::Rows);
	}
	QModelIndex rootIndex() const {
		return selected.isValid() ? mapFromSource(selected.parent()) : QModelIndex{};
	}
protected:
	bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
		if (!selected.isValid() || !sourceModel()) return false;
		const auto index = sourceModel()->index(row, 0, parent);
		for (QModelIndex p = selected; p.isValid(); p = p.parent())
			if (p == index) return true;
		for (QModelIndex p = index.parent(); p.isValid(); p = p.parent())
			if (p == selected) return true;
		return false;
	}
};

struct DynamicConfiguratorProvider::Impl {
	QPointer<DynamicConfiguratorProvider> q;
	QPointer<DynamicConfiguratorBinding> binding, desired;
	std::shared_ptr<DynamicViewState> state;
	// A second view only for writes from Watchlist rows, so a row write never
	// moves the Details selection or touches its draft.
	std::shared_ptr<DynamicViewState> writer;
	quint32 writeId = 0, watchWriteSession = 0;
	QString writeText, writeName, watchWrite, watchWriteKind;
	// The written row's type, to say why a text is not a value of it.
	WatchValueType writeType;
	// A value inside a struct or array Field: where it is, so the write walks
	// to it in the Field just read; and the value as it will be written.
	std::optional<WatchLeaves::Leaf> writeLeaf;
	QString writeIntended;
	bool watchWroteLast = false;
	EndpointModel endpoints;
	EndpointDefinitionModel definitions;
	QPointer<EndpointPreviews> previews;
	QList<QPointer<QObject>> previewViews;
	QMetaObject::Connection previewSourceChanged;
	quint64 previewRevision = 0;
	QPersistentModelIndex selectedIndex;
	ValueModel values, responses;
	// Before watch: a read completing while the Watchlist goes still finds it.
	WatchTraces traces;
	DynamicWatchlist watch;
	// After watch: it goes first, while the rows it shows still exist.
	WatchRows rows;
	// Until the Watchlist has a model: its format and the rows' own.
	WatchNumberFormat watchFormat = WatchNumberFormat::Original;
	QHash<QString, WatchNumberFormat> rowFormats;

	// The live Plot's readings (LP-1): the rows' paths, taken again whenever
	// the Watchlist's membership changed, then the frame's values.
	void feedTraces(const WatchFrame& frame)
	{
		const auto* model = qobject_cast<const WatchlistModel*>(watch.model());
		if (!model)
			return;
		if (traces.membership() != model->membership()) {
			QHash<quint64, QString> paths;
			QHash<QString, bool> categorical;
			for (int row = 0; row < model->count(); ++row) {
				const auto key = model->key(row);
				if (watch_key::isUnresolved(key))
					continue;
				const auto index = model->index(row, 0, {});
				const auto path = index.data(WatchlistModel::NameRole).toString();
				paths.insert(quint64(key), path);
				const bool states = index.data(WatchlistModel::CategoricalRole).toBool();
				categorical.insert(path, states);
				// An enum's names by code, from its editor's choices
				// ("Idle (0)" and "0").
				if (states) {
					QHash<qint64, QString> names;
					for (const auto& choice : index.data(WatchlistModel::EditorChoicesRole).toList()) {
						const auto map = choice.toMap();
						bool ok = false;
						const auto code = map.value(QStringLiteral("text")).toString().toLongLong(&ok);
						auto label = map.value(QStringLiteral("label")).toString();
						const auto open = label.lastIndexOf(QStringLiteral(" ("));
						if (ok && open > 0)
							names.insert(code, label.left(open));
					}
					traces.setStates(path, std::move(names));
				}
			}
			traces.setPaths(model->membership(), std::move(paths), std::move(categorical), frame.completed.monotonicNs);
		}
		traces.append(frame);
	}
	// Moves with every change of a format, from wherever it comes (a panel, a
	// project loaded): a reset's Undo holds only while it has not moved.
	quint64 formatRevision = 0;
	// The rows shown in a format of their own, counted once per change of the
	// formats or the rows, not on every reading (WL-3c).
	mutable quint64 ownCountedRevision = ~quint64{0}, ownCountedMembership = ~quint64{0};
	mutable int ownCount = 0;
	int ownFormatRows() const
	{
		const auto* model = qobject_cast<const WatchlistModel*>(watch.model());
		if (ownCountedRevision == formatRevision && ownCountedMembership == watch.membership())
			return ownCount;
		ownCount = 0;
		for (int row = 0; model && row < model->count(); ++row)
			if (const auto own = model->rowFormat(row); own && *own != watchFormat)
				++ownCount;
		ownCountedRevision = formatRevision;
		ownCountedMembership = watch.membership();
		return ownCount;
	}
	QHash<QString, WatchNumberFormat> pathFormats() const
	{
		const auto* model = qobject_cast<const WatchlistModel*>(watch.model());
		return model ? model->pathFormats() : rowFormats;
	}
	// false, and nothing changed, while the model refuses (it is changing).
	bool putPathFormats(QHash<QString, WatchNumberFormat> formats)
	{
		if (auto* model = qobject_cast<WatchlistModel*>(watch.model()))
			return model->setPathFormats(std::move(formats));
		rowFormats = std::move(formats);
		return true;
	}
	void putFormat(WatchNumberFormat format)
	{
		watchFormat = format;
		if (auto* model = qobject_cast<WatchlistModel*>(watch.model()))
			model->setFormat(format);
	}
	DynamicBatch batch;
	QString selection, responseEndpoint, status = QStringLiteral("Not connected"), write, command,
	                                     service;
	bool writeAttention = false, commandAttention = false, serviceAttention = false;
	std::uint32_t writeSession = 0, commandSession = 0, serviceSession = 0;
	int category = 0;
	quint32 responseId = 0;
	PresentationOverlay presentation;
	QString presentationError;
	bool allowed = false, updating = false;
	QSet<quintptr> invalid;
	QMetaObject::Connection changed, destroyed;
	// The Device tree's watch boxes: what can be watched under each node,
	// a counter that moves when the tree changes, the Fields held as of a
	// membership count, and the last change setWatchMany made.
	WatchCoverage coverage;
	quint64 treeEpoch = 0;
	// Moves when the boxes become answerable or stop being so (a model update,
	// a connection, a Watchlist source), so a row that answered -1 asks again.
	quint64 availability = 0;
	bool available = false;
	QSet<quint64> heldKeys;
	quint64 heldMembership = ~quint64{0};
	// The one change Undo takes back: rows added or removed, or (formats) a
	// reset of the number formats, with what they were.
	struct WatchUndo {
		bool valid = false, added = false, formats = false;
		QSet<WatchKey> addedIds;
		QVector<WatchlistModel::Removed> removed;
		quint64 membership = 0, treeEpoch = 0, formatRevision = 0;
		WatchNumberFormat format = WatchNumberFormat::Original;
		QHash<QString, WatchNumberFormat> pathFormats;
	} undo;

	const QSet<quint64>& held()
	{
		if (heldMembership != watch.membership()) {
			heldKeys.clear();
			for (const auto id : watch.held())
				heldKeys.insert(id);
			heldMembership = watch.membership();
		}
		return heldKeys;
	}

	// Built on first use after the tree changed: the Fields' ids collected in
	// one walk, the Watchlist asked once which it can hold, then the tree
	// walked once more into the coverage. Not built while the Watchlist cannot
	// tell, so an empty answer is never kept.
	void ensureCoverage()
	{
		if (coverage.built() || !current() || endpoints.changing() || !watch.canTellHoldable())
			return;
		QSet<quint32> fields;
		QVector<QModelIndex> stack{QModelIndex{}};
		while (!stack.isEmpty()) {
			const auto parent = stack.takeLast();
			for (int row = 0, rows = endpoints.rowCount(parent); row < rows; ++row) {
				const auto child = endpoints.index(row, 0, parent);
				if (endpoints.data(child, EndpointModel::IsEndpointRole).toBool()) {
					if (endpoints.data(child, EndpointModel::CategoryRole).toInt() == 1 &&
					    endpoints.data(child, EndpointModel::ReadableRole).toBool())
						fields.insert(endpoints.data(child, EndpointModel::EndpointIdRole).toUInt());
				} else if (endpoints.rowCount(child) > 0) {
					stack.push_back(child);
				}
			}
		}
		const auto answer = watch.holdable(fields);
		if (!answer)
			return;
		const auto& holdable = *answer;
		// A struct or array Field counts its values from the schema, opened or
		// not, so the walk stops at every endpoint.
		coverage.build(
		    endpoints,
		    [this, &holdable](const QModelIndex& index) -> std::optional<WatchCoverage::Holdable> {
			    if (!endpoints.data(index, EndpointModel::IsEndpointRole).toBool() ||
			        endpoints.data(index, EndpointModel::CategoryRole).toInt() != 1 ||
			        !endpoints.data(index, EndpointModel::ReadableRole).toBool())
				    return std::nullopt;
			    const auto id = endpoints.data(index, EndpointModel::EndpointIdRole).toUInt();
			    const auto found = holdable.constFind(id);
			    if (found == holdable.cend())
				    return std::nullopt;
			    return WatchCoverage::Holdable{watch_key::whole(id), endpoints.pathOf(index), found->leaves,
			                                   found->aggregate};
		    },
		    [this](const QModelIndex& index) {
			    return !endpoints.data(index, EndpointModel::IsEndpointRole).toBool();
		    });
	}

	void treeChanged()
	{
		coverage.reset();
		++treeEpoch;
	}

	explicit Impl(DynamicConfiguratorProvider* owner) : q(owner), watch(owner)
	{}

	bool current() const
	{
		return binding && binding->current() && state && !state->retired && state->view.isCurrent();
	}

	void present(bool notify = true)
	{
		const auto fingerprint = current() ? binding->fingerprint() : QString{};
		endpoints.setPresentation(presentation, fingerprint, notify);
		values.setPresentation(presentation, fingerprint, category, state ? state->endpointId : 0,
		                       category == 1 ? QStringLiteral("value") : QStringLiteral("request"),
		                       notify);
		responses.setPresentation(presentation, fingerprint, 3, responseId,
		                          QStringLiteral("response"), notify);
	}

	bool busy() const
	{
		return state && state->action != DynamicViewState::Action::None;
	}

	// Everything the panel does stays available while its Watchlist polls:
	// selecting, reading into Details, editing, writing, Commands, Services
	// and Batch. The Watchlist reads through the session's one ordered FIFO,
	// skips its tick while any of these is pending, and holds no part of this
	// view (a read never replaces the snapshot, a draft or a response). Other
	// panels on the same session could always do this; now this one can too.
	bool selectable() const
	{
		return current() && !busy() && !batch.active() && !updating && !endpoints.changing() &&
		       !values.changing() && !responses.changing();
	}

	bool mutate() const
	{
		return selectable() && allowed && binding->mutationsAllowed() && !state->staleData;
	}

	std::optional<BatchRow> batchRow(const QString& operation) const
	{
		if (!selectable() || !invalid.isEmpty() || !state || state->staleData)
			return {};
		BatchRow row;
		row.id = state->endpointId;
		row.session = state->view.session();
		row.name = selection;
		if (operation == QStringLiteral("read") && category == 1) {
			const auto bound = state->view.bindField(row.id);
			if (!bound)
				return {};
			row.operation = cl::Operation::FieldRead;
			row.responseType = bound.endpoint.responseType();
			row.responseBytes = bound.endpoint.responseBytes();
		} else if (operation == QStringLiteral("write") && category == 1 && state->view.editing()) {
			const auto snapshot = state->view.snapshotInfo();
			if (!snapshot.ready || snapshot.bytes > 4096)
				return {};
			row.operation = cl::Operation::FieldWrite;
			row.requestType = snapshot.type;
			row.request.resize(qsizetype(snapshot.bytes));
		} else if (operation == QStringLiteral("command") && category == 2 &&
		           state->view.canExecute()) {
			const auto bound = state->view.bindCommand(row.id);
			if (!bound)
				return {};
			row.operation = cl::Operation::Command;
			row.requestType = bound.endpoint.requestType();
			row.request.resize(bound.endpoint.requestBytes());
		} else if (operation == QStringLiteral("service") && category == 3 &&
		           state->view.canCallService()) {
			const auto bound = state->view.bindService(row.id);
			if (!bound)
				return {};
			row.operation = cl::Operation::Service;
			row.requestType = bound.endpoint.requestType();
			row.responseType = bound.endpoint.responseType();
			row.responseBytes = bound.endpoint.responseBytes();
			row.request.resize(bound.endpoint.requestBytes());
		} else
			return {};
		return row;
	}

	// A Batch row for an endpoint found by name or path (BA-1): a Field read,
	// or a Command or Service that takes no input. No draft, no selection.
	// Whether a Command or Service takes input, from its bound endpoint.
	bool takesInput(const EndpointEntry& entry) const
	{
		if (entry.category == 1)
			return false;
		if (!current() || !state)
			return entry.requestBytes > 0;
		if (entry.category == 2) {
			const auto bound = state->view.bindCommand(entry.id);
			return bound ? bound.endpoint.requestBytes() != 0 : entry.requestBytes > 0;
		}
		const auto bound = state->view.bindService(entry.id);
		return bound ? bound.endpoint.requestBytes() != 0 : entry.requestBytes > 0;
	}

	// No draft and no selection is used, so a stale Details view does not
	// stop it (a write from the Watchlist marks that view stale).
	// request: a Command's or a Service's values typed in the row (BA-2).
	std::optional<BatchRow> pathRow(const EndpointEntry& entry, const QByteArray* request = nullptr) const
	{
		if (!selectable() || !state)
			return {};
		BatchRow row;
		row.id = entry.id;
		row.session = state->view.session();
		row.name = entry.path;
		if (entry.category == 1 && entry.readable) {
			const auto bound = state->view.bindField(row.id);
			if (!bound)
				return {};
			row.operation = cl::Operation::FieldRead;
			row.responseType = bound.endpoint.responseType();
			row.responseBytes = bound.endpoint.responseBytes();
		} else if (entry.category == 2) {
			const auto bound = state->view.bindCommand(row.id);
			const auto bytes = bound ? bound.endpoint.requestBytes() : 0;
			if (!bound || (bytes != 0 && (!request || std::size_t(request->size()) != bytes)))
				return {};
			row.operation = cl::Operation::Command;
			row.requestType = bound.endpoint.requestType();
			if (bytes)
				row.request = *request;
		} else if (entry.category == 3) {
			const auto bound = state->view.bindService(row.id);
			const auto bytes = bound ? bound.endpoint.requestBytes() : 0;
			if (!bound || (bytes != 0 && (!request || std::size_t(request->size()) != bytes)))
				return {};
			row.operation = cl::Operation::Service;
			if (bytes)
				row.request = *request;
			row.requestType = bound.endpoint.requestType();
			row.responseType = bound.endpoint.responseType();
			row.responseBytes = bound.endpoint.responseBytes();
		} else
			return {};
		return row;
	}

	QString history(const QString& value, std::uint32_t origin) const
	{
		return !value.isEmpty() && (!current() || origin != state->view.session())
		           ? value + QStringLiteral(" - previous session")
		           : value;
	}

	bool releasePreviewLeases()
	{
		// Rebinding one presentation must not retire the shared cache or the
		// other panels' leases. Each release can synchronously delete/rebind us.
		const auto revision = ++previewRevision;
		const auto views = std::move(previewViews);
		const QPointer<EndpointPreviews> cache = previews;
		for (const auto& view : views) {
			if (view && cache)
				cache->clearDemand(view);
			if (!q || previewRevision != revision)
				return false;
		}
		return q && previewRevision == revision;
	}

	void notify()
	{
		definitions.setSelection(current() ? QModelIndex(selectedIndex) : QModelIndex{});
		const bool now = current() && !updating && !endpoints.changing() && watch.canTellHoldable();
		if (now != available) {
			available = now;
			++availability;
		}
		if (q)
			Q_EMIT q->stateChanged();
	}

	void sync()
	{
		if (updating)
			return;
		// The host must first receive its durable completion record. A permission
		// notification between native progress and poll cannot consume that record.
		if (state && !state->eventReported &&
		    (state->read || state->applied || state->commanded || state->serviced))
			return;
		if (writer && !writer->eventReported && (writer->read || writer->applied))
			return;
		updating = true;
		std::optional<bool> readDone, applyDone, watchWriteDone;
		bool continueWatchWrite = false;
		if (writer && writer->read) {
			writer->read.reset();
			writer->action = DynamicViewState::Action::None;
			continueWatchWrite = true;
		}
		if (writer && writer->applied) {
			const auto result = *writer->applied;
			writer->applied.reset();
			writer->action = DynamicViewState::Action::None;
			const bool written = result.writeCompleted && result.write.succeeded();
			watchWriteDone = written && result.refreshCompleted && result.refresh.operation.succeeded() &&
			                 result.refresh.published;
			const auto back = *watchWriteDone ? readBack() : QString{};
			const auto words = [](const char* text) {
				return QCoreApplication::translate("DynamicConfiguratorProvider", text);
			};
			if (written && !back.isEmpty()) {
				watchWrite = (back == writeIntended ? words("Wrote %1 = %2 · read back %3.")
				                                    : words("Wrote %1 = %2; the device reads back %3."))
				                 .arg(writeName, writeIntended, back);
				watchWriteKind = back == writeIntended ? QStringLiteral("confirmed") : QStringLiteral("differs");
			} else if (written) {
				watchWrite = words("Wrote %1 = %2; it could not be read back.").arg(writeName, writeIntended);
				watchWriteKind = QStringLiteral("unconfirmed");
			} else if (writeLeaf && (result.write.status == cl::Status::PayloadFull ||
			                         result.write.status == cl::Status::BufferTooSmall)) {
				watchWrite = words("%1 is larger than one write, so a value in it can't be written on its own.")
				                 .arg(endpoints.fieldPath(writeId));
				watchWriteKind = QStringLiteral("refused");
			} else {
				watchWrite = writeName + (result.write.status == cl::Status::OutcomeUnknown
				                              ? QStringLiteral(": ")
				                              : QStringLiteral(" not written: ")) +
				             operationText(result.write);
				watchWriteKind = result.write.status == cl::Status::OutcomeUnknown ? QStringLiteral("unconfirmed")
				                                                                   : QStringLiteral("refused");
			}
		}
		std::optional<std::pair<bool, bool>> commandDone, serviceDone;
		if (state) {
			if (state->read) {
				const auto result = *state->read;
				state->read.reset();
				state->action = DynamicViewState::Action::None;
				const auto snapshot = state->view.snapshotInfo();
				readDone = current() && !state->staleData && result.published && snapshot.ready &&
				           snapshot.revision == result.revision;
				status =
				    *readDone ? QStringLiteral("Read complete") : operationText(result.operation);
			}
			if (state->applied) {
				const auto result = *state->applied;
				state->applied.reset();
				state->action = DynamicViewState::Action::None;
				applyDone = result.writeCompleted && result.write.succeeded() &&
				            result.refreshCompleted && result.refresh.operation.succeeded() &&
				            result.refresh.published;
				writeAttention = !*applyDone;
				write = *applyDone ? QStringLiteral("Applied - confirmed by fresh Field read")
				        : result.writeCompleted && result.write.succeeded()
				            ? QStringLiteral("Applied - readback unconfirmed")
				            : operationText(result.write);
				status = write;
			}
			if (state->commanded) {
				const auto result = *state->commanded;
				state->commanded.reset();
				state->action = DynamicViewState::Action::None;
				command = status = operationText(result);
				commandAttention = !result.executed();
				commandDone = {result.executed(), result.accepted()};
			}
			if (state->serviced) {
				const auto result = *state->serviced;
				state->serviced.reset();
				state->action = DynamicViewState::Action::None;
				serviceDone = {result.operation.succeeded(), result.published};
				serviceAttention = !result.operation.succeeded() ||
				                   (!result.published &&
				                    (result.valueStatus != integration::dynamic::ValueStatus::NoValue ||
				                     result.modelStatus != ins::ModelStatus::Ok));
				service = status = result.operation.succeeded()
				                       ? result.published
				                             ? QStringLiteral("Completed - response published")
				                             : QStringLiteral("Completed - value status %1")
				                                   .arg(static_cast<unsigned>(result.valueStatus))
				                       : operationText(result.operation);
			}
			if (!current()) {
				// Only a provider that is gone stops here: one rebound while
				// its leases were released still retires, and still announces
				// the outcomes it has already taken off the queue.
				if (!releasePreviewLeases() && !q) {
					updating = false;
					return;
				}
				watch.retire();
				batch.retire();
				// A disconnect breaks every line on the Plot.
				traces.breakAll();
				allowed = false;
				category = 0;
				selectedIndex = QModelIndex{};
				selection.clear();
				responseEndpoint.clear();
				invalid.clear();
				values.clear();
				responses.clear();
				endpoints.clear();
			} else if (state->dirty) {
				if (state->staleData) {
					// Polling goes on and reads again at once; a stopped list
					// shows its values as stale.
					watch.dataChanged();
					batch.invalidateValues();
					values.clear();
					responses.clear();
					invalid.clear();
					status = watchWroteLast
					             ? QStringLiteral("A Watchlist write changed device data - read again")
					             : QStringLiteral(
					                   "Data changed in another panel - read or prepare arguments again");
					watchWroteLast = false;
				} else {
					values.resetSnapshot(state->view);
					responses.resetServiceResponse(state->view);
				}
			}
			state->dirty = false;
		}
		updating = false;
		// A write, Command or Service may have changed watched values: the
		// Watchlist reads them again now rather than on its next tick.
		if (applyDone && state)
			watch.wake(state->endpointId);
		else if (commandDone || serviceDone)
			watch.wake();
		if (watchWriteDone.has_value()) {
			// After any finished write attempt (confirmed, refused or of unknown
			// outcome) the row shows the device's value: read it again at once
			// while a cycle runs, otherwise read the list once.
			if (watch.active())
				watch.wake(writeId);
			else
				watch.begin(false);
		}
		if (continueWatchWrite)
			submitWatchWrite();
		QPointer<DynamicConfiguratorProvider> alive = q;
		notify();
		if (alive && readDone)
			Q_EMIT alive->readFinished(*readDone);
		if (alive && applyDone)
			Q_EMIT alive->applyFinished(*applyDone);
		if (alive && commandDone)
			Q_EMIT alive->commandFinished(commandDone->first, commandDone->second);
		if (alive && serviceDone)
			Q_EMIT alive->serviceFinished(serviceDone->first, serviceDone->second);
		if (alive && watchWriteDone)
			Q_EMIT alive->watchWriteFinished(*watchWriteDone);
	}

	// The written value in the writer's current snapshot: the Field itself,
	// or the value the leaf's steps lead to.
	std::optional<ins::NodeHandle> nodeOf() const
	{
		if (!writer)
			return std::nullopt;
		const auto& view = writer->view;
		auto node = view.root();
		if (!node)
			return std::nullopt;
		auto handle = node.value;
		if (writeLeaf)
			for (std::size_t step = 0; step < writeLeaf->depth; ++step) {
				const auto next = view.child(handle, writeLeaf->steps[step].index);
				if (!next)
					return std::nullopt;
				handle = next.value;
			}
		return handle;
	}

	// The value read back after a write, as the editor writes numbers.
	QString readBack() const
	{
		const auto node = nodeOf();
		if (!node)
			return {};
		const auto scalar = writer->view.scalar(*node);
		return scalar ? std::visit([](const auto& number) { return detail::scalarEditorText(number); },
		                           scalar.value.value)
		              : QString{};
	}

	// What a typed text writes, in the same words readBack uses: "2.0" -> "2".
	static QString canonicalScalarText(ScalarCode code, const QString& text)
	{
		QString canonical;
		const auto as = [&]<class T>() {
			T number{};
			if (detail::parseScalar(text, number))
				canonical = detail::scalarEditorText(number);
		};
		switch (code) {
			case ScalarCode::Bool: as.template operator()<bool>(); break;
			case ScalarCode::U8: as.template operator()<std::uint8_t>(); break;
			case ScalarCode::S8: as.template operator()<std::int8_t>(); break;
			case ScalarCode::U16: as.template operator()<std::uint16_t>(); break;
			case ScalarCode::S16: as.template operator()<std::int16_t>(); break;
			case ScalarCode::U32: as.template operator()<std::uint32_t>(); break;
			case ScalarCode::S32: as.template operator()<std::int32_t>(); break;
			case ScalarCode::U64: as.template operator()<std::uint64_t>(); break;
			case ScalarCode::S64: as.template operator()<std::int64_t>(); break;
			case ScalarCode::F32: as.template operator()<float>(); break;
			case ScalarCode::F64: as.template operator()<double>(); break;
		}
		return canonical.isEmpty() ? text.trimmed() : canonical;
	}

	void failWatchWrite(QString text)
	{
		if (writer && writer->view.editing())
			writer->view.cancelEdit();
		watchWrite = std::move(text);
		watchWriteKind = QStringLiteral("refused");
		QPointer<DynamicConfiguratorProvider> alive = q;
		notify();
		if (alive)
			Q_EMIT alive->watchWriteFinished(false);
	}

	// Second step of a row write: the writer view has read the Field; make an
	// exact draft of the new value and write it with a confirming read.
	void submitWatchWrite()
	{
		if (!writer || writer->retired || !current() || !allowed || !binding ||
		    !binding->mutationsAllowed()) {
			failWatchWrite(writeName + QStringLiteral(" not written: changes are not allowed now"));
			return;
		}
		auto& view = writer->view;
		const auto snapshot = view.snapshotInfo();
		if (!snapshot.ready || view.beginEdit() != rt::Status::Ok) {
			failWatchWrite(writeName + QStringLiteral(" not written: the Field could not be read first"));
			return;
		}
		bool parsed = false;
		auto set = rt::Status::TypeMismatch;
		const auto node = nodeOf();
		if (!node) {
			failWatchWrite(writeName + QStringLiteral(" not written: the Field changed since it was watched"));
			return;
		}
		if (const auto info = view.info(*node)) {
			set = detail::setScalarText(view, *node, info.value.code, writeText, parsed);
			writeIntended = parsed ? canonicalScalarText(info.value.code, writeText) : QString{};
		}
		if (!parsed || set != rt::Status::Ok) {
			// Why, in words: the type's range for a number past it (WL-3b).
			const auto why = writeType.valid() ? watch_format::encode(writeText, writeType).error : QString{};
			failWatchWrite(writeName + QStringLiteral(" not written: ") +
			               (why.isEmpty() ? QStringLiteral("\"") + writeText + QStringLiteral("\" is not a valid value") : why));
			return;
		}
		auto target = writer;
		const auto submitted = view.apply([target](const wb::ApplyResult& value) noexcept {
			target->applied = value;
			target->dirty = true;
		});
		if (!submitted) {
			failWatchWrite(writeName + QStringLiteral(" not written: refused %1")
			                               .arg(static_cast<unsigned>(submitted.status)));
			return;
		}
		writer->action = DynamicViewState::Action::Apply;
		writer->submittedSession = view.session();
		writer->sequence = submitted.handle.sequence;
		writer->eventReported = false;
		if (binding)
			binding->requestSubmitted();
		watchWrite = QStringLiteral("Writing ") + writeName;
		watchWriteKind = QStringLiteral("writing");
		watchWroteLast = true;
		if (binding)
			binding->mutationAccepted(writer);
		notify();
	}
};

DynamicConfiguratorProvider::DynamicConfiguratorProvider(DynamicConfiguratorBinding* binding,
                                                         QObject* parent)
    : QObject(parent), d(std::make_shared<Impl>(this))
{
	d->definitions.setSourceModel(&d->endpoints);
	std::weak_ptr<Impl> weak = d;
	d->watch.setNotify(
	    [weak] {
		    if (auto p = weak.lock())
			    p->notify();
	    },
	    [weak] {
		    const auto p = weak.lock();
		    // Only an operation in flight (or a model update) holds the
		    // Watchlist; an open draft, Field or request, does not: a watch
		    // read never touches it.
		    return !p || p->busy() || p->updating;
	    });
	d->watch.setCompletionObserver([weak](const WatchFrame& frame) {
		if (const auto p = weak.lock(); p && p->q) {
			p->feedTraces(frame);
			Q_EMIT p->q->watchCompleted(frame);
		}
	});
	// A value inside a struct or array Field is written by writing its whole
	// Field, which one write must hold.
	d->watch.setWritable([weak](WatchKey key) {
		const auto p = weak.lock();
		const auto field = watch_key::field(key);
		return p && p->endpoints.fieldWritable(field) &&
		       (watch_key::isWhole(key) || p->endpoints.fieldWireBytes(field) <= workbenchPayloadBytes);
	});
	d->watch.setWriteRefusal([weak](WatchKey key) -> QString {
		const auto p = weak.lock();
		const auto field = watch_key::field(key);
		if (!p || watch_key::isWhole(key) || !p->endpoints.fieldWritable(field))
			return {};
		return tr("%1 is larger than one write, so a value in it can't be written on its own.")
		    .arg(p->endpoints.fieldPath(field));
	});
	d->batch.setNotify(
	    [weak] {
		    if (auto p = weak.lock())
			    p->notify();
	    },
	    [weak] {
		    const auto p = weak.lock();
		    return !p || p->busy() || p->updating;
	    },
	    [weak] {
		    const auto p = weak.lock();
		    return p && p->allowed && p->binding && p->binding->mutationsAllowed();
	    },
	    [weak] {
		    if (auto p = weak.lock(); p && p->q)
		    {
			    p->watch.wake();
			    Q_EMIT p->q->batchFinished();
		    }
	    });
	// Any change of the tree's shape makes the watch boxes' coverage stale.
	const auto treeChanged = [weak] {
		if (auto p = weak.lock())
			p->treeChanged();
	};
	connect(&d->endpoints, &QAbstractItemModel::modelReset, this, treeChanged);
	// Opening a struct or an array adds its value rows; the boxes count values
	// from the schema, so nothing they cover changed and Undo stays.
	const auto rowsChanged = [treeChanged](const QModelIndex& parent) {
		if (parent.isValid() && (parent.data(EndpointModel::IsEndpointRole).toBool() ||
		                         parent.data(EndpointModel::PreviewChildRole).toBool()))
			return;
		treeChanged();
	};
	connect(&d->endpoints, &QAbstractItemModel::rowsInserted, this,
	        [rowsChanged](const QModelIndex& parent, int, int) { rowsChanged(parent); });
	connect(&d->endpoints, &QAbstractItemModel::rowsRemoved, this,
	        [rowsChanged](const QModelIndex& parent, int, int) { rowsChanged(parent); });
	connect(&d->endpoints, &QAbstractItemModel::rowsMoved, this, treeChanged);
	connect(&d->endpoints, &QAbstractItemModel::layoutChanged, this, treeChanged);
	setBinding(binding);
}

DynamicConfiguratorProvider::~DynamicConfiguratorProvider()
{
	d->q.clear();
	++d->previewRevision;
	QObject::disconnect(d->previewSourceChanged);
	const auto views = std::move(d->previewViews);
	const QPointer<EndpointPreviews> cache = d->previews;
	for (const auto& view : views)
		if (view && cache) cache->clearDemand(view);
	if (d->state)
		d->state->retire();
	if (d->writer)
		d->writer->retire();
	QObject::disconnect(d->changed);
	QObject::disconnect(d->destroyed);
}

void DynamicConfiguratorProvider::setBinding(DynamicConfiguratorBinding* binding)
{
	auto p = d;
	QPointer<DynamicConfiguratorProvider> alive(this);
	QPointer<DynamicConfiguratorBinding> target(binding);
	if (p->updating || (binding == p->binding && (!p->state || !p->state->retired)))
		return;
	if (binding && binding->thread() != thread())
		return;
	if (!p->releasePreviewLeases())
		return;
	p->watch.retire();
	p->batch.retire();
	if (!alive)
		return;
	// Host replacement settles the old owner first. Never move accepted work to
	// a different binding. A caller may retry attachment after the old completion.
	if (p->busy() || (p->writer && p->writer->action != DynamicViewState::Action::None)) {
		p->desired = binding;
		if (p->state)
			p->state->retire();
		if (p->writer)
			p->writer->retire();
		return;
	}
	if (p->state)
		p->state->retire();
	if (p->writer)
		p->writer->retire();
	QObject::disconnect(p->changed);
	QObject::disconnect(p->destroyed);
	p->updating = true;
	p->values.clear();
	p->responses.clear();
	p->selectedIndex = QModelIndex{};
	p->endpoints.clear();
	if (!alive)
		return;
	p->state.reset();
	p->writer.reset();
	p->binding = target;
	p->desired = target;
	p->allowed = false;
	p->category = 0;
	p->selection.clear();
	p->responseEndpoint.clear();
	p->invalid.clear();
	if (target) {
		p->state = target->acquire();
		p->writer = target->acquire();
		p->present(false);
		std::weak_ptr<Impl> weak = p;
		p->changed = connect(target, &DynamicConfiguratorBinding::changed, alive, [weak] {
			if (auto state = weak.lock()) {
				state->sync();
				if (state->q && !state->busy() &&
				    (state->desired != state->binding ||
				     (state->state && state->state->retired && state->binding &&
				      state->binding->current())))
					state->q->setBinding(state->desired);
			}
		});
		p->destroyed = connect(target, &QObject::destroyed, alive, [weak] {
			if (auto state = weak.lock())
				state->sync();
		});
		if (p->state)
			p->endpoints.rebuild(p->state->view);
	}
	// Connected and idle, the status says nothing: the panels say what to do.
	p->status = p->current() ? QString() : QStringLiteral("Not connected");
	p->present();
	p->updating = false;
	p->notify();
}

bool DynamicConfiguratorProvider::setPresentation(const QByteArray& json)
{
	auto p = d;
	if (!canSetPresentation())
		return false;
	auto overlay = p->presentation;
	if (!overlay.replace(json, p->presentationError)) {
		p->notify();
		return false;
	}
	setPresentationOverlay(overlay);
	return true;
}

bool DynamicConfiguratorProvider::canSetPresentation() const
{
	return !d->updating && !d->endpoints.changing() && !d->values.changing() &&
	       !d->responses.changing();
}

void DynamicConfiguratorProvider::setPresentationOverlay(const PresentationOverlay& overlay)
{
	auto p = d;
	if (p->updating)
		return;
	p->updating = true;
	p->presentation = overlay;
	p->presentationError.clear();
	p->present();
	p->updating = false;
	p->notify();
}

QString DynamicConfiguratorProvider::presentationError() const
{
	return d->presentationError;
}

QAbstractItemModel* DynamicConfiguratorProvider::endpoints() const
{
	return &d->endpoints;
}

QVariantMap DynamicConfiguratorProvider::selectionInfo() const
{
	return d->current() ? d->endpoints.endpoint(d->selectedIndex) : QVariantMap{};
}
QAbstractItemModel* DynamicConfiguratorProvider::definitions() const { return &d->definitions; }
QModelIndex DynamicConfiguratorProvider::definitionRootIndex() const { return d->definitions.rootIndex(); }

bool DynamicConfiguratorProvider::previewsSupported() const
{
	return d->previews && d->previews->current();
}

int DynamicConfiguratorProvider::previewIntervalMs() const
{
	return d->previews ? d->previews->interval() : EndpointPreviews::intervalDefaultMs;
}

void DynamicConfiguratorProvider::setPreviewIntervalMs(int ms)
{
	if (!d->previews || d->previews->interval() == std::clamp(ms, int(EndpointPreviews::intervalMinimumMs),
	                                                           int(EndpointPreviews::intervalMaximumMs)))
		return;
	d->previews->setInterval(ms);
	d->notify();
}

void DynamicConfiguratorProvider::readPreviewsNow()
{
	if (d->previews)
		d->previews->readNow();
}

void DynamicConfiguratorProvider::setEndpointPreviews(EndpointPreviews* previews)
{
	auto p = d;
	const auto revision = ++p->previewRevision;
	const QPointer<DynamicConfiguratorProvider> alive(this);
	const QPointer<EndpointPreviews> target(previews);
	QObject::disconnect(p->previewSourceChanged);
	const auto views = std::move(p->previewViews);
	const QPointer<EndpointPreviews> previous = p->previews;
	for (const auto& view : views) {
		if (view && previous) previous->clearDemand(view);
		if (!alive || p->previewRevision != revision) return;
	}
	p->previews = target;
	p->endpoints.setPreviews(target);
	if (!alive || p->previewRevision != revision) return;
	if (target) {
		const auto sourceChanged = [weak = std::weak_ptr<Impl>(p)] {
			if (auto state = weak.lock(); state && state->q) {
				const auto schema = state->previews ? state->previews->schema() : runtime::RuntimeSchema{};
				state->values.setSchemaMetadata(schema);
				if (state->q) state->responses.setSchemaMetadata(schema);
				if (state->q) state->notify();
			}
		};
		p->previewSourceChanged = connect(target, &EndpointPreviews::sourceChanged, this, sourceChanged);
		sourceChanged();
	}
}

bool DynamicConfiguratorProvider::setPreviewDemand(QObject* view, const QVariantList& indexes)
{
	auto p = d;
	if (!p->previews || !p->current() || !view || indexes.size() > EndpointPreviews::demandLimit)
		return false;
	QList<quint32> ids;
	for (const auto& item : indexes) {
		if (!item.canConvert<QModelIndex>()) continue;
		const auto id = p->endpoints.previewDemandField(item.value<QModelIndex>());
		if (id.isValid() && !ids.contains(id.toUInt())) ids.append(id.toUInt());
	}
	p->previewViews.removeIf([](const auto& item) { return item.isNull(); });
	if (!p->previewViews.contains(view)) p->previewViews.append(view);
	return p->previews->setDemand(view, ids);
}

void DynamicConfiguratorProvider::clearPreviewDemand(QObject* view)
{
	auto p = d;
	if (p->previews) p->previews->clearDemand(view);
	p->previewViews.removeIf([view](const auto& item) { return !item || item == view; });
}

QAbstractItemModel* DynamicConfiguratorProvider::values() const
{
	return &d->values;
}

QAbstractItemModel* DynamicConfiguratorProvider::responses() const
{
	return &d->responses;
}

QAbstractItemModel* DynamicConfiguratorProvider::batchItems() const
{
	return d->batch.model();
}

QAbstractItemModel* DynamicConfiguratorProvider::batchResponses() const
{
	return d->batch.responses();
}

bool DynamicConfiguratorProvider::batchSupported() const
{
	return d->batch.supported();
}

bool DynamicConfiguratorProvider::batchPending() const
{
	return d->batch.pending();
}

bool DynamicConfiguratorProvider::canExecuteBatch() const
{
	return d->batch.canExecute();
}

int DynamicConfiguratorProvider::batchCount() const
{
	return d->batch.count();
}

int DynamicConfiguratorProvider::batchSelection() const
{
	return d->batch.selection();
}

QString DynamicConfiguratorProvider::batchStatus() const
{
	return d->batch.status();
}

QString DynamicConfiguratorProvider::batchResponseEndpoint() const
{
	return d->batch.responseEndpoint();
}

bool DynamicConfiguratorProvider::canAddBatch(const QString& operation) const
{
	const auto row = d->batchRow(operation);
	return row && d->batch.canAdd(*row);
}

bool DynamicConfiguratorProvider::addBatch(const QString& operation)
{
	auto p = d;
	auto row = p->batchRow(operation);
	if (!row || !p->batch.canAdd(*row))
		return false;
	if (row->operation != cl::Operation::FieldRead) {
		const auto copied = p->state->view.copyDraft(
		    {reinterpret_cast<std::byte*>(row->request.data()), std::size_t(row->request.size())});
		if (!copied || copied.value != std::uint32_t(row->request.size()))
			return false;
	}
	return p->batch.add(std::move(*row));
}

namespace {
QString batchWords(const char* text)
{
	return QCoreApplication::translate("DynamicConfiguratorProvider", text);
}

QString operationWord(const EndpointEntry& entry)
{
	return entry.category == 1 ? QStringLiteral("read") : entry.category == 2 ? QStringLiteral("command")
	                                                                          : QStringLiteral("service");
}

cl::Operation operationOf(const EndpointEntry& entry)
{
	return entry.category == 1 ? cl::Operation::FieldRead
	       : entry.category == 2 ? cl::Operation::Command : cl::Operation::Service;
}

// What adding an endpoint does, in words: "a read of x", "Command x", "a call of Service x".
QString addedWhat(const EndpointEntry& entry, const QString& values = {})
{
	const auto what = entry.category == 1   ? batchWords("a read of %1").arg(entry.path)
	                  : entry.category == 2 ? batchWords("Command %1").arg(entry.path)
	                                        : batchWords("a call of Service %1").arg(entry.path);
	return values.isEmpty() ? what : QStringLiteral("%1 (%2)").arg(what, values);
}

// A Command's or Service's values typed after its path (BA-2), checked as
// Details would: nullopt when the endpoint is not in the schema.
std::optional<request_arguments::Result> typedArguments(const DynamicBatch& batch, const EndpointEntry& entry,
                                                        const QString& tail)
{
	const auto schema = batch.schema();
	const auto utf8 = entry.path.toUtf8();
	const std::string_view path{utf8.constData(), std::size_t(utf8.size())};
	const auto* endpoint = entry.category == 2 ? schema.findCommand(path) : schema.findService(path);
	if (!endpoint)
		return std::nullopt;
	return request_arguments::check(schema, endpoint->typeId, entry.path, tail);
}

// "value 2, factor 3": the values as they will be sent.
QString valuesSaid(const request_arguments::Result& result)
{
	QStringList parts;
	for (const auto& item : result.items)
		if (item.status == request_arguments::Item::Status::Ok)
			parts << (item.name.isEmpty() ? item.canonical : QStringLiteral("%1 %2").arg(item.name, item.canonical));
	return parts.join(QStringLiteral(", "));
}

// "value=2 factor=3": the values in the line's own form, as an item shows
// them, so a copied item pastes back into the row.
QString valuesLine(const request_arguments::Result& result)
{
	QStringList parts;
	for (const auto& item : result.items)
		if (item.status == request_arguments::Item::Status::Ok)
			parts << (item.name.isEmpty() ? item.canonical : QStringLiteral("%1=%2").arg(item.name, item.canonical));
	return parts.join(QLatin1Char(' '));
}

QVariantList argumentsOf(const request_arguments::Result& result)
{
	QVariantList out;
	for (const auto& item : result.items) {
		const auto status = item.status == request_arguments::Item::Status::Ok        ? QStringLiteral("ok")
		                    : item.status == request_arguments::Item::Status::Error     ? QStringLiteral("error")
		                    : item.status == request_arguments::Item::Status::Aggregate ? QStringLiteral("aggregate")
		                                                                                 : QStringLiteral("missing");
		out.append(QVariantMap{{QStringLiteral("name"), item.name}, {QStringLiteral("typeText"), item.typeText},
		                       {QStringLiteral("status"), status}, {QStringLiteral("text"), item.text},
		                       {QStringLiteral("canonical"), item.canonical}, {QStringLiteral("error"), item.error}});
	}
	return out;
}

// The status a refusal is said with, and the words.
QString refusalStatus(DynamicBatch::Refusal refusal)
{
	switch (refusal) {
		case DynamicBatch::Refusal::Items:
		case DynamicBatch::Refusal::Bytes:
			return QStringLiteral("full");
		case DynamicBatch::Refusal::Permission:
			return QStringLiteral("permission");
		case DynamicBatch::Refusal::Previous:
			return QStringLiteral("previous");
		default:
			return QStringLiteral("busy");
	}
}

QString refusalText(DynamicBatch::Refusal refusal, int count, int limit)
{
	const bool empty = count == 0;
	switch (refusal) {
		case DynamicBatch::Refusal::Busy:
			return batchWords("The Batch is running. Add when its reply is in.");
		case DynamicBatch::Refusal::Session:
			return batchWords("The device is changing; try again in a moment.");
		case DynamicBatch::Refusal::Previous:
			return batchWords("The Batch holds items of an earlier connection, which are never sent again. Clear it first.");
		case DynamicBatch::Refusal::Permission:
			return batchWords("Changes are off. Allow changes to add a Command or a Service.");
		case DynamicBatch::Refusal::Items:
			return count >= limit ? batchWords("The Batch holds %L1 items, all this device takes. Remove one first.").arg(count)
			                      : batchWords("The Batch takes %L1 more; this device takes %L2 items in all.").arg(limit - count).arg(limit);
		case DynamicBatch::Refusal::Bytes:
			return empty ? batchWords("That is more than one Batch can carry on this device.")
			             : batchWords("That would make the Batch longer than the device takes. Remove an item first.");
		case DynamicBatch::Refusal::None:
			break;
	}
	return {};
}
} // namespace

// The line as one endpoint the Batch can add, or why not. The kind of a
// Command or Service and whether it takes input are taken from its bound
// endpoint, which holds whatever the tree's labels do not yet.
QVariantMap DynamicConfiguratorProvider::checkBatchPath(const QString& text) const
{
	auto p = d;
	QVariantMap out{{QStringLiteral("status"), QStringLiteral("empty")}, {QStringLiteral("message"), QString{}},
	                {QStringLiteral("path"), QString{}}, {QStringLiteral("operation"), QString{}},
	                {QStringLiteral("item"), 0}, {QStringLiteral("also"), 0}};
	if (text.trimmed().isEmpty())
		return out;
	const auto say = [&out](const QString& status, const QString& message) {
		out[QStringLiteral("status")] = status;
		out[QStringLiteral("message")] = message;
		return out;
	};
	if (p->batch.pending())
		return say(QStringLiteral("busy"), batchWords("The Batch is running. Add when its reply is in."));
	const auto list = p->endpoints.endpointList();
	if (list.isEmpty())
		return say(QStringLiteral("busy"), p->current() ? batchWords("The device is changing; try again in a moment.")
		                                                : batchWords("Connect to a device to add to the Batch."));
	auto pick = endpoint_paths::resolve(text, list);
	out[QStringLiteral("path")] = pick.path;
	if (pick.status == QStringLiteral("missing")) {
		// Half typed: what the suggestions go on to is taken by Enter.
		const auto next = batchPathCandidates(text);
		if (!next.isEmpty()) {
			const auto first = next.first().toMap();
			const auto name = first.value(QStringLiteral("completion")).toString();
			out[QStringLiteral("path")] = name;
			return say(QStringLiteral("partial"), first.value(QStringLiteral("container")).toBool()
			                                          ? batchWords("Enter goes into %1.").arg(name)
			                                          : batchWords("Enter takes %1.").arg(name));
		}
	}
	// A Command or a Service that takes input, with its values typed after
	// it (BA-2): what is missing or wrong, or the request it makes.
	std::optional<request_arguments::Result> typed;
	if (pick.status == QStringLiteral("input") && pick.matches.size() == 1) {
		typed = typedArguments(p->batch, pick.matches.first(), pick.tail);
		if (typed) {
			out[QStringLiteral("path")] = pick.matches.first().path;
			out[QStringLiteral("operation")] = operationWord(pick.matches.first());
			out[QStringLiteral("arguments")] = argumentsOf(*typed);
			// The Batch's own refusals come first once the line names one
			// endpoint - changes off, a full Batch, items of an earlier
			// connection - not after every value is typed. A request of
			// zeros is as long as the typed one, so it is refused alike.
			const auto& entry = pick.matches.first();
			const QByteArray zeros(qsizetype(entry.requestBytes), '\0');
			if (const auto probe = p->batch.supported() ? p->pathRow(entry, &zeros) : std::nullopt) {
				const auto refusal = p->batch.refusal({&*probe, 1});
				if (refusal != DynamicBatch::Refusal::None)
					return say(refusalStatus(refusal), refusalText(refusal, p->batch.count(), p->batch.itemLimit()));
			}
			out[QStringLiteral("next")] = typed->next;
			// What Enter writes while values are missing: the next "name=",
			// or a name or a value half typed, completed; empty, nothing -
			// a value still to type, or several it could become.
			out[QStringLiteral("line")] = QString{};
			if (!typed->adding.isEmpty()) {
				auto base = text;
				while (!base.isEmpty() && base.back().isSpace())
					base.chop(1);
				base.chop(typed->typing.size());
				out[QStringLiteral("line")] = base + typed->adding;
			}
			if (typed->unnamed) {
				out[QStringLiteral("named")] = typed->named;
				return say(QStringLiteral("unnamed"), typed->message);
			}
			if (typed->aggregate)
				return say(QStringLiteral("input"), typed->message);
			if (!typed->ok())
				return say(typed->error.isEmpty() && std::none_of(typed->items.begin(), typed->items.end(), [](const auto& item) {
					           return item.status == request_arguments::Item::Status::Error;
				           }) ? QStringLiteral("needs") : QStringLiteral("error"),
				           typed->message);
			pick.status = QStringLiteral("adds");
		}
	}
	if (pick.status == QStringLiteral("adds") && pick.member) {
		// A value inside the Field must be one.
		const auto values = p->watch.resolvePath(pick.path, false);
		if (values.missing)
			return say(QStringLiteral("missing"), batchWords("%1 is not on this device.").arg(pick.path));
		if (!values.error.isEmpty())
			return say(QStringLiteral("error"), values.error);
	}
	if (pick.status != QStringLiteral("adds"))
		return say(pick.status, pick.message);
	const auto& entry = pick.matches.first();
	out[QStringLiteral("path")] = entry.path;
	out[QStringLiteral("operation")] = operationWord(entry);
	if (p->takesInput(entry) && !typed)
		return say(QStringLiteral("input"),
		           batchWords("%1 takes input. Enter selects it: fill it in Details, then Add selected draft.").arg(entry.path));
	if (!p->batch.supported())
		return say(QStringLiteral("unsupported"), batchWords("This device has no Batch."));
	auto row = p->pathRow(entry, typed ? &typed->request : nullptr);
	if (row && typed && !valuesLine(*typed).isEmpty())
		row->name = entry.path + QLatin1Char(' ') + valuesLine(*typed);
	if (!row)
		return say(QStringLiteral("busy"), batchWords("Wait for the current read or write to finish, then add."));
	const auto refusal = p->batch.refusal({&*row, 1});
	if (refusal != DynamicBatch::Refusal::None)
		return say(refusalStatus(refusal), refusalText(refusal, p->batch.count(), p->batch.itemLimit()));
	const int item = p->batch.count() + 1;
	int also = 0;
	if (const auto* items = p->batch.model())
		for (std::size_t at = 0; at < items->rows().size() && !also; ++at) {
			const auto& held = items->rows()[at];
			if (held.id == row->id && held.operation == row->operation && held.session == row->session &&
			    held.request == row->request)
				also = int(at) + 1;
		}
	out[QStringLiteral("item")] = item;
	out[QStringLiteral("also")] = also;
	QString message = pick.member ? batchWords("A Batch reads whole Fields: adds a read of %1 as item %L2").arg(entry.path).arg(item)
	                              : batchWords("Adds %1 as item %L2").arg(addedWhat(entry, typed ? valuesSaid(*typed) : QString{})).arg(item);
	if (entry.category != 1)
		message += entry.category == 2 ? batchWords("; it runs when the Batch is executed")
		                               : batchWords("; it is called when the Batch is executed");
	if (also)
		message += batchWords("; it is item %L1 too").arg(also);
	return say(QStringLiteral("adds"), message + QLatin1Char('.'));
}

QVariantList DynamicConfiguratorProvider::batchPathCandidates(const QString& text) const
{
	auto p = d;
	QVariantList out;
	const auto list = p->endpoints.endpointList();
	if (list.isEmpty())
		return out;
	// After a Command's or a Service's path and a space (BA-2): its arguments
	// not given yet, in declared order; after "name=", a bool's or an enum's
	// values. Choosing one writes it into the line.
	auto pick = endpoint_paths::resolve(text, list);
	// "control.Scale " - its name and a space: no values yet, all to give.
	if (!pick.hasTail && text.endsWith(QLatin1Char(' ')) && pick.matches.size() == 1 && pick.matches.first().category != 1)
		pick.hasTail = true;
	if (pick.hasTail && pick.matches.size() == 1 && p->takesInput(pick.matches.first())) {
		const auto& entry = pick.matches.first();
		const auto schema = p->batch.schema();
		const auto utf8 = entry.path.toUtf8();
		const std::string_view where{utf8.constData(), std::size_t(utf8.size())};
		const auto* endpoint = entry.category == 2 ? schema.findCommand(where) : schema.findService(where);
		if (!endpoint)
			return out;
		const auto inputs = request_arguments::arguments(schema, endpoint->typeId);
		const auto given = request_arguments::parse(pick.tail);
		auto line = text;
		while (!line.isEmpty() && line.back().isSpace())
			line.chop(1);
		const bool spaced = text.size() > line.size(); // the last word finished
		const auto argumentAt = [&inputs](const QString& name) {
			for (std::size_t i = 0; i < inputs.size(); ++i)
				if (!inputs[i].name.isEmpty() && inputs[i].name == name)
					return int(i);
			for (std::size_t i = 0; i < inputs.size(); ++i)
				if (!inputs[i].name.isEmpty() && inputs[i].name.compare(name, Qt::CaseInsensitive) == 0)
					return int(i);
			return -1;
		};
		const auto given_ = [&given, &argumentAt](int at) {
			return std::any_of(given.begin(), given.end(),
			                   [&](const request_arguments::Given& g) { return !g.name.isEmpty() && argumentAt(g.name) == at; });
		};
		// "enum Idle, Run, Stop": an enum's first names beside its argument.
		const auto shownType = [](const request_arguments::Argument& input) {
			if (!input.value.enumeration || input.choices.isEmpty())
				return input.typeText;
			auto names = input.choices.mid(0, 3).join(QStringLiteral(", "));
			if (input.choices.size() > 3)
				names += QStringLiteral(", …");
			return input.typeText + QLatin1Char(' ') + names;
		};
		const auto argument = [&](std::size_t i, const QString& base) {
			const auto& input = inputs[i];
			const auto completion = base + input.name + QLatin1Char('=');
			return QVariantMap{{QStringLiteral("path"), input.name + QLatin1Char('=')},
			                   {QStringLiteral("completion"), completion},
			                   {QStringLiteral("name"), input.name},
			                   {QStringLiteral("kind"), QString{}},
			                   {QStringLiteral("kindCode"), QStringLiteral("argument")},
			                   {QStringLiteral("typeText"), shownType(input)},
			                   {QStringLiteral("note"), input.scalar ? batchWords("argument %L1 of %L2").arg(int(i) + 1).arg(int(inputs.size()))
			                                                         : batchWords("in Details")},
			                   {QStringLiteral("container"), false},
			                   {QStringLiteral("continues"), true}};
		};
		// After "name=", or "name=Ru" last on the line: a bool's or an enum's
		// values, those it starts first (all when it starts none).
		if (!spaced && !given.empty() && !given.back().name.isEmpty()) {
			const auto& last = given.back();
			const int at = argumentAt(last.name);
			if (at < 0 || inputs[std::size_t(at)].choices.isEmpty())
				return out;
			const auto& input = inputs[std::size_t(at)];
			const auto base = line.chopped(last.text.size());
			QList<qsizetype> shown;
			for (qsizetype i = 0; i < input.choices.size(); ++i)
				if (input.choices[i].startsWith(last.text, Qt::CaseInsensitive))
					shown << i;
			if (shown.isEmpty())
				for (qsizetype i = 0; i < input.choices.size(); ++i)
					shown << i;
			for (const auto i : std::as_const(shown)) {
				const auto code = input.codes.value(i);
				const auto codeText = !input.value.enumeration ? QString{}
				                      : input.value.scalar == WatchScalar::U64 ? QString::number(quint64(code))
				                                                               : QString::number(code);
				out.append(QVariantMap{{QStringLiteral("path"), input.choices[i]},
				                       {QStringLiteral("completion"), base + input.choices[i] + QLatin1Char(' ')},
				                       {QStringLiteral("name"), input.choices[i]},
				                       {QStringLiteral("kind"), QString{}},
				                       {QStringLiteral("kindCode"), QStringLiteral("value")},
				                       {QStringLiteral("typeText"), codeText},
				                       {QStringLiteral("note"), batchWords("value for %1").arg(input.name)},
				                       {QStringLiteral("container"), false},
				                       {QStringLiteral("continues"), true}});
			}
			return out;
		}
		// A word with no "=" last on the line: the arguments it starts.
		if (!spaced && !given.empty()) {
			const auto& word = given.back().text;
			const auto base = line.chopped(word.size());
			for (std::size_t i = 0; i < inputs.size(); ++i)
				if (!inputs[i].name.isEmpty() && !given_(int(i)) && inputs[i].name.startsWith(word, Qt::CaseInsensitive))
					out.append(argument(i, base));
			return out;
		}
		// After the path or a value and a space: the arguments not given yet.
		for (std::size_t i = 0; i < inputs.size(); ++i)
			if (!inputs[i].name.isEmpty() && !given_(int(i)))
				out.append(argument(i, line + QLatin1Char(' ')));
		return out;
	}
	auto typed = text.trimmed();
	// A leading kind word narrows the suggestions as it narrows the line.
	int category = 0;
	QString keyword;
	const auto space = typed.indexOf(QLatin1Char(' '));
	if (space > 0) {
		const auto first = typed.left(space).toCaseFolded();
		category = first == QLatin1String("read") ? 1 : first == QLatin1String("command") ? 2
		         : first == QLatin1String("service") ? 3 : 0;
		if (category) {
			keyword = typed.left(space) + QLatin1Char(' ');
			typed = typed.mid(space + 1).trimmed();
		}
	}
	QHash<QString, int> kinds;  // endpoints a path names
	for (const auto& entry : list)
		++kinds[entry.path];
	const auto* items = p->batch.model();
	const auto session = p->current() ? p->state->view.session() : 0;
	const auto inBatch = [items, session](const EndpointEntry& entry) {
		int count = 0;
		const auto operation = operationOf(entry);
		for (std::size_t at = 0; items && at < items->rows().size(); ++at) {
			const auto& held = items->rows()[at];
			count += held.id == entry.id && held.operation == operation && held.session == session;
		}
		return count;
	};
	const auto candidate = [&](const EndpointEntry& entry) {
		const bool shared = kinds.value(entry.path) > 1;
		const auto access = !entry.readable && entry.writable ? QStringLiteral("-w")
		                    : entry.writable                  ? QStringLiteral("rw") : QStringLiteral("r-");
		const bool input = entry.category != 1 && p->takesInput(entry);
		const int count = inBatch(entry);
		auto typeText = entry.category == 1 ? entry.typeText + QLatin1Char(' ') + access
		                : entry.category == 2 && !input ? batchWords("no input") : entry.typeText;
		if (input) {
			// "(value: f32, factor: f32) -> f32": the names to type (BA-2).
			const auto schema = p->batch.schema();
			const auto utf8 = entry.path.toUtf8();
			const std::string_view where{utf8.constData(), std::size_t(utf8.size())};
			if (const auto* endpoint = entry.category == 2 ? schema.findCommand(where) : schema.findService(where)) {
				typeText = request_arguments::signatureText(schema, endpoint->typeId);
				if (entry.category == 3 && endpoint->responseTypeId)
					typeText += QStringLiteral(" -> ") + schema_text::typeText(schema, endpoint->responseTypeId);
			}
		}
		const auto completion = shared ? operationWord(entry) + QLatin1Char(' ') + entry.path : keyword + entry.path;
		return QVariantMap{
		    {QStringLiteral("path"), completion},
		    {QStringLiteral("completion"), completion},
		    {QStringLiteral("kind"), endpoint_paths::kindWord(entry.category)},
		    {QStringLiteral("kindCode"), operationWord(entry)},
		    {QStringLiteral("typeText"), typeText},
		    {QStringLiteral("operation"), operationWord(entry)},
		    {QStringLiteral("input"), input},
		    {QStringLiteral("note"), count == 1 ? batchWords("in Batch")
		                             : count > 1 ? batchWords("%L1 in Batch").arg(count) : QString{}},
		    {QStringLiteral("container"), false},
		    // Chosen, it takes a space and its arguments follow (BA-2).
		    {QStringLiteral("follow"), input ? QStringLiteral(" ") : QString{}},
		    {QStringLiteral("continues"), input}};
	};
	const auto wanted = [category](const EndpointEntry& entry) { return category == 0 || entry.category == category; };
	constexpr int limit = 50;
	if (typed.contains(QLatin1Char('.')) || typed.contains(QLatin1Char('['))) {
		for (const auto& entry : list)
			if (wanted(entry) && entry.path.startsWith(typed, Qt::CaseInsensitive) && out.size() < limit)
				out.append(candidate(entry));
		return out;
	}
	// Groups first, to go into; then every endpoint the words name, best first.
	QStringList groups;
	for (const auto& entry : list)
		if (wanted(entry) && !groups.contains(entry.group) && entry.group.startsWith(typed, Qt::CaseInsensitive))
			groups.append(entry.group);
	for (const auto& group : std::as_const(groups)) {
		if (out.size() >= limit)
			break;
		out.append(QVariantMap{{QStringLiteral("path"), keyword + group}, {QStringLiteral("completion"), keyword + group},
		                       {QStringLiteral("kind"), batchWords("group")}, {QStringLiteral("kindCode"), QStringLiteral("group")},
		                       {QStringLiteral("typeText"), QString{}},
		                       {QStringLiteral("container"), true}, {QStringLiteral("next"), QStringLiteral(".")}});
	}
	if (typed.isEmpty())
		return out;
	const auto terms = typed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	for (int rank = 0; rank <= 2 && out.size() < limit; ++rank)
		for (const auto& entry : list)
			if (wanted(entry) && endpoint_paths::nameRank(entry.path, terms) == rank && out.size() < limit)
				out.append(candidate(entry));
	return out;
}

QVariantMap DynamicConfiguratorProvider::addBatchPaths(const QStringList& lines)
{
	auto p = d;
	QVariantMap result{{QStringLiteral("status"), QStringLiteral("refused")}, {QStringLiteral("added"), 0},
	                   {QStringLiteral("refused"), QVariantList{}}, {QStringLiteral("message"), QString{}},
	                   {QStringLiteral("items"), QVariantList{}}, {QStringLiteral("first"), 0}};
	std::vector<BatchRow> rows;
	QVariantList refused, items;
	int line = 0, firstRefused = 0;
	QString firstPath, firstReason;
	for (const auto& text : lines) {
		if (text.trimmed().isEmpty())
			continue;
		++line;
		// Each line is read as it would be checked, so a line says the same in
		// both places.
		const auto check = checkBatchPath(text);
		const auto status = check.value(QStringLiteral("status")).toString();
		const auto pick = endpoint_paths::resolve(text, p->endpoints.endpointList());
		const auto typed = status == QStringLiteral("adds") && pick.matches.size() == 1 && pick.status == QStringLiteral("input")
		                       ? typedArguments(p->batch, pick.matches.first(), pick.tail)
		                       : std::nullopt;
		auto row = status == QStringLiteral("adds") && pick.matches.size() == 1
		               ? p->pathRow(pick.matches.first(), typed && typed->ok() ? &typed->request : nullptr)
		               : std::nullopt;
		if (row && typed && !valuesLine(*typed).isEmpty())
			row->name = pick.matches.first().path + QLatin1Char(' ') + valuesLine(*typed);
		if (!row) {
			const auto path = check.value(QStringLiteral("path")).toString();
			const auto reason = status == QStringLiteral("partial")
			                        ? batchWords("%1 is not on this device.").arg(text.trimmed())
			                        : check.value(QStringLiteral("message")).toString();
			refused.append(QVariantMap{{QStringLiteral("line"), line}, {QStringLiteral("path"), path},
			                           {QStringLiteral("reason"), reason}});
			if (!firstRefused) {
				firstRefused = line;
				firstPath = text.trimmed();
				firstReason = reason;
			}
			continue;
		}
		items.append(QVariantMap{{QStringLiteral("path"), pick.matches.first().path},
		                         {QStringLiteral("operation"), operationWord(pick.matches.first())},
		                         {QStringLiteral("what"), addedWhat(pick.matches.first(), typed ? valuesSaid(*typed) : QString{})}});
		rows.push_back(*row);
	}
	result[QStringLiteral("refused")] = refused;
	// Every line or none: a Batch is a recipe, and one step left out changes it.
	if (!refused.isEmpty()) {
		auto message = line == 1 ? firstReason
		                         : batchWords("Nothing added: line %L1, %2: %3").arg(firstRefused).arg(firstPath, firstReason);
		if (refused.size() == 2)
			message += batchWords(" (and one more line)");
		else if (refused.size() > 2)
			message += batchWords(" (and %L1 more lines)").arg(refused.size() - 1);
		result[QStringLiteral("message")] = message;
		return result;
	}
	if (rows.empty()) {
		result[QStringLiteral("status")] = QStringLiteral("ok");
		return result;
	}
	const auto refusal = p->batch.refusal(rows);
	if (refusal != DynamicBatch::Refusal::None) {
		result[QStringLiteral("status")] = refusalStatus(refusal);
		result[QStringLiteral("message")] = refusalText(refusal, p->batch.count(), p->batch.itemLimit());
		return result;
	}
	const int first = p->batch.count() + 1;
	const auto count = rows.size();
	if (!p->batch.add(std::move(rows)))
		return result;
	result[QStringLiteral("status")] = QStringLiteral("ok");
	result[QStringLiteral("added")] = static_cast<int>(count);
	result[QStringLiteral("items")] = items;
	result[QStringLiteral("first")] = first;
	return result;
}

bool DynamicConfiguratorProvider::selectBatchPath(const QString& text)
{
	auto p = d;
	// An edit in Details is never replaced unseen: finish or cancel it first.
	// A Command or a Service merely selected counts as editing its draft, so
	// the test is whether there is anything to lose, not whether it is open.
	if (selectionRequiresDiscard())
		return false;
	const auto pick = endpoint_paths::resolve(text, p->endpoints.endpointList());
	if (pick.matches.size() != 1)
		return false;
	const auto index = p->endpoints.endpointIndex(pick.matches.first().category, pick.matches.first().id);
	if (!index.isValid() || !selectEndpoint(index))
		return false;
	// The values typed after the path go into the draft (BA-2), each valid one
	// as Details takes it; the rest are left for Details. With changes off the
	// draft takes none, and the endpoint is only selected.
	if (!pick.hasTail || (!commandSelected() && !serviceSelected()))
		return true;
	const auto typed = typedArguments(p->batch, pick.matches.first(), pick.tail);
	if (!typed || !typed->error.isEmpty())
		return true;
	// The draft's rows follow the request's members in declared order, and
	// a row's name may be a presentation label: an argument is found by its
	// place. A request that is one value is the root itself.
	const bool bare = typed->items.size() == 1 && typed->items.front().name.isEmpty();
	const auto draftMember = [&values = p->values, bare](int place) {
		const auto root = values.index(0, 0);
		if (bare)
			return root;
		// fetchMore may add nothing (the model changing, a row it cannot
		// read) while canFetchMore stays true: stop when no row came.
		for (int rows = values.rowCount(root); rows <= place && values.canFetchMore(root);) {
			values.fetchMore(root);
			const int now = values.rowCount(root);
			if (now <= rows)
				break;
			rows = now;
		}
		return place < values.rowCount(root) ? values.index(place, 0, root) : QModelIndex{};
	};
	for (std::size_t i = 0; i < typed->items.size(); ++i)
		if (typed->items[i].status == request_arguments::Item::Status::Ok)
			if (const auto at = draftMember(int(i)); at.isValid())
				setScalarText(at, typed->items[i].editorText);
	return true;
}

bool DynamicConfiguratorProvider::removeBatch(int row)
{
	auto p = d;
	return p->batch.remove(row);
}

bool DynamicConfiguratorProvider::moveBatch(int row, int delta)
{
	auto p = d;
	return p->batch.move(row, delta);
}

bool DynamicConfiguratorProvider::clearBatch()
{
	auto p = d;
	return p->batch.clear();
}

bool DynamicConfiguratorProvider::selectBatch(int row)
{
	auto p = d;
	return p->batch.select(row);
}

bool DynamicConfiguratorProvider::executeBatch()
{
	auto p = d;
	return p->batch.execute();
}

void DynamicConfiguratorProvider::setBatchSource(BatchSource source)
{
	auto p = d;
	p->batch.setSource(std::move(source));
}

void DynamicConfiguratorProvider::pollBatch()
{
	auto p = d;
	p->batch.poll();
}

void DynamicConfiguratorProvider::setWatchSource(WatchlistSource source)
{
	auto p = d;
	// Which Fields can be held may differ with the new source.
	p->treeChanged();
	{
		WatchRows::Changes held{p->rows};
		p->watch.setSource(std::move(source));
	}
	// The Watchlist's model exists from its first source on.
	p->rows.setSource(p->watch.model());
	if (auto* model = qobject_cast<WatchlistModel*>(p->watch.model())) {
		model->setFormat(p->watchFormat);
		if (!p->rowFormats.isEmpty())
			model->setPathFormats(std::exchange(p->rowFormats, {}));
	}
	p->notify();
}

void DynamicConfiguratorProvider::pollWatch()
{
	auto p = d;
	p->watch.poll();
}

QAbstractItemModel* DynamicConfiguratorProvider::watchlist() const
{
	return d->watch.model();
}

QObject* DynamicConfiguratorProvider::watchRows() const
{
	return &d->rows;
}

QObject* DynamicConfiguratorProvider::watchTraces() const
{
	return &d->traces;
}

bool DynamicConfiguratorProvider::watchReadManySupported() const
{
	return d->watch.supported();
}

bool DynamicConfiguratorProvider::watchActive() const
{
	return d->watch.active();
}

bool DynamicConfiguratorProvider::watchPending() const
{
	return d->watch.pending();
}

bool DynamicConfiguratorProvider::canAddWatch() const
{
	return d->current() && !d->busy() && !d->updating && d->category == 1 &&
	       d->watch.canAdd(d->state->endpointId);
}

int DynamicConfiguratorProvider::watchCount() const
{
	return d->watch.count();
}

bool DynamicConfiguratorProvider::watchReadable() const
{
	return d->watch.readable();
}

QString DynamicConfiguratorProvider::watchStatus() const
{
	return d->watch.status();
}

QVariantMap DynamicConfiguratorProvider::watchPace() const
{
	const auto pace = d->watch.pace();
	if (pace.cycles == 0)
		return {};
	return {{QStringLiteral("cycles"), pace.cycles},
	        {QStringLiteral("polling"), pace.polling},
	        {QStringLiteral("routine"), pace.routine},
	        {QStringLiteral("values"), pace.values},
	        {QStringLiteral("readMany"), pace.readMany},
	        {QStringLiteral("batch"), pace.batch},
	        {QStringLiteral("fieldRead"), pace.fieldRead},
	        {QStringLiteral("again"), pace.again},
	        {QStringLiteral("lastCycleMs"), pace.lastCycleMs},
	        {QStringLiteral("medianCycleMs"), pace.medianCycleMs},
	        {QStringLiteral("medianPeriodMs"), pace.medianPeriodMs},
	        {QStringLiteral("achievedPeriodMs"), pace.achievedPeriodMs},
	        {QStringLiteral("intervalMs"), pace.intervalMs},
	        {QStringLiteral("skippedTicks"), qulonglong(pace.skippedTicks)},
	        {QStringLiteral("behind"), pace.behind}};
}

bool DynamicConfiguratorProvider::watchIndividual() const
{
	return d->watch.individual();
}

int DynamicConfiguratorProvider::watchIntervalMs() const
{
	return d->watch.interval();
}

void DynamicConfiguratorProvider::setWatchIndividual(bool value)
{
	auto p = d;
	p->watch.setIndividual(value);
}

void DynamicConfiguratorProvider::setWatchIntervalMs(int ms)
{
	auto p = d;
	p->watch.setInterval(ms);
}

bool DynamicConfiguratorProvider::addWatch()
{
	auto p = d;
	return canAddWatch() && p->watch.add(p->state->endpointId, p->selection);
}

bool DynamicConfiguratorProvider::watchCandidate(const QModelIndex& index) const
{
	auto p = d;
	if (!p->current() || p->endpoints.changing())
		return false;
	const auto endpoint = p->endpoints.endpoint(index);
	return !endpoint.isEmpty() && endpoint.value(QStringLiteral("category")).toInt() == 1 &&
	       endpoint.value(QStringLiteral("readable")).toBool();
}

QVariantMap DynamicConfiguratorProvider::addWatchMany(const QVariantList& indexes)
{
	auto p = d;
	QVariantMap result{{QStringLiteral("status"), QStringLiteral("refused")},
	                   {QStringLiteral("added"), 0}, {QStringLiteral("duplicate"), 0},
	                   {QStringLiteral("unsupported"), 0}, {QStringLiteral("capacity"), 0}};
	if (!p->selectable())
		return result;
	QVector<QPair<quint32, QString>> fields;
	int unsupported = 0;
	for (const auto& value : indexes) {
		if (!value.canConvert<QModelIndex>()) {
			result[QStringLiteral("status")] = QStringLiteral("invalid");
			return result;
		}
		const auto endpoint = p->endpoints.endpoint(value.value<QModelIndex>());
		if (endpoint.isEmpty()) {
			result[QStringLiteral("status")] = p->current() ? QStringLiteral("invalid")
			                                             : QStringLiteral("stale");
			return result;
		}
		if (endpoint.value(QStringLiteral("category")).toInt() != 1 ||
		    !endpoint.value(QStringLiteral("readable")).toBool()) {
			++unsupported;
			continue;
		}
		fields.push_back({endpoint.value(QStringLiteral("id")).toUInt(),
		                  endpoint.value(QStringLiteral("path")).toString()});
	}
	if (!p->selectable()) {
		result[QStringLiteral("status")] = QStringLiteral("stale");
		return result;
	}
	{
		WatchRows::Changes held{p->rows};
		result = p->watch.addMany(fields);
	}
	if (result.value(QStringLiteral("added")).toInt() > 0)
		p->notify();
	if (result.value(QStringLiteral("status")) == QStringLiteral("ok"))
		result[QStringLiteral("unsupported")] =
		    result.value(QStringLiteral("unsupported")).toInt() + unsupported;
	return result;
}

void DynamicConfiguratorProvider::removeWatch(int row)
{
	auto p = d;
	p->watch.remove(row);
}

QVariantMap DynamicConfiguratorProvider::removeWatchRows(const QVariantList& rows)
{
	auto p = d;
	QVariantMap result{{QStringLiteral("status"), QStringLiteral("refused")}, {QStringLiteral("changed"), 0}};
	const auto* model = qobject_cast<const WatchlistModel*>(p->watch.model());
	if (!model)
		return result;
	QSet<WatchKey> keys;
	for (const auto& row : rows) {
		bool ok = false;
		const int at = row.toInt(&ok);
		if (!ok || at < 0 || at >= model->count()) {
			result[QStringLiteral("status")] = QStringLiteral("invalid");
			return result;
		}
		keys.insert(model->key(at));
	}
	if (keys.isEmpty()) {
		result[QStringLiteral("status")] = QStringLiteral("ok");
		return result;
	}
	// Every key is a row's, so nothing removed means the Watchlist refused.
	QVector<WatchlistModel::Removed> removed;
	{
		WatchRows::Changes held{p->rows};
		removed = p->watch.removeMany(keys);
	}
	if (removed.isEmpty())
		return result;
	result[QStringLiteral("status")] = QStringLiteral("ok");
	result[QStringLiteral("changed")] = static_cast<int>(removed.size());
	p->undo = {};
	p->undo.valid = true;
	p->undo.removed = removed;
	p->undo.membership = p->watch.membership();
	p->undo.treeEpoch = p->treeEpoch;
	p->notify();
	return result;
}

namespace {
// A line as typed or pasted: its path, without a copied value after it.
QString pathOfLine(const QString& line)
{
	auto path = line;
	const auto equals = path.indexOf(QStringLiteral(" = "));
	if (equals >= 0)
		path = path.left(equals);
	const auto tab = path.indexOf(QLatin1Char('\t'));
	if (tab >= 0)
		path = path.left(tab);
	return path.trimmed();
}
QString words(const char* text)
{
	return QCoreApplication::translate("DynamicConfiguratorProvider", text);
}
} // namespace

QVariantMap DynamicConfiguratorProvider::checkWatchPath(const QString& text) const
{
	auto p = d;
	QVariantMap out{{QStringLiteral("status"), QStringLiteral("empty")}, {QStringLiteral("message"), QString{}},
	                {QStringLiteral("adds"), 0}, {QStringLiteral("already"), 0}, {QStringLiteral("row"), -1}};
	const auto typed = pathOfLine(text);
	if (typed.isEmpty())
		return out;
	const auto say = [&](const QString& status, const QString& message) {
		out[QStringLiteral("status")] = status;
		out[QStringLiteral("message")] = message;
		return out;
	};
	const auto resolved = p->watch.resolvePath(typed, false);
	if (resolved.missing && p->watch.unboundRow(typed) >= 0) {
		out[QStringLiteral("row")] = p->watch.unboundRow(typed);
		return say(QStringLiteral("watched"), words("%1 is already in the Watchlist; it is not on this device.").arg(typed));
	}
	if (resolved.missing)
		return say(QStringLiteral("missing"),
		           words("Not on this device. Enter adds it anyway; it is read on a device that has it."));
	if (!resolved.error.isEmpty())
		return say(QStringLiteral("error"), resolved.error);
	out[QStringLiteral("adds")] = static_cast<int>(resolved.adds);
	out[QStringLiteral("already")] = static_cast<int>(resolved.already);
	const int room = watchLimit() - watchCount();
	if (static_cast<int>(resolved.adds) > room)
		return say(QStringLiteral("error"), words("%1 adds %L2 values; the Watchlist has room for %L3 more of its %L4.")
		                                        .arg(typed)
		                                        .arg(resolved.adds)
		                                        .arg(room)
		                                        .arg(watchLimit()));
	if (resolved.adds == 0) {
		const auto* model = qobject_cast<const WatchlistModel*>(p->watch.model());
		out[QStringLiteral("row")] = model ? model->rowOf(resolved.firstWatched) : -1;
		return say(QStringLiteral("watched"), resolved.already == 1
		                                          ? words("%1 is already watched.").arg(typed)
		                                          : words("Every value of %1 is already watched.").arg(typed));
	}
	const auto adds = resolved.adds == 1 && resolved.already == 0 && !typed.contains(QLatin1String(" "))
	                      ? words("Adds %1").arg(typed)
	                      : words("Adds %L1 values of %2").arg(resolved.adds).arg(typed);
	if (resolved.already == 0)
		return say(QStringLiteral("adds"), adds);
	return say(QStringLiteral("adds"), resolved.already == 1 ? words("%1; 1 more is watched already.").arg(adds)
	                                                         : words("%1; %L2 more are watched already.").arg(adds).arg(resolved.already));
}

QVariantList DynamicConfiguratorProvider::watchPathCandidates(const QString& text) const
{
	QVariantList out;
	for (const auto& c : d->watch.pathCandidates(text, 50))
		out.append(QVariantMap{{QStringLiteral("path"), c.path},       {QStringLiteral("typeText"), c.typeText},
		                       {QStringLiteral("values"), c.values},   {QStringLiteral("container"), c.container},
		                       {QStringLiteral("watched"), c.watched}, {QStringLiteral("next"), c.next}});
	return out;
}

QVariantMap DynamicConfiguratorProvider::addWatchPaths(const QStringList& lines)
{
	auto p = d;
	QVariantMap result{{QStringLiteral("status"), QStringLiteral("ok")}, {QStringLiteral("added"), 0},
	                   {QStringLiteral("already"), 0}, {QStringLiteral("missing"), QVariantList{}},
	                   {QStringLiteral("refused"), QVariantList{}}};
	if (!p->current() || p->updating || p->endpoints.changing()) {
		result[QStringLiteral("status")] = QStringLiteral("refused");
		result[QStringLiteral("message")] = words("The device is changing; try again in a moment.");
		return result;
	}
	QVector<WatchEntry> add;
	QSet<WatchKey> seen;
	QSet<QString> seenMissing;
	QVariantList missing, refused;
	int already = 0;
	for (const auto& line : lines) {
		const auto path = pathOfLine(line);
		if (path.isEmpty())
			continue;
		const auto resolved = p->watch.resolvePath(path);
		if (resolved.missing) {
			// Kept as a row, once: a row for it already is enough.
			if (seenMissing.contains(path) || p->watch.unboundRow(path) >= 0) {
				++already;
				continue;
			}
			seenMissing.insert(path);
			missing.append(path);
			add.push_back(p->watch.missingEntry(path));
			continue;
		}
		if (!resolved.error.isEmpty()) {
			refused.append(QVariantMap{{QStringLiteral("path"), path}, {QStringLiteral("reason"), resolved.error}});
			continue;
		}
		already += static_cast<int>(resolved.already);
		for (const auto& entry : resolved.entries)
			if (!seen.contains(entry.key)) {
				seen.insert(entry.key);
				add.push_back(entry);
			}
	}
	result[QStringLiteral("missing")] = missing;
	result[QStringLiteral("refused")] = refused;
	result[QStringLiteral("already")] = already;
	if (add.isEmpty())
		return result;
	// All of them or none, as a tree box: a part would leave the paste half done.
	const int room = watchLimit() - watchCount();
	if (add.size() > room) {
		result[QStringLiteral("status")] = QStringLiteral("capacity");
		result[QStringLiteral("message")] =
		    words("These add %L1 values; the Watchlist has room for %L2 more of its %L3. Remove some first.")
		        .arg(add.size())
		        .arg(room)
		        .arg(watchLimit());
		return result;
	}
	QVariantMap added;
	{
		WatchRows::Changes held{p->rows};
		added = p->watch.addEntries(add);
	}
	if (added.value(QStringLiteral("status")).toString() != QStringLiteral("ok")) {
		result[QStringLiteral("status")] = added.value(QStringLiteral("status"));
		return result;
	}
	const int count = added.value(QStringLiteral("added")).toInt();
	// The values added; the rows kept for paths not on this device are said apart.
	result[QStringLiteral("added")] = count - static_cast<int>(missing.size());
	if (count > 0) {
		p->undo = {};
		p->undo.valid = true;
		p->undo.added = true;
		for (const auto& entry : std::as_const(add))
			p->undo.addedIds.insert(entry.key);
		p->undo.membership = p->watch.membership();
		p->undo.treeEpoch = p->treeEpoch;
	}
	p->notify();
	return result;
}

QVariantMap DynamicConfiguratorProvider::restoreWatchBindings(const QVariantList& bindings)
{
	return putWatchBindings(bindings, true);
}

QVariantMap DynamicConfiguratorProvider::putWatchBindings(const QVariantList& bindings, bool replace)
{
	auto p = d;
	QVariantMap result{{QStringLiteral("status"), QStringLiteral("refused")}, {QStringLiteral("added"), 0},
	                   {QStringLiteral("duplicate"), 0}, {QStringLiteral("capacity"), 0},
	                   {QStringLiteral("missing"), QVariantList{}}};
	// Everything addEntries checks first, so a replacement never empties the
	// list and then fails.
	if (!p->current() || p->updating || p->endpoints.changing() || !p->watch.canTellHoldable() ||
	    (replace && bindings.size() > watchLimit()))
		return result;
	QVector<WatchEntry> entries;
	QVariantList missing;
	QSet<WatchKey> seen;
	QSet<QString> seenPaths;
	int duplicate = 0;
	for (qsizetype at = 0; at < bindings.size(); ++at) {
		const auto binding = bindings.at(at).toMap();
		const auto group = binding.value(QStringLiteral("group")).toString();
		const auto name = binding.value(QStringLiteral("name")).toString();
		const auto member = binding.value(QStringLiteral("member")).toString();
		const auto [resolution, entry] = p->watch.resolve(group, name, member);
		if (resolution == DynamicWatchlist::Resolution::Ok) {
			if (seen.contains(entry.key)) {
				++duplicate;  // the same value twice: once is enough
				continue;
			}
			auto named = entry;
			if (named.name.isEmpty())
				named.name = group + QLatin1Char('.') + name;
			seen.insert(named.key);
			entries.push_back(named);
			continue;
		}
		missing.append(static_cast<int>(at));
		// Kept as a row by its path, once - only a path that can be written
		// back the same way: a group and a name.
		if (group.isEmpty() || name.isEmpty())
			continue;
		auto path = group + QLatin1Char('.') + name;
		if (!member.isEmpty())
			path += (member.startsWith(QLatin1Char('[')) ? QString{} : QStringLiteral(".")) + member;
		if (seenPaths.contains(path) || (!replace && p->watch.unboundRow(path) >= 0)) {
			++duplicate;
			continue;
		}
		seenPaths.insert(path);
		entries.push_back(p->watch.missingEntry(path));
	}
	result[QStringLiteral("duplicate")] = duplicate;
	result[QStringLiteral("missing")] = missing;
	WatchRows::Changes held{p->rows};
	if (replace)
		p->watch.clear();
	if (!entries.isEmpty()) {
		const auto added = p->watch.addEntries(entries);
		result[QStringLiteral("capacity")] = added.value(QStringLiteral("capacity"));
		if (added.value(QStringLiteral("status")).toString() != QStringLiteral("ok")) {
			result[QStringLiteral("status")] = added.value(QStringLiteral("status"));
			p->notify();
			return result;
		}
		int kept = 0;
		for (const auto& entry : std::as_const(entries))
			kept += watch_key::isUnresolved(entry.key) ? 1 : 0;
		result[QStringLiteral("added")] = added.value(QStringLiteral("added")).toInt() - kept;
		// A path this device has but cannot watch (a Command now, a name two
		// Fields share) says why, rather than "Not on device"; a struct kept
		// whole becomes its values.
		p->watch.rebindRows();
	}
	result[QStringLiteral("status")] = QStringLiteral("ok");
	if (replace)
		p->undo = {};
	p->notify();
	return result;
}

QVariantMap DynamicConfiguratorProvider::watchEditPreview(int row, const QString& text) const
{
	auto p = d;
	QVariantMap out{{QStringLiteral("preview"), QString{}}, {QStringLiteral("error"), QString{}}};
	const auto* model = qobject_cast<const WatchlistModel*>(p->watch.model());
	if (!model || row < 0 || row >= model->count() || text.trimmed().isEmpty())
		return out;
	const auto type = model->valueType(row);
	const auto typed = text.trimmed();
	// A row never read has no type to say yet; a row without a device writes
	// nothing; an enum is chosen by name, its code only in numbers.
	static const QRegularExpression number(QStringLiteral("^(-?[0-9]+|0[xX][0-9A-Fa-f_]+)$"));
	if (!type.valid() || model->isUnbound(row) || (type.enumeration && !number.match(typed).hasMatch()))
		return out;
	// A start not yet a number ("-", "0x", "1e") is judged on Enter, not on the key.
	static const QRegularExpression unfinished(QStringLiteral("^([-+]|0[xX]|.*[eE][-+]?|.*\\.)$"));
	if (unfinished.match(typed).hasMatch())
		return out;
	const auto encoded = watch_format::encode(typed, type);
	if (!encoded.error.isEmpty()) {
		out[QStringLiteral("error")] = encoded.error;
		return out;
	}
	const bool hex = watch_format::isHex(typed);
	const auto shown = watch_format::text(encoded.value, hex ? WatchNumberFormat::Dec : WatchNumberFormat::Hex);
	if (shown.isEmpty())
		return out;
	// A float's hex is its bits, not its number: said so.
	const bool real = type.scalar == WatchScalar::F32 || type.scalar == WatchScalar::F64;
	out[QStringLiteral("preview")] = !hex && real
	    ? QStringLiteral("= %1 (%2 bits)").arg(shown, type.scalar == WatchScalar::F32 ? QStringLiteral("f32") : QStringLiteral("f64"))
	    : QStringLiteral("= ") + shown;
	return out;
}

QString DynamicConfiguratorProvider::watchFormat() const
{
	return watch_format::name(d->watchFormat);
}

void DynamicConfiguratorProvider::setWatchFormat(const QString& format)
{
	auto p = d;
	const auto parsed = watch_format::parse(format);
	if (!parsed || *parsed == p->watchFormat)
		return;
	p->putFormat(*parsed);
	++p->formatRevision;
	p->notify();
}

bool DynamicConfiguratorProvider::chooseWatchFormat(const QString& format)
{
	auto p = d;
	const auto parsed = watch_format::parse(format);
	if (!parsed)
		return false;
	if (*parsed == p->watchFormat)
		return true;
	// A format chosen is one change Undo takes back (WL-2c): Ctrl+Z after it
	// returns the format, never an older change under it.
	Impl::WatchUndo undo;
	undo.valid = true;
	undo.formats = true;
	undo.format = p->watchFormat;
	undo.pathFormats = p->pathFormats();
	p->putFormat(*parsed);
	undo.formatRevision = ++p->formatRevision;
	p->undo = std::move(undo);
	p->notify();
	return true;
}

bool DynamicConfiguratorProvider::setWatchRowFormat(const QVariantList& rows, const QString& format)
{
	auto p = d;
	auto* model = qobject_cast<WatchlistModel*>(p->watch.model());
	const auto parsed = watch_format::parse(format);
	if (!model || (!format.isEmpty() && !parsed))
		return false;
	QVector<int> chosen;
	for (const auto& row : rows) {
		bool ok = false;
		const int at = row.toInt(&ok);
		if (!ok || at < 0 || at >= model->count())
			return false;
		chosen.push_back(at);
	}
	const auto target = format.isEmpty() ? std::nullopt : parsed;
	const auto before = model->pathFormats();
	bool done = true;
	{
		WatchRows::Changes held{p->rows};
		for (const int row : std::as_const(chosen))
			done = model->setRowFormat(row, target) && done;
	}
	if (model->pathFormats() != before) {
		// One change for all the rows given, which Undo takes back.
		Impl::WatchUndo undo;
		undo.valid = true;
		undo.formats = true;
		undo.format = p->watchFormat;
		undo.pathFormats = before;
		undo.formatRevision = ++p->formatRevision;
		p->undo = std::move(undo);
	}
	p->notify();
	return done;
}

int DynamicConfiguratorProvider::watchOwnFormatCount() const
{
	return d->ownFormatRows();
}

bool DynamicConfiguratorProvider::watchFormatsDiffer() const
{
	if (d->watchFormat != WatchNumberFormat::Original)
		return true;
	const auto* model = qobject_cast<const WatchlistModel*>(d->watch.model());
	for (int row = 0; model && row < model->count(); ++row)
		if (const auto own = model->rowFormat(row); own && *own != WatchNumberFormat::Original)
			return true;
	return false;
}

QVariantMap DynamicConfiguratorProvider::resetWatchFormats()
{
	auto p = d;
	QVariantMap result{{QStringLiteral("status"), QStringLiteral("unchanged")}, {QStringLiteral("rows"), 0}};
	auto before = p->pathFormats();
	if (p->watchFormat == WatchNumberFormat::Original && before.isEmpty())
		return result;
	const int rows = p->ownFormatRows();
	if (!p->putPathFormats({})) {
		result[QStringLiteral("status")] = QStringLiteral("refused");
		return result;
	}
	Impl::WatchUndo undo;
	undo.valid = true;
	undo.formats = true;
	undo.format = p->watchFormat;
	undo.pathFormats = std::move(before);
	p->putFormat(WatchNumberFormat::Original);
	undo.formatRevision = ++p->formatRevision;
	p->undo = std::move(undo);
	p->notify();
	result[QStringLiteral("status")] = QStringLiteral("ok");
	result[QStringLiteral("rows")] = rows;
	return result;
}

QVariantMap DynamicConfiguratorProvider::watchRowFormats() const
{
	const auto* model = qobject_cast<const WatchlistModel*>(d->watch.model());
	const auto formats = model ? model->pathFormats() : d->rowFormats;
	QVariantMap saved;
	for (auto at = formats.cbegin(); at != formats.cend(); ++at)
		saved.insert(at.key(), watch_format::name(at.value()));
	return saved;
}

bool DynamicConfiguratorProvider::setWatchRowFormats(const QVariantMap& formats)
{
	auto p = d;
	QHash<QString, WatchNumberFormat> parsed;
	for (auto at = formats.cbegin(); at != formats.cend(); ++at) {
		const auto format = watch_format::parse(at.value().toString());
		if (at.key().isEmpty() || !format)
			return false;
		parsed.insert(at.key(), *format);
	}
	if (parsed.size() > WatchlistModel::formatLimit || !p->putPathFormats(std::move(parsed)))
		return false;
	++p->formatRevision;
	p->notify();
	return true;
}

void DynamicConfiguratorProvider::clearWatch()
{
	auto p = d;
	p->watch.clear();
}

int DynamicConfiguratorProvider::watchRevision() const
{
	return static_cast<int>((d->watch.membership() + d->treeEpoch + d->availability + d->formatRevision) & 0x7fffffffU);
}

bool DynamicConfiguratorProvider::canUndoWatch() const
{
	const auto& undo = d->undo;
	// A reset of the formats needs no device: only that no format changed since.
	if (undo.formats)
		return undo.valid && undo.formatRevision == d->formatRevision;
	return undo.valid && undo.membership == d->watch.membership() &&
	       undo.treeEpoch == d->treeEpoch && d->current() && !d->updating;
}

int DynamicConfiguratorProvider::watchState(const QModelIndex& index) const
{
	auto p = d;
	if (!p->current() || p->updating || p->endpoints.changing() || !index.isValid() ||
	    index.model() != &p->endpoints)
		return -1;
	p->ensureCoverage();
	using Role = EndpointModel::Role;
	if (index.data(Role::PreviewChildRole).toBool())
		return p->coverage.memberState(index.data(Role::PreviewFieldIdRole).toUInt(),
		                               index.data(Role::WatchLeafFirstRole).toUInt(),
		                               index.data(Role::WatchLeafCountRole).toUInt(), p->held(),
		                               p->watch.membership());
	return p->coverage.state(index, p->held(), p->watch.membership());
}

QVariantList DynamicConfiguratorProvider::watchBindings() const
{
	auto p = d;
	QVariantList saved;
	// Without a device every row is kept by its path (WL-4c) and saved so.
	if (p->current() && !p->watch.canTellHoldable())
		return saved;
	for (int row = 0; row < p->watch.count(); ++row) {
		const auto binding = p->watch.binding(row);
		if (binding.isEmpty())
			return {};
		saved.push_back(binding);
	}
	return saved;
}

QVariantMap DynamicConfiguratorProvider::addWatchBindings(const QVariantList& bindings)
{
	return putWatchBindings(bindings, false);
}

QVariantMap DynamicConfiguratorProvider::watchTally(const QModelIndex& index) const
{
	auto p = d;
	if (!p->current() || p->updating || p->endpoints.changing() || !index.isValid() ||
	    index.model() != &p->endpoints)
		return {};
	p->ensureCoverage();
	using Role = EndpointModel::Role;
	qint64 watched = 0, total = 0;
	QString kind = index.data(Role::WatchLeafKindRole).toString();
	if (index.data(Role::PreviewChildRole).toBool()) {
		const auto field = index.data(Role::PreviewFieldIdRole).toUInt();
		const auto first = index.data(Role::WatchLeafFirstRole).toUInt();
		total = index.data(Role::WatchLeafCountRole).toUInt();
		if (total == 0 || !p->coverage.aggregate(field))
			return {};
		watched = p->coverage.countIn(field, first, quint32(total), p->held(), p->watch.membership());
	} else {
		const auto tally = p->coverage.tally(index, p->held(), p->watch.membership());
		if (!tally)
			return {};
		watched = tally->watched;
		total = tally->total;
		if (!index.data(Role::IsEndpointRole).toBool())
			kind = tally->aggregates > 0 ? QStringLiteral("values") : QStringLiteral("fields");
	}
	if (kind.isEmpty())
		kind = QStringLiteral("values");
	return {{QStringLiteral("watched"), watched}, {QStringLiteral("total"), total},
	        {QStringLiteral("kind"), kind}};
}

QString DynamicConfiguratorProvider::watchRefusal(const QModelIndex& index) const
{
	auto p = d;
	// Until the Watchlist can tell what it holds, a box is not refused yet.
	if (!p->current() || p->updating || p->endpoints.changing() || !index.isValid() ||
	    index.model() != &p->endpoints || !p->watch.canTellHoldable() || watchState(index) >= 0)
		return {};
	using Role = EndpointModel::Role;
	if (index.data(Role::CategoryRole).toInt() != static_cast<int>(telemetry::descriptor::Category::Field))
		return {};
	// The Field a row belongs to, by its path: the row itself or the nearest
	// endpoint above a member.
	auto field = index;
	while (field.isValid() && !field.data(Role::IsEndpointRole).toBool())
		field = field.parent();
	const auto path = [](const QModelIndex& at) {
		const auto group = at.parent().data(Role::NameRole).toString();
		const auto name = at.data(Role::NameRole).toString();
		return group.isEmpty() ? name : group + QLatin1Char('.') + name;
	};
	// A Field's reason; a value row inside it gives its Field's.
	const auto reason = [&p, &path](const QModelIndex& at) -> QString {
		if (!at.data(Role::ReadableRole).toBool())
			return tr("%1 is write only, so it can't be watched.").arg(path(at));
		if (!at.data(Role::PreviewAggregateRole).toBool())
			return {};
		if (at.data(Role::WatchLeafCountRole).toUInt() == 0)
			return tr("%1 has no values to watch.").arg(path(at));
		if (at.data(Role::WireBytesRole).toUInt() > workbenchSnapshotBytes)
			return tr("%1 is larger than the Watchlist reads at once (64 KiB), so it can't be watched.").arg(path(at));
		if (!p->watch.readsMembers())
			return tr("This connection can't read struct members, so %1 can't be watched.").arg(path(at));
		return {};
	};
	if (index.data(Role::PreviewChildRole).toBool() || index.data(Role::IsEndpointRole).toBool())
		return field.isValid() ? reason(field) : QString{};
	// A group or the Fields themselves, with nothing the Watchlist can hold.
	return p->endpoints.rowCount(index) > 0
	    ? tr("Nothing in %1 can be watched.").arg(index.data(Role::NameRole).toString())
	    : QString{};
}

QVariantMap DynamicConfiguratorProvider::setWatchMany(const QVariantList& indexes, bool watched)
{
	auto p = d;
	QVariantMap result{{QStringLiteral("status"), QStringLiteral("refused")},
	                   {QStringLiteral("changed"), 0}, {QStringLiteral("unchanged"), 0},
	                   {QStringLiteral("fields"), 0}, {QStringLiteral("room"), 0},
	                   {QStringLiteral("values"), false}};
	// A read or a write in Details does not hold the tree's boxes, nor does
	// polling (the Watchlist takes changes between two reads); a model update
	// and a stale connection do.
	if (!p->current() || p->updating || p->endpoints.changing())
		return result;
	p->ensureCoverage();
	using Role = EndpointModel::Role;
	// Whole Fields by key, and per struct or array Field the ranges of its
	// values asked for, merged so a value counts once.
	QVector<WatchCoverage::Holdable> whole;
	QSet<quint64> seen;
	struct Span {
		quint32 first = 0, count = 0;
	};
	QHash<quint32, std::vector<Span>> spans;
	QVector<quint32> order;
	// Tree order: a whole Field (its place in whole) or a struct's values
	// (its id), as each is first met.
	QVector<std::pair<bool, quint32>> items;
	const auto span = [&spans, &order, &items](quint32 field, quint32 first, quint32 count) {
		if (count == 0)
			return;
		if (!spans.contains(field)) {
			order.push_back(field);
			items.push_back({false, field});
		}
		spans[field].push_back({first, count});
	};
	for (const auto& value : indexes) {
		const auto index = value.value<QModelIndex>();
		if (!value.canConvert<QModelIndex>() || !index.isValid() || index.model() != &p->endpoints) {
			result[QStringLiteral("status")] = QStringLiteral("invalid");
			return result;
		}
		if (index.data(Role::PreviewChildRole).toBool()) {
			const auto field = index.data(Role::PreviewFieldIdRole).toUInt();
			if (!p->coverage.aggregate(field)) {
				result[QStringLiteral("status")] = QStringLiteral("stale");
				return result;
			}
			span(field, index.data(Role::WatchLeafFirstRole).toUInt(),
			     index.data(Role::WatchLeafCountRole).toUInt());
			continue;
		}
		// An index of a tree since replaced, or past the walk's bounds.
		if (!p->coverage.contains(index)) {
			result[QStringLiteral("status")] = QStringLiteral("stale");
			return result;
		}
		for (const auto& field : p->coverage.holdableUnder(index)) {
			if (field.aggregate)
				span(watch_key::field(field.key), 0, field.leaves);
			else if (!seen.contains(field.key)) {
				seen.insert(field.key);
				items.push_back({true, quint32(whole.size())});
				whole.push_back(field);
			}
		}
	}
	qint64 total = whole.size();
	for (auto& ranges : spans) {
		std::sort(ranges.begin(), ranges.end(),
		          [](const Span& a, const Span& b) { return a.first < b.first; });
		std::vector<Span> merged;
		for (const auto& range : ranges) {
			if (!merged.empty() && range.first <= quint64{merged.back().first} + merged.back().count) {
				const auto end = (std::max)(quint64{merged.back().first} + merged.back().count,
				                            quint64{range.first} + range.count);
				merged.back().count = quint32(end - merged.back().first);
			} else {
				merged.push_back(range);
			}
		}
		ranges = std::move(merged);
		for (const auto& range : ranges)
			total += range.count;
	}
	const auto& held = p->held();
	const auto membership = p->watch.membership();
	const int room = watchlistRowLimit - p->watch.count();
	result[QStringLiteral("fields")] = total;
	result[QStringLiteral("room")] = room;
	result[QStringLiteral("values")] = !spans.isEmpty();
	const auto watchedIn = [&](quint32 field, const Span& range) {
		return p->coverage.watchedIn(field, range.first, range.count, held, membership);
	};
	if (watched) {
		qint64 adding = 0;
		for (const auto& field : whole)
			adding += held.contains(field.key) ? 0 : 1;
		for (const auto field : order)
			for (const auto& range : spans.value(field))
				adding += qint64(range.count) -
				          p->coverage.countIn(field, range.first, range.count, held, membership);
		result[QStringLiteral("unchanged")] = total - adding;
		result[QStringLiteral("adding")] = adding;
		// All of them or none: a part would leave the box partly ticked, and the
		// next tick would try again. Counted before any row is made.
		if (adding > room) {
			result[QStringLiteral("status")] = QStringLiteral("capacity");
			return result;
		}
		QVector<WatchEntry> add;
		for (const auto& [isWhole, at] : items) {
			if (isWhole) {
				const auto& field = whole[qsizetype(at)];
				if (!held.contains(field.key))
					add.push_back(WatchEntry::of(field.key, field.name));
				continue;
			}
			const auto field = at;
			const auto holdable = p->coverage.aggregate(field);
			for (const auto& range : spans.value(field)) {
				const auto entries =
				    holdable ? p->watch.entries(field, range.first, range.count, holdable->name) : std::nullopt;
				if (!entries) {
					result[QStringLiteral("status")] = QStringLiteral("stale");
					return result;
				}
				add += *entries;
			}
		}
		if (!add.isEmpty()) {
			QVariantMap added;
			{
				WatchRows::Changes held{p->rows};
				added = p->watch.addEntries(add);
			}
			if (added.value(QStringLiteral("status")).toString() != QStringLiteral("ok")) {
				result[QStringLiteral("status")] = added.value(QStringLiteral("status"));
				return result;
			}
			const int count = added.value(QStringLiteral("added")).toInt();
			result[QStringLiteral("changed")] = count;
			if (count > 0) {
				p->undo = {};
				p->undo.valid = true;
				p->undo.added = true;
				for (const auto& entry : add)
					p->undo.addedIds.insert(entry.key);
				p->undo.membership = p->watch.membership();
				p->undo.treeEpoch = p->treeEpoch;
			}
		}
	} else {
		QSet<WatchKey> keys;
		for (const auto& field : whole)
			if (held.contains(field.key))
				keys.insert(field.key);
		for (const auto field : order)
			for (const auto& range : spans.value(field))
				for (const auto ordinal : watchedIn(field, range))
					keys.insert(watch_key::member(field, ordinal));
		result[QStringLiteral("unchanged")] = total - keys.size();
		if (!keys.isEmpty()) {
			QVector<WatchlistModel::Removed> removed;
			{
				WatchRows::Changes held{p->rows};
				removed = p->watch.removeMany(keys);
			}
			if (removed.isEmpty())
				return result;
			result[QStringLiteral("changed")] = static_cast<int>(removed.size());
			p->undo = {};
			p->undo.valid = true;
			p->undo.removed = removed;
			p->undo.membership = p->watch.membership();
			p->undo.treeEpoch = p->treeEpoch;
		}
	}
	result[QStringLiteral("status")] = QStringLiteral("ok");
	p->notify();
	return result;
}

QVariantMap DynamicConfiguratorProvider::undoWatch()
{
	auto p = d;
	QVariantMap result{{QStringLiteral("status"), QStringLiteral("refused")},
	                   {QStringLiteral("changed"), 0}};
	if (!canUndoWatch())
		return result;
	if (p->undo.formats) {
		// The rows' formats first: if the model refuses, nothing changed and
		// Undo stays.
		const auto& undo = p->undo;
		const auto now = p->pathFormats();
		int changed = undo.format != p->watchFormat ? 1 : 0;
		for (auto at = undo.pathFormats.cbegin(); at != undo.pathFormats.cend(); ++at)
			changed += now.value(at.key()) != at.value() || !now.contains(at.key()) ? 1 : 0;
		for (auto at = now.cbegin(); at != now.cend(); ++at)
			changed += undo.pathFormats.contains(at.key()) ? 0 : 1;
		if (!p->putPathFormats(undo.pathFormats))
			return result;
		p->putFormat(undo.format);
		p->undo = {};
		++p->formatRevision;
		result[QStringLiteral("status")] = QStringLiteral("ok");
		result[QStringLiteral("changed")] = changed;
		p->notify();
		return result;
	}
	const auto undo = std::exchange(p->undo, {});
	int changed = 0;
	{
		WatchRows::Changes held{p->rows};
		if (undo.added)
			changed = static_cast<int>(p->watch.removeMany(undo.addedIds).size());
		else if (p->watch.restore(undo.removed))
			changed = static_cast<int>(undo.removed.size());
	}
	if (changed == 0)
		return result;
	result[QStringLiteral("status")] = QStringLiteral("ok");
	result[QStringLiteral("changed")] = changed;
	p->notify();
	return result;
}

void DynamicConfiguratorProvider::refreshWatch()
{
	auto p = d;
	p->watch.begin(false);
}

void DynamicConfiguratorProvider::startWatch()
{
	auto p = d;
	p->watch.begin(true);
}

bool DynamicConfiguratorProvider::canWriteWatch() const
{
	return d->current() && d->allowed && d->binding && d->binding->mutationsAllowed() && d->writer &&
	       !d->writer->retired && d->writer->action == DynamicViewState::Action::None;
}

bool DynamicConfiguratorProvider::watchWriting() const
{
	return d->writer && d->writer->action != DynamicViewState::Action::None;
}

QString DynamicConfiguratorProvider::watchWriteKind() const
{
	// Said only while its words are: an outcome of an earlier session is gone.
	return watchWriteOutcome().isEmpty() ? QString{} : d->watchWriteKind;
}

QString DynamicConfiguratorProvider::watchWriteOutcome() const
{
	return d->history(d->watchWrite, d->watchWriteSession);
}

bool DynamicConfiguratorProvider::writeWatch(int row, const QString& text)
{
	auto p = d;
	auto* model = p->watch.model();
	if (!canWriteWatch() || !model || row < 0 || row >= model->rowCount())
		return false;
	const auto index = model->index(row, 0);
	if (!model->data(index, WatchlistModel::WritableRole).toBool())
		return false;
	p->watchWriteSession = p->writer->view.session();
	// Hex is taken whatever the row shows: as the value's exact text.
	auto typed = text.trimmed();
	if (watch_format::isHex(typed)) {
		const auto* watched = qobject_cast<const WatchlistModel*>(model);
		const auto parsed = watch_format::fromHex(typed, watched ? watched->valueType(row) : WatchValueType{});
		if (!parsed.error.isEmpty()) {
			p->watchWrite = model->data(index, WatchlistModel::NameRole).toString() + QStringLiteral(" not written: ") +
			                parsed.error;
			p->watchWriteKind = QStringLiteral("refused");
			p->notify();
			return false;
		}
		typed = parsed.canonical;
	}
	const auto key = WatchKey(model->data(index, WatchlistModel::WatchKeyRole).toULongLong());
	const auto id = watch_key::field(key);
	std::optional<WatchLeaves::Leaf> leaf;
	if (!watch_key::isWhole(key) && !(leaf = p->watch.leaf(key))) {
		p->watchWrite = QStringLiteral("Write refused: the Field is not available");
		p->watchWriteKind = QStringLiteral("refused");
		p->notify();
		return false;
	}
	auto& view = p->writer->view;
	if (view.editing())
		view.cancelEdit();
	const auto field = view.bindField(id);
	if (!field || view.selectField(field.endpoint) != cl::Status::Ok) {
		p->watchWrite = QStringLiteral("Write refused: the Field is not available");
		p->watchWriteKind = QStringLiteral("refused");
		p->notify();
		return false;
	}
	auto target = p->writer;
	const auto submitted = view.refresh([target](const ins::ReadResult& value) noexcept {
		target->read = value;
		target->dirty = true;
	});
	if (!submitted) {
		p->watchWrite =
		    QStringLiteral("Write refused: %1").arg(static_cast<unsigned>(submitted.status));
		p->watchWriteKind = QStringLiteral("refused");
		p->notify();
		return false;
	}
	p->writer->action = DynamicViewState::Action::Read;
	p->writer->staleData = false;
	p->writer->submittedSession = view.session();
	p->writer->sequence = submitted.handle.sequence;
	p->writer->eventReported = false;
	if (p->binding)
		p->binding->requestSubmitted();
	p->writeId = id;
	p->writeLeaf = std::move(leaf);
	p->writeIntended = typed;
	p->writeText = typed;
	p->writeName = model->data(index, WatchlistModel::NameRole).toString();
	const auto* watchedRow = qobject_cast<const WatchlistModel*>(model);
	p->writeType = watchedRow ? watchedRow->valueType(row) : WatchValueType{};
	p->watchWrite = QStringLiteral("Writing ") + p->writeName;
	p->watchWriteKind = QStringLiteral("writing");
	p->notify();
	return true;
}

void DynamicConfiguratorProvider::stopWatch()
{
	auto p = d;
	p->watch.stop();
	// The Plot's lines end here; the time not read is not drawn.
	p->traces.breakAll();
}

QString DynamicConfiguratorProvider::selection() const
{
	return d->selection;
}

QModelIndex DynamicConfiguratorProvider::selectedEndpointIndex() const
{
	const QModelIndex index = d->selectedIndex;
	return d->current() && d->category != 0 &&
	               d->endpoints.matchesEndpoint(index, d->category, d->state->endpointId)
	           ? index : QModelIndex{};
}

QString DynamicConfiguratorProvider::serviceResponseEndpoint() const
{
	return d->current() && d->responses.rowCount() ? d->responseEndpoint : QString{};
}

bool DynamicConfiguratorProvider::connected() const
{
	return d->current();
}

bool DynamicConfiguratorProvider::busy() const
{
	// The Watchlist's own reads are not this view's work: Cancel, Batch editing
	// and the busy indicator stay as they are while it polls.
	return d->busy() || d->batch.pending();
}

bool DynamicConfiguratorProvider::editing() const
{
	return d->current() && d->state->view.editing();
}

bool DynamicConfiguratorProvider::selectionRequiresDiscard() const
{
	return d->current() && (d->state->view.dirty() || !d->invalid.isEmpty());
}

bool DynamicConfiguratorProvider::allowMutations() const
{
	return d->allowed && d->binding && d->binding->mutationsAllowed();
}

void DynamicConfiguratorProvider::setAllowMutations(bool allowed)
{
	auto p = d;
	if (p->updating)
		return;
	p->allowed = allowed && p->current() && p->binding->mutationsAllowed();
	p->notify();
}

bool DynamicConfiguratorProvider::canEdit() const
{
	return d->mutate() && d->state->view.canEdit();
}

bool DynamicConfiguratorProvider::canApply() const
{
	return d->mutate() && d->state->view.canApply() && d->invalid.isEmpty();
}

bool DynamicConfiguratorProvider::canRefresh() const
{
	return d->selectable() && d->category == 1;
}

bool DynamicConfiguratorProvider::commandSelected() const
{
	return d->current() && d->category == 2;
}

bool DynamicConfiguratorProvider::canExecute() const
{
	return d->mutate() && d->state->view.canExecute() && d->invalid.isEmpty();
}

bool DynamicConfiguratorProvider::serviceSelected() const
{
	return d->current() && d->category == 3;
}

bool DynamicConfiguratorProvider::canCallService() const
{
	return d->mutate() && d->state->view.canCallService() && d->invalid.isEmpty();
}

QString DynamicConfiguratorProvider::writeOutcome() const
{
	return d->history(d->write, d->writeSession);
}

QString DynamicConfiguratorProvider::commandOutcome() const
{
	return d->history(d->command, d->commandSession);
}

QString DynamicConfiguratorProvider::serviceOutcome() const
{
	return d->history(d->service, d->serviceSession);
}

bool DynamicConfiguratorProvider::writeOutcomeNeedsAttention() const
{
	return d->writeAttention;
}

bool DynamicConfiguratorProvider::commandOutcomeNeedsAttention() const
{
	return d->commandAttention;
}

bool DynamicConfiguratorProvider::serviceOutcomeNeedsAttention() const
{
	return d->serviceAttention;
}

QString DynamicConfiguratorProvider::statusText() const
{
	return d->status;
}

bool DynamicConfiguratorProvider::selectEndpoint(const QModelIndex& index)
{
	auto p = d;
	if (!p->selectable())
		return false;
	// Keep a fresh Field or prepared request draft. An invalidated or
	// cancelled request still needs explicit preparation on selection.
	if (index.isValid() && index == p->selectedIndex && !p->state->staleData &&
	    (p->category == 1 || p->state->view.editing()))
		return true;
	const auto endpoint = p->endpoints.endpoint(index);
	if (endpoint.isEmpty())
		return false;
	const auto id = endpoint.value(QStringLiteral("id")).toUInt();
	const auto category = endpoint.value(QStringLiteral("category")).toInt();
	cl::Status status = cl::Status::InvalidArgument;
	if (category == 1) {
		const auto field = p->state->view.bindField(id);
		if (field)
			status = p->state->view.selectField(field.endpoint);
	} else if (category == 2) {
		const auto endpoint = p->state->view.bindCommand(id);
		if (endpoint)
			status = p->state->view.selectCommand(endpoint.endpoint);
	} else if (category == 3) {
		const auto endpoint = p->state->view.bindService(id);
		if (endpoint)
			status = p->state->view.selectService(endpoint.endpoint);
	}
	if (status != cl::Status::Ok) {
		p->status = QStringLiteral("Selection refused: %1").arg(static_cast<unsigned>(status));
		p->notify();
		return false;
	}
	p->updating = true;
	p->selection = endpoint.value(QStringLiteral("path")).toString();
	p->category = category;
	p->state->endpointId = id;
	p->selectedIndex = index;
	p->present(false);
	p->invalid.clear();
	p->state->staleData = false;
	p->values.resetSnapshot(p->state->view);
	p->responses.clear();
	p->present();
	p->status = category == 1
	                ? QStringLiteral("Press Refresh to read")
	                : QStringLiteral("Arguments start at canonical zero; no request sent");
	p->updating = false;
	p->notify();
	return true;
}

bool DynamicConfiguratorProvider::refresh()
{
	auto p = d;
	if (!canRefresh())
		return false;
	auto state = p->state;
	const auto submitted = state->view.refresh([state](const ins::ReadResult& value) noexcept {
		state->read = value;
		state->dirty = true;
	});
	if (!submitted) {
		p->status = QStringLiteral("Read refused: %1").arg(static_cast<unsigned>(submitted.status));
		p->notify();
		return false;
	}
	state->action = DynamicViewState::Action::Read;
	state->staleData = false;
	state->submittedSession = state->view.session();
	state->sequence = submitted.handle.sequence;
	state->eventReported = false;
	if (p->binding)
		p->binding->requestSubmitted();
	p->updating = true;
	p->values.clear();
	p->updating = false;
	p->status = QStringLiteral("Read pending");
	p->notify();
	return true;
}

bool DynamicConfiguratorProvider::beginEdit()
{
	auto p = d;
	if (!canEdit())
		return false;
	if (p->state->view.beginEdit() != rt::Status::Ok)
		return false;
	p->updating = true;
	p->invalid.clear();
	p->values.resetSnapshot(p->state->view);
	p->updating = false;
	p->notify();
	return true;
}

void DynamicConfiguratorProvider::cancelEdit()
{
	auto p = d;
	if (!p->selectable())
		return;
	p->state->view.cancelEdit();
	p->state->staleData = false;
	p->invalid.clear();
	p->updating = true;
	p->values.resetSnapshot(p->state->view);
	p->updating = false;
	p->notify();
}

bool DynamicConfiguratorProvider::apply()
{
	auto p = d;
	if (!canApply())
		return false;
	auto state = p->state;
	const auto submitted = state->view.apply([state](const wb::ApplyResult& value) noexcept {
		state->applied = value;
		state->dirty = true;
	});
	if (!submitted) {
		p->status =
		    QStringLiteral("Write refused: %1").arg(static_cast<unsigned>(submitted.status));
		p->notify();
		return false;
	}
	state->action = DynamicViewState::Action::Apply;
	p->writeSession = state->view.session();
	state->submittedSession = state->view.session();
	state->sequence = submitted.handle.sequence;
	state->eventReported = false;
	if (p->binding)
		p->binding->requestSubmitted();
	p->write = p->status = QStringLiteral("Write pending");
	p->writeAttention = false;
	p->updating = true;
	p->values.clear();
	p->responses.clear();
	p->updating = false;
	if (p->binding)
		p->binding->mutationAccepted(state);
	p->notify();
	return true;
}

bool DynamicConfiguratorProvider::executeCommand()
{
	auto p = d;
	if (!canExecute())
		return false;
	auto state = p->state;
	const auto submitted = state->view.execute([state](const ins::OperationResult& value) noexcept {
		state->commanded = value;
		state->dirty = true;
	});
	if (!submitted) {
		p->status =
		    QStringLiteral("Command refused: %1").arg(static_cast<unsigned>(submitted.status));
		p->notify();
		return false;
	}
	state->action = DynamicViewState::Action::Command;
	p->commandSession = state->view.session();
	state->submittedSession = state->view.session();
	state->sequence = submitted.handle.sequence;
	state->eventReported = false;
	if (p->binding)
		p->binding->requestSubmitted();
	p->command = p->status = QStringLiteral("Command pending");
	p->commandAttention = false;
	p->updating = true;
	p->values.clear();
	p->responses.clear();
	p->updating = false;
	if (p->binding)
		p->binding->mutationAccepted(state);
	p->notify();
	return true;
}

bool DynamicConfiguratorProvider::callService()
{
	auto p = d;
	if (!canCallService())
		return false;
	auto state = p->state;
	const auto submitted =
	    state->view.callService([state](const wb::ServiceResult& value) noexcept {
		    state->serviced = value;
		    state->dirty = true;
	    });
	if (!submitted) {
		p->status =
		    QStringLiteral("Service refused: %1").arg(static_cast<unsigned>(submitted.status));
		p->notify();
		return false;
	}
	state->action = DynamicViewState::Action::Service;
	p->serviceSession = state->view.session();
	p->responseEndpoint = p->selection;
	p->responseId = state->endpointId;
	state->submittedSession = state->view.session();
	state->sequence = submitted.handle.sequence;
	state->eventReported = false;
	if (p->binding)
		p->binding->requestSubmitted();
	p->service = p->status = QStringLiteral("Service pending");
	p->serviceAttention = false;
	p->updating = true;
	p->present(false);
	p->values.clear();
	p->responses.clear();
	p->present();
	p->updating = false;
	if (p->binding)
		p->binding->mutationAccepted(state);
	p->notify();
	return true;
}

bool DynamicConfiguratorProvider::setScalarText(const QModelIndex& index, const QString& value)
{
	auto p = d;
	if (!p->mutate() || (!editing() && !commandSelected() && !serviceSelected()))
		return false;
	const auto node = p->values.node(index);
	if (!node)
		return false;
	if (value == p->values.data(index, ValueModel::EditorTextRole).toString()) {
		p->invalid.remove(index.internalId());
		p->notify();
		return true;
	}
	const auto info = commandSelected() || serviceSelected() ? p->state->view.editorInfo(node.value)
	                                                         : p->state->view.info(node.value);
	if (!info)
		return false;
	bool parsed = false;
	const auto status =
	    detail::setScalarText(p->state->view, node.value, info.value.code, value, parsed);
	const bool valid = parsed && status == rt::Status::Ok;
	if (valid) {
		p->invalid.remove(index.internalId());
		p->updating = true;
		p->values.updateScalar(index);
		p->updating = false;
	} else
		p->invalid.insert(index.internalId());
	p->status = valid ? QStringLiteral("Local draft updated")
	                  : QStringLiteral("Invalid exact scalar value - draft unchanged");
	p->notify();
	return valid;
}
} // namespace telemetry::ux
