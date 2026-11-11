// Native Telemetry Model + Descriptor for the review device. SPDX-License-Identifier: MIT
#include "MyDevice.hpp"
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <array>
#include <cmath>
#include <cstddef>

namespace mydevice {

telemetry::WriteResult Device::writeSettings(const Settings& next) noexcept
{
    if (!std::isfinite(next.alarmLevel) || next.alarmLevel < -40.0f || next.alarmLevel > 125.0f ||
        next.periodMs < 10 || next.periodMs > 60000)
        return telemetry::WriteResult::InvalidValue;
    settings = next;
    return telemetry::WriteResult::Applied;
}

telemetry::CommandResult Device::resetUptime() noexcept
{
    uptimeTicks = 0;
    return telemetry::CommandResult::Executed;
}

void Device::tick() noexcept
{
    ++uptimeTicks;
    temperature = 21.5f + 3.0f * std::sin(static_cast<float>(uptimeTicks % 628) / 100.0f);
    supply = 3.3f + 0.05f * std::cos(static_cast<float>(uptimeTicks % 314) / 50.0f);
    if (settings.enabled && temperature > settings.alarmLevel)
        ++alarms;
}

namespace {
constinit Device state;

constexpr telemetry::FieldTable measure{
    telemetry::field<&Device::readTemperature>("Temperature", state),
    telemetry::field<&Device::readSupply>("Supply", state),
    telemetry::field<&Device::readUptime>("Uptime", state),
    telemetry::field<&Device::readAlarms>("Alarms", state)};
constexpr telemetry::FieldTable config{
    telemetry::field<&Device::readSettings, &Device::writeSettings>("Settings", state)};
constexpr telemetry::FieldCatalogTable fields{
    telemetry::group("measure", measure), telemetry::group("config", config)};

constexpr telemetry::CommandTable actions{
    telemetry::command<&Device::resetUptime>("ResetUptime", state)};
constexpr telemetry::CommandCatalogTable commands{telemetry::group("control", actions)};
constexpr telemetry::ServiceCatalogTable<> services{};

constexpr telemetry::Model nativeModel{fields, commands, services};
constexpr telemetry::descriptor::Descriptor nativeDescriptor{nativeModel};
constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<nativeDescriptor>();

alignas(std::max_align_t) std::array<std::byte, nativeModel.maxScratch() ? nativeModel.maxScratch() : 1> scratch{};
telemetry::Workspace nativeWorkspace{scratch};
} // namespace

Device& device() noexcept { return state; }
telemetry::ModelView model() noexcept { return nativeModel.view(); }
telemetry::Workspace& workspace() noexcept { return nativeWorkspace; }
std::span<const std::byte> descriptor() noexcept { return descriptorBytes; }
std::uint64_t fingerprint() noexcept { return nativeDescriptor.fingerprint(); }

} // namespace mydevice
