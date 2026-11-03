/*
 * @file DemoPeer.cpp
 * @brief Real native Model, Descriptor and Encoded dispatcher for the Qt demo.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "DemoPeer.hpp"

#include <integration/encoded/Dispatcher.hpp>
#include <integration/tcp/Server.hpp>
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>

#include <array>
#include <bit>
#include <algorithm>
#include <cmath>
#include <limits>

namespace inspector_qml::demo {

struct Voltage {
	float Ua, Ub, Uc;
};

enum class Mode : std::int16_t {
	Idle = -7,
	Active = 12
};

struct Empty {};

struct Nested {
	std::array<std::array<std::int32_t, 3>, 2> Matrix;
	std::array<Voltage, 2> Voltages;
	bool Enabled;
};

struct Calibration {
	float reference;
};

struct Identity {
	std::uint32_t version, serial;
};

struct Config {
	float Gain = 1.0f;
	std::uint64_t Counter = std::numeric_limits<std::uint64_t>::max();
	std::int64_t Offset = std::numeric_limits<std::int64_t>::min();
	bool Enabled = true;
	Mode OperatingMode = static_cast<Mode>(-2);
	std::array<std::array<std::int32_t, 2>, 2> Matrix{{{{1, -2}}, {{3, -4}}}};
	std::array<float, 6> FloatBits{
	    std::bit_cast<float>(UINT32_C(0x7fc12345)), std::bit_cast<float>(UINT32_C(0xffc54321)),
	    std::bit_cast<float>(UINT32_C(0x7f812345)), std::bit_cast<float>(UINT32_C(0xff854321)),
	    std::bit_cast<float>(UINT32_C(0x00000000)), std::bit_cast<float>(UINT32_C(0x80000000))};
	std::array<double, 6> DoubleBits{std::bit_cast<double>(UINT64_C(0x7ff8123456789abc)),
	                                 std::bit_cast<double>(UINT64_C(0xfff854321abcdef0)),
	                                 std::bit_cast<double>(UINT64_C(0x7ff0123456789abc)),
	                                 std::bit_cast<double>(UINT64_C(0xfff054321abcdef0)),
	                                 std::bit_cast<double>(UINT64_C(0x0000000000000000)),
	                                 std::bit_cast<double>(UINT64_C(0x8000000000000000))};
};

struct ServiceReply {
	float Gain;
	std::uint64_t Counter;
	std::int64_t Offset;
	bool Enabled;
	Mode OperatingMode;
	std::array<std::array<std::int32_t, 2>, 2> Matrix;
	std::array<std::uint32_t, 300> Samples;
};

struct WritableDevice {
	Config config{};
	Config lastCommand{};
	std::uint64_t writes = 0;
	std::uint64_t commands = 0, executed = 0, accepted = 0;
	std::uint64_t services = 0;
	bool rejectWrites = false;

	Config read() const noexcept
	{
		return config;
	}

	telemetry::WriteResult write(const Config& requested) noexcept
	{
		++writes;
		if (rejectWrites || !std::isfinite(requested.Gain))
			return telemetry::WriteResult::InvalidValue;
		config = requested;
		config.Gain = std::clamp(config.Gain, 0.0f, 10.0f);
		return telemetry::WriteResult::Applied;
	}

	telemetry::CommandResult execute(const Config& request) noexcept
	{
		++commands;
		lastCommand = request;
		++executed;
		return telemetry::CommandResult::Executed;
	}

	telemetry::CommandResult accept(const Config& request) noexcept
	{
		++commands;
		lastCommand = request;
		++accepted;
		// Acceptance is deliberately separate from execution. This demo does not
		// claim that an application job has subsequently completed.
		return telemetry::CommandResult::Accepted;
	}

	telemetry::CommandResult ping() noexcept
	{
		++commands;
		++executed;
		return telemetry::CommandResult::Executed;
	}

	telemetry::CommandResult emptyCommand(const Empty&) noexcept
	{
		return ping();
	}

	telemetry::ServiceResult<ServiceReply> transform(const Config& request) noexcept
	{
		++services;
		if (!std::isfinite(request.Gain) || request.Gain < 0)
			return telemetry::ServiceResult<ServiceReply>::failure(
			    telemetry::ServiceStatus::InvalidArgument);
		return telemetry::ServiceResult<ServiceReply>::successFrom(
		    [&request]() noexcept -> ServiceReply {
			    ServiceReply reply{std::clamp(request.Gain, 0.0f, 10.0f),
			                       request.Counter,
			                       request.Offset,
			                       request.Enabled,
			                       request.OperatingMode,
			                       request.Matrix,
			                       {}};
			    const auto seed = static_cast<std::uint32_t>(request.Counter & 0xffu);
			    for (std::size_t i = 0; i < reply.Samples.size(); ++i)
				    reply.Samples[i] = seed + static_cast<std::uint32_t>(i);
			    return reply;
		    });
	}

	telemetry::ServiceResult<ServiceReply> reject(const Config&) noexcept
	{
		++services;
		return telemetry::ServiceResult<ServiceReply>::failure(
		    telemetry::ServiceStatus::InvalidArgument);
	}

	telemetry::ServiceResult<void> pingService() noexcept
	{
		++services;
		return telemetry::ServiceResult<void>::success();
	}

	telemetry::ServiceResult<Empty> emptyService(const Empty&) noexcept
	{
		++services;
		return telemetry::ServiceResult<Empty>::success({});
	}
};

struct Device {
	Voltage voltage() const noexcept
	{
		return {230.1f, 229.8f, 231.2f};
	}

	std::uint64_t counter() const noexcept
	{
		return std::numeric_limits<std::uint64_t>::max();
	}

	std::int64_t offset() const noexcept
	{
		return std::numeric_limits<std::int64_t>::min();
	}

	Mode mode() const noexcept
	{
		return static_cast<Mode>(-2);
	}

	std::array<std::uint16_t, 16384> samples() const noexcept
	{
		std::array<std::uint16_t, 16384> values{};
		for (std::size_t i = 0; i < values.size(); ++i)
			values[i] = static_cast<std::uint16_t>(i);
		return values;
	}

	Empty empty() const noexcept
	{
		return {};
	}

	Nested nested() const noexcept
	{
		return {{{{1, -2, 3}, {4, 5, -6}}}, {{{1.25f, 2.5f, 3.75f}, {4.5f, 5.25f, 6.0f}}}, true};
	}

	telemetry::CommandResult calibrate(const Calibration&) const noexcept
	{
		return telemetry::CommandResult::Executed;
	}

	telemetry::ServiceResult<Identity> identity() const noexcept
	{
		return telemetry::ServiceResult<Identity>::success({1, 42});
	}
};

// Independent scalar Field IDs, rather than members of an aggregate. Values
// deliberately exercise exact wide integers, open enums and floating bits.
struct WatchDevice {
	template<std::size_t I>
	std::uint64_t wide() const noexcept
	{
		return UINT64_MAX - I;
	}

	template<std::size_t I>
	std::int64_t signedWide() const noexcept
	{
		return INT64_MIN + static_cast<std::int64_t>(I);
	}

	template<std::size_t I>
	bool flag() const noexcept
	{
		return I % 2 == 0;
	}

	template<std::size_t I>
	Mode mode() const noexcept
	{
		return static_cast<Mode>(-2 - int(I));
	}

	template<std::size_t I>
	float real() const noexcept
	{
		constexpr std::array<std::uint32_t, 4> bits{0x7fc12345u, 0x80000000u, 0x3fa00000u,
		                                            0xff800000u};
		return std::bit_cast<float>(bits[I]);
	}

	template<std::size_t I>
	double precise() const noexcept
	{
		constexpr std::array<std::uint64_t, 4> bits{
		    UINT64_C(0x7ff8123456789abc), UINT64_C(0x8000000000000000),
		    UINT64_C(0x4004000000000000), UINT64_C(0x7ff0000000000000)};
		return std::bit_cast<double>(bits[I]);
	}
};

} // namespace inspector_qml::demo

namespace telemetry::reflection {

template<>
struct EnumReflection<inspector_qml::demo::Mode> {
	inline static constexpr auto entries =
	    enumEntries(enumEntry(inspector_qml::demo::Mode::Idle, "Idle"),
	                enumEntry(inspector_qml::demo::Mode::Active, "Active"));
};

} // namespace telemetry::reflection

namespace inspector_qml {
namespace {

demo::Device device;
constexpr telemetry::FieldTable measureFields{
    telemetry::field<&demo::Device::voltage>("Voltage", device)};
constexpr telemetry::FieldTable deviceFields{
    telemetry::field<&demo::Device::counter>("Counter", device),
    telemetry::field<&demo::Device::offset>("Offset", device),
    telemetry::field<&demo::Device::mode>("Mode", device)};
constexpr telemetry::FieldTable dataFields{
    telemetry::field<&demo::Device::samples>("Samples", device),
    telemetry::field<&demo::Device::empty>("Empty", device),
    telemetry::field<&demo::Device::nested>("Nested", device)};
constexpr telemetry::FieldCatalogTable fields{telemetry::group("measure", measureFields),
                                              telemetry::group("device", deviceFields),
                                              telemetry::group("data", dataFields)};
constexpr telemetry::CommandTable controlCommands{
    telemetry::command<&demo::Device::calibrate>("Calibrate", device)};
constexpr telemetry::CommandCatalogTable commands{telemetry::group("control", controlCommands)};
constexpr telemetry::ServiceTable deviceServices{
    telemetry::service<&demo::Device::identity>("Identity", device)};
constexpr telemetry::ServiceCatalogTable services{telemetry::group("device", deviceServices)};
constexpr telemetry::Model model{fields, commands, services};
constexpr telemetry::descriptor::Descriptor nativeDescriptor{model};
constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<nativeDescriptor>();

demo::WatchDevice watchDevice;
#define WATCH_FIELD(method, name, I)                                                               \
	telemetry::field<&demo::WatchDevice::method<I>>(name "_" #I, watchDevice)
constexpr telemetry::FieldTable wideFields{
    WATCH_FIELD(wide, "U64", 0), WATCH_FIELD(wide, "U64", 1), WATCH_FIELD(wide, "U64", 2),
    WATCH_FIELD(wide, "U64", 3), WATCH_FIELD(wide, "U64", 4), WATCH_FIELD(wide, "U64", 5),
    WATCH_FIELD(wide, "U64", 6), WATCH_FIELD(wide, "U64", 7)};
constexpr telemetry::FieldTable signedFields{
    WATCH_FIELD(signedWide, "S64", 0), WATCH_FIELD(signedWide, "S64", 1),
    WATCH_FIELD(signedWide, "S64", 2), WATCH_FIELD(signedWide, "S64", 3),
    WATCH_FIELD(signedWide, "S64", 4), WATCH_FIELD(signedWide, "S64", 5),
    WATCH_FIELD(signedWide, "S64", 6), WATCH_FIELD(signedWide, "S64", 7)};
constexpr telemetry::FieldTable flagFields{
    WATCH_FIELD(flag, "Bool", 0), WATCH_FIELD(flag, "Bool", 1), WATCH_FIELD(flag, "Bool", 2),
    WATCH_FIELD(flag, "Bool", 3)};
constexpr telemetry::FieldTable enumFields{
    WATCH_FIELD(mode, "Enum", 0), WATCH_FIELD(mode, "Enum", 1), WATCH_FIELD(mode, "Enum", 2),
    WATCH_FIELD(mode, "Enum", 3)};
constexpr telemetry::FieldTable realFields{WATCH_FIELD(real, "F32", 0), WATCH_FIELD(real, "F32", 1),
                                           WATCH_FIELD(real, "F32", 2),
                                           WATCH_FIELD(real, "F32", 3)};
constexpr telemetry::FieldTable preciseFields{
    WATCH_FIELD(precise, "F64", 0), WATCH_FIELD(precise, "F64", 1), WATCH_FIELD(precise, "F64", 2),
    WATCH_FIELD(precise, "F64", 3)};
#undef WATCH_FIELD
constexpr telemetry::FieldCatalogTable watchFields{
    telemetry::group("measure", measureFields), telemetry::group("device", deviceFields),
    telemetry::group("data", dataFields),       telemetry::group("wide", wideFields),
    telemetry::group("signed", signedFields),   telemetry::group("flags", flagFields),
    telemetry::group("modes", enumFields),      telemetry::group("real", realFields),
    telemetry::group("precise", preciseFields)};
constexpr telemetry::Model watchModel{watchFields, commands, services};
constexpr telemetry::descriptor::Descriptor watchDescriptor{watchModel};
constexpr auto watchDescriptorBytes = telemetry::descriptor::packDescriptor<watchDescriptor>();

demo::WritableDevice writablePrototype;
constexpr telemetry::FieldTable writableFields{
    telemetry::field<&demo::WritableDevice::read, &demo::WritableDevice::write>("Config",
                                                                                writablePrototype)};
constexpr telemetry::FieldCatalogTable writableCatalogs{telemetry::group("device", writableFields)};
constexpr telemetry::CommandTable writableCommands{
    telemetry::command<&demo::WritableDevice::execute>("Execute", writablePrototype),
    telemetry::command<&demo::WritableDevice::accept>("Accept", writablePrototype),
    telemetry::command<&demo::WritableDevice::ping>("Ping", writablePrototype),
    telemetry::command<&demo::WritableDevice::emptyCommand>("Empty", writablePrototype)};
constexpr telemetry::CommandCatalogTable writableCommandCatalogs{
    telemetry::group("control", writableCommands)};
constexpr telemetry::ServiceTable writableServices{
    telemetry::service<&demo::WritableDevice::transform>("Transform", writablePrototype),
    telemetry::service<&demo::WritableDevice::reject>("Reject", writablePrototype),
    telemetry::service<&demo::WritableDevice::pingService>("Ping", writablePrototype),
    telemetry::service<&demo::WritableDevice::emptyService>("Empty", writablePrototype)};
constexpr telemetry::ServiceCatalogTable writableServiceCatalogs{
    telemetry::group("control", writableServices)};
constexpr telemetry::Model writablePrototypeModel{writableCatalogs, writableCommandCatalogs,
                                                  writableServiceCatalogs};
constexpr telemetry::descriptor::Descriptor writableDescriptor{writablePrototypeModel};
constexpr auto writableDescriptorBytes =
    telemetry::descriptor::packDescriptor<writableDescriptor>();

} // namespace

struct DemoPeer::Impl {
	bool writable = false;
	bool readMany = false;
	demo::WritableDevice device;
	decltype(writableFields) fields{
	    telemetry::field<&demo::WritableDevice::read, &demo::WritableDevice::write>("Config",
	                                                                                device)};
	decltype(writableCatalogs) catalogs{telemetry::group("device", fields)};
	decltype(writableCommands) commands{
	    telemetry::command<&demo::WritableDevice::execute>("Execute", device),
	    telemetry::command<&demo::WritableDevice::accept>("Accept", device),
	    telemetry::command<&demo::WritableDevice::ping>("Ping", device),
	    telemetry::command<&demo::WritableDevice::emptyCommand>("Empty", device)};
	decltype(writableCommandCatalogs) commandCatalogs{telemetry::group("control", commands)};
	decltype(writableServices) services{
	    telemetry::service<&demo::WritableDevice::transform>("Transform", device),
	    telemetry::service<&demo::WritableDevice::reject>("Reject", device),
	    telemetry::service<&demo::WritableDevice::pingService>("Ping", device),
	    telemetry::service<&demo::WritableDevice::emptyService>("Empty", device)};
	decltype(writableServiceCatalogs) serviceCatalogs{telemetry::group("control", services)};
	decltype(writablePrototypeModel) writableModel{catalogs, commandCatalogs, serviceCatalogs};
	alignas(std::max_align_t)
	    std::array<std::byte, std::max({model.maxScratch(), writablePrototypeModel.maxScratch(),
	                                    watchModel.maxScratch()})> scratch{};
	telemetry::Workspace workspace{scratch};
};

DemoPeer::DemoPeer(bool writable, bool readMany) : impl_(std::make_unique<Impl>())
{
	impl_->writable = writable;
	impl_->readMany = readMany && !writable;
}

DemoPeer::~DemoPeer() = default;

telemetry::client::Input DemoPeer::descriptor() const noexcept
{
	return impl_->writable   ? telemetry::client::Input{writableDescriptorBytes}
	       : impl_->readMany ? telemetry::client::Input{watchDescriptorBytes}
	                         : telemetry::client::Input{descriptorBytes};
}

bool DemoPeer::writable() const noexcept
{
	return impl_->writable;
}

bool DemoPeer::readManyEnabled() const noexcept
{
	return impl_->readMany;
}

std::uint64_t DemoPeer::writeCount() const noexcept
{
	return impl_->device.writes;
}

std::uint64_t DemoPeer::commandCount() const noexcept
{
	return impl_->device.commands;
}

std::uint64_t DemoPeer::executedCount() const noexcept
{
	return impl_->device.executed;
}

std::uint64_t DemoPeer::acceptedCount() const noexcept
{
	return impl_->device.accepted;
}

std::uint64_t DemoPeer::lastCommandCounter() const noexcept
{
	return impl_->device.lastCommand.Counter;
}

float DemoPeer::lastCommandGain() const noexcept
{
	return impl_->device.lastCommand.Gain;
}

std::uint64_t DemoPeer::serviceCount() const noexcept
{
	return impl_->device.services;
}

void DemoPeer::setRejectWrites(bool reject) noexcept
{
	impl_->device.rejectWrites = reject;
}

std::unique_ptr<telemetry::integration::tcp::Server>
DemoPeer::makeTcpServer(resource::FileSystemView files, telemetry::client::Output rx,
                        telemetry::client::Output tx)
{
	return std::make_unique<telemetry::integration::tcp::Server>(
	    impl_->writable   ? impl_->writableModel.view()
	    : impl_->readMany ? watchModel.view()
	                      : model.view(),
	    impl_->workspace, files, rx, tx);
}

std::size_t DemoPeer::dispatch(telemetry::client::Input request, telemetry::client::Output reply,
                               std::uint32_t session) noexcept
{
	if (impl_->readMany) {
		const telemetry::protocol::encoded::read_many::Capability capability{
		    session, watchDescriptor.fingerprint()};
		const auto result = telemetry::integration::encoded::dispatchEncoded(
		    watchModel.view(), impl_->workspace, session, request, reply, capability);
		return result ? result.written : 0;
	}
	const auto result = telemetry::integration::encoded::dispatchEncoded(
	    impl_->writable ? impl_->writableModel.view() : model.view(), impl_->workspace, session,
	    request, reply);
	return result ? result.written : 0;
}

} // namespace inspector_qml
