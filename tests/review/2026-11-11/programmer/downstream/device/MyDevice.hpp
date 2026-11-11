// Programmer-review downstream: a small device Model written from the public docs.
// Business values only; no Qt and no transport. SPDX-License-Identifier: MIT
#pragma once
#include <telemetry/result/EndpointStatus.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

namespace resource { class FileSystemView; }
namespace telemetry { class Workspace; struct ModelView; }

namespace mydevice {

struct Settings {
    float alarmLevel = 30.0f;
    std::uint32_t periodMs = 500;
    bool enabled = true;
};

struct Device {
    float temperature = 21.5f;
    float supply = 3.3f;
    // Deliberately above 2^53 so the client must keep it exact (decimal text).
    std::uint64_t uptimeTicks = 0x1234'5678'9ABC'DEF0ull;
    std::int32_t alarms = 0;
    Settings settings{};

    float readTemperature() const noexcept { return temperature; }
    float readSupply() const noexcept { return supply; }
    std::uint64_t readUptime() const noexcept { return uptimeTicks; }
    std::int32_t readAlarms() const noexcept { return alarms; }
    Settings readSettings() const noexcept { return settings; }
    telemetry::WriteResult writeSettings(const Settings& next) noexcept;
    telemetry::CommandResult resetUptime() noexcept;
    void tick() noexcept; // simulated change, called by the host timer
};

Device& device() noexcept;
telemetry::ModelView model() noexcept;
telemetry::Workspace& workspace() noexcept;
std::span<const std::byte> descriptor() noexcept;
std::uint64_t fingerprint() noexcept;

} // namespace mydevice
