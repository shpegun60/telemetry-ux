// One session-owned view registry with automatic retirement and rebinding. MIT.
#include "QtConnectionBinding.hpp"
#include "DynamicWatchlist.hpp"
#include "DynamicBatch.hpp"
#include <QtCore/QThread>
#include <algorithm>
#include <memory>
#include <vector>

namespace telemetry::ux {
namespace {
QString batchValueStatus(integration::batch::ValueStatus value)
{
	using Status = integration::batch::ValueStatus;
	switch (value) {
		case Status::Ok:
			return QStringLiteral("Ok");
		case Status::Stale:
			return QStringLiteral("Stale");
		case Status::NoValue:
			return QStringLiteral("NoValue");
		case Status::InvalidPayload:
			return QStringLiteral("InvalidPayload");
		case Status::TypeMismatch:
			return QStringLiteral("TypeMismatch");
	}
	return QStringLiteral("InvalidPayload");
}

BatchSource batchSource(integration::qt::ConnectionSession* value)
{
	using Session = integration::qt::ConnectionSession;
	namespace bt = integration::batch;
	const QPointer<Session> session(value);
	const auto storage = value ? value->storage() : nullptr;
	BatchSource source;
	source.storage = storage;
	source.current = [session, storage] {
		return session && storage && session->ready() && session->storage() == storage &&
		       storage->inspector.isCurrent();
	};
	const auto current = source.current;
	source.busy = [session] {
		return !session || session->busy();
	};
	source.session = [storage, current] {
		return current() ? storage->inspector.session() : 0;
	};
	source.schema = [storage, current] {
		return current() ? storage->schema : runtime::RuntimeSchema{};
	};
	source.capability = [session, current]() -> std::optional<BatchSource::Limits> {
		const auto cap = current() ? session->batchCapability() : std::nullopt;
		if (!cap)
			return {};
		return BatchSource::Limits{cap->itemLimit, cap->requestLimit, cap->replyLimit};
	};
	source.submit = [session, current](std::span<const BatchSource::Request> items,
	                                   BatchSource::Completion done) -> client::Submission {
		if (!current())
			return {client::Status::NotAdmitted, {}};
		if (items.size() > 32)
			return {client::Status::InvalidArgument, {}};
		std::array<Session::BatchRequest, 32> requests{};
		for (std::size_t i = 0; i < items.size(); ++i)
			requests[i] = {items[i].operation, items[i].id, items[i].payload};
		const auto origin = session->storage()->inspector.session();
		return session->batch(
		    std::span{requests}.first(items.size()),
		    [done = std::move(done), origin](const bt::BatchResult& result) noexcept {
			    BatchCompletion out;
			    out.status = result.operation.status;
			    out.succeeded = result.operation.succeeded();
			    out.valueStatus = batchValueStatus(result.valueStatus);
			    out.count = result.count <= out.rows.size() ? int(result.count) : 0;
			    for (int i = 0; i < out.count; ++i) {
				    const auto& item = result.items[std::size_t(i)];
				    auto& row = out.rows[std::size_t(i)];
				    row.id = item.id;
				    row.operation = item.operation.operation;
				    const auto valueStatus = batchValueStatus(item.valueStatus);
				    row.result = {{QStringLiteral("status"), unsigned(item.operation.status)},
				                  {QStringLiteral("dispatch"), item.operation.dispatch},
				                  {QStringLiteral("endpointStatus"), item.operation.endpointStatus},
				                  {QStringLiteral("succeeded"), item.operation.succeeded()},
				                  {QStringLiteral("accepted"), item.operation.accepted()},
				                  {QStringLiteral("executed"), item.operation.executed()},
				                  {QStringLiteral("session"), origin},
				                  {QStringLiteral("valueStatus"), valueStatus}};
				    row.hasValue = item.hasValue();
				    if (row.hasValue)
					    row.payload =
					        QByteArray(reinterpret_cast<const char*>(item.operation.payload.data()),
					                   qsizetype(item.operation.payload.size()));
			    }
			    *done = std::move(out);
		    });
	};
	return source;
}

WatchValueType watchType(const integration::qt::ConnectionStorage& storage, quint32 id)
{
	const auto binding = storage.inspector.bindField(id);
	const auto* type = binding ? storage.schema.type(binding.endpoint.responseType()) : nullptr;
	if (!type || (type->kind != TypeKind::Scalar && type->kind != TypeKind::Enum) ||
	    type->wireBytes == 0 || type->wireBytes > 8) return {};
	const auto* scalar = type->kind == TypeKind::Enum ? storage.schema.type(type->relatedTypeId) : type;
	if (!scalar || scalar->kind != TypeKind::Scalar) return {};
	WatchScalar code = WatchScalar::Unknown;
	switch (scalar->scalarCode) {
		case ScalarCode::Bool: code = WatchScalar::Boolean; break;
		case ScalarCode::U8: code = WatchScalar::U8; break;
		case ScalarCode::S8: code = WatchScalar::S8; break;
		case ScalarCode::U16: code = WatchScalar::U16; break;
		case ScalarCode::S16: code = WatchScalar::S16; break;
		case ScalarCode::U32: code = WatchScalar::U32; break;
		case ScalarCode::S32: code = WatchScalar::S32; break;
		case ScalarCode::U64: code = WatchScalar::U64; break;
		case ScalarCode::S64: code = WatchScalar::S64; break;
		case ScalarCode::F32: code = WatchScalar::F32; break;
		case ScalarCode::F64: code = WatchScalar::F64; break;
	}
	WatchValueType result{type->id, code, quint8(type->wireBytes), type->kind == TypeKind::Enum};
	return result.valid() ? result : WatchValueType{};
}
WatchNativeStatus watchStatus(const client::ResultView& result, int valueStatus)
{
	return {qint32(result.status), result.dispatch, result.endpointStatus, result.codec, valueStatus};
}
void copyWatchValue(WatchValueBits& into, const runtime::DynamicValueView& value)
{
	const auto bytes = value.bytes();
	if (bytes) (void)into.assign(bytes.value.data(), bytes.value.size());
}

WatchlistSource watchSource(integration::qt::ConnectionSession* value)
{
	using Session = integration::qt::ConnectionSession;
	namespace rm = integration::read_many;
	const QPointer<Session> session(value);
	const auto storage = value ? value->storage() : nullptr;
	WatchlistSource source;
	source.storage = storage;
	source.current = [session, storage] {
		return session && storage && session->ready() && session->storage() == storage &&
		       storage->inspector.isCurrent();
	};
	const auto current = source.current;
	source.busy = [session] {
		return !session || session->busy();
	};
	source.capability = [session, current]() -> std::optional<WatchlistSource::Limits> {
		const auto capability = current() ? session->readManyCapability() : std::nullopt;
		if (!capability)
			return {};
		return WatchlistSource::Limits{capability->itemLimit, capability->requestLimit,
		                               capability->replyLimit};
	};
	source.fieldBytes = [storage, current](quint32 id) -> std::optional<unsigned> {
		if (!current())
			return {};
		const auto binding = storage->inspector.bindField(id);
		const auto* type =
		    binding ? storage->schema.type(binding.endpoint.responseType()) : nullptr;
		if (!type || (type->kind != TypeKind::Scalar && type->kind != TypeKind::Enum) ||
		    type->wireBytes == 0 || type->wireBytes > 8)
			return {};
		return type->wireBytes;
	};
	// The same rule as fieldBytes, over every Field in one pass, for the Device
	// tree's watch boxes: binding each Field by id would search the schema
	// once per Field.
	source.holdable = [storage, current]() -> QSet<quint32> {
		QSet<quint32> ids;
		if (!current())
			return ids;
		for (const auto& endpoint : storage->schema.endpoints(telemetry::descriptor::Category::Field)) {
			// A Field read answers with the Field's own type (DynamicClient::contract).
			const auto* type = storage->schema.type(endpoint.typeId);
			if (type && (type->kind == TypeKind::Scalar || type->kind == TypeKind::Enum) &&
			    type->wireBytes != 0 && type->wireBytes <= 8)
				ids.insert(endpoint.id);
		}
		return ids;
	};
	source.readMany = [session, storage, current](std::span<const std::uint32_t> ids,
	                                     WatchlistSource::Completion done) -> client::Submission {
		if (!current())
			return {client::Status::NotAdmitted, {}};
		if (ids.empty() || ids.size() > 32) return {client::Status::InvalidArgument, {}};
		auto frozen = std::make_shared<std::array<WatchlistCompletion::Row, 32>>();
		for (std::size_t i = 0; i < ids.size(); ++i) {
			(*frozen)[i].id = ids[i];
			(*frozen)[i].value.type = watchType(*storage, ids[i]);
			if (!(*frozen)[i].value.type.valid()) return {client::Status::NotAdmitted, {}};
		}
		const WatchSourceSnapshot origin{storage->inspector.session(), storage->schema.fingerprint(), storage->schema.generation()};
		return session->readMany(
		    ids, [done = std::move(done), frozen = std::move(frozen), origin](const rm::ReadManyResult& result) noexcept {
			    WatchlistCompletion out;
			    out.rows = *frozen;
			    out.source = origin;
			    out.native = watchStatus(result.operation, int(result.valueStatus));
			    out.completed = WatchTimestamp::now();
			    out.status = result.operation.status;
			    if (out.status == client::Status::Ok &&
			        (!result.operation.succeeded() || result.valueStatus != rm::ValueStatus::Ok))
				    out.status = client::Status::CodecError;
			    out.count = result.count;
			    if (out.count > int(out.rows.size())) {
				    out.status = client::Status::CodecError;
				    out.count = 0;
			    }
			    for (int i = 0; i < out.count; ++i) {
				    const auto& item = result.items[i];
				    auto& row = out.rows[i];
				    row.id = item.id;
				    row.itemStatus = qint32(item.status);
				    row.valueStatus = qint32(item.valueStatus);
				    switch (item.status) {
					    case rm::wire::ItemStatus::Ok:
						    row.status =
						        item.hasValue() ? QStringLiteral("Ok") : QStringLiteral("Stale");
						    break;
					    case rm::wire::ItemStatus::NotFound:
						    row.status = QStringLiteral("NotFound");
						    break;
					    case rm::wire::ItemStatus::UnsupportedType:
						    row.status = QStringLiteral("UnsupportedType");
						    break;
					    case rm::wire::ItemStatus::Unavailable:
						    row.status = QStringLiteral("Unavailable");
						    break;
					    case rm::wire::ItemStatus::ReadError:
						    row.status = QStringLiteral("ReadError");
						    break;
				    }
				    if (item.hasValue()) {
					    copyWatchValue(row.value, item.value);
					    row.text = WatchlistModel::formatValue(item.value);
					    row.sample = WatchlistModel::sampleOf(item.value);
				    }
			    }
			    *done = std::move(out);
		    });
	};
	source.readField = [session, storage, current](quint32 id,
	                                      WatchlistSource::Completion done) -> client::Submission {
		if (!current())
			return {client::Status::NotAdmitted, {}};
		const auto type = watchType(*storage, id);
		if (!type.valid()) return {client::Status::NotAdmitted, {}};
		const WatchSourceSnapshot origin{storage->inspector.session(), storage->schema.fingerprint(), storage->schema.generation()};
		return session->readFieldValue(
		    id, [id, type, origin, done = std::move(done)](
		            const integration::dynamic::DynamicResult& result) noexcept {
			    WatchlistCompletion out;
			    out.source = origin;
			    out.native = watchStatus(result.operation, int(result.valueStatus));
			    out.completed = WatchTimestamp::now();
			    out.rows[0].value.type = type;
			    out.rows[0].itemStatus = result.operation.endpointStatus;
			    out.rows[0].valueStatus = qint32(result.valueStatus);
			    out.status = result.operation.status;
			    out.count = 1;
			    out.rows[0].id = id;
			    out.rows[0].status =
			        result.hasValue() ? QStringLiteral("Ok")
			                          : QStringLiteral("Field refused (dispatch %1, endpoint %2)")
			                                .arg(result.operation.dispatch)
			                                .arg(result.operation.endpointStatus);
			    if (result.operation.succeeded() && !result.hasValue())
				    out.status = client::Status::CodecError;
			    if (result.hasValue()) {
				    copyWatchValue(out.rows[0].value, result.value);
				    out.rows[0].text = WatchlistModel::formatValue(result.value);
				    out.rows[0].sample = WatchlistModel::sampleOf(result.value);
			    }
			    *done = std::move(out);
		    });
	};
	return source;
}
} // namespace

class ConnectionViews final : public QObject {
public:
	QPointer<integration::qt::ConnectionSession> session;
	std::unique_ptr<DynamicConfiguratorBinding> current;
	std::vector<std::unique_ptr<DynamicConfiguratorBinding>> retired;
	std::vector<QPointer<DynamicConfiguratorProvider>> providers;
	// Each progress starts with the next provider, so Watchlists on one
	// session take turns instead of the first one always going first.
	std::size_t turn = 0;
	std::vector<QPointer<QtConnectionBinding>> adapters;
	bool allowed = false, publishing = false;

	explicit ConnectionViews(integration::qt::ConnectionSession& owner)
	    : QObject(&owner), session(&owner)
	{
		QObject::connect(&owner, &integration::qt::ConnectionSession::aboutToRetire, this, [this] {
			retire();
		});
		QObject::connect(&owner, &integration::qt::ConnectionSession::progressed, this, [this] {
			poll();
		});
		QObject::connect(&owner, &integration::qt::ConnectionSession::dataInvalidated, this,
		                 [this] {
			                 if (current)
				                 current->invalidateData();
		                 });
		QObject::connect(&owner, &integration::qt::ConnectionSession::admissionChanged, this,
		                 [this] {
			                 admit();
		                 });
		QObject::connect(&owner, &QObject::destroyed, this, [this] {
			session.clear();
			retire();
			poll();
		});
		admit();
	}

	~ConnectionViews() override
	{
		retire();
		poll();
		// Session destruction closes/drains its native owner before destroying
		// QObject children. Accepted view backing therefore remains stable.
		for (const auto& binding : retired)
			if (!binding->drained())
				qFatal("Destroy the Qt connection outside its native callback stack");
	}

	void retire()
	{
		if (!current)
			return;
		current->retire();
		retired.push_back(std::move(current));
		const auto liveProviders = providers;
		const QPointer<ConnectionViews> alive(this);
		for (const auto& provider : liveProviders) {
			if (provider)
				provider->setBinding(nullptr);
			if (!alive)
				return;
		}
	}

	void poll()
	{
		if (publishing)
			return;
		publishing = true;
		const QPointer<ConnectionViews> alive(this);
		for (std::size_t i = 0; i < retired.size(); ++i) {
			retired[i]->poll();
			if (!alive)
				return;
		}
		if (current)
			current->poll();
		if (!alive)
			return;
		const auto watchers = providers;
		const auto first = watchers.empty() ? 0 : turn++ % watchers.size();
		for (std::size_t k = 0; k < watchers.size(); ++k) {
			const auto& provider = watchers[(first + k) % watchers.size()];
			if (provider) {
				provider->pollWatch();
				if (provider)
					provider->pollBatch();
			}
			if (!alive)
				return;
		}
		std::erase_if(retired, [](const auto& binding) {
			return binding->drained();
		});
		std::erase_if(providers, [](const auto& value) {
			return value.isNull();
		});
		std::erase_if(adapters, [](const auto& value) {
			return value.isNull();
		});
		publishing = false;
	}

	void admit()
	{
		if (!session || !session->ready()) {
			retire();
			poll();
			return;
		}
		if (current)
			return;
		const auto owner = session->storage();
		if (!owner)
			return;
		current = std::make_unique<DynamicConfiguratorBinding>(
		    owner->inspector, owner, nullptr,
		    QStringLiteral("0x%1").arg(owner->schema.fingerprint(), 16, 16, QLatin1Char('0')));
		current->setMutationsAllowed(allowed);
		QObject::connect(current.get(), &DynamicConfiguratorBinding::operationFinished, this,
		                 [this](const QVariantMap& facts) {
			                 const auto observers = adapters;
			                 for (const auto& adapter : observers)
				                 if (adapter)
					                 Q_EMIT adapter->operationFinished(facts);
		                 });
		const auto liveProviders = providers;
		const QPointer<ConnectionViews> alive(this);
		for (const auto& provider : liveProviders) {
			if (provider) {
				provider->setBinding(current.get());
				if (provider)
					provider->setWatchSource(watchSource(session));
				if (provider)
					provider->setBatchSource(batchSource(session));
			}
			if (!alive)
				return;
		}
	}
};

QtConnectionBinding::QtConnectionBinding(integration::qt::ConnectionSession* session,
                                         QObject* parent)
    : QObject(parent), session_(session)
{
	if (!session)
		return;
	Q_ASSERT(session->thread() == thread());
	ConnectionViews* registry = nullptr;
	for (auto* child : session->children())
		if (auto* existing = dynamic_cast<ConnectionViews*>(child)) {
			registry = existing;
			break;
		}
	if (!registry)
		registry = new ConnectionViews(*session);
	views_ = registry;
	registry->adapters.emplace_back(this);
	QObject::connect(session, &integration::qt::ConnectionSession::stateChanged, this,
	                 &QtConnectionBinding::stateChanged);
	QObject::connect(session, &QObject::destroyed, this, [this] {
		Q_EMIT stateChanged();
	});
}

QtConnectionBinding::~QtConnectionBinding() = default;

DynamicConfiguratorProvider* QtConnectionBinding::createProvider(QObject* parent)
{
	Q_ASSERT(thread() == QThread::currentThread());
	auto* registry = dynamic_cast<ConnectionViews*>(views_.data());
	auto* provider = new DynamicConfiguratorProvider(registry ? registry->current.get() : nullptr,
	                                                 parent ? parent : this);
	if (registry)
		registry->providers.emplace_back(provider);
	provider->setWatchSource(watchSource(session_));
	provider->setBatchSource(batchSource(session_));
	providers_.removeIf([](const auto& value) {
		return value.isNull();
	});
	providers_.append(provider);
	const QPointer<DynamicConfiguratorProvider> alive(provider);
	provider->setPresentationOverlay(presentation_);
	return alive.data();
}

bool QtConnectionBinding::setPresentation(const QByteArray& json)
{
	Q_ASSERT(thread() == QThread::currentThread());
	if (presentationChanging_)
		return false;
	providers_.removeIf([](const auto& value) {
		return value.isNull();
	});
	for (const auto& provider : providers_)
		if (provider && !provider->canSetPresentation())
			return false;
	auto overlay = presentation_;
	if (!overlay.replace(json, presentationError_)) {
		Q_EMIT stateChanged();
		return false;
	}
	presentation_ = overlay;
	presentationChanging_ = true;
	const auto providers = providers_;
	const QPointer<QtConnectionBinding> alive(this);
	for (const auto& provider : providers) {
		if (provider)
			provider->setPresentationOverlay(overlay);
		if (!alive)
			return true;
	}
	presentationChanging_ = false;
	Q_EMIT stateChanged();
	return true;
}

QString QtConnectionBinding::presentationError() const
{
	return presentationError_;
}

bool QtConnectionBinding::connected() const
{
	return session_ && session_->ready();
}

QString QtConnectionBinding::lastError() const
{
	return session_ ? session_->lastError() : QStringLiteral("Connection is closed");
}

bool QtConnectionBinding::mutationsAllowed() const
{
	const auto* registry = dynamic_cast<const ConnectionViews*>(views_.data());
	return connected() && registry && registry->allowed;
}

void QtConnectionBinding::setMutationsAllowed(bool allowed)
{
	auto* registry = dynamic_cast<ConnectionViews*>(views_.data());
	if (!registry || registry->allowed == allowed)
		return;
	registry->allowed = allowed;
	const QPointer<ConnectionViews> alive(registry);
	if (registry->current)
		registry->current->setMutationsAllowed(allowed);
	if (!alive)
		return;
	const auto observers = registry->adapters;
	for (const auto& adapter : observers)
		if (adapter)
			Q_EMIT adapter->stateChanged();
}
} // namespace telemetry::ux
