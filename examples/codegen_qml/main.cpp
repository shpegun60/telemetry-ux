/*
 * @file main.cpp
 * @brief One generated QML UI over real native Encoded or Resource/RPC dispatch.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <DemoModel.hpp>
#include <DeviceApi.hpp>
#include <DeviceClientAdapter.hpp>
#include <integration/rpc/Backend.hpp>
#include <integration/encoded/Backend.hpp>
#include <integration/encoded/Dispatcher.hpp>
#include <resource/FileSystem.hpp>
#include <resource/protocol/Protocol.hpp>
#include <integration/telemetry/v3/RpcSessionPool.hpp>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <utility>

namespace cf = codegen_fixture;
namespace qa = device_api::qml;
namespace rpc = resource::telemetry::v3;

static QByteArray load(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

// Both modes share the generated adapter and its ONE DeviceClient execution
// queue. Only the complete-packet loopback port/backend changes.
struct Loopback {
	std::array<std::byte, cf::model.maxScratch()> scratch{};
	telemetry::Workspace workspace{scratch};
	std::array<std::byte, 512> request{}, response{}, clientRequest{}, clientResponse{};
	std::array<std::byte, 512> encodedTx{}, encodedResult{}, reply{}, portCopy{};
	std::array<std::byte, 48> packet{};
	telemetry::ModelView model{cf::model.view()};
	rpc::RpcMailbox mailbox{model, workspace, request, response};
	resource::FileSystem<1> files{resource::filesystem(resource::file("/rpc", mailbox))};
	rpc::RpcSessionPool<1> pool{std::array{rpc::RpcSessionBinding{mailbox, files.view()}}};
	rpc::RpcSessionHandle remote{};
	quint32 remoteGeneration = 0;
	::telemetry::client::rpc::RpcClient native{0, clientRequest, clientResponse, packet};
	QElapsedTimer elapsed;
	telemetry::client::Clock clock{this, [](void* self) noexcept {
		                               return static_cast<quint32>(
		                                   static_cast<Loopback*>(self)->elapsed.elapsed());
	                               }};
	telemetry::integration::rpc::RpcBackend rpcBackend{native, clock};
	telemetry::integration::encoded::EncodedBackend encodedBackend{encodedTx, encodedResult, clock};
	telemetry::client::Backend& backend;
	qa::DeviceClientAdapter<> adapter;
	QTimer timer;
	QPointer<qa::DeviceApi> api;
	QByteArray header;
	bool useRpc = false, recovering = false;

	explicit Loopback(qa::DeviceApi& deviceApi, QCoreApplication& app, QByteArray schemaHeader,
	                  bool rpcMode)
	    : backend(rpcMode ? static_cast<telemetry::client::Backend&>(rpcBackend)
	                      : static_cast<telemetry::client::Backend&>(encodedBackend)),
	      adapter(deviceApi, backend, clock), api(&deviceApi), header(std::move(schemaHeader)),
	      useRpc(rpcMode)
	{
		elapsed.start();
		if (useRpc && !openRemote())
			QTimer::singleShot(0, &app, [&app] {
				app.exit(1);
			});
		QObject::connect(&timer, &QTimer::timeout, &adapter, [this, &app] {
			if (!pump(app))
				app.exit(1);
		});
		timer.setInterval(1);
		timer.start();
	}

	bool openRemote()
	{
		if (remoteGeneration == std::numeric_limits<quint32>::max())
			return false;
		const auto result = pool.acquire({1, ++remoteGeneration, 0});
		if (result.status != rpc::RpcSessionStatus::Ok)
			return false;
		remote = result.session;
		return true;
	}

	bool pump(QCoreApplication& app)
	{
		const auto now = clock.now();
		adapter.process(now);
		if (adapter.recoveryRequired()) {
			// Drain the shared FIFO first. Recovery never resubmits those jobs.
			if (!recovering && adapter.pending() == 0) {
				recovering = true;
				QMetaObject::invokeMethod(
				    &adapter,
				    [this, &app] {
					    const auto generation = adapter.recoveryGeneration();
					    // This in-process port has no remaining DMA/socket copies.
					    // The synchronous fixture cannot remain Running here.
					    if (useRpc) {
						    if (pool.release(remote) != rpc::RpcSessionStatus::Ok) {
							    app.exit(1);
							    return;
						    }
						    (void)pool.process(clock.now());
						    if (!openRemote()) {
							    app.exit(1);
							    return;
						    }
					    }
					    if (!adapter.confirmSessionClosed(generation) || !api ||
					        !api->admitDescriptor(header)) {
						    app.exit(1);
						    return;
					    }
					    recovering = false;
				    },
				    Qt::QueuedConnection);
			}
			return true;
		}
		if (useRpc)
			(void)pool.process(now);
		if (const auto pending = adapter.nextPacket()) {
			if (pending.bytes.size() > portCopy.size())
				return false;
			std::copy(pending.bytes.begin(), pending.bytes.end(), portCopy.begin());
			const auto copied = std::span<const std::byte>{portCopy}.first(pending.bytes.size());
			if (!adapter.prepareHandoff(pending.session, pending.requestId))
				return true;
			if (!adapter.onSendAccepted(pending.session, pending.requestId))
				return false;
			std::size_t written = 0;
			if (useRpc) {
				const auto result = pool.handle(remote, copied, reply);
				if (result.status != rpc::RpcSessionStatus::Ok ||
				    result.reply.status != resource::Status::Ok)
					return false;
				if (result.ticket.serial &&
				    pool.replyAccepted(remote, result.ticket) != rpc::RpcSessionStatus::Ok)
					return false;
				written = result.reply.written;
			} else {
				const auto result = ::telemetry::integration::encoded::dispatchEncoded(
				    model, workspace, pending.session, copied, reply);
				if (!result)
					return false;
				written = result.written;
			}
			if (!adapter.onReply(std::span(reply).first(written), pending.session,
			                     pending.requestId))
				return false;
		}
		if (useRpc)
			(void)pool.process(now);
		return true;
	}

	bool stopAndDrain()
	{
		timer.stop(); // Producers and the in-process transport stop first.
		adapter.close();
		for (unsigned step = 0; step != 10 && !adapter.drained(); ++step)
			adapter.process(clock.now());
		return adapter.drained();
	}
};

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	if (argc < 3) {
		std::cerr << "Usage: telemetry_qml_example descriptor.bin config.bin [--rpc] [--smoke]\n";
		return 2;
	}
	bool useRpc = false, smoke = false;
	for (int index = 3; index < argc; ++index) {
		const QByteArray option{argv[index]};
		if (option == "--rpc")
			useRpc = true;
		else if (option == "--smoke")
			smoke = true;
		else
			return 2;
	}
	const auto descriptor = load(QString::fromLocal8Bit(argv[1]));
	const auto config = load(QString::fromLocal8Bit(argv[2]));
	const QByteArray actualDescriptor{reinterpret_cast<const char*>(cf::descriptorBytes.data()),
	                                  static_cast<qsizetype>(cf::descriptorBytes.size())};
	if (descriptor != actualDescriptor ||
	    telemetry::decode(
	        std::span<const std::byte>{reinterpret_cast<const std::byte*>(config.constData()),
	                                   static_cast<std::size_t>(config.size())},
	        client_example::device.config) != telemetry::CodecStatus::Ok)
		return 2;
	QQmlApplicationEngine engine;
	engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Telemetry/Example/Main.qml")));
	if (engine.rootObjects().isEmpty())
		return 1;
	auto* root = engine.rootObjects().first();
	root->setProperty("transportMode", useRpc ? "Resource/RPC loopback" : "Native Encoded");
	auto* api = root->findChild<qa::DeviceApi*>("deviceApi");
	if (!api)
		return 1;
	api->setInputEngine(&engine);
	Loopback loopback{*api, app, descriptor.left(64), useRpc};
	if (!api->admitDescriptor(descriptor.left(64))) {
		(void)loopback.stopAndDrain();
		return 1;
	}
	if (smoke) {
		QObject::connect(api, &qa::DeviceApi::operationFinished, &app,
		                 [&app](const QString&, const QVariantMap& result) {
			                 app.exit(result.value("success").toBool() ? 0 : 1);
		                 });
		QTimer::singleShot(5000, &app, [&app] {
			app.exit(3);
		});
	}
	api->device()->refreshConfig();
	const int result = app.exec();
	return loopback.stopAndDrain() ? result : 1;
}
