/*
 * @file DemoPeer.hpp
 * @brief Opaque native demonstration peer for the runtime Qt Inspector.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLE_INSPECTOR_QML_DEMO_PEER_HPP
#define TELEMETRY_EXAMPLE_INSPECTOR_QML_DEMO_PEER_HPP
#pragma once

#include <client/ClientResult.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace resource {
class FileSystemView;
}

namespace telemetry::integration::tcp {
class Server;
}

namespace inspector_qml {

// Only the peer implementation knows native DTOs. The controller consumes the
// immutable Descriptor and complete Encoded frames through this boundary.
class DemoPeer {
public:
	explicit DemoPeer(bool writable = false, bool readMany = false);
	~DemoPeer();
	DemoPeer(const DemoPeer&) = delete;
	DemoPeer& operator=(const DemoPeer&) = delete;

	[[nodiscard]] telemetry::client::Input descriptor() const noexcept;
	[[nodiscard]] bool writable() const noexcept;
	[[nodiscard]] bool readManyEnabled() const noexcept;
	[[nodiscard]] std::uint64_t writeCount() const noexcept;
	[[nodiscard]] std::uint64_t commandCount() const noexcept;
	[[nodiscard]] std::uint64_t executedCount() const noexcept;
	[[nodiscard]] std::uint64_t acceptedCount() const noexcept;
	[[nodiscard]] std::uint64_t lastCommandCounter() const noexcept;
	[[nodiscard]] float lastCommandGain() const noexcept;
	[[nodiscard]] std::uint64_t serviceCount() const noexcept;
	void setRejectWrites(bool reject) noexcept;
	// The returned server borrows this peer's workspace and caller-owned files
	// and buffers. It must be destroyed before any of them.
	std::unique_ptr<telemetry::integration::tcp::Server>
	makeTcpServer(resource::FileSystemView files, telemetry::client::Output rx,
	              telemetry::client::Output tx);
	// A valid request produces a correlated reply, including Model refusals.
	// Envelope, session, overlap or capacity refusal returns zero bytes.
	[[nodiscard]] std::size_t dispatch(telemetry::client::Input request,
	                                   telemetry::client::Output reply,
	                                   std::uint32_t session) noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace inspector_qml
#endif // TELEMETRY_EXAMPLE_INSPECTOR_QML_DEMO_PEER_HPP
