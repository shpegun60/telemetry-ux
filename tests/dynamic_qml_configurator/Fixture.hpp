// Actual installed runtime client/backend/dispatcher, one fake monotonic owner clock. MIT.
#pragma once
#include <Telemetry/UX/DynamicConfiguratorProvider.hpp>
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <integration/encoded/Backend.hpp>
#include <integration/encoded/Dispatcher.hpp>
#include <QtCore/QCoreApplication>
#include <QtCore/QModelIndex>
#include <QtCore/QVariantMap>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace dynamic_qml_fixture {
namespace cl = telemetry::client;
namespace rt = telemetry::runtime;
namespace ins = telemetry::integration::inspector;
namespace en = telemetry::integration::encoded;
using Provider = telemetry::ux::DynamicConfiguratorProvider;
using Binding = telemetry::ux::DynamicConfiguratorBinding;
using Workbench = telemetry::ux::Workbench;
using ValueModel = telemetry::ux::ValueModel;
using EndpointModel = telemetry::ux::EndpointModel;
inline unsigned checks = 0, warnings = 0;

inline void check(bool value, int line) noexcept
{
	++checks;
	if (!value) {
		std::fprintf(stderr, "Dynamic QML provider check %u failed at line %d\n", checks, line);
		std::exit(1);
	}
}

#define CHECK(...) ::dynamic_qml_fixture::check(bool(__VA_ARGS__), __LINE__)

enum class Mode : std::int16_t {
	Reverse = -7,
	Forward = 12
};

struct Config {
	std::uint64_t serial = UINT64_MAX;
	std::int64_t offset = INT64_MIN;
	float gain = 1.25f;
	double precise = -0.0;
	std::uint32_t limit = 1;
	Mode mode = static_cast<Mode>(-123);
	std::array<std::uint16_t, 300> samples{};
};

struct Request {
	std::uint32_t multiplier = 0;
};

struct Empty {};

struct Device {
	Config config{};
	std::uint32_t reads = 0, writes = 0, commands = 0, services = 0;

	Config read() noexcept
	{
		++reads;
		return config;
	}

	std::uint64_t serial() noexcept
	{
		++reads;
		return config.serial;
	}

	telemetry::WriteResult write(const Config& value) noexcept
	{
		++writes;
		config = value;
		config.limit = std::min(config.limit, std::uint32_t{100});
		return telemetry::WriteResult::Applied;
	}

	telemetry::CommandResult reset() noexcept
	{
		++commands;
		return telemetry::CommandResult::Executed;
	}

	telemetry::CommandResult accept() noexcept
	{
		++commands;
		return telemetry::CommandResult::Accepted;
	}

	telemetry::ServiceResult<Config> inspect(const Request& request) noexcept
	{
		++services;
		if (!request.multiplier)
			return telemetry::ServiceResult<Config>::failure(
			    telemetry::ServiceStatus::InvalidArgument);
		return telemetry::ServiceResult<Config>::successFrom([this]() noexcept {
			return config;
		});
	}

	telemetry::ServiceResult<void> ping() noexcept
	{
		++services;
		return telemetry::ServiceResult<void>::success();
	}

	telemetry::ServiceResult<Empty> empty() noexcept
	{
		++services;
		return telemetry::ServiceResult<Empty>::success({});
	}
};

inline Device device;
inline constexpr telemetry::FieldTable localFields{
    telemetry::field<&Device::read, &Device::write>("Config", device),
    telemetry::field<&Device::serial>("Serial", device)};
inline constexpr telemetry::CommandTable localCommands{
    telemetry::command<&Device::reset>("Reset", device),
    telemetry::command<&Device::accept>("Accept", device)};
inline constexpr telemetry::ServiceTable localServices{
    telemetry::service<&Device::inspect>("Inspect", device),
    telemetry::service<&Device::ping>("Ping", device),
    telemetry::service<&Device::empty>("Empty", device)};
inline constexpr telemetry::FieldCatalogTable fields{telemetry::group("device", localFields)};
inline constexpr telemetry::CommandCatalogTable commands{telemetry::group("device", localCommands)};
inline constexpr telemetry::ServiceCatalogTable services{telemetry::group("device", localServices)};
inline constexpr telemetry::Model model{fields, commands, services};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();

inline void events()
{
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QCoreApplication::processEvents();
}

struct Backing {
	std::uint32_t now = 0;
	std::uint32_t generation = 17;
	cl::Clock clock{this, +[](void* self) noexcept {
		                return static_cast<Backing*>(self)->now;
	                }};
	std::array<std::byte, descriptorBytes.size()> bytes = descriptorBytes;
	std::array<rt::RuntimeType, 64> types{};
	std::array<rt::RuntimeMember, 128> members{};
	std::array<rt::RuntimeEnumEntry, 16> enums{};
	std::array<rt::RuntimeCatalog, 8> catalogs{};
	std::array<rt::RuntimeEndpoint, 32> endpoints{};
	rt::RuntimeSchemaWorkspace metadata{types, members, enums, catalogs, endpoints};
	rt::RuntimeSchema schema;
	std::array<std::byte, 65536> tx{}, rx{}, port{}, reply{}, scratch{}, serviceResponse{};
	telemetry::Workspace workspace{scratch};
	en::EncodedBackend backend{tx, rx, clock};
	Workbench owner{backend, clock, serviceResponse};
	std::vector<std::uint32_t> requestIds;
	unsigned packets = 0;

	explicit Backing(std::uint32_t session = 17) : generation(session)
	{
		const auto parsed = rt::RuntimeSchema::parse(bytes, metadata);
		CHECK(parsed);
		schema = parsed.schema;
		CHECK(owner.admit(schema, cl::Input{bytes}.first(64), session) == cl::Status::Ok);
	}

	~Backing()
	{
		owner.close();
		owner.process(now);
		CHECK(owner.drained());
	}

	cl::Packet offered()
	{
		owner.process(now);
		auto packet = owner.nextPacket();
		CHECK(packet && packet.bytes.size() <= port.size());
		std::copy(packet.bytes.begin(), packet.bytes.end(), port.begin());
		packet.bytes = cl::Input{port}.first(packet.bytes.size());
		return packet;
	}

	std::size_t execute(cl::Packet packet, bool deliver = true)
	{
		CHECK(owner.prepareHandoff(packet.session, packet.requestId));
		CHECK(owner.onSendAccepted(packet.session, packet.requestId));
		const auto result =
		    en::dispatchEncoded(model.view(), workspace, packet.session, packet.bytes, reply);
		CHECK(result);
		requestIds.push_back(packet.requestId);
		++packets;
		if (deliver)
			CHECK(owner.onReply(cl::Input{reply}.first(result.written), packet.session,
			                    packet.requestId));
		owner.process(now);
		return result.written;
	}

	void pump(Binding& binding)
	{
		for (unsigned i = 0; i < 64 && owner.pending(); ++i) {
			execute(offered());
			binding.poll();
			events();
		}
		binding.poll();
		events();
		CHECK(owner.pending() == 0);
	}
};

struct Harness {
	std::shared_ptr<Backing> storage = std::make_shared<Backing>();
	std::unique_ptr<Binding> binding = std::make_unique<Binding>(storage->owner, storage);

	Harness()
	{
		device = {};
		binding->setMutationsAllowed(true);
	}

	~Harness()
	{
		binding->retire();
		storage->owner.close();
		storage->owner.process(storage->now);
		binding->poll();
		CHECK(binding->drained());
	}

	void pump()
	{
		storage->pump(*binding);
	}
};

inline QModelIndex endpoint(Provider& provider, const char* name, const QModelIndex& parent = {})
{
	auto* model = provider.endpoints();
	for (int row = 0; row < model->rowCount(parent); ++row) {
		const auto index = model->index(row, 0, parent);
		if (model->data(index, EndpointModel::IsEndpointRole).toBool() &&
		    model->data(index, EndpointModel::NameRole).toString() == QLatin1String(name))
			return index;
		if (const auto nested = endpoint(provider, name, index); nested.isValid())
			return nested;
	}
	return {};
}

inline void select(Provider& provider, const char* name)
{
	const auto index = endpoint(provider, name);
	CHECK(index.isValid());
	CHECK(provider.selectEndpoint(index));
}

inline QModelIndex member(QAbstractItemModel* model, const char* name)
{
	const auto root = model->index(0, 0);
	if (model->canFetchMore(root))
		model->fetchMore(root);
	for (int row = 0; row < model->rowCount(root); ++row) {
		const auto index = model->index(row, 0, root);
		if (model->data(index, ValueModel::NameRole).toString() == QLatin1String(name))
			return index;
	}
	return {};
}

inline QString scalar(QAbstractItemModel* model, const char* name)
{
	const auto index = member(model, name);
	CHECK(index.isValid());
	return model->data(index, ValueModel::ValueRole).toString();
}

inline bool edit(Provider& provider, const char* name, const char* text)
{
	const auto index = member(provider.values(), name);
	CHECK(index.isValid());
	return provider.setScalarText(index, QString::fromLatin1(text));
}
} // namespace dynamic_qml_fixture
