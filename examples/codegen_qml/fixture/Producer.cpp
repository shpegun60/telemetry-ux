/*
 * @file Producer.cpp
 * @brief Export the UX demo Descriptor and canonical payload fixtures.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include "DemoModel.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace cf = codegen_fixture;
namespace ex = client_example;

static void save(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
	std::ofstream file(path, std::ios::binary);
	file.write(reinterpret_cast<const char*>(bytes.data()),
	           static_cast<std::streamsize>(bytes.size()));
	if (!file)
		throw std::runtime_error("Cannot write native fixture");
}

template<class T>
static void saveValue(const std::filesystem::path& path, const T& value)
{
	std::array<std::byte, telemetry::wireSize<T>> bytes{};
	if (telemetry::encode(value, bytes) != telemetry::CodecStatus::Ok)
		throw std::runtime_error("Native encoding failed");
	save(path, bytes);
}

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	const std::filesystem::path directory{argv[1]};
	std::filesystem::create_directories(directory);
	save(directory / "descriptor.bin", cf::descriptorBytes);
	saveValue(directory / "config.bin", ex::initial);
	saveValue(directory / "request.bin", ex::request);
	const auto result = ex::device.inspect(ex::request);
	if (result.status() != telemetry::ServiceStatus::Ok)
		return 3;
	saveValue(directory / "response.bin", result.value());
	saveValue(directory / "scalars.bin", cf::initial);
	saveValue(directory / "empty.bin", cf::Empty{});
	return 0;
}
