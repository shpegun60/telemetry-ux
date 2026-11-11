/* Manifest-only localhost HTTP and official Qt WebSocket transport. MIT. */
#ifndef TELEMETRY_LOCAL_EMULATOR_WEB_SERVER_HPP
#define TELEMETRY_LOCAL_EMULATOR_WEB_SERVER_HPP
#pragma once
#include <QObject>
#include <QString>
#include <memory>
class DeviceServer;

class WebServer final : public QObject {
	Q_OBJECT
public:
	explicit WebServer(DeviceServer& device, QObject* parent = nullptr);
	~WebServer() override;
	bool listen(const QString& webRoot, quint16 httpPort = 8080, quint16 wsPort = 0);
	void close();
	QString errorString() const;
	QString url() const;
	quint16 httpPort() const;

private:
	struct Impl;
	std::unique_ptr<Impl> d;
};
#endif
