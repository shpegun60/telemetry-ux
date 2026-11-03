/*
 * Actual QML, native Encoded and Resource/RPC qualification of the generated API.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include <DemoModel.hpp>
#include <DeviceApi.hpp>
#include <QmlRpcAdapter.hpp>
#include <resource/protocol/Protocol.hpp>
#include <integration/telemetry/v3/RpcSessionPool.hpp>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QPointer>
#include <QtCore/QThread>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QSignalSpy>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace cf = codegen_fixture;
namespace ts = telemetry;
namespace rpc = resource::telemetry::v3;
namespace qa = device_api::qml;
static unsigned checks = 0;

static void check(bool ok, const char* message)
{
	++checks;
	if (!ok)
		throw std::runtime_error(message);
}

static void spin(const std::function<bool()>& done, const std::function<void()>& pump = {})
{
	QElapsedTimer timer;
	timer.start();
	while (!done() && timer.elapsed() < 7000) {
		QCoreApplication::processEvents();
		if (pump)
			pump();
		QThread::msleep(1);
	}
	check(done(), "bounded asynchronous operation timed out");
}

static QByteArray fixture(const QString& directory, const char* name)
{
	QFile file(directory + QLatin1Char('/') + QString::fromLatin1(name));
	check(file.open(QIODevice::ReadOnly), "cannot open native fixture");
	return file.readAll();
}

static QString invoke(QObject* object, const char* method)
{
	QString token;
	check(QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QString, token)), method);
	check(!token.isEmpty(), "operation token empty");
	return token;
}

static QString invoke(QObject* object, const char* method, const QVariant& value)
{
	QString token;
	check(QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QString, token),
	                                Q_ARG(QVariant, value)),
	      method);
	check(!token.isEmpty(), "operation token empty");
	return token;
}

static QString qmlInvoke(QObject* object, const char* method)
{
	QVariant value;
	check(QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, value)), method);
	check(!value.toString().isEmpty(), "QML operation token empty");
	return value.toString();
}

static QVariantMap await(QSignalSpy& spy, const QString& token,
                         const std::function<void()>& pump = {})
{
	const auto locate = [&]() {
		for (const auto& args : spy)
			if (args[0].toString() == token)
				return true;
		return false;
	};
	spin(locate, pump);
	for (qsizetype i = 0; i < spy.size(); ++i) {
		if (spy[i][0].toString() == token) {
			const auto result = spy.takeAt(i)[1].toMap();
			check(result.contains("success") && result.contains("dispatchStatus") &&
			          result.contains("endpointStatus") && result.contains("accepted"),
			      "result evidence keys");
			return result;
		}
	}
	throw std::runtime_error("missing result");
}

static void success(const QVariantMap& result)
{
	if (!result.value("success").toBool())
		std::cerr << "unexpected refusal " << result.value("errorKind").toString().toStdString()
		          << ": " << result.value("message").toString().toStdString() << '\n';
	check(result.value("success").toBool(), "expected success");
	check(result.value("dispatchStatus").toInt() == 0, "successful dispatch evidence");
}

static void refused(const QVariantMap& result, const char* kind)
{
	check(!result.value("success").toBool() && !result.value("accepted").toBool(),
	      "refused success/accepted");
	if (result.value("errorKind").toString() != QLatin1String(kind))
		std::cerr << "expected " << kind << " got "
		          << result.value("errorKind").toString().toStdString() << '\n';
	check(result.value("errorKind").toString() == QLatin1String(kind), "refusal kind");
}

struct Request {
	QString token;
	quint32 session;
	int op;
	quint32 id, requestType, responseType;
	QByteArray bytes;
};

static void direct(qa::DeviceApi& api, const Request& r)
{
	std::array<std::byte, 4096> output{};
	const auto input =
	    std::as_bytes(std::span{r.bytes.constData(), static_cast<std::size_t>(r.bytes.size())});
	ts::DispatchStatus dispatch = ts::DispatchStatus::InternalError;
	int endpoint = 0;
	std::uint32_t written = 0;
	switch (r.op) {
		case 4: {
			const auto v = ts::readFieldEncoded(cf::model.view(), r.id, output, cf::workspace);
			dispatch = v.dispatch;
			written = v.written;
			break;
		}
		case 1: {
			const auto v = ts::writeFieldEncoded(cf::model.view(), r.id, input, cf::workspace);
			dispatch = v.dispatch;
			endpoint = static_cast<int>(v.endpointStatus);
			break;
		}
		case 2: {
			const auto v = ts::executeCommandEncoded(cf::model.view(), r.id, input, cf::workspace);
			dispatch = v.dispatch;
			endpoint = static_cast<int>(v.endpointStatus);
			break;
		}
		case 3: {
			const auto v =
			    ts::callServiceEncoded(cf::model.view(), r.id, input, output, cf::workspace);
			dispatch = v.dispatch;
			endpoint = static_cast<int>(v.endpointStatus);
			written = v.written;
			break;
		}
		default:
			throw std::runtime_error("unexpected explicit operation");
	}
	api.complete(r.token, r.session, static_cast<int>(dispatch), endpoint,
	             QByteArray(reinterpret_cast<const char*>(output.data()), written));
}

static void rpcLifetimes(const QByteArray& header)
{
	// Independent sessions keep cancellation/destruction probes separate from
	// ordinary successful request execution. No native model packet is applied.
	for (int scenario = 0; scenario < 5; ++scenario) {
		std::array<std::byte, 4096> request{}, response{};
		std::array<std::byte, 64> packet{};
		::telemetry::client::rpc::RpcClient client{0, request, response, packet};
		auto api = std::make_unique<qa::DeviceApi>();
		check(api->admitDescriptor(header), "RPC lifecycle admission");
		QSignalSpy finished(api.get(), &qa::DeviceApi::operationFinished);
		auto adapter = std::make_unique<qa::RpcAdapter<>>(*api, client);
		check(adapter->beginSession() == ::telemetry::client::rpc::RpcClientStatus::Ok,
		      "RPC lifecycle session");
		api->setRequestTimeoutMs(scenario < 2 ? 20 : 5000);
		const auto token =
		    scenario == 0 ? invoke(api->device(), "refreshConfig") : invoke(api->device(), "reset");
		QCoreApplication::sendPostedEvents(api.get(), QEvent::MetaCall);
		if (scenario == 0) {
			// requestReady is queued, but the receiving adapter has not run.
			QThread::msleep(30);
			QCoreApplication::sendPostedEvents(adapter.get(), QEvent::MetaCall);
			// Force adapter-posted failures ahead of timer events on every Qt
			// version. The expired token must retain the API's timeout result.
			QCoreApplication::sendPostedEvents(api.get(), QEvent::MetaCall);
			check(!adapter->nextPacket(), "expired queued request began RPC");
			refused(await(finished, token), "Timeout");
			continue;
		}
		QCoreApplication::sendPostedEvents(adapter.get(), QEvent::MetaCall);
		check(client.active(), "RPC lifecycle request did not become active");
		if (scenario == 1) {
			QThread::msleep(30);
			check(!adapter->nextPacket(), "expired unsent RPC produced a packet");
			check(client.diagnostic().status ==
			          ::telemetry::client::rpc::RpcClientStatus::Cancelled,
			      "unsent RPC did not retain exact native cancellation evidence");
			QCoreApplication::sendPostedEvents(api.get(), QEvent::MetaCall);
			refused(await(finished, token), "OutcomeUnknown");
		} else if (scenario == 2) {
			const auto outgoing = adapter->nextPacket();
			check(bool(outgoing), "RPC final handoff packet missing");
			check(adapter->onSendAccepted(outgoing.generation, outgoing.requestId),
			      "RPC final handoff acceptance");
			adapter->loseSession(outgoing.generation);
			refused(await(finished, token), "OutcomeUnknown");
			spin([&] {
				return !api->ready();
			});
		} else if (scenario == 3) {
			api.reset();
			check(!adapter->nextPacket(), "destroyed API left a sendable RPC packet");
			check(!client.active(), "destroyed API left RPC active");
		} else {
			adapter.reset();
			refused(await(finished, token), "Transport");
			spin([&] {
				return !api->ready();
			});
		}
	}
}

static void handoffClassification(const QByteArray& header, const QByteArray& configBytes,
                                  const QByteArray& responseBytes)
{
	// The legacy default remains conservative. Opt-in callers must obtain the
	// owner-thread gate before allowing a transport to execute/send the request.
	for (bool tracking : {false, true}) {
		for (bool handed : {false, true}) {
			for (bool reset : {false, true}) {
				qa::DeviceApi api;
				check(api.admitDescriptor(header), "handoff admission");
				check(api.setTransportHandoffTracking(tracking), "set initial handoff mode");
				api.setRequestTimeoutMs(200);
				QSignalSpy finished(&api, &qa::DeviceApi::operationFinished);
				const auto token = invoke(api.device(), "reset");
				const auto epoch = api.session();
				check(!api.transportHandoffAccepted(token, epoch), "unoffered gate accepted");
				check(!api.setTransportHandoffTracking(!tracking), "pending mode changed");
				QCoreApplication::sendPostedEvents(&api, QEvent::MetaCall);
				check(api.requestActive(token, epoch), "offered request inactive");
				check(!api.transportHandoffAccepted(token, epoch + 1), "wrong-epoch gate accepted");
				if (handed) {
					check(api.transportHandoffAccepted(token, epoch), "owner handoff refused");
					check(!api.transportHandoffAccepted(token, epoch),
					      "duplicate gate authorized replay");
				}
				if (reset)
					api.resetSession();
				else {
					QThread::msleep(250);
					check(!api.transportHandoffAccepted(token, epoch), "expired gate accepted");
				}
				const bool unknown = !tracking || handed;
				refused(await(finished, token), unknown ? "OutcomeUnknown"
				                                : reset ? "Cancelled"
				                                        : "Timeout");
				check(!api.transportHandoffAccepted(token, epoch),
				      "terminal token authorized handoff");
				check(api.setTransportHandoffTracking(!tracking), "settled mode could not change");
			}
		}
	}
	{
		qa::DeviceApi owner;
		check(owner.admitDescriptor(header) && owner.setTransportHandoffTracking(true),
		      "owner-thread gate admission");
		QSignalSpy finished(&owner, &qa::DeviceApi::operationFinished);
		const auto token = invoke(owner.device(), "reset");
		const auto epoch = owner.session();
		QCoreApplication::sendPostedEvents(&owner, QEvent::MetaCall);
		bool workerGate = true, workerMode = true;
		std::thread worker([&] {
			workerGate = owner.transportHandoffAccepted(token, epoch);
			workerMode = owner.setTransportHandoffTracking(false);
		});
		worker.join();
		check(!workerGate && !workerMode, "worker bypassed owner-thread gate");
		check(owner.transportHandoffAccepted(token, epoch),
		      "owner gate refused after worker rejection");
		owner.resetSession();
		refused(await(finished, token), "OutcomeUnknown");
	}
	// A tracked read never becomes an indeterminate mutation after handoff.
	qa::DeviceApi api;
	check(api.admitDescriptor(header), "read handoff admission");
	check(api.setTransportHandoffTracking(true), "read handoff mode");
	api.setRequestTimeoutMs(200);
	QSignalSpy finished(&api, &qa::DeviceApi::operationFinished);
	const auto token = invoke(api.device(), "refreshConfig");
	QCoreApplication::sendPostedEvents(&api, QEvent::MetaCall);
	check(api.transportHandoffAccepted(token, api.session()), "read handoff gate");
	refused(await(finished, token), "Timeout");
	qa::DeviceApi seed;
	check(seed.admitDescriptor(header), "known completion input admission");
	QSignalSpy seeded(&seed, &qa::DeviceApi::operationFinished);
	const auto initial = invoke(seed.device(), "refreshConfig");
	QCoreApplication::sendPostedEvents(&seed, QEvent::MetaCall);
	seed.complete(initial, seed.session(), 0, 0, configBytes);
	const auto config = await(seeded, initial).value("value").toMap();
	check(!config.isEmpty(), "known completion typed input");
	const QVariantMap request{{"config", config}, {"multiplier", 4}};
	// Once complete() has checked the result, a later session change cannot
	// erase known success/Accepted or its value. Cache freshness is independent.
	for (bool readmit : {false, true}) {
		for (int scenario = 0; scenario < 7; ++scenario) {
			qa::DeviceApi completed;
			check(completed.admitDescriptor(header), "queued completion admission");
			QSignalSpy result(&completed, &qa::DeviceApi::operationFinished);
			QSignalSpy changed(completed.device(), &qa::Group_0Api::configChanged);
			const auto pending = scenario == 0   ? invoke(completed.device(), "reset")
			                     : scenario == 1 ? invoke(completed.edge(), "accept", QVariantMap{})
			                     : scenario == 2 ? invoke(completed.device(), "refreshConfig")
			                     : scenario == 3 ? invoke(completed.device(), "writeConfig", config)
			                     : scenario == 4 ? invoke(completed.device(), "inspect", request)
			                     : scenario == 5 ? invoke(completed.edge(), "good")
			                                     : invoke(completed.device(), "ping");
			const auto epoch = completed.session();
			QCoreApplication::sendPostedEvents(&completed, QEvent::MetaCall);
			completed.complete(pending, epoch, 0, scenario == 1 ? 1 : 0,
			                   scenario == 2   ? configBytes
			                   : scenario == 4 ? responseBytes
			                                   : QByteArray{});
			completed.resetSession();
			if (readmit)
				check(completed.admitDescriptor(header), "new peer admitted before old delivery");
			const auto evidence = await(result, pending);
			if (scenario == 1) {
				check(!evidence.value("success").toBool() && evidence.value("accepted").toBool() &&
				          evidence.value("errorKind").toString().isEmpty(),
				      "known Accepted survives reset without claiming Executed");
			} else {
				success(evidence);
			}
			check(evidence.value("dispatchStatus").toInt() == 0 &&
			          evidence.value("endpointStatus").toInt() == (scenario == 1 ? 1 : 0),
			      "queued completion lost exact native status evidence");
			if (scenario == 2)
				check(evidence.value("value").toMap() == config,
				      "known read retains checked value");
			if (scenario == 4)
				check(evidence.value("value").toMap().value("config").toMap() == config &&
				          evidence.value("value").toMap().value("checksum").toUInt() == 263168,
				      "known Service retains checked response value");
			if (scenario == 5)
				check(evidence.value("value").isValid() &&
				          evidence.value("value").toMap().isEmpty(),
				      "known empty Service keeps value distinct from Void");
			if (scenario == 6)
				check(!evidence.value("value").isValid(), "known Void Service keeps no value");
			check(!completed.device()->configValid() && !completed.device()->config().isValid() &&
			          changed.isEmpty(),
			      "old completion has no new-session cache side effect");
			QCoreApplication::sendPostedEvents(&completed, QEvent::MetaCall);
			check(result.isEmpty(), "checked old result delivered exactly once");
		}
	}
}

static void knownWriteCannotClearNewCache(const QByteArray& header, const QByteArray& configBytes)
{
	qa::DeviceApi api;
	check(api.admitDescriptor(header), "old write cache admission");
	QSignalSpy finished(&api, &qa::DeviceApi::operationFinished);
	const auto seed = invoke(api.device(), "refreshConfig");
	QCoreApplication::sendPostedEvents(&api, QEvent::MetaCall);
	api.complete(seed, api.session(), 0, 0, configBytes);
	const auto config = await(finished, seed).value("value");
	const auto oldWrite = invoke(api.device(), "writeConfig", config);
	QCoreApplication::sendPostedEvents(&api, QEvent::MetaCall);
	api.complete(oldWrite, api.session(), 0, 0, {});
	api.resetSession();
	check(api.admitDescriptor(header), "fresh read cache admission");
	QSignalSpy changed(api.device(), &qa::Group_0Api::configChanged);

	struct BeforeDelivery final : QObject {
		std::function<void()> before;

		bool eventFilter(QObject*, QEvent* event) override
		{
			if (event->type() == QEvent::MetaCall && before) {
				auto action = std::exchange(before, {});
				action();
			}
			return false;
		}
	} filter;

	QVariant freshValue;
	filter.before = [&] {
		// The old write delivery is already on the event stack. Nested delivery
		// can populate a fresh peer cache before that old finish callback runs.
		auto native = client_example::initial;
		native.gain = 3.5f;
		std::array<std::byte, ts::wireSize<client_example::Config>> bytes{};
		check(ts::encode(native, bytes) == ts::CodecStatus::Ok, "fresh native config encoding");
		const auto read = invoke(api.device(), "refreshConfig");
		QCoreApplication::sendPostedEvents(&api, QEvent::MetaCall);
		api.complete(read, api.session(), 0, 0,
		             QByteArray(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
		QCoreApplication::sendPostedEvents(&api, QEvent::MetaCall);
		check(api.device()->configValid() && changed.size() == 1,
		      "fresh cache exists before old write finishes");
		freshValue = api.device()->config();
		check(freshValue.toMap().value("gain").toFloat() == 3.5f,
		      "fresh peer cache has a distinct checked value");
	};
	api.installEventFilter(&filter);
	QCoreApplication::sendPostedEvents(&api, QEvent::MetaCall);
	api.removeEventFilter(&filter);
	check(!filter.before, "old delivery interception executed");
	check(api.device()->configValid() && api.device()->config() == freshValue &&
	          changed.size() == 1,
	      "old successful write cannot invalidate new-session cache");
	success(await(finished, oldWrite));
}

static void knownRpcReplyBeforeDelivery(const QByteArray& header)
{
	for (int scenario = 0; scenario != 3; ++scenario) {
		qa::DeviceApi api;
		check(api.admitDescriptor(header), "known RPC result admission");
		std::array<std::byte, 4096> request{}, response{}, serverRequest{}, serverResponse{};
		std::array<std::byte, 64> packet{}, copied{}, reply{};
		constexpr auto view = cf::model.view();
		rpc::RpcMailbox mailbox{view, cf::workspace, serverRequest, serverResponse};
		resource::FileSystem files{std::array{resource::file("/rpc", mailbox)}};
		::telemetry::client::rpc::RpcClient native{0, request, response, packet};
		qa::RpcAdapter<> adapter{api, native};
		check(adapter.beginSession() == ::telemetry::client::rpc::RpcClientStatus::Ok,
		      "known RPC result session");
		QSignalSpy finished(&api, &qa::DeviceApi::operationFinished);
		const auto token = scenario == 0   ? invoke(api.device(), "reset")
		                   : scenario == 1 ? invoke(api.edge(), "accept", QVariantMap{})
		                                   : invoke(api.edge(), "good");
		for (std::uint32_t now = 0; now != 400 && !native.completed(); ++now) {
			QCoreApplication::sendPostedEvents(&api, QEvent::MetaCall);
			QCoreApplication::sendPostedEvents(&adapter, QEvent::MetaCall);
			adapter.process(now);
			mailbox.process(now);
			if (const auto next = adapter.nextPacket()) {
				std::copy(next.bytes.begin(), next.bytes.end(), copied.begin());
				check(adapter.prepareHandoff(next.generation, next.requestId),
				      "known RPC permission");
				check(adapter.onSendAccepted(next.generation, next.requestId), "known RPC handoff");
				const auto result = resource::protocol::process(
				    files.view(), resource::Input{copied}.first(next.bytes.size()), reply);
				check(result.status == resource::Status::Ok, "known RPC server reply");
				mailbox.process(now);
				check(adapter.onReply(resource::Input{reply}.first(result.written), next.generation,
				                      next.requestId),
				      "known RPC checked reply");
			}
		}
		check(native.completed() && finished.isEmpty(),
		      "RPC checked result precedes queued public notification");
		api.resetSession();
		const auto result = await(finished, token);
		if (scenario == 1)
			check(result.value("accepted").toBool() && !result.value("success").toBool() &&
			          result.value("errorKind").toString().isEmpty(),
			      "RPC checked Accepted survives reset before public notification");
		else
			success(result);
	}
}

static void pooledRecovery(QQmlEngine& engine, const QByteArray& header)
{
	// Use QML-created backends and QML-issued non-idempotent commands, with the
	// actual native client and ACK-gated server pool, rather than a fake reply.
	for (int scenario = 0; scenario < 5; ++scenario) {
		const bool tracking = scenario == 4 || (scenario & 1) != 0;
		const bool reset = (scenario & 2) != 0;
		const bool executed = scenario == 4;
		QQmlComponent component(&engine);
		component.setData(R"QML(import QtQml
import Telemetry.Generated.device_api
QtObject {
    property DeviceApi api: DeviceApi {}
    function command() { return api.device.reset() }
}

)QML",
		                  QUrl());
		std::unique_ptr<QObject> root(component.create());
		if (!root)
			std::cerr << component.errorString().toStdString();
		check(bool(root), "recovery QML component creation");
		auto* api = qobject_cast<qa::DeviceApi*>(root->property("api").value<QObject*>());
		check(api != nullptr, "recovery QML backend");
		check(api->admitDescriptor(header), "pool schema admission");
		api->setRequestTimeoutMs(200);
		QSignalSpy finished(api, &qa::DeviceApi::operationFinished);
		std::array<std::byte, 4096> serverRequest{}, serverResponse{}, request{}, response{};
		std::array<std::byte, 64> packetStorage{}, reply{}, oldPacket{};
		constexpr auto view = cf::model.view();
		rpc::RpcMailbox mailbox{view, cf::workspace, serverRequest, serverResponse};
		resource::FileSystem files{std::array{resource::file("/rpc", mailbox)}};
		rpc::RpcSessionPool<1> pool{std::array{rpc::RpcSessionBinding{mailbox, files.view()}}};
		const rpc::RpcConnectionKey connection{17, 1, 0};
		const auto admitted = pool.acquire(connection);
		check(admitted.status == rpc::RpcSessionStatus::Ok, "pool initial acquire");
		auto handle = admitted.session;
		::telemetry::client::rpc::RpcClientOptions options;
		options.ackTimeoutMs = 5;
		::telemetry::client::rpc::RpcClient client{0, request, response, packetStorage, options};
		qa::RpcAdapter adapter{*api, client};
		if (tracking)
			check(adapter.enableHandoffTracking(), "adapter handoff opt-in");
		check(adapter.beginSession() == ::telemetry::client::rpc::RpcClientStatus::Ok,
		      "pool client session");
		const auto callsBefore = client_example::device.calls;
		const auto token = qmlInvoke(root.get(), "command");
		QCoreApplication::sendPostedEvents(api, QEvent::MetaCall);
		QCoreApplication::sendPostedEvents(&adapter, QEvent::MetaCall);
		const auto outgoing = adapter.nextPacket();
		check(bool(outgoing), "pool command packet");
		check(outgoing.bytes.size() == 40 && outgoing.bytes[13] == std::byte{1},
		      "void command did not use one final WRITE");
		check(adapter.prepareHandoff(outgoing.generation, outgoing.requestId),
		      "prepare final WRITE");
		check(adapter.prepareHandoff(outgoing.generation, outgoing.requestId),
		      "same packet preparation not retry-safe");
		std::copy(outgoing.bytes.begin(), outgoing.bytes.end(), oldPacket.begin());
		const auto oldSize = outgoing.bytes.size();
		const auto oldGeneration = outgoing.generation;
		const auto oldRequestId = outgoing.requestId;
		check(pool.process(0).dispatched == 0, "pool dispatched before ingress");
		const auto held = pool.handle(handle, outgoing.bytes, reply);
		check(held.status == rpc::RpcSessionStatus::Ok &&
		          held.reply.status == resource::Status::Ok && held.ticket.serial != 0,
		      "final WRITE did not produce ACK ticket");
		check(adapter.onSendAccepted(oldGeneration, oldRequestId), "final WRITE handoff evidence");
		adapter.process(6);
		const auto retry = adapter.nextPacket();
		check(bool(retry) && retry.bytes.size() == oldSize &&
		          std::equal(retry.bytes.begin(), retry.bytes.end(), oldPacket.begin()),
		      "native final WRITE retry changed request bytes");
		check(adapter.prepareHandoff(retry.generation, retry.requestId),
		      "one-shot API gate prevented same-transaction transport retry");
		const auto retryAck = pool.handle(handle, retry.bytes, reply);
		check(retryAck.reply.status == resource::Status::Ok && retryAck.ticket.serial != 0 &&
		          retryAck.ticket != held.ticket,
		      "final WRITE retry did not replace ACK ticket");
		check(adapter.onSendAccepted(retry.generation, retry.requestId),
		      "native retry handoff evidence");
		// Do not call replyAccepted, and do not deliver this WRITE ACK to the
		// client: transport has not accepted the server's reply storage.
		check(mailbox.state() == ::telemetry::protocol::rpc::RpcState::Pending,
		      "withheld ACK not Pending");
		check(pool.process(100000).dispatched == 0 &&
		          mailbox.state() == ::telemetry::protocol::rpc::RpcState::Pending,
		      "Pending mailbox aged out or dispatched without ACK");
		check(client_example::device.calls == callsBefore, "withheld ACK executed command");
		const auto existing = pool.acquire(connection);
		check(existing.status == rpc::RpcSessionStatus::Existing && existing.session == handle,
		      "same-key acquire unexpectedly reopened Pending session");
		// Concrete pre-fix liveness condition: a larger transaction in this
		// same admitted server context is rejected while the old one is Pending.
		auto newPacket = oldPacket;
		const auto transaction =
		    resource::protocol::client::detail::load<std::uint32_t>(std::span{oldPacket}, 24);
		resource::protocol::client::detail::store(std::span{newPacket}, 24, transaction + 1);
		const auto blocked = pool.handle(handle, std::span{newPacket}.first(oldSize), reply);
		check(blocked.status == rpc::RpcSessionStatus::Ok &&
		          blocked.reply.status == resource::Status::InvalidData,
		      "higher transaction bypassed retained Pending request");
		check(pool.process(200000).dispatched == 0 && client_example::device.calls == callsBefore,
		      "rejected next transaction executed a command");
		if (executed) {
			check(pool.replyAccepted(handle, retryAck.ticket) == rpc::RpcSessionStatus::Ok,
			      "old final ACK transport acceptance");
			check(pool.process(200001).dispatched == 1 &&
			          mailbox.state() == ::telemetry::protocol::rpc::RpcState::Ready,
			      "accepted old final ACK did not execute server command");
		}
		const auto callsAfterOld = callsBefore + (executed ? 1u : 0u);
		check(client_example::device.calls == callsAfterOld, "old command effect evidence");
		if (reset)
			api->resetSession();
		else
			QThread::msleep(250);
		check(!adapter.nextPacket(), "abandoned Pending request retained a sendable packet");
		refused(await(finished, token), "OutcomeUnknown");
		spin([&] {
			return !api->ready();
		});
		check(adapter.recoveryRequired() && adapter.recoveryGeneration() == oldGeneration,
		      "abandoned final WRITE did not require exact session recovery");
		check(!adapter.confirmSessionClosed(oldGeneration + 1),
		      "wrong recovery generation accepted");
		check(!adapter.onReply(std::span{reply}.first(held.reply.written), oldGeneration,
		                       oldRequestId),
		      "quarantined old reply accepted");
		check(api->admitDescriptor(header), "local-only re-admission");
		check(adapter.beginSession() == ::telemetry::client::rpc::RpcClientStatus::NoSession &&
		          !adapter.nextPacket(),
		      "local generation alone cleared remote recovery obligation");
		refused(await(finished, qmlInvoke(root.get(), "command")), "Transport");
		check(mailbox.state() == (executed ? ::telemetry::protocol::rpc::RpcState::Ready
		                                   : ::telemetry::protocol::rpc::RpcState::Pending) &&
		          client_example::device.calls == callsAfterOld,
		      "quarantine replayed or reset the remote command");
		// The host excludes/drains ingress for the old connection before this
		// release. Old bytes are never relabelled as a new admitted connection.
		const auto oldHandle = handle;
		check(pool.release(oldHandle) == rpc::RpcSessionStatus::Ok, "release old remote session");
		check(mailbox.state() == ::telemetry::protocol::rpc::RpcState::Idle,
		      "release did not clear Pending mailbox");
		const auto fresh = pool.acquire({17, 2, 0});
		check(fresh.status == rpc::RpcSessionStatus::Ok &&
		          fresh.session.generation != oldHandle.generation,
		      "fresh remote handle not acquired");
		handle = fresh.session;
		check(pool.handle(oldHandle, std::span{oldPacket}.first(oldSize), reply).status ==
		          rpc::RpcSessionStatus::InvalidSession,
		      "old remote handle admitted delayed frame");
		check(pool.replyAccepted(handle, held.ticket) == rpc::RpcSessionStatus::StaleReply,
		      "old ACK ticket opened new session dispatch gate");
		check(adapter.confirmSessionClosed(oldGeneration) && !adapter.recoveryRequired(),
		      "explicit remote closure confirmation");
		check(!adapter.confirmSessionClosed(oldGeneration),
		      "duplicate closure confirmation accepted");
		check(api->admitDescriptor(header), "fresh peer descriptor admission");
		check(adapter.beginSession() == ::telemetry::client::rpc::RpcClientStatus::Ok,
		      "fresh client session");
		check(!adapter.nextPacket() && client_example::device.calls == callsAfterOld,
		      "session recovery automatically replayed abandoned command");
		api->setRequestTimeoutMs(5000);
		quint32 now = 200001;
		const auto pump = [&] {
			adapter.process(now++);
			(void)pool.process(now);
			if (const auto packet = adapter.nextPacket()) {
				check(adapter.prepareHandoff(packet.generation, packet.requestId),
				      "fresh packet preparation");
				const auto result = pool.handle(handle, packet.bytes, reply);
				check(result.status == rpc::RpcSessionStatus::Ok, "fresh pool ingress");
				check(adapter.onSendAccepted(packet.generation, packet.requestId),
				      "fresh request handoff");
				if (result.ticket.serial)
					check(pool.replyAccepted(handle, result.ticket) == rpc::RpcSessionStatus::Ok,
					      "fresh final ACK handoff");
				(void)pool.process(now);
				check(adapter.onReply(std::span{reply}.first(result.reply.written),
				                      packet.generation, packet.requestId),
				      "fresh RPC reply");
			}
		};
		adapter.process(now);
		const auto freshToken = qmlInvoke(root.get(), "command");
		check(freshToken != token, "recovery reused abandoned user token");
		success(await(finished, freshToken, pump));
		check(client_example::device.calls == callsAfterOld + 1,
		      "fresh user command not executed exactly once");
		for (int i = 0; i < 5; ++i) {
			QCoreApplication::processEvents();
			pump();
		}
		check(client_example::device.calls == callsAfterOld + 1, "completion replayed command");
	}
}

static void trackedAdapterHandoffs(const QByteArray& header)
{
	for (int scenario = 0; scenario < 3; ++scenario) {
		std::array<std::byte, 4096> request{}, response{};
		std::array<std::byte, 64> packet{};
		::telemetry::client::rpc::RpcClient client{0, request, response, packet};
		qa::DeviceApi api;
		check(api.admitDescriptor(header), "tracked adapter admission");
		api.setRequestTimeoutMs(200);
		QSignalSpy finished(&api, &qa::DeviceApi::operationFinished);
		qa::RpcAdapter adapter{api, client};
		check(adapter.enableHandoffTracking(), "tracked adapter enable");
		check(adapter.beginSession() == ::telemetry::client::rpc::RpcClientStatus::Ok,
		      "tracked adapter session");
		const auto token = invoke(api.device(), "reset");
		QCoreApplication::sendPostedEvents(&api, QEvent::MetaCall);
		QCoreApplication::sendPostedEvents(&adapter, QEvent::MetaCall);
		const auto offered = adapter.nextPacket();
		check(bool(offered), "tracked adapter offered packet");
		if (scenario == 1) {
			// A caller that reports actual handoff without preparing violated the
			// opt-in contract; that cannot erase already accepted native evidence.
			check(!adapter.onSendAccepted(offered.generation, offered.requestId),
			      "unprepared handoff reported valid");
		} else {
			if (scenario == 2)
				check(adapter.prepareHandoff(offered.generation, offered.requestId),
				      "prepared transport interval");
			QThread::msleep(250);
			check(!adapter.prepareHandoff(offered.generation, offered.requestId),
			      "expired packet gate authorized send");
			check(!adapter.nextPacket(), "expired tracked token left sendable packet");
		}
		refused(await(finished, token), scenario == 0 ? "Timeout" : "OutcomeUnknown");
		check(adapter.recoveryRequired() == (scenario != 0),
		      "tracked adapter recovery classification");
		if (scenario == 0)
			check(api.ready() && client.diagnostic().status ==
			                         ::telemetry::client::rpc::RpcClientStatus::Cancelled,
			      "certainly unsent timeout damaged current admitted session");
		else
			spin([&] {
				return !api.ready();
			});
	}
	// Reconstructing an adapter around retained native state must not silently
	// forget a previous unknown transaction merely because its Api token is gone.
	std::array<std::byte, 4096> request{}, response{};
	std::array<std::byte, 64> packet{};
	::telemetry::client::rpc::RpcClient client{0, request, response, packet};
	check(client.startSession(1) == ::telemetry::client::rpc::RpcClientStatus::Ok,
	      "retained native session");
	check(client.beginCommand(::telemetry::client::rpc::CommandEndpoint<void>{
	          1, ::telemetry::client::rpc::trustedRpcMapping}) ==
	          ::telemetry::client::rpc::RpcClientStatus::Ok,
	      "retained native command");
	const auto outgoing = client.nextPacket();
	check(bool(outgoing) && client.onSendAccepted(outgoing.generation, outgoing.requestId),
	      "retained final native handoff");
	client.loseSession(outgoing.generation);
	check(client.status() == ::telemetry::client::rpc::RpcClientStatus::OutcomeUnknown,
	      "retained native unknown evidence");
	qa::DeviceApi api;
	check(api.admitDescriptor(header), "retained API admission");
	qa::RpcAdapter adapter{api, client};
	check(adapter.recoveryRequired() && adapter.recoveryGeneration() == 0 && !adapter.nextPacket(),
	      "new adapter bypassed retained native recovery obligation");
	spin([&] {
		return !api.ready();
	});
	check(!adapter.confirmSessionClosed(1) && adapter.confirmSessionClosed(0),
	      "unknown-generation recovery sentinel");
	check(api.admitDescriptor(header) &&
	          adapter.beginSession() == ::telemetry::client::rpc::RpcClientStatus::Ok,
	      "retained-state explicit closure could not reopen session");
}

static void admissionReentrancy(const QByteArray& header)
{
	for (const bool validInner : {false, true}) {
		qa::DeviceApi api;
		bool nested = false, inner = !validInner;
		QObject::connect(
		    &api, &qa::DeviceApi::sessionChanged, &api,
		    [&] {
			    if (!nested) {
				    nested = true;
				    inner = api.admitDescriptor(validInner ? header : QByteArray(64, '\0'));
			    }
		    },
		    Qt::DirectConnection);
		check(!api.admitDescriptor(header), "outer admission survived newer admission attempt");
		check(nested && inner == validInner, "nested admission result");
		check(api.session() == 2 && api.ready() == validInner,
		      "outer admission changed newer state");
		check(api.admitDescriptor(header) && api.ready() && api.session() == 3,
		      "fresh admission after nested attempt");
	}
	{
		qa::DeviceApi api;
		bool nested = false;
		QObject::connect(
		    &api, &qa::DeviceApi::sessionChanged, &api,
		    [&] {
			    if (!nested) {
				    nested = true;
				    api.resetSession();
			    }
		    },
		    Qt::DirectConnection);
		check(!api.admitDescriptor(header), "outer admission survived newer reset");
		check(nested, "session notification did not run synchronously");
		check(api.session() == 2 && !api.ready(), "outer admission reopened reset session");
		check(api.admitDescriptor(header) && api.session() == 3, "fresh admission after reset");
	}
	{
		qa::DeviceApi api;
		QObject::connect(
		    &api, &qa::DeviceApi::readyChanged, &api,
		    [&] {
			    if (api.ready())
				    api.resetSession();
		    },
		    Qt::DirectConnection);
		check(!api.admitDescriptor(header), "admission reported ready after reset notification");
		check(api.session() == 2 && !api.ready(), "ready notification reset was overwritten");
	}
	{
		auto* victim = new qa::DeviceApi;
		QPointer<qa::DeviceApi> alive(victim);
		QObject::connect(
		    victim, &qa::DeviceApi::readyChanged, victim,
		    [victim] {
			    if (victim->ready())
				    delete victim;
		    },
		    Qt::DirectConnection);
		check(!victim->admitDescriptor(header) && !alive, "admission survived object deletion");
	}
}

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	try {
		std::cout << "Qt runtime=" << qVersion() << " compiler=" << __VERSION__ << '\n';
		check(argc == 2, "expected native fixture directory");
		const auto directory = QString::fromLocal8Bit(argv[1]);
		const auto descriptor = fixture(directory, "descriptor.bin");
		const auto configBytes = fixture(directory, "config.bin");
		const auto requestBytes = fixture(directory, "request.bin");
		const auto responseBytes = fixture(directory, "response.bin");
		QQmlEngine engine;
		QQmlComponent component(&engine);
		component.loadUrl(QUrl(QStringLiteral("qrc:/qt/qml/Telemetry/Tests/Smoke.qml")));
		if (component.isError())
			std::cerr << component.errorString().toStdString();
		std::unique_ptr<QObject> root(component.create());
		check(root != nullptr, "actual generated QML module failed to instantiate");
		auto* api = qobject_cast<qa::DeviceApi*>(root->property("api").value<QObject*>());
		check(api != nullptr, "QML instantiated wrong API type");
		api->setInputEngine(&engine);
		auto* device = api->device();
		auto* edge = api->edge();
		QSignalSpy finished(api, &qa::DeviceApi::operationFinished);
		QSignalSpy cacheChanges(device, &qa::Group_0Api::configChanged);
		check(finished.isValid() && cacheChanges.isValid(), "signal metadata");
		QList<Request> requests;
		bool automatic = true;
		auto connection = QObject::connect(
		    api, &qa::DeviceApi::requestReady, api,
		    [&](QString token, quint32 session, int op, quint32 id, quint32 q, quint32 p,
		        QByteArray bytes) {
			    requests.append({token, session, op, id, q, p, bytes});
			    if (automatic && api->requestActive(token, session))
				    direct(*api, requests.last());
		    },
		    Qt::QueuedConnection);
		refused(await(finished, qmlInvoke(root.get(), "refresh")), "Schema");
		check(requests.isEmpty(), "unadmitted API dispatched");
		check(!api->admitDescriptor(descriptor.left(63)), "short header admitted");
		for (int i = 0; i < 64; ++i) {
			auto bad = descriptor.left(64);
			bad[i] = char(static_cast<unsigned char>(bad[i]) ^ 1);
			check(!api->admitDescriptor(bad), "corrupted declared header admitted");
		}
		check(api->admitDescriptor(descriptor.left(64)) && api->ready(), "valid admission");
		success(await(finished, qmlInvoke(root.get(), "refresh")));
		check(device->configValid() && root->property("cacheValid").toBool(),
		      "cache validity binding");
		const auto config = device->config().toMap();
		check(config.value("serial").typeId() == QMetaType::QString &&
		          config.value("serial").toString() == "18446744073709551615",
		      "u64 QString exactness");
		check(config.value("offset").toString() == "-9223372036854775808", "i64 QString exactness");
		check(config.value("gain").toFloat() == 0 && std::signbit(config.value("gain").toFloat()),
		      "F32 negative zero");
		QVariant answer;
		check(QMetaObject::invokeMethod(root.get(), "wideExact", Q_RETURN_ARG(QVariant, answer)) &&
		          answer.toBool(),
		      "QML u64 exactness");
		check(
		    QMetaObject::invokeMethod(root.get(), "negativeZero", Q_RETURN_ARG(QVariant, answer)) &&
		        answer.toBool(),
		    "QML negative zero");
		const auto count = requests.size();
		for (int i = 0; i < 1000; ++i) {
			(void)device->config();
			(void)device->configValid();
		}
		QQuickWindow window;
		window.resize(64, 64);
		QSignalSpy frames(&window, &QQuickWindow::frameSwapped);
		qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());
		window.show();
		spin([&] {
			return !frames.isEmpty();
		});
		window.hide();
		check(requests.size() == count && cacheChanges.size() == 1,
		      "property getter performed I/O or spurious NOTIFY");
		success(await(finished, qmlInvoke(root.get(), "write")));
		check(requests.last().op == 1 && requests.last().id == 0 &&
		          requests.last().bytes == configBytes,
		      "QML object input differs from native wire bytes");
		check(!device->configValid(), "successful write must invalidate cache");
		success(await(finished, invoke(device, "apply", config)));
		check(requests.last().op == 2 && requests.last().bytes == configBytes,
		      "command native bytes");
		success(await(finished, invoke(device, "reset")));
		QVariantMap request{{"config", config}, {"multiplier", 4}};
		success(await(finished, invoke(device, "inspect", request)));
		check(requests.last().op == 3 && requests.last().bytes == requestBytes,
		      "service native bytes");
		for (int status = 0; status <= 3; ++status) {
			request["multiplier"] = status;
			const auto r = await(finished, invoke(device, "inspect", request));
			refused(r, "Endpoint");
			check(r.value("endpointStatus").toInt() == (status == 0   ? 1
			                                            : status == 1 ? 3
			                                            : status == 2 ? 2
			                                                          : 4),
			      "raw endpoint evidence");
		}
		request["multiplier"] = 4;
		const auto offline = await(finished, invoke(device, "offline", request));
		refused(offline, "Dispatch");
		check(offline.value("dispatchStatus").toInt() == 9, "late binding evidence");
		success(await(finished, invoke(device, "ping")));
		success(await(finished, invoke(edge, "good")));
		refused(await(finished, invoke(edge, "fail_2")), "Endpoint");
		success(await(finished, invoke(edge, "consume", QVariantMap{})));
		const auto accepted = await(finished, invoke(edge, "accept", QVariantMap{}));
		check(!accepted.value("success").toBool() && accepted.value("accepted").toBool() &&
		          accepted.value("errorKind").toString().isEmpty(),
		      "Accepted work evidence lost");
		success(await(finished, invoke(edge, "refreshScalars")));
		const auto scalars = edge->scalars().toMap();
		check(scalars.value("u64").toString() == "18446744073709551615" &&
		          scalars.value("i64").toString() == "-9223372036854775808",
		      "all scalar 64bit strings");
		check(std::isinf(scalars.value("f64").toDouble()), "IEEE infinity lost");
		auto before = requests.size();
		for (const auto method :
		     {"badWide", "throwingGetter", "typedArray", "sparseArray", "cycle"})
			refused(await(finished, qmlInvoke(root.get(), method)), "Validation");
		for (const auto& invalid : {QVariant{42.0}, QVariant{"18446744073709551616"},
		                            QVariant{"01"}, QVariant{"+1"}, QVariant{" 1"}}) {
			auto bad = config;
			bad["serial"] = invalid;
			refused(await(finished, invoke(device, "writeConfig", bad)), "Validation");
		}
		for (const auto& invalid : {QVariant{"true"}, QVariant{1}}) {
			auto bad = config;
			bad["enabled"] = invalid;
			refused(await(finished, invoke(device, "writeConfig", bad)), "Validation");
		}
		auto bad = config;
		bad["gain"] = 1e100;
		refused(await(finished, invoke(device, "writeConfig", bad)), "Validation");
		check(requests.size() == before, "validation refusal dispatched");
		success(await(finished, qmlInvoke(root.get(), "getterOnce")));
		check(requests.last().bytes == configBytes, "QML getter read repeatedly during encoding");
		before = requests.size();
		refused(await(finished, qmlInvoke(root.get(), "getterReset")), "Validation");
		check(requests.size() == before && !api->ready(),
		      "getter session change dispatched to another peer");
		check(api->admitDescriptor(descriptor.left(64)), "readmission after getter session change");
		auto ieee = config;
		ieee["gain"] = std::numeric_limits<double>::quiet_NaN();
		success(await(finished, invoke(device, "writeConfig", ieee)));
		success(await(finished, invoke(device, "refreshConfig")));
		check(std::isnan(device->config().toMap().value("gain").toDouble()), "NaN lost");
		success(await(finished, invoke(device, "writeConfig", config)));

		automatic = false;
		const auto pending = invoke(device, "refreshConfig");
		spin([&] {
			return requests.last().token == pending;
		});
		refused(await(finished, invoke(device, "refreshConfig")), "Busy");
		api->complete(pending, api->session(), 0, 0, QByteArray(1, '\0'));
		refused(await(finished, pending), "Codec");
		const auto boolToken = invoke(device, "refreshConfig");
		spin([&] {
			return requests.last().token == boolToken;
		});
		auto malformedBool = configBytes;
		malformedBool[28] = 2;
		api->complete(boolToken, api->session(), 0, 0, malformedBool);
		refused(await(finished, boolToken), "Codec");
		const auto stale = invoke(device, "refreshConfig");
		spin([&] {
			return requests.last().token == stale;
		});
		const auto oldSession = api->session();
		api->resetSession();
		refused(await(finished, stale), "Cancelled");
		check(!device->configValid() && !api->ready(), "reset cache/session invalidation");
		check(api->admitDescriptor(descriptor.left(64)) && api->session() != oldSession,
		      "session renewal");
		const auto fresh = invoke(device, "refreshConfig");
		spin([&] {
			return requests.last().token == fresh;
		});
		api->complete(fresh, oldSession, 0, 0, configBytes);
		QCoreApplication::processEvents();
		check(!device->configValid(), "stale session completed new request");
		api->complete(fresh, api->session(), 0, 0, configBytes);
		success(await(finished, fresh));
		api->complete(fresh, api->session(), 0, 0, configBytes);
		QCoreApplication::processEvents();
		check(finished.isEmpty(), "duplicate completion emitted");
		const auto threaded = invoke(device, "refreshConfig");
		spin([&] {
			return requests.last().token == threaded;
		});
		const auto threadEpoch = api->session();
		const auto notificationsBeforeThread = cacheChanges.size();
		std::thread worker([api, threaded, threadEpoch, configBytes] {
			api->complete(threaded, threadEpoch, 0, 0, configBytes);
		});
		worker.join();
		success(await(finished, threaded));
		check(cacheChanges.size() == notificationsBeforeThread + 1,
		      "worker completion failed to queue cache NOTIFY to owner");
		api->setRequestTimeoutMs(20);
		refused(await(finished, invoke(device, "refreshConfig")), "Timeout");
		refused(await(finished, invoke(device, "reset")), "OutcomeUnknown");
		// Continuous submissions must not restart the deadline timer of old work.
		const auto starvation = invoke(device, "refreshConfig");
		auto quickConnection = QObject::connect(
		    api, &qa::DeviceApi::requestReady, api,
		    [api](const QString& token, quint32 epoch, int op, quint32, quint32, quint32,
		          const QByteArray&) {
			    if (op == 2 && api->requestActive(token, epoch))
				    api->complete(token, epoch, 0, 0, {});
		    },
		    Qt::QueuedConnection);
		QTimer submissions;
		submissions.setInterval(1);
		QObject::connect(&submissions, &QTimer::timeout, api, [device] {
			device->reset();
		});
		submissions.start();
		QElapsedTimer elapsed;
		elapsed.start();
		refused(await(finished, starvation), "Timeout");
		check(elapsed.elapsed() < 500, "new requests starved old deadline");
		submissions.stop();
		QObject::disconnect(quickConnection);
		QCoreApplication::processEvents();
		finished.clear();
		api->setRequestTimeoutMs(5000);
		QObject::disconnect(connection);

		QPointer<qa::DeviceApi> victim = new qa::DeviceApi;
		check(victim->admitDescriptor(descriptor.left(64)), "deletion probe admission");
		QObject::connect(
		    victim, &qa::DeviceApi::requestReady, victim,
		    [victim, configBytes](QString token, quint32 epoch, int, quint32, quint32, quint32,
		                          QByteArray) {
			    if (victim)
				    victim->complete(token, epoch, 0, 0, configBytes);
		    },
		    Qt::QueuedConnection);
		QObject::connect(victim->device(), &qa::Group_0Api::configChanged, victim, [victim] {
			delete victim.data();
		});
		victim->device()->refreshConfig();
		spin([&] {
			return victim.isNull();
		});

		std::array<std::byte, 4096> req{}, res{}, clientReq{}, clientRes{};
		std::array<std::byte, 64> packetStorage{}, reply{};
		constexpr auto view = cf::model.view();
		rpc::RpcMailbox mailbox{view, cf::workspace, req, res};
		resource::FileSystem files{std::array{resource::file("/rpc", mailbox)}};
		::telemetry::client::rpc::RpcClient client{0, clientReq, clientRes, packetStorage};
		qa::RpcAdapter adapter{*api, client};
		check(adapter.beginSession() == ::telemetry::client::rpc::RpcClientStatus::Ok,
		      "RPC session begin");
		quint32 now = 0;
		unsigned packets = 0;
		const auto pump = [&] {
			adapter.process(now++);
			if (const auto packet = adapter.nextPacket()) {
				++packets;
				check(adapter.onSendAccepted(packet.generation, packet.requestId),
				      "RPC send acceptance");
				const auto result = resource::protocol::process(files.view(), packet.bytes, reply);
				mailbox.process(now);
				check(!adapter.onReply(std::span{reply}.first(result.written),
				                       packet.generation + 1, packet.requestId),
				      "wrong-generation RPC reply accepted");
				check(adapter.onReply(std::span{reply}.first(result.written), packet.generation,
				                      packet.requestId),
				      "real Resource RPC reply refused");
			}
		};
		const auto rpcResult = [&](const QString& token) {
			return await(finished, token, pump);
		};
		success(rpcResult(invoke(device, "refreshConfig")));
		success(rpcResult(qmlInvoke(root.get(), "write")));
		success(rpcResult(invoke(device, "apply", config)));
		success(rpcResult(invoke(device, "inspect", request)));
		for (int status = 0; status < 4; ++status) {
			request["multiplier"] = status;
			const auto r = rpcResult(invoke(device, "inspect", request));
			refused(r, "Endpoint");
			check(r.value("endpointStatus").toInt() == (status == 0   ? 1
			                                            : status == 1 ? 3
			                                            : status == 2 ? 2
			                                                          : 4),
			      "RPC endpoint evidence");
		}
		request["multiplier"] = 4;
		success(rpcResult(invoke(device, "ping")));
		success(rpcResult(invoke(edge, "good")));
		refused(rpcResult(invoke(edge, "fail_2")), "Endpoint");
		success(rpcResult(invoke(edge, "consume", QVariantMap{})));
		const auto rpcAccepted = rpcResult(invoke(edge, "accept", QVariantMap{}));
		check(!rpcAccepted.value("success").toBool() && rpcAccepted.value("accepted").toBool() &&
		          rpcAccepted.value("errorKind").toString().isEmpty(),
		      "RPC accepted evidence");
		refused(rpcResult(invoke(device, "offline", request)), "Dispatch");
		const auto simultaneous = invoke(device, "refreshConfig");
		const auto busyTransport = invoke(edge, "refreshScalars");
		success(rpcResult(simultaneous));
		refused(rpcResult(busyTransport), "Transport");
		check(packets > 20, "RPC request/result fragmentation not exercised");
		const auto queued = invoke(device, "reset");
		api->resetSession();
		refused(await(finished, queued), "Cancelled");
		const auto callsBefore = client_example::device.calls;
		for (int i = 0; i < 20; ++i) {
			QCoreApplication::processEvents();
			pump();
		}
		check(client_example::device.calls == callsBefore,
		      "queued stale request replayed after reset");
		rpcLifetimes(descriptor.left(64));
		handoffClassification(descriptor.left(64), configBytes, responseBytes);
		knownWriteCannotClearNewCache(descriptor.left(64), configBytes);
		knownRpcReplyBeforeDelivery(descriptor.left(64));
		pooledRecovery(engine, descriptor.left(64));
		trackedAdapterHandoffs(descriptor.left(64));
		admissionReentrancy(descriptor.left(64));
		qobject_cast<QQuickItem*>(root.get())->setParentItem(nullptr);
		std::cout << "QML checks=" << checks << " RPC packets=" << packets << '\n';
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "QML check failed after " << checks << ": " << error.what() << '\n';
		return 1;
	}
}
