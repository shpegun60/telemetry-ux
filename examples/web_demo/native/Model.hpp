/* Persistent application Model for the browser starter; no transport or UI dependencies. MIT. */
#ifndef TELEMETRY_UX_EXAMPLES_WEB_DEMO_NATIVE_MODEL_HPP
#define TELEMETRY_UX_EXAMPLES_WEB_DEMO_NATIVE_MODEL_HPP
#pragma once
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>

namespace web_demo {
enum class Mode : std::int16_t {
	Reverse = -7,
	Forward = 12
};

struct Empty {};

struct Nested {
	std::array<std::array<std::int16_t, 2>, 2> matrix;
	bool flag;
};

struct Config {
	bool enabled;
	std::uint8_t u8;
	std::int8_t s8;
	std::uint16_t u16;
	std::int16_t s16;
	std::uint32_t u32;
	std::int32_t s32;
	std::uint64_t large;
	std::int64_t negative;
	float f32;
	double gain;
	double zero;
	Mode mode;
	Nested nested;
};

struct Counters {
	std::uint32_t reads, writes, commands, services;
};

inline Config initial() noexcept
{
	return {
	    true,
	    UINT8_MAX,
	    INT8_MIN,
	    UINT16_MAX,
	    INT16_MIN,
	    UINT32_MAX,
	    INT32_MIN,
	    UINT64_MAX,
	    INT64_MIN,
	    1.25f,
	    4.25,
	    std::bit_cast<double>(UINT64_C(0x8000000000000000)),
	    static_cast<Mode>(-123),
	    {std::array{std::array<std::int16_t, 2>{1, -2}, std::array<std::int16_t, 2>{3, -4}}, true}};
}

struct Device {
	Config current = initial();
	Counters counters{};

	Config read() noexcept
	{
		++counters.reads;
		return current;
	}

	Counters readCounters() noexcept
	{
		++counters.reads;
		return counters;
	}

	std::uint64_t wide() noexcept { ++counters.reads; return current.large; }
	std::int64_t signedWide() noexcept { ++counters.reads; return current.negative; }
	Mode mode() noexcept { ++counters.reads; return current.mode; }

	void set(const Config& value) noexcept
	{
		current = value;
		if (current.gain > 10)
			current.gain = 10;
	}

	telemetry::WriteResult write(const Config& value) noexcept
	{
		++counters.writes;
		set(value);
		return telemetry::WriteResult::Applied;
	}

	telemetry::CommandResult setConfig(const Config& value) noexcept
	{
		++counters.commands;
		set(value);
		return telemetry::CommandResult::Executed;
	}

	telemetry::CommandResult accepted() noexcept
	{
		++counters.commands;
		return telemetry::CommandResult::Accepted;
	}

	telemetry::CommandResult reset() noexcept
	{
		++counters.commands;
		current = initial();
		return telemetry::CommandResult::Executed;
	}

	telemetry::ServiceResult<Config> echo(const Config& value) noexcept
	{
		++counters.services;
		if (value.gain < 0)
			return telemetry::ServiceResult<Config>::failure(
			    telemetry::ServiceStatus::InvalidArgument);
		return telemetry::ServiceResult<Config>::success(value);
	}

	telemetry::ServiceResult<Config> inspect() noexcept
	{
		++counters.services;
		return telemetry::ServiceResult<Config>::success(current);
	}

	telemetry::ServiceResult<void> ping() noexcept
	{
		++counters.services;
		return telemetry::ServiceResult<void>::success();
	}

	telemetry::ServiceResult<Empty> empty() noexcept
	{
		++counters.services;
		return telemetry::ServiceResult<Empty>::success({});
	}
};

inline Device device{};
inline telemetry::OwnerSlot<Device> unavailableDevice{};

inline constexpr telemetry::FieldTable controlFields{
    telemetry::field<&Device::read, &Device::write>("Config", device),
    telemetry::field<&Device::read>("ReadOnly", device)};
inline constexpr telemetry::FieldTable diagnosticFields{
    telemetry::field<&Device::readCounters>("Counters", device),
    telemetry::field<&Device::wide>("Wide", device),
    telemetry::field<&Device::signedWide>("Signed", device),
    telemetry::field<&Device::mode>("Mode", device),
    telemetry::field<&Device::wide>("Unavailable", unavailableDevice)};
inline constexpr telemetry::CommandTable commands{
    telemetry::command<&Device::setConfig>("SetConfig", device),
    telemetry::command<&Device::accepted>("Accepted", device),
    telemetry::command<&Device::reset>("Reset", device)};
inline constexpr telemetry::ServiceTable services{
    telemetry::service<&Device::echo>("Echo", device),
    telemetry::service<&Device::inspect>("Inspect", device),
    telemetry::service<&Device::ping>("Ping", device),
    telemetry::service<&Device::empty>("Empty", device)};
inline constexpr telemetry::FieldCatalogTable fieldCatalogs{
    telemetry::group("control", controlFields), telemetry::group("diagnostics", diagnosticFields)};
inline constexpr telemetry::CommandCatalogTable commandCatalogs{
    telemetry::group("control", commands)};
inline constexpr telemetry::ServiceCatalogTable serviceCatalogs{
    telemetry::group("control", services)};
inline constexpr telemetry::Model model{fieldCatalogs, commandCatalogs, serviceCatalogs};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
static_assert(descriptor.valid());
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();
} // namespace web_demo
#endif // TELEMETRY_UX_EXAMPLES_WEB_DEMO_NATIVE_MODEL_HPP
