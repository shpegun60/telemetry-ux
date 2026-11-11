// Shared native execution, independent Qt presentation. Ruslan Kovtun, codexAi. MIT.
#include "DynamicConfiguratorProvider.hpp"
#include "DynamicWatchlist.hpp"
#include "DynamicBatch.hpp"
#include "ScalarEditor.hpp"
#include <QtCore/QPersistentModelIndex>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QThread>
#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
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

struct DynamicConfiguratorProvider::Impl {
	QPointer<DynamicConfiguratorProvider> q;
	QPointer<DynamicConfiguratorBinding> binding, desired;
	std::shared_ptr<DynamicViewState> state;
	EndpointModel endpoints;
	QPersistentModelIndex selectedIndex;
	ValueModel values, responses;
	DynamicWatchlist watch;
	DynamicBatch batch;
	QString selection, responseEndpoint, status = QStringLiteral("Not connected"), write, command,
	                                     service;
	std::uint32_t writeSession = 0, commandSession = 0, serviceSession = 0;
	int category = 0;
	quint32 responseId = 0;
	PresentationOverlay presentation;
	QString presentationError;
	bool allowed = false, updating = false;
	QSet<quintptr> invalid;
	QMetaObject::Connection changed, destroyed;

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

	bool idle() const
	{
		return selectable() && !watch.active();
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

	QString history(const QString& value, std::uint32_t origin) const
	{
		return !value.isEmpty() && (!current() || origin != state->view.session())
		           ? value + QStringLiteral(" - previous session")
		           : value;
	}

	void notify()
	{
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
		updating = true;
		std::optional<bool> readDone, applyDone;
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
				commandDone = {result.executed(), result.accepted()};
			}
			if (state->serviced) {
				const auto result = *state->serviced;
				state->serviced.reset();
				state->action = DynamicViewState::Action::None;
				serviceDone = {result.operation.succeeded(), result.published};
				service = status = result.operation.succeeded()
				                       ? result.published
				                             ? QStringLiteral("Completed - response published")
				                             : QStringLiteral("Completed - value status %1")
				                                   .arg(static_cast<unsigned>(result.valueStatus))
				                       : operationText(result.operation);
			}
			if (!current()) {
				watch.retire();
				batch.retire();
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
					status = QStringLiteral(
					    "Data changed in another panel - read or prepare arguments again");
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
	}
};

DynamicConfiguratorProvider::DynamicConfiguratorProvider(DynamicConfiguratorBinding* binding,
                                                         QObject* parent)
    : QObject(parent), d(std::make_shared<Impl>(this))
{
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
	setBinding(binding);
}

DynamicConfiguratorProvider::~DynamicConfiguratorProvider()
{
	if (d->state)
		d->state->retire();
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
	p->watch.retire();
	p->batch.retire();
	if (!alive)
		return;
	// Host replacement settles the old owner first. Never move accepted work to
	// a different binding. A caller may retry attachment after the old completion.
	if (p->busy()) {
		p->desired = binding;
		if (p->state)
			p->state->retire();
		return;
	}
	if (p->state)
		p->state->retire();
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
	p->binding = target;
	p->desired = target;
	p->allowed = false;
	p->category = 0;
	p->selection.clear();
	p->responseEndpoint.clear();
	p->invalid.clear();
	if (target) {
		p->state = target->acquire();
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
	p->status =
	    p->current() ? QStringLiteral("Select an endpoint") : QStringLiteral("Not connected");
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
	p->watch.setSource(std::move(source));
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

QString DynamicConfiguratorProvider::watchStatus() const
{
	return d->watch.status();
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
	if (!p->idle())
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
	if (!p->idle()) {
		result[QStringLiteral("status")] = QStringLiteral("stale");
		return result;
	}
	result = p->watch.addMany(fields);
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

void DynamicConfiguratorProvider::clearWatch()
{
	auto p = d;
	p->watch.clear();
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

void DynamicConfiguratorProvider::stopWatch()
{
	auto p = d;
	p->watch.stop();
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
	return d->busy() || d->watch.pending() || d->batch.pending();
}

bool DynamicConfiguratorProvider::editing() const
{
	return d->current() && d->state->view.editing();
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

QString DynamicConfiguratorProvider::statusText() const
{
	return d->status;
}

bool DynamicConfiguratorProvider::selectEndpoint(const QModelIndex& index)
{
	auto p = d;
	if (!p->selectable())
		return false;
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
	p->write = p->status = QStringLiteral("Write pending");
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
	p->command = p->status = QStringLiteral("Command pending");
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
	p->service = p->status = QStringLiteral("Service pending");
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
