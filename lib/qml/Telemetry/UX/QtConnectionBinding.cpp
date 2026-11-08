// One session-owned view registry with automatic retirement and rebinding. MIT.
#include "QtConnectionBinding.hpp"
#include <QtCore/QThread>
#include <algorithm>
#include <vector>

namespace telemetry::ux {
class ConnectionViews final : public QObject {
public:
	QPointer<integration::qt::ConnectionSession> session;
	std::unique_ptr<DynamicConfiguratorBinding> current;
	std::vector<std::unique_ptr<DynamicConfiguratorBinding>> retired;
	std::vector<QPointer<DynamicConfiguratorProvider>> providers;
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
			if (provider)
				provider->setBinding(current.get());
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
