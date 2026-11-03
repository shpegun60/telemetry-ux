/*
 * @file DemoModel.hpp
 * @brief Application-owned native model for the generated QML demo and its checks.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 *
 * Initial DTOs, callbacks, labels and values come from Telemetry SDK
 * tests/codegen/Device.hpp and examples/structured_client/Device.hpp at
 * 6e15220b1271139b3f1996a16d385c660f856f45. This is an independently owned
 * demo model, not a copy of an SDK codec, dispatcher, client or wire parser.
 * Keep all table owners and their borrowed callback targets alive for the demo.
 */
#ifndef TELEMETRY_UX_EXAMPLES_CODEGEN_QML_DEMO_MODEL_HPP
#define TELEMETRY_UX_EXAMPLES_CODEGEN_QML_DEMO_MODEL_HPP
#pragma once

#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <array>
#include <cstdint>
#include <limits>

namespace client_example {
namespace ts = telemetry;

enum class Mode : std::int16_t {
	Off = -1,
	On = 2
};

struct Config {
	std::uint64_t serial;
	std::int64_t offset;
	float gain;
	double precise;
	bool enabled;
	Mode mode;
	std::array<std::uint16_t, 3> samples;
};

struct Request {
	Config config;
	std::uint32_t multiplier;
};

struct Response {
	Config config;
	std::uint32_t checksum;
};

inline constexpr Config initial{
    UINT64_MAX, INT64_MIN, -0.0f, 0.125, true, static_cast<Mode>(-2), {1, 256, 65535}};
inline constexpr Request request{initial, 4};

// Public methods:
// - readConfig(): Copy current configuration.
// - writeConfig(): Replace current configuration.
// - apply(): Replace current configuration.
// - reset(): Restore example defaults.
// - inspect(): Inspect typed request.
// - ping(): Count statusless call.
struct Device {
	Config config = initial;
	unsigned calls = 0;

	Config readConfig() const noexcept
	{
		return config;
	}

	telemetry::WriteResult writeConfig(const Config& value) noexcept
	{
		++calls;
		config = value;
		return telemetry::WriteResult::Applied;
	}

	telemetry::CommandResult apply(const Config& value) noexcept
	{
		++calls;
		config = value;
		return telemetry::CommandResult::Executed;
	}

	telemetry::CommandResult reset() noexcept
	{
		++calls;
		config = initial;
		return telemetry::CommandResult::Executed;
	}

	ts::ServiceResult<Response> inspect(const Request& value) noexcept
	{
		++calls;
		// These are application decisions, not descriptor limits/defaults.
		switch (value.multiplier) {
			case 0:
				return ts::ServiceResult<Response>::failure(ts::ServiceStatus::InvalidArgument);
			case 1:
				return ts::ServiceResult<Response>::failure(ts::ServiceStatus::Busy);
			case 2:
				return ts::ServiceResult<Response>::failure(ts::ServiceStatus::Unavailable);
			case 3:
				return ts::ServiceResult<Response>::failure(ts::ServiceStatus::Failed);
			default:
				break;
		}
		std::uint32_t sum = 0;
		for (auto sample : value.config.samples)
			sum += sample;
		return ts::ServiceResult<Response>::success({value.config, sum * value.multiplier});
	}

	void ping() noexcept
	{
		++calls;
	}
};

inline Device device;
inline telemetry::FunctionSlot<Response(const Request&) noexcept> offline;
inline constexpr ts::FieldTable localFields{
    ts::field<&Device::readConfig, &Device::writeConfig>("Config", device)};
inline constexpr ts::CommandTable localCommands{ts::command<&Device::apply>("Apply", device),
                                                ts::command<&Device::reset>("Reset", device)};
inline constexpr ts::ServiceTable localServices{ts::service<&Device::inspect>("Inspect", device),
                                                ts::service<&Device::ping>("Ping", device),
                                                ts::service("Offline", offline)};
} // namespace client_example

namespace codegen_fixture {
namespace ts = telemetry;
namespace ex = client_example;

struct Empty {};

struct Scalars {
	bool flag;
	std::uint8_t u8;
	std::int8_t i8;
	std::uint16_t u16;
	std::int16_t i16;
	std::uint32_t u32;
	std::int32_t i32;
	std::uint64_t u64;
	std::int64_t i64;
	float f32;
	double f64;
	std::array<std::array<std::int8_t, 2>, 2> matrix;
	std::array<Empty, 2> empty;
};

inline constexpr Scalars initial{true,
                                 UINT8_MAX,
                                 INT8_MIN,
                                 UINT16_MAX,
                                 INT16_MIN,
                                 UINT32_MAX,
                                 INT32_MIN,
                                 UINT64_MAX,
                                 INT64_MIN,
                                 -0.0f,
                                 std::numeric_limits<double>::infinity(),
                                 {{{-1, 2}, {-3, 4}}},
                                 {}};

// These callbacks expose zero-byte outcomes independently of value decoding.
// Public methods:
// - read(): Return all canonical scalar values.
// - good(): Return an empty aggregate successfully.
// - fail(): Refuse an empty aggregate response.
// - consume(): Consume an empty aggregate and return no response.
// - accepted(): Accept work without claiming its physical completion.
struct EdgeDevice {
	unsigned calls = 0;

	Scalars read() const noexcept
	{
		return initial;
	}

	ts::ServiceResult<Empty> good() noexcept
	{
		++calls;
		return ts::ServiceResult<Empty>::success({});
	}

	ts::ServiceResult<Empty> fail() noexcept
	{
		++calls;
		return ts::ServiceResult<Empty>::failure(ts::ServiceStatus::Busy);
	}

	void consume(const Empty&) noexcept
	{
		++calls;
	}

	ts::CommandResult accepted(const Empty&) noexcept
	{
		++calls;
		return ts::CommandResult::Accepted;
	}
};

inline EdgeDevice edge;
inline constexpr ts::FieldTable edgeFields{ts::field<&EdgeDevice::read>("Scalars", edge)};
inline constexpr ts::CommandTable edgeCommands{ts::command<&EdgeDevice::accepted>("Accept", edge)};
inline constexpr ts::ServiceTable edgeServices{ts::service<&EdgeDevice::good>("Good", edge),
                                               ts::service<&EdgeDevice::fail>("Fail", edge),
                                               ts::service<&EdgeDevice::consume>("Consume", edge)};
inline constexpr ts::FieldCatalogTable fields{ts::group("device", ex::localFields),
                                              ts::group("edge", edgeFields)};
inline constexpr ts::CommandCatalogTable commands{ts::group("device", ex::localCommands),
                                                  ts::group("edge", edgeCommands)};
inline constexpr ts::ServiceCatalogTable services{ts::group("device", ex::localServices),
                                                  ts::group("edge", edgeServices)};
inline constexpr ts::Model model{fields, commands, services};
inline constexpr ::telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = ::telemetry::descriptor::packDescriptor<descriptor>();
inline std::array<std::byte, model.maxScratch()> scratch{};
inline ts::Workspace workspace{scratch};
} // namespace codegen_fixture
#endif
