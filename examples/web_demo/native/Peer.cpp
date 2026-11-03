/* Demo-only persistent native process. SDK dispatch owns every Telemetry frame/status. MIT. */
#include "Model.hpp"
#include <integration/encoded/Dispatcher.hpp>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>

namespace {
constexpr std::size_t frameLimit = 65536;

enum class LineStatus {
	End,
	Ok,
	Malformed,
	IoError
};

LineStatus readLine(std::span<char> line, std::size_t& size) noexcept
{
	size = 0;
	int byte = 0;
	while ((byte = std::fgetc(stdin)) != EOF && byte != '\n') {
		if (!byte || size == line.size() - 1)
			return LineStatus::Malformed;
		line[size++] = static_cast<char>(byte);
	}
	if (byte == EOF && std::ferror(stdin))
		return LineStatus::IoError;
	if (byte == EOF && !size)
		return LineStatus::End;
	if (size && line[size - 1] == '\r')
		--size;
	line[size] = '\0';
	return LineStatus::Ok;
}

int digit(char value) noexcept
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	return -1;
}

void jsonNumber(double value) noexcept
{
	if (std::isfinite(value))
		std::printf("%.17g", value);
	else
		std::fputs("null", stdout);
}

void state(std::uint32_t session) noexcept
{
	const auto& device = web_demo::device;
	const auto& value = device.current;
	const auto& counts = device.counters;
	std::printf(
	    "{\"session\":%u,\"reads\":%u,\"writes\":%u,\"commands\":%u,\"services\":%u,"
	    "\"enabled\":%s,\"u8\":%u,\"s8\":%d,\"u16\":%u,\"s16\":%d,"
	    "\"u32\":%u,\"s32\":%d,\"large\":\"%llu\",\"negative\":\"%lld\",\"mode\":%d,\"gain\":",
	    session, counts.reads, counts.writes, counts.commands, counts.services,
	    value.enabled ? "true" : "false", static_cast<unsigned>(value.u8),
	    static_cast<int>(value.s8), static_cast<unsigned>(value.u16), static_cast<int>(value.s16),
	    value.u32, value.s32, static_cast<unsigned long long>(value.large),
	    static_cast<long long>(value.negative), static_cast<int>(value.mode));
	jsonNumber(value.gain);
	std::fputs(",\"zero\":", stdout);
	jsonNumber(value.zero);
	std::fputs(",\"f32\":", stdout);
	jsonNumber(value.f32);
	const auto& matrix = value.nested.matrix;
	std::printf(",\"nested\":{\"matrix\":[[%d,%d],[%d,%d]],\"flag\":%s}}\n",
	            static_cast<int>(matrix[0][0]), static_cast<int>(matrix[0][1]),
	            static_cast<int>(matrix[1][0]), static_cast<int>(matrix[1][1]),
	            value.nested.flag ? "true" : "false");
}
} // namespace

int main(int argc, char** argv)
{
	if (argc == 3 && std::strcmp(argv[1], "--descriptor") == 0) {
		auto* output = std::fopen(argv[2], "wb");
		if (!output)
			return 2;
		const auto size = web_demo::descriptorBytes.size();
		const auto written = std::fwrite(web_demo::descriptorBytes.data(), 1, size, output);
		const auto closed = std::fclose(output);
		return written == size && closed == 0 ? 0 : 2;
	}
	if (argc != 1)
		return 2;

	// Trusted owner state is never inferred from a request header.
	std::uint32_t session = 17;
	std::array<char, frameLimit * 2 + 3> line{};
	std::array<std::byte, frameLimit> request{}, response{}, scratch{};
	static_assert(web_demo::model.maxFieldScratch() <= frameLimit);
	telemetry::Workspace workspace{scratch};
	for (;;) {
		std::size_t size = 0;
		const auto read = readLine(line, size);
		if (read == LineStatus::End)
			return 0;
		if (read != LineStatus::Ok)
			return read == LineStatus::IoError ? 5 : 3;
		if (std::strcmp(line.data(), "state") == 0) {
			state(session);
		} else if (std::strcmp(line.data(), "reset") == 0) {
			web_demo::device = web_demo::Device{};
			std::puts("{}");
		} else if (std::strncmp(line.data(), "session ", 8) == 0) {
			std::uint32_t admitted = 0;
			const auto parsed = std::from_chars(line.data() + 8, line.data() + size, admitted);
			if (parsed.ec != std::errc{} || parsed.ptr != line.data() + size || !admitted)
				return 3;
			session = admitted;
			std::printf("{\"session\":%u}\n", session);
		} else if (std::strcmp(line.data(), "readmany") == 0) {
			std::array<std::byte, 24> capability{};
			const auto built = telemetry::protocol::encoded::read_many::writeCapability(
			    capability, {session, web_demo::descriptor.fingerprint()});
			if (!built) return 4;
			for (const auto byte : capability)
				std::printf("%02x", std::to_integer<unsigned>(byte));
			std::putchar('\n');
		} else {
			if (!size || size % 2 || size / 2 > request.size())
				return 3;
			for (std::size_t i = 0; i < size / 2; ++i) {
				const auto high = digit(line[2 * i]), low = digit(line[2 * i + 1]);
				if (high < 0 || low < 0)
					return 3;
				request[i] = std::byte((high << 4) | low);
			}
			const auto result = telemetry::integration::encoded::dispatchEncoded(
			    web_demo::model.view(), workspace, session,
			    std::span<const std::byte>{request}.first(size / 2), response,
			    telemetry::protocol::encoded::read_many::Capability{
			        session, web_demo::descriptor.fingerprint()});
			if (!result)
				return 4;
			for (std::size_t i = 0; i < result.written; ++i)
				std::printf("%02x", std::to_integer<unsigned>(response[i]));
			std::putchar('\n');
		}
		if (std::fflush(stdout) != 0)
			return 5;
	}
}
