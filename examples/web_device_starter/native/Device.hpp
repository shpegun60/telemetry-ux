// Edit this application Model; the browser discovers it without a generated UI. MIT.
#pragma once
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <array>
#include <cstdint>

namespace device_starter {
struct Config {
	bool enabled = true;
	std::uint32_t periodMs = 1000;
	float gain = 1;
};

struct ScaleRequest {
	float input;
};

struct ScaleResponse {
	float output;
};

struct Device {
	Config config{};
	std::uint32_t reads = 0, writes = 0, commands = 0, services = 0;
	std::uint64_t serialNumber() const noexcept { return UINT64_C(9007199254740993); }
	Config readConfig() noexcept { ++reads; return config; }
	telemetry::WriteResult writeConfig(const Config& value) noexcept
	{
		++writes;
		config = value;
		if (config.periodMs < 10)
			config.periodMs = 10;
		return telemetry::WriteResult::Applied;
	}
	telemetry::CommandResult reset() noexcept
	{
		++commands;
		config = {};
		return telemetry::CommandResult::Executed;
	}
	telemetry::ServiceResult<ScaleResponse> scale(const ScaleRequest& value) noexcept
	{
		++services;
		return telemetry::ServiceResult<ScaleResponse>::success({value.input * config.gain});
	}
};

inline Device device;
inline constexpr telemetry::FieldTable fields{
	telemetry::field<&Device::readConfig, &Device::writeConfig>("Config", device),
	telemetry::field<&Device::serialNumber>("SerialNumber", device)};
inline constexpr telemetry::CommandTable commands{
	telemetry::command<&Device::reset>("Reset", device)};
inline constexpr telemetry::ServiceTable services{
	telemetry::service<&Device::scale>("Scale", device)};
inline constexpr telemetry::FieldCatalogTable fieldCatalogs{telemetry::group("device", fields)};
inline constexpr telemetry::CommandCatalogTable commandCatalogs{telemetry::group("device", commands)};
inline constexpr telemetry::ServiceCatalogTable serviceCatalogs{telemetry::group("device", services)};
inline constexpr telemetry::Model model{fieldCatalogs, commandCatalogs, serviceCatalogs};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
static_assert(descriptor.valid());
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();
} // namespace device_starter
