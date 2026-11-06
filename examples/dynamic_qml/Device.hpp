// Your application types and endpoint tables. No Qt or transport in this Model. MIT.
#pragma once
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <cstdint>

namespace own_device {
struct Config {
	std::uint64_t serial = UINT64_C(9007199254740993);
	float gain = 1.25f;
	bool enabled = true;
};

struct Device {
	Config config;
	unsigned reads = 0;

	Config read() noexcept
	{
		++reads;
		return config;
	}

	telemetry::WriteResult write(const Config& value) noexcept
	{
		config = value;
		return telemetry::WriteResult::Applied;
	}

	telemetry::CommandResult reset() noexcept
	{
		config = {};
		return telemetry::CommandResult::Executed;
	}

	telemetry::ServiceResult<Config> inspect() noexcept
	{
		return telemetry::ServiceResult<Config>::success(config);
	}
};

inline Device device;
inline constexpr telemetry::FieldTable fields{
    telemetry::field<&Device::read, &Device::write>("Config", device)};
inline constexpr telemetry::CommandTable commands{
    telemetry::command<&Device::reset>("Reset", device)};
inline constexpr telemetry::ServiceTable services{
    telemetry::service<&Device::inspect>("Inspect", device)};
inline constexpr telemetry::FieldCatalogTable fieldCatalogs{telemetry::group("device", fields)};
inline constexpr telemetry::CommandCatalogTable commandCatalogs{
    telemetry::group("device", commands)};
inline constexpr telemetry::ServiceCatalogTable serviceCatalogs{
    telemetry::group("device", services)};
inline constexpr telemetry::Model model{fieldCatalogs, commandCatalogs, serviceCatalogs};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto bytes = telemetry::descriptor::packDescriptor<descriptor>();
} // namespace own_device
