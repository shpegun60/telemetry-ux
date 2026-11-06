// Reference server process adapter; the installed SDK validates all Encoded bytes. MIT.
#include "Device.hpp"
#include <integration/encoded/Dispatcher.hpp>
#include <integration/encoded/BatchDispatcher.hpp>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <span>

namespace {
constexpr std::size_t frameLimit = 65536;
int hex(char c) noexcept
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// No unbounded getline allocation. An oversized/malformed line retires this process.
bool readLine(std::span<char> bytes, std::size_t& size) noexcept
{
	size = 0;
	int c;
	while ((c = std::fgetc(stdin)) != EOF && c != '\n') {
		if (!c || size == bytes.size() - 1)
			return false;
		bytes[size++] = static_cast<char>(c);
	}
	if (std::ferror(stdin) || (c == EOF && !size))
		return false;
	if (size && bytes[size - 1] == '\r')
		--size;
	bytes[size] = 0;
	return true;
}
} // namespace

int main(int argc, char** argv)
{
	if (argc == 3 && std::strcmp(argv[1], "--descriptor") == 0) {
		auto* file = std::fopen(argv[2], "wb");
		if (!file)
			return 2;
		const auto size = device_starter::descriptorBytes.size();
		const auto written = std::fwrite(device_starter::descriptorBytes.data(), 1, size, file);
		const auto closed = std::fclose(file);
		return written == size && closed == 0 ? 0 : 2;
	}
	if (argc != 1)
		return 2;
	std::uint32_t session = 0;
	std::array<char, 2 * frameLimit + 3> line{};
	std::array<std::byte, frameLimit> request{}, response{}, scratch{};
	telemetry::Workspace workspace{scratch};
	std::size_t size;
	while (readLine(line, size)) {
		if (std::strncmp(line.data(), "session ", 8) == 0) {
			std::uint32_t next = 0;
			const auto parsed = std::from_chars(line.data() + 8, line.data() + size, next);
			if (parsed.ec != std::errc{} || parsed.ptr != line.data() + size || !next || next <= session)
				return 3;
			session = next; // trusted server owner only; never copied from a request header
			std::printf("{\"session\":%u}\n", session);
		} else if (std::strcmp(line.data(), "state") == 0) {
			const auto& d = device_starter::device;
			std::printf("{\"session\":%u,\"reads\":%u,\"writes\":%u,\"commands\":%u,"
			            "\"services\":%u,\"periodMs\":%u}\n", session, d.reads, d.writes,
			            d.commands, d.services, d.config.periodMs);
		} else if (std::strcmp(line.data(), "readmany") == 0 ||
		           std::strcmp(line.data(), "mixedbatch") == 0) {
			std::array<std::byte, 24> capability{};
			const auto built = std::strcmp(line.data(), "readmany") == 0
			    ? telemetry::protocol::encoded::read_many::writeCapability(
			          capability, {session, device_starter::descriptor.fingerprint()})
			    : telemetry::protocol::encoded::batch::writeCapability(
			          capability, {session, device_starter::descriptor.fingerprint()});
			if (!built)
				return 4;
			for (const auto byte : capability)
				std::printf("%02x", std::to_integer<unsigned>(byte));
			std::putchar('\n');
		} else {
			if (!session || size < 64 || size % 2 || size / 2 > request.size())
				return 3;
			for (std::size_t i = 0; i < size / 2; ++i) {
				const auto high = hex(line[2 * i]), low = hex(line[2 * i + 1]);
				if (high < 0 || low < 0)
					return 3;
				request[i] = std::byte((high << 4) | low);
			}
			const auto input = std::span<const std::byte>{request}.first(size / 2);
			const auto result = request[6] == std::byte{6}
			    ? telemetry::integration::encoded::dispatchEncoded(
			          device_starter::model.view(), workspace, session, input, response,
			          telemetry::protocol::encoded::batch::Capability{
			              session, device_starter::descriptor.fingerprint()})
			    : telemetry::integration::encoded::dispatchEncoded(
			          device_starter::model.view(), workspace, session, input, response,
			          telemetry::protocol::encoded::read_many::Capability{
			              session, device_starter::descriptor.fingerprint()});
			if (!result)
				return 4;
			for (std::size_t i = 0; i < result.written; ++i)
				std::printf("%02x", std::to_integer<unsigned>(response[i]));
			std::putchar('\n');
		}
		if (std::fflush(stdout) != 0)
			return 5;
	}
	return std::feof(stdin) && !std::ferror(stdin) ? 0 : 3;
}
