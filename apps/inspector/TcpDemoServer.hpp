/*
 * @file TcpDemoServer.hpp
 * @brief Single-client TCP demonstration peer and deterministic loopback fault controls.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_INSPECTOR_QML_TCP_DEMO_SERVER_HPP
#define TELEMETRY_EXAMPLES_INSPECTOR_QML_TCP_DEMO_SERVER_HPP
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtNetwork/QHostAddress>
#include <memory>

namespace inspector_qml {

// Application-owned socket port, not a library transport or a hardware model.
// Each connection receives an independent portable Server and native workspace.
// Only one client is accepted at once. All calls belong to its Qt owner thread.
class TcpDemoServer final : public QObject {
public:
	explicit TcpDemoServer(QObject* parent = nullptr);
	~TcpDemoServer() override;
	bool listen(quint16 port = 0, const QHostAddress& address = QHostAddress::LocalHost);
	quint16 port() const noexcept;
	QString errorString() const;
	bool hasClient() const noexcept;
	void close();

	// Controls affect subsequent traffic; replacing Descriptor/path first closes
	// the old connection. The default file table puts a decoy before Descriptor.
	void setReplyFragmentBytes(qsizetype bytes, int intervalMilliseconds = 0);
	void setReplyDelay(int milliseconds);
	void setCloseAfterRequestBytes(qsizetype bytes);
	void setCloseAfterReplyBytes(qsizetype bytes);
	void setDropNextEncodedReply(bool drop = true);
	void setNextFieldReadUnavailable(bool unavailable = true);
	void setPrependPreviousEncodedReply(bool prepend = true);
	void setDescriptorOverride(QByteArray bytes);
	void setDescriptorPath(QString path);
	void setDescriptorChunkBytes(qsizetype bytes);
	void setNextReplyOverride(QByteArray bytes);
	void setWritableDemo(bool writable);
	void setRejectWrites(bool reject);

	quint64 requests() const noexcept;
	quint64 fieldReads() const noexcept;
	quint64 fieldWrites() const noexcept;
	quint64 nativeWrites() const noexcept;
	quint64 nativeCommands() const noexcept;
	quint64 executedCommands() const noexcept;
	quint64 acceptedCommands() const noexcept;
	quint64 nativeServices() const noexcept;
	quint64 serviceCalls() const noexcept;
	quint64 connections() const noexcept;
	quint64 receivedBytes() const noexcept;
	quint64 sentBytes() const noexcept;
	QVector<quint32> resourceReadIndices() const;
	QByteArray descriptor() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace inspector_qml
#endif // TELEMETRY_EXAMPLES_INSPECTOR_QML_TCP_DEMO_SERVER_HPP
