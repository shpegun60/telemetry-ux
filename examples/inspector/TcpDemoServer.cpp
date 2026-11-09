/*
 * @file TcpDemoServer.cpp
 * @brief Drive the portable TCP server from owned Qt socket buffers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "TcpDemoServer.hpp"
#include "DemoPeer.hpp"
#include <integration/tcp/Server.hpp>
#include <resource/BytesFile.hpp>
#include <resource/FileSystem.hpp>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <algorithm>
#include <array>
#include <optional>
#include <string>

namespace inspector_qml {
namespace tcp = telemetry::integration::tcp;

namespace {
resource::Input input(const QByteArray& bytes) noexcept
{
	return {reinterpret_cast<const std::byte*>(bytes.constData()),
	        static_cast<std::size_t>(bytes.size())};
}

quint32 little32(const QByteArray& bytes, qsizetype at) noexcept
{
	quint32 value = 0;
	for (unsigned i = 0; i < 4; ++i)
		value |= quint32(static_cast<unsigned char>(bytes[at + i])) << (i * 8);
	return value;
}
} // namespace

struct TcpDemoServer::Impl {
	struct File {
		QByteArray bytes;
		qsizetype chunk = 256;

		resource::FileSize size() const noexcept
		{
			return static_cast<resource::FileSize>(bytes.size());
		}

		resource::ReadResult read(resource::Cursor cursor, resource::Output output) const noexcept
		{
			if (chunk > 0)
				output = output.first(std::min(output.size(), static_cast<std::size_t>(chunk)));
			return resource::BytesFile{input(bytes)}.read(cursor, output);
		}
	};

	struct Connection {
		DemoPeer peer;
		File decoy, descriptor;
		std::string path;
		resource::FileSystem<2> files;
		std::array<std::byte, 65568> rx{}, tx{};
		std::unique_ptr<tcp::Server> server;
		QByteArray pending, requestPrefix, reply;
		qsizetype replyOffset = 0;
		quint64 requestBytes = 0;
		bool closeAfterReply = false;

		Connection(QByteArray bytes, const QString& name, qsizetype chunk, bool writable,
		           bool rejectWrites)
		    : peer(writable), decoy{QByteArrayLiteral("Telemetry demo version 1"), 256},
		      descriptor{std::move(bytes), chunk}, path(name.toUtf8().toStdString()),
		      files(resource::filesystem(resource::file("/device/version.bin", decoy),
		                                 resource::file(std::string_view{path}, descriptor)))
		{
			peer.setRejectWrites(rejectWrites);
			server = peer.makeTcpServer(files.view(), rx, tx);
		}
	};

	TcpDemoServer* owner;
	QTcpServer listener;
	QPointer<QTcpSocket> socket;
	QTimer writer;
	std::unique_ptr<Connection> connection;
	QByteArray descriptorOverride, previousEncodedReply;
	QString descriptorPath = QStringLiteral("/telemetry/descriptor.bin");
	std::optional<QByteArray> replyOverride;
	qsizetype fragmentBytes = 0, closeRequestBytes = 0, closeReplyBytes = 0, descriptorChunk = 256;
	int fragmentInterval = 0, replyDelay = 0;
	bool descriptorOverridden = false, dropEncoded = false, prependPrevious = false,
	     unavailable = false, writableDemo = false, rejectWrites = false;
	quint64 generation = 0, totalRequests = 0, totalReads = 0, totalWrites = 0,
	        totalNativeWrites = 0, totalConnections = 0;
	quint64 received = 0, sent = 0;
	quint64 totalNativeCommands = 0, totalExecutedCommands = 0, totalAcceptedCommands = 0;
	quint64 totalNativeServices = 0, totalServices = 0;
	QVector<quint32> resourceIndices;

	explicit Impl(TcpDemoServer* object) : owner(object)
	{
		QObject::connect(&listener, &QTcpServer::newConnection, owner, [this] {
			accept();
		});
		writer.setSingleShot(true);
		QObject::connect(&writer, &QTimer::timeout, owner, [this] {
			write();
		});
	}

	QByteArray descriptorBytes() const
	{
		if (descriptorOverridden)
			return descriptorOverride;
		DemoPeer peer{writableDemo};
		const auto bytes = peer.descriptor();
		return {reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())};
	}

	void retire()
	{
		++generation;
		writer.stop();
		if (socket) {
			auto* retired = socket.data();
			socket.clear();
			QObject::disconnect(retired, nullptr, owner, nullptr);
			retired->abort();
			retired->deleteLater();
		}
		if (connection)
			connection->server->close();
		connection.reset();
	}

	void accept()
	{
		while (listener.hasPendingConnections()) {
			auto* candidate = listener.nextPendingConnection();
			if (socket) {
				candidate->abort();
				candidate->deleteLater();
				continue;
			}
			++generation;
			++totalConnections;
			socket = candidate;
			connection = std::make_unique<Connection>(descriptorBytes(), descriptorPath,
			                                          descriptorChunk, writableDemo, rejectWrites);
			const auto admitted = generation;
			QObject::connect(candidate, &QTcpSocket::readyRead, owner, [this, admitted] {
				if (generation == admitted)
					read();
			});
			QObject::connect(candidate, &QTcpSocket::bytesWritten, owner, [this, admitted](qint64) {
				if (generation == admitted && !writer.isActive())
					writer.start(fragmentInterval);
			});
			QObject::connect(candidate, &QTcpSocket::disconnected, owner, [this, admitted] {
				if (generation == admitted)
					retire();
			});
			if (candidate->bytesAvailable())
				read();
		}
	}

	void read()
	{
		if (!socket || !connection)
			return;
		auto& state = *connection;
		const auto bytes = socket->readAll();
		if (state.pending.size() + bytes.size() > 2 * qsizetype(state.rx.size())) {
			retire();
			return;
		}
		state.pending += bytes;
		consume();
	}

	void consume()
	{
		if (!socket || !connection || !connection->reply.isEmpty())
			return;
		auto& state = *connection;
		while (!state.pending.isEmpty()) {
			auto available = state.pending.size();
			if (closeRequestBytes > 0) {
				const auto remaining = quint64(closeRequestBytes) > state.requestBytes
				                           ? quint64(closeRequestBytes) - state.requestBytes
				                           : 0;
				available = std::min(available, static_cast<qsizetype>(remaining));
				if (!available) {
					closeRequestBytes = 0;
					retire();
					return;
				}
			}
			const auto offered = input(state.pending).first(static_cast<std::size_t>(available));
			const auto previousWrites = state.peer.writeCount();
			const auto previousCommands = state.peer.commandCount();
			const auto previousExecuted = state.peer.executedCount();
			const auto previousAccepted = state.peer.acceptedCount();
			const auto previousServices = state.peer.serviceCount();
			const auto result = state.server->receive(offered);
			totalNativeWrites += state.peer.writeCount() - previousWrites;
			totalNativeCommands += state.peer.commandCount() - previousCommands;
			totalExecutedCommands += state.peer.executedCount() - previousExecuted;
			totalAcceptedCommands += state.peer.acceptedCount() - previousAccepted;
			totalNativeServices += state.peer.serviceCount() - previousServices;
			const auto used = static_cast<qsizetype>(result.consumed);
			if (used > available) {
				retire();
				return;
			}
			if (state.requestPrefix.size() < 32)
				state.requestPrefix +=
				    state.pending.left(std::min(used, 32 - state.requestPrefix.size()));
			state.pending.remove(0, used);
			state.requestBytes += static_cast<quint64>(used);
			received += static_cast<quint64>(used);
			if (closeRequestBytes > 0 && state.requestBytes >= quint64(closeRequestBytes)) {
				closeRequestBytes = 0;
				retire();
				return;
			}
			if (result.status == tcp::Status::Ready) {
				++totalRequests;
				const bool encoded = state.requestPrefix.startsWith("TEQ1");
				if (encoded && state.requestPrefix.size() >= 7 &&
				    state.requestPrefix[6] ==
				        static_cast<char>(telemetry::protocol::encoded::Operation::FieldRead))
					++totalReads;
				if (encoded && state.requestPrefix.size() >= 7 &&
				    state.requestPrefix[6] ==
				        static_cast<char>(telemetry::protocol::encoded::Operation::FieldWrite))
					++totalWrites;
				if (encoded && state.requestPrefix.size() >= 7 &&
				    state.requestPrefix[6] ==
				        static_cast<char>(telemetry::protocol::encoded::Operation::Service))
					++totalServices;
				if (!encoded && state.requestPrefix.size() >= 5 &&
				    state.requestPrefix[0] == char(3))
					resourceIndices.push_back(little32(state.requestPrefix, 1));
				state.requestPrefix.clear();
				state.requestBytes = 0;
				const auto reply = state.server->reply();
				state.reply = QByteArray(reinterpret_cast<const char*>(reply.data()),
				                         static_cast<qsizetype>(reply.size()));
				state.server->releaseReply();
				if (encoded && unavailable) {
					auto parsed = telemetry::protocol::encoded::parse(input(state.reply));
					if (parsed && parsed.header.operation ==
					                  telemetry::protocol::encoded::Operation::FieldRead) {
						unavailable = false;
						parsed.header.dispatch = 9; // Existing DispatchStatus::Unavailable.
						parsed.header.payloadBytes = 0;
						state.reply.resize(32);
						const auto output = std::span<std::byte>{
						    reinterpret_cast<std::byte*>(state.reply.data()), 32};
						if (telemetry::protocol::encoded::writeHeader(output, parsed.header) !=
						    telemetry::protocol::encoded::Status::Ok) {
							retire();
							return;
						}
					}
				}
				if (replyOverride) {
					state.reply = std::move(*replyOverride);
					replyOverride.reset();
				}
				if (encoded) {
					auto current = state.reply;
					if (prependPrevious) {
						state.reply.prepend(previousEncodedReply);
						prependPrevious = false;
					}
					previousEncodedReply = std::move(current);
				}
				if (encoded && dropEncoded) {
					dropEncoded = false;
					state.reply.clear();
					continue;
				}
				if (closeReplyBytes > 0) {
					state.reply.truncate(std::min(state.reply.size(), closeReplyBytes));
					state.closeAfterReply = true;
					closeReplyBytes = 0;
				}
				writer.start(replyDelay);
				return;
			}
			if (result.status != tcp::Status::NeedMore || !used) {
				retire();
				return;
			}
		}
	}

	void write()
	{
		if (!socket || !connection)
			return;
		auto& state = *connection;
		const auto remaining = state.reply.size() - state.replyOffset;
		if (remaining > 0) {
			const auto chunk = fragmentBytes > 0 ? std::min(remaining, fragmentBytes) : remaining;
			const auto accepted = socket->write(state.reply.constData() + state.replyOffset, chunk);
			if (accepted < 0) {
				retire();
				return;
			}
			state.replyOffset += accepted;
			sent += static_cast<quint64>(accepted);
			writer.start(fragmentInterval);
			return;
		}
		if (socket->bytesToWrite() != 0)
			return;
		state.reply.clear();
		state.replyOffset = 0;
		if (state.closeAfterReply) {
			socket->disconnectFromHost();
			return;
		}
		consume();
	}
};

TcpDemoServer::TcpDemoServer(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>(this))
{}

TcpDemoServer::~TcpDemoServer()
{
	close();
}

bool TcpDemoServer::listen(quint16 port, const QHostAddress& address)
{
	return impl_->listener.listen(address, port);
}

quint16 TcpDemoServer::port() const noexcept
{
	return impl_->listener.serverPort();
}

QString TcpDemoServer::errorString() const
{
	return impl_->listener.errorString();
}

bool TcpDemoServer::hasClient() const noexcept
{
	return !impl_->socket.isNull();
}

void TcpDemoServer::close()
{
	impl_->listener.close();
	impl_->retire();
}

void TcpDemoServer::setReplyFragmentBytes(qsizetype bytes, int intervalMilliseconds)
{
	impl_->fragmentBytes = std::max(qsizetype(0), bytes);
	impl_->fragmentInterval = std::clamp(intervalMilliseconds, 0, 1000);
}

void TcpDemoServer::setReplyDelay(int milliseconds)
{
	impl_->replyDelay = std::clamp(milliseconds, 0, 60000);
}

void TcpDemoServer::setCloseAfterRequestBytes(qsizetype bytes)
{
	impl_->closeRequestBytes = std::max(qsizetype(0), bytes);
	if (impl_->connection)
		impl_->connection->requestBytes = 0;
}

void TcpDemoServer::setCloseAfterReplyBytes(qsizetype bytes)
{
	impl_->closeReplyBytes = std::max(qsizetype(0), bytes);
}

void TcpDemoServer::setDropNextEncodedReply(bool drop)
{
	impl_->dropEncoded = drop;
}

void TcpDemoServer::setNextFieldReadUnavailable(bool unavailable)
{
	impl_->unavailable = unavailable;
}

void TcpDemoServer::setPrependPreviousEncodedReply(bool prepend)
{
	impl_->prependPrevious = prepend;
}

void TcpDemoServer::setDescriptorOverride(QByteArray bytes)
{
	impl_->retire();
	impl_->descriptorOverride = std::move(bytes);
	impl_->descriptorOverridden = true;
}

void TcpDemoServer::setDescriptorPath(QString path)
{
	impl_->retire();
	impl_->descriptorPath = std::move(path);
}

void TcpDemoServer::setDescriptorChunkBytes(qsizetype bytes)
{
	impl_->descriptorChunk = std::clamp(bytes, qsizetype(1), qsizetype(65535));
	if (impl_->connection)
		impl_->connection->descriptor.chunk = impl_->descriptorChunk;
}

void TcpDemoServer::setNextReplyOverride(QByteArray bytes)
{
	impl_->replyOverride = std::move(bytes);
}

void TcpDemoServer::setWritableDemo(bool writable)
{
	impl_->retire();
	impl_->writableDemo = writable;
}

void TcpDemoServer::setRejectWrites(bool reject)
{
	impl_->rejectWrites = reject;
	if (impl_->connection)
		impl_->connection->peer.setRejectWrites(reject);
}

quint64 TcpDemoServer::requests() const noexcept
{
	return impl_->totalRequests;
}

quint64 TcpDemoServer::fieldReads() const noexcept
{
	return impl_->totalReads;
}

quint64 TcpDemoServer::fieldWrites() const noexcept
{
	return impl_->totalWrites;
}

quint64 TcpDemoServer::nativeWrites() const noexcept
{
	return impl_->totalNativeWrites;
}

quint64 TcpDemoServer::nativeCommands() const noexcept
{
	return impl_->totalNativeCommands;
}

quint64 TcpDemoServer::executedCommands() const noexcept
{
	return impl_->totalExecutedCommands;
}

quint64 TcpDemoServer::acceptedCommands() const noexcept
{
	return impl_->totalAcceptedCommands;
}

quint64 TcpDemoServer::nativeServices() const noexcept
{
	return impl_->totalNativeServices;
}

quint64 TcpDemoServer::serviceCalls() const noexcept
{
	return impl_->totalServices;
}

quint64 TcpDemoServer::connections() const noexcept
{
	return impl_->totalConnections;
}

quint64 TcpDemoServer::receivedBytes() const noexcept
{
	return impl_->received;
}

quint64 TcpDemoServer::sentBytes() const noexcept
{
	return impl_->sent;
}

QVector<quint32> TcpDemoServer::resourceReadIndices() const
{
	return impl_->resourceIndices;
}

QByteArray TcpDemoServer::descriptor() const
{
	return impl_->descriptorBytes();
}

} // namespace inspector_qml
