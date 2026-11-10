// One explicit ordered request; Qt owns presentation, the Session owns execution. MIT.
#include "DynamicBatch.hpp"
#include <algorithm>
#include <initializer_list>
#include <utility>

namespace telemetry::ux {
namespace {
QString operationName(client::Operation op)
{
	switch (op) {
		case client::Operation::FieldRead:
			return QStringLiteral("read");
		case client::Operation::FieldWrite:
			return QStringLiteral("write");
		case client::Operation::Command:
			return QStringLiteral("command");
		case client::Operation::Service:
			return QStringLiteral("service");
		default:
			return {};
	}
}

QString statusText(client::Status status)
{
	if (status == client::Status::OutcomeUnknown)
		return QStringLiteral("OutcomeUnknown - may have executed; no automatic retry");
	if (status == client::Status::QueueFull)
		return QStringLiteral("QueueFull - draft and previous results retained");
	if (status == client::Status::Timeout)
		return QStringLiteral("Timeout");
	if (status == client::Status::Cancelled)
		return QStringLiteral("Cancelled");
	if (status == client::Status::Unsupported)
		return QStringLiteral("Batch unavailable for this session");
	return QStringLiteral("Status %1").arg(static_cast<unsigned>(status));
}

QString itemText(client::Operation operation, const QVariantMap& result)
{
	if (result.value(QStringLiteral("accepted")).toBool())
		return QStringLiteral("Accepted - execution is not confirmed");
	if (result.value(QStringLiteral("executed")).toBool())
		return QStringLiteral("Executed");
	if (result.value(QStringLiteral("succeeded")).toBool())
		return QStringLiteral("Completed");
	if (result.value(QStringLiteral("dispatch")).toUInt() != 0)
		return QStringLiteral("Dispatch refused");
	const auto code = result.value(QStringLiteral("endpointStatus")).toUInt();
	const auto name = [code](std::initializer_list<const char*> names) {
		return code < names.size() ? QString::fromLatin1(*(names.begin() + code))
		                           : QStringLiteral("Endpoint refused");
	};
	if (operation == client::Operation::Service)
		return name({"Ok", "InvalidArgument", "Unavailable", "Busy", "Failed"});
	if (operation == client::Operation::FieldWrite)
		return name({"Applied", "NotFound", "ReadOnly", "InvalidValue", "Busy", "Unavailable"});
	if (operation == client::Operation::Command)
		return name({"Executed", "Accepted", "NotFound", "Unavailable", "ArgumentCountMismatch",
		             "InvalidValue", "Busy", "Failed"});
	return QStringLiteral("Read refused");
}
} // namespace

int BatchListModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : int(rows_.size());
}

QVariant BatchListModel::data(const QModelIndex& index, int role) const
{
	if (index.model() != this || index.column() != 0 || index.row() < 0 ||
	    index.row() >= int(rows_.size()))
		return {};
	const auto& row = rows_[std::size_t(index.row())];
	switch (role) {
		case Qt::DisplayRole:
		case NameRole:
			return row.name;
		case OperationRole:
			return operationName(row.operation);
		case EndpointIdRole:
			return row.id;
		case StatusRole:
			return row.status;
		case HasValueRole:
			return row.hasValue && (!valueGuard_ || valueGuard_());
		case HistoricalRole:
			return row.historical;
		case ResultRole:
			return row.result;
		default:
			return {};
	}
}

QHash<int, QByteArray> BatchListModel::roleNames() const
{
	return {{NameRole, "name"},     {OperationRole, "operation"}, {EndpointIdRole, "endpointId"},
	        {StatusRole, "status"}, {HasValueRole, "hasValue"},   {HistoricalRole, "historical"},
	        {ResultRole, "result"}};
}

bool BatchListModel::changing() const noexcept
{
	return changing_;
}

const std::vector<BatchRow>& BatchListModel::rows() const noexcept
{
	return rows_;
}

void BatchListModel::replace(std::vector<BatchRow> rows)
{
	if (changing_)
		return;
	changing_ = true;
	beginResetModel();
	rows_ = std::move(rows);
	endResetModel();
	changing_ = false;
}

void BatchListModel::setValueGuard(std::function<bool()> guard)
{
	valueGuard_ = std::move(guard);
}

struct DynamicBatch::Impl : std::enable_shared_from_this<DynamicBatch::Impl> {
	struct Request {
		std::shared_ptr<void> storage;
		quint32 session = 0;
		std::vector<BatchRow> rows;
		std::optional<BatchCompletion> completion;
	};

	BatchSource source;
	BatchListModel model;
	ValueModel responses;
	QObject deferredContext;
	std::array<std::byte, 65536> responseMemory{};
	integration::inspector::SnapshotStore snapshot;
	std::shared_ptr<Request> request;
	std::function<void()> notify, finished;
	std::function<bool()> blocked, allowed;
	int selected = -1;
	bool publishing = false, invalidationPending = false, pendingHistorical = false,
	     invalidationQueued = false, retired = true;
	QString status = QStringLiteral("Add ordered operations; Execute sends one Batch");

	Impl()
	    : snapshot(responseMemory, {this, +[](const void* p) noexcept {
		                                const auto& state = *static_cast<const Impl*>(p);
		                                return state.current() && !state.invalidationPending;
	                                }})
	{
		model.setValueGuard([this] {
			return current() && !invalidationPending;
		});
	}

	bool current() const
	{
		return !retired && source.storage && source.current && source.current();
	}

	bool editable() const
	{
		return current() && !request && !publishing && !invalidationPending && !model.changing() &&
		       !responses.changing() && (!blocked || !blocked()) &&
		       (!source.busy || !source.busy());
	}

	bool permitted() const
	{
		return allowed && allowed();
	}

	void signal()
	{
		if (notify)
			notify();
	}

	bool budgets(const BatchRow* candidate = nullptr) const
	{
		const auto limits = current() && source.capability ? source.capability() : std::nullopt;
		if (!limits)
			return false;
		quint64 tx = 2, rx = 2;
		const auto add = [&](const BatchRow& row) {
			tx += 8 + quint64(row.request.size());
			rx += 12 + quint64(row.responseBytes);
		};
		for (const auto& row : model.rows())
			add(row);
		if (candidate)
			add(*candidate);
		const auto count = model.rows().size() + (candidate ? 1 : 0);
		return count <= 32 && count <= limits->items && tx <= 4096 && tx <= limits->requestBytes &&
		       rx <= responseMemory.size() && rx <= limits->replyBytes;
	}

	void showResponse()
	{
		responses.clear();
		snapshot.invalidate();
		if (invalidationPending || !current() || selected < 0 ||
		    selected >= int(model.rows().size()))
			return;
		const auto& row = model.rows()[std::size_t(selected)];
		if (!row.hasValue || row.historical || row.session != source.session() || !source.schema)
			return;
		const auto schema = source.schema();
		if (snapshot.admit(schema, row.session) != integration::inspector::ModelStatus::Ok)
			return;
		const auto category = row.operation == client::Operation::Service
		                          ? descriptor::Category::Service
		                          : descriptor::Category::Field;
		if (snapshot.selectEndpoint(row.id, row.responseType, category) !=
		    integration::inspector::ModelStatus::Ok)
			return;
		const auto identity = snapshot.snapshotInfo();
		const auto bytes =
		    client::Input{reinterpret_cast<const std::byte*>(row.response.constData()),
		                  std::size_t(row.response.size())};
		if (snapshot.publish(bytes, identity.admission, identity.revision) ==
		    integration::inspector::ModelStatus::Ok)
			responses.resetSnapshot(snapshot);
	}

	void deferInvalidation()
	{
		if (invalidationQueued)
			return;
		invalidationQueued = true;
		const auto weak = weak_from_this();
		QMetaObject::invokeMethod(
		    &deferredContext,
		    [weak] {
			    if (const auto p = weak.lock()) {
				    p->invalidationQueued = false;
				    if (p->publishing || p->model.changing() || p->responses.changing()) {
					    p->deferInvalidation();
					    return;
				    }
				    p->finishPublication();
			    }
		    },
		    Qt::QueuedConnection);
	}

	void finishPublication()
	{
		publishing = false;
		if (std::exchange(invalidationPending, false))
			invalidate(std::exchange(pendingHistorical, false));
	}

	void invalidate(bool historical)
	{
		if (publishing || model.changing() || responses.changing()) {
			invalidationPending = true;
			pendingHistorical |= historical;
			deferInvalidation();
			return;
		}
		if (std::none_of(model.rows().begin(), model.rows().end(), [historical](const auto& row) {
			    return row.hasValue || !row.response.isEmpty() || (historical && !row.historical);
		    }))
			return;
		publishing = true;
		auto rows = model.rows();
		for (auto& row : rows) {
			row.hasValue = false;
			row.response.clear();
			row.historical |= historical;
		}
		responses.clear();
		snapshot.invalidate();
		model.replace(std::move(rows));
		finishPublication();
		signal();
	}
};

DynamicBatch::DynamicBatch() : d(std::make_shared<Impl>())
{}

DynamicBatch::~DynamicBatch() = default;

void DynamicBatch::setNotify(std::function<void()> notify, std::function<bool()> blocked,
                             std::function<bool()> allowed, std::function<void()> finished)
{
	d->notify = std::move(notify);
	d->blocked = std::move(blocked);
	d->allowed = std::move(allowed);
	d->finished = std::move(finished);
}

void DynamicBatch::setSource(BatchSource source)
{
	auto p = d;
	p->invalidate(true);
	p->source = std::move(source);
	p->retired = false;
	p->signal();
}

void DynamicBatch::retire()
{
	auto p = d;
	if (p->retired)
		return;
	p->retired = true;
	p->invalidate(true);
}

void DynamicBatch::invalidateValues()
{
	auto p = d;
	p->invalidate(false);
}

bool DynamicBatch::supported() const
{
	return d->current() && d->source.capability && d->source.capability().has_value();
}

bool DynamicBatch::pending() const
{
	return bool(d->request);
}

bool DynamicBatch::active() const
{
	return pending() || d->publishing || d->model.changing() || d->responses.changing();
}

bool DynamicBatch::canAdd(const BatchRow& row) const
{
	return d->editable() && d->budgets(&row) && d->source.session &&
	       row.session == d->source.session() &&
	       (row.operation == client::Operation::FieldRead || d->permitted());
}

bool DynamicBatch::add(BatchRow row)
{
	auto p = d;
	if (!canAdd(row))
		return false;
	const bool fieldDraft = row.operation == client::Operation::FieldWrite;
	row.request = QByteArray(row.request.constData(), row.request.size());
	p->publishing = true;
	auto rows = p->model.rows();
	rows.push_back(std::move(row));
	p->selected = int(rows.size()) - 1;
	p->responses.clear();
	p->snapshot.invalidate();
	p->model.replace(std::move(rows));
	p->status =
	    fieldDraft
	        ? QStringLiteral(
	              "Field draft captured. Cancel closes the current editor; the Batch keeps its copy. Execute sends one ordered request.")
	        : QStringLiteral("Draft - Execute sends one ordered Batch");
	p->finishPublication();
	p->signal();
	return true;
}

bool DynamicBatch::remove(int row)
{
	auto p = d;
	if (!p->editable() || row < 0 || row >= count())
		return false;
	p->publishing = true;
	auto rows = p->model.rows();
	rows.erase(rows.begin() + row);
	p->selected = -1;
	p->responses.clear();
	p->snapshot.invalidate();
	p->model.replace(std::move(rows));
	p->finishPublication();
	p->signal();
	return true;
}

bool DynamicBatch::move(int row, int delta)
{
	auto p = d;
	if (!p->editable() || (delta != -1 && delta != 1) || row < 0 || row >= count() ||
	    row + delta < 0 || row + delta >= count())
		return false;
	p->publishing = true;
	auto rows = p->model.rows();
	std::swap(rows[std::size_t(row)], rows[std::size_t(row + delta)]);
	if (p->selected == row)
		p->selected += delta;
	else if (p->selected == row + delta)
		p->selected = row;
	p->model.replace(std::move(rows));
	p->finishPublication();
	p->signal();
	return true;
}

bool DynamicBatch::clear()
{
	auto p = d;
	if (p->request || p->publishing || p->model.changing() || p->responses.changing() ||
	    (p->blocked && p->blocked()))
		return false;
	p->publishing = true;
	p->selected = -1;
	p->responses.clear();
	p->snapshot.invalidate();
	p->model.replace({});
	p->status = QStringLiteral("Add ordered operations; Execute sends one Batch");
	p->finishPublication();
	p->signal();
	return true;
}

bool DynamicBatch::select(int row)
{
	auto p = d;
	if (p->request || p->publishing || p->model.changing() || p->responses.changing() || row < 0 ||
	    row >= count())
		return false;
	p->publishing = true;
	p->selected = row;
	p->showResponse();
	p->finishPublication();
	p->signal();
	return true;
}

bool DynamicBatch::canExecute() const
{
	const auto p = d;
	if (!p->editable() || !count() || !p->budgets() || !p->source.submit || !p->source.session)
		return false;
	for (const auto& row : p->model.rows())
		if (row.session != p->source.session() || row.historical ||
		    (row.operation != client::Operation::FieldRead && !p->permitted()))
			return false;
	return true;
}

bool DynamicBatch::execute()
{
	auto p = d;
	if (!canExecute())
		return false;
	auto request = std::make_shared<Impl::Request>();
	request->storage = p->source.storage;
	request->session = p->source.session();
	request->rows = p->model.rows();
	std::array<BatchSource::Request, 32> inputs{};
	for (std::size_t i = 0; i < request->rows.size(); ++i) {
		const auto& row = request->rows[i];
		inputs[i] = {row.operation,
		             row.id,
		             {reinterpret_cast<const std::byte*>(row.request.constData()),
		              std::size_t(row.request.size())}};
	}
	p->publishing = true;
	p->request = request;
	const auto submitted = p->source.submit(std::span{inputs}.first(request->rows.size()),
	                                        BatchSource::Completion{request, &request->completion});
	if (!submitted) {
		p->request.reset();
		p->status = statusText(submitted.status);
		p->finishPublication();
		p->signal();
		return false;
	}
	auto rows = request->rows;
	for (auto& row : rows) {
		row.status = QStringLiteral("Pending");
		row.result.clear();
		row.response.clear();
		row.hasValue = false;
	}
	p->responses.clear();
	p->snapshot.invalidate();
	p->model.replace(std::move(rows));
	p->status = QStringLiteral("Pending - one ordered Batch request");
	p->finishPublication();
	p->signal();
	return true;
}

void DynamicBatch::poll()
{
	auto p = d;
	if (p->publishing || p->model.changing() || p->responses.changing())
		return;
	if (!p->request || !p->request->completion)
		return;
	const auto request = p->request;
	const auto completion = std::move(*request->completion);
	p->publishing = true;
	const bool current = p->current() && p->source.storage == request->storage &&
	                     p->source.session && p->source.session() == request->session;
	auto rows = request->rows;
	bool checked = completion.count == int(rows.size());
	for (std::size_t i = 0; checked && i < rows.size(); ++i)
		checked = completion.rows[i].id == rows[i].id &&
		          completion.rows[i].operation == rows[i].operation;
	for (std::size_t i = 0; i < rows.size(); ++i) {
		auto& row = rows[i];
		row.response.clear();
		row.hasValue = false;
		row.result.clear();
		row.historical = !current;
		if (checked) {
			const auto& result = completion.rows[i];
			row.result = result.result;
			row.status = itemText(row.operation, row.result);
			const auto valueStatus = row.result.value(QStringLiteral("valueStatus")).toString();
			if (valueStatus == QStringLiteral("InvalidPayload") ||
			    valueStatus == QStringLiteral("TypeMismatch"))
				row.status += QStringLiteral(" - invalid response value");
			else if (!current || valueStatus == QStringLiteral("Stale"))
				row.status += QStringLiteral(" - value is stale");
			if (current && result.hasValue) {
				row.response = result.payload;
				row.hasValue = true;
			}
		} else
			row.status = completion.status == client::Status::Ok
			                 ? QStringLiteral("No checked item outcome")
			                 : statusText(completion.status);
	}
	p->model.replace(std::move(rows));
	p->request.reset();
	p->showResponse();
	p->status = completion.status == client::Status::Ok && completion.succeeded
	                ? completion.valueStatus == QStringLiteral("InvalidPayload") ||
	                          completion.valueStatus == QStringLiteral("TypeMismatch")
	                      ? QStringLiteral(
	                            "Batch reply received - invalid response values; inspect each item")
	                      : QStringLiteral("Batch reply received - inspect each item")
	                : statusText(completion.status);
	p->finishPublication();
	p->signal();
	if (p->finished)
		p->finished();
}

int DynamicBatch::count() const
{
	return int(d->model.rows().size());
}

int DynamicBatch::selection() const
{
	return d->selected;
}

QString DynamicBatch::status() const
{
	return d->status;
}

QString DynamicBatch::responseEndpoint() const
{
	return d->selected >= 0 && d->selected < count()
	           ? d->model.rows()[std::size_t(d->selected)].name
	           : QString{};
}

BatchListModel* DynamicBatch::model() const
{
	return &d->model;
}

ValueModel* DynamicBatch::responses() const
{
	return &d->responses;
}
} // namespace telemetry::ux
