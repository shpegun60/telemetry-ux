/* One generated QML facade, one bounded FIFO and one real RPC mailbox. MIT. */
#include <DemoModel.hpp>
#include <DeviceClientAdapter.hpp>
#include <integration/rpc/Backend.hpp>
#include <integration/encoded/RetryingBackend.hpp>
#include <integration/encoded/Dispatcher.hpp>
#include <integration/telemetry/v3/RpcSessionPool.hpp>
#include <QCoreApplication>
#include <QEvent>
#include <QSignalSpy>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace cf = codegen_fixture;
namespace qa = device_api::qml;
namespace rpc = resource::telemetry::v3;
namespace tc = telemetry::client;
static unsigned checks = 0, packets = 0;

static void check(bool value, const char* message)
{
	++checks;
	if (!value)
		throw std::runtime_error(message);
}

static QString invoke(QObject* object, const char* method)
{
	QString token;
	check(QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QString, token)), method);
	check(!token.isEmpty(), "nonempty token");
	return token;
}

static QString invoke(QObject* object, const char* method, const QVariant& value)
{
	QString token;
	check(QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QString, token),
	                                Q_ARG(QVariant, value)),
	      method);
	check(!token.isEmpty(), "nonempty value operation token");
	return token;
}

static void events()
{
	for (unsigned i = 0; i != 4; ++i)
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
	QCoreApplication::processEvents();
}

// Advance the real backend clock precisely inside the final permission call,
// after the QML precheck. No event delivery or API mutation is involved.
struct HandoffClockBackend final : tc::Backend {
	tc::Backend& backend;
	std::uint32_t& now;
	std::uint32_t advanceOnPrepare = 0;

	HandoffClockBackend(tc::Backend& value, std::uint32_t& clock) : backend(value), now(clock)
	{}

	tc::Status start(tc::RequestView request, std::uint32_t value) noexcept override
	{
		return backend.start(request, value);
	}

	void process(std::uint32_t value) noexcept override
	{
		backend.process(value);
	}

	bool completed() const noexcept override
	{
		return backend.completed();
	}

	tc::ResultView result() const noexcept override
	{
		return backend.result();
	}

	void releaseResult() noexcept override
	{
		backend.releaseResult();
	}

	void cancel(tc::Status reason) noexcept override
	{
		backend.cancel(reason);
	}

	bool ready() const noexcept override
	{
		return backend.ready();
	}

	bool recoveryRequired() const noexcept override
	{
		return backend.recoveryRequired();
	}

	std::uint32_t session() const noexcept override
	{
		return backend.session();
	}

	bool openSession(std::uint32_t value) noexcept override
	{
		return backend.openSession(value);
	}

	bool confirmSessionClosed(std::uint32_t value) noexcept override
	{
		return backend.confirmSessionClosed(value);
	}

	void close() noexcept override
	{
		backend.close();
	}

	tc::Packet nextPacket() noexcept override
	{
		return backend.nextPacket();
	}

	bool prepareHandoff(std::uint32_t session, std::uint32_t request) noexcept override
	{
		now += std::exchange(advanceOnPrepare, 0);
		return backend.prepareHandoff(session, request);
	}

	bool onSendAccepted(std::uint32_t session, std::uint32_t request) noexcept override
	{
		return backend.onSendAccepted(session, request);
	}

	bool onReply(tc::Input bytes, std::uint32_t session, std::uint32_t request) noexcept override
	{
		return backend.onReply(bytes, session, request);
	}
};

struct Harness {
	qa::DeviceApi api;
	std::uint32_t now = 0;
	std::array<std::byte, cf::model.maxScratch()> scratch{};
	telemetry::Workspace workspace{scratch};
	telemetry::ModelView model{cf::model.view()};
	std::array<std::byte, 256> request{}, response{}, clientRequest{}, clientResponse{};
	std::array<std::byte, 48> packet{}, copy{};
	std::array<std::byte, 64> reply{};
	rpc::RpcMailbox mailbox{model, workspace, request, response};
	resource::FileSystem<1> files{resource::filesystem(resource::file("/rpc", mailbox))};
	rpc::RpcSessionPool<1> pool{std::array{rpc::RpcSessionBinding{mailbox, files.view()}}};
	rpc::RpcSessionHandle remote = pool.acquire({1, 1, 0}).session;
	::telemetry::client::rpc::RpcClient native{0, clientRequest, clientResponse, packet};
	tc::Clock clock{this, [](void* self) noexcept {
		                return static_cast<Harness*>(self)->now;
	                }};
	::telemetry::integration::rpc::RpcBackend rpcBackend{native, clock};
	HandoffClockBackend backend{rpcBackend, now};
	qa::DeviceClientAdapter<qa::DeviceApi, 3, 256> adapter{api, backend, clock};
	QSignalSpy finished{&api, &qa::DeviceApi::operationFinished};

	Harness()
	{
		check(api.admitDescriptor(header()), "admit observed schema header");
		events();
	}

	~Harness()
	{
		adapter.close();
		for (unsigned i = 0; i != 8 && !adapter.drained(); ++i)
			adapter.process(now++);
	}

	static QByteArray header()
	{
		return {reinterpret_cast<const char*>(cf::descriptorBytes.data()), 64};
	}

	void pump()
	{
		events();
		adapter.process(now);
		(void)pool.process(now);
		if (const auto pending = adapter.nextPacket()) {
			check(native.active(), "one native transaction active");
			std::copy(pending.bytes.begin(), pending.bytes.end(), copy.begin());
			if (adapter.prepareHandoff(pending.session, pending.requestId)) {
				check(adapter.onSendAccepted(pending.session, pending.requestId),
				      "complete packet handoff");
				const auto response =
				    pool.handle(remote, tc::Input{copy}.first(pending.bytes.size()), reply);
				check(response.status == rpc::RpcSessionStatus::Ok, "pool ingress");
				check(response.reply.status == resource::Status::Ok, "resource reply");
				if (response.ticket.serial)
					check(pool.replyAccepted(remote, response.ticket) == rpc::RpcSessionStatus::Ok,
					      "ACK-gated server dispatch");
				check(adapter.onReply(tc::Input{reply}.first(response.reply.written),
				                      pending.session, pending.requestId),
				      "paired response");
				++packets;
			}
		}
		(void)pool.process(now++);
	}

	QVariantMap result(const QString& token)
	{
		for (const auto& row : finished)
			if (row[0].toString() == token)
				return row[1].toMap();
		return {};
	}

	void wait(unsigned count)
	{
		for (unsigned step = 0; step != 400 && finished.size() < qsizetype(count); ++step)
			pump();
		events();
		check(finished.size() == qsizetype(count), "exactly one completion per token");
	}
};

static void handoffDeadlineRace()
{
	Harness h;
	h.api.setRequestTimeoutMs(5000);
	const auto calls = client_example::device.calls;
	const auto token = invoke(h.api.device(), "reset");
	events();
	const auto packet = h.adapter.nextPacket();
	check(packet && packet.mayExecute, "deadline race reaches final WRITE permission");
	h.backend.advanceOnPrepare = 6000;
	check(!h.adapter.prepareHandoff(packet.session, packet.requestId),
	      "deadline crossing inside core gate refuses all transport handoff");
	check(!h.native.active() && !h.adapter.recoveryRequired(),
	      "never-permitted final WRITE retains certain nonexecution");
	// Reset immediately, before the core's queued Timeout delivery. A rejected
	// joint permission must not leave QML's mayExecute bit set in this interval.
	h.api.resetSession();
	events();
	check(h.result(token).value("errorKind").toString() == QStringLiteral("Cancelled"),
	      "rejected core handoff never turns session reset into OutcomeUnknown");
	check(client_example::device.calls == calls, "rejected handoff executes no callback");
}

static void acceptedHandoffRemainsUnknown()
{
	Harness h;
	const auto calls = client_example::device.calls;
	const auto token = invoke(h.api.device(), "reset");
	events();
	const auto packet = h.adapter.nextPacket();
	check(packet && packet.mayExecute, "accepted case reaches final WRITE");
	std::copy(packet.bytes.begin(), packet.bytes.end(), h.copy.begin());
	check(h.adapter.prepareHandoff(packet.session, packet.requestId),
	      "joint final handoff permitted");
	check(h.adapter.prepareHandoff(packet.session, packet.requestId),
	      "same transaction reuses Qt permission with fresh core gate");
	check(h.adapter.onSendAccepted(packet.session, packet.requestId),
	      "actual complete packet accepted");
	const auto reply =
	    h.pool.handle(h.remote, tc::Input{h.copy}.first(packet.bytes.size()), h.reply);
	check(reply.status == rpc::RpcSessionStatus::Ok && reply.ticket.serial != 0,
	      "actual server receives final WRITE");
	check(h.pool.replyAccepted(h.remote, reply.ticket) == rpc::RpcSessionStatus::Ok,
	      "actual server commits ACK and executes callback");
	(void)h.pool.process(h.now);
	check(client_example::device.calls == calls + 1, "one permitted native effect executed");
	// The response is withheld. Later deadline/session events cannot undo that
	// possible effect or turn a successful joint permission back into NotSent.
	h.now += 6000;
	check(!h.adapter.prepareHandoff(packet.session, packet.requestId),
	      "later expired permission cannot erase an earlier possible handoff");
	h.api.resetSession();
	events();
	check(h.result(token).value("errorKind").toString() == QStringLiteral("OutcomeUnknown"),
	      "true handoff keeps OutcomeUnknown after deadline and session reset");
	check(h.adapter.recoveryRequired(), "true final WRITE timeout still requires remote recovery");
}

static void qtDeadlineRefusesBeforeCore()
{
	Harness h;
	h.api.setRequestTimeoutMs(50);
	const auto calls = client_example::device.calls;
	const auto token = invoke(h.api.device(), "reset");
	events();
	const auto packet = h.adapter.nextPacket();
	check(packet && packet.mayExecute, "Qt expiry case reaches final WRITE");
	QThread::msleep(75);
	h.backend.advanceOnPrepare = 6000;
	check(!h.adapter.prepareHandoff(packet.session, packet.requestId),
	      "expired Qt deadline refuses permission before core callback");
	check(h.backend.advanceOnPrepare == 6000 && h.now == 0,
	      "expired Qt precheck never calls backend permission");
	h.adapter.process(h.now);
	QCoreApplication::sendPostedEvents(&h.api, QEvent::MetaCall);
	events();
	check(h.result(token).value("errorKind").toString() == QStringLiteral("Timeout"),
	      "expired Qt precheck remains Timeout without send");
	check(client_example::device.calls == calls && !h.adapter.recoveryRequired(),
	      "Qt refusal executes no effect and creates no uncertain recovery");
}

static void knownQueuedReplyBeforeDelivery()
{
	for (int scenario = 0; scenario != 3; ++scenario) {
		Harness h;
		const auto token = scenario == 0   ? invoke(h.api.device(), "reset")
		                   : scenario == 1 ? invoke(h.api.edge(), "accept", QVariantMap{})
		                                   : invoke(h.api.edge(), "good");
		events();
		check(h.adapter.pending() == 1, "known queued result submitted");
		for (unsigned step = 0; step != 400 && h.adapter.pending() != 0; ++step)
			h.pump();
		check(h.adapter.pending() == 0 && h.finished.isEmpty(),
		      "core checked completion precedes queued public notification");
		h.api.resetSession();
		events();
		const auto result = h.result(token);
		if (scenario == 1)
			check(result.value("accepted").toBool() && !result.value("success").toBool() &&
			          result.value("errorKind").toString().isEmpty(),
			      "queued checked Accepted survives reset before public notification");
		else
			check(result.value("success").toBool(),
			      "queued checked success survives reset before public notification");
		check(h.finished.size() == 1, "one checked queued notification after reset");
	}
}

struct RetryingHarness {
	qa::DeviceApi api;
	std::uint32_t now = 0;
	std::array<std::byte, cf::model.maxScratch()> scratch{};
	telemetry::Workspace workspace{scratch};
	std::array<std::byte, 1024> tx{}, result{}, copy{}, reply{};
	tc::Clock clock{this, +[](void* self) noexcept {
		                return static_cast<RetryingHarness*>(self)->now;
	                }};
	telemetry::integration::encoded::RetryingEncodedBackend backend{
	    tx, result, clock, {.replyTimeoutMs = 3, .maxRetries = 2}};
	qa::DeviceClientAdapter<qa::DeviceApi, 3, 256> adapter{api, backend, clock};
	QSignalSpy finished{&api, &qa::DeviceApi::operationFinished};
	bool dropFirstCommand = true;
	unsigned commandPackets = 0;
	std::uint32_t commandId = 0, commandSession = 0;
	std::array<std::byte, 1024> originalCommand{};
	std::size_t commandBytes = 0;

	RetryingHarness()
	{
		check(api.admitDescriptor(Harness::header()), "retry facade admits observed header");
		events();
	}

	~RetryingHarness()
	{
		adapter.close();
		for (unsigned i = 0; i < 8 && !adapter.drained(); ++i)
			adapter.process(now++);
	}

	bool send()
	{
		const auto packet = adapter.nextPacket();
		if (!packet)
			return false;
		check(packet.bytes.size() <= copy.size(), "retry packet bounded");
		std::copy(packet.bytes.begin(), packet.bytes.end(), copy.begin());
		check(adapter.prepareHandoff(packet.session, packet.requestId),
		      "fresh QML retry handoff permission");
		check(adapter.onSendAccepted(packet.session, packet.requestId),
		      "retry complete packet accepted");
		const auto parsed =
		    telemetry::protocol::encoded::parse(tc::Input{copy}.first(packet.bytes.size()));
		check(bool(parsed), "actual encoded retry frame");
		const bool command =
		    parsed.header.operation == telemetry::protocol::encoded::Operation::Command;
		if (command && parsed.header.endpoint == 1) { // Native fixture's Reset command.
			++commandPackets;
			if (commandPackets == 1) {
				commandId = packet.requestId;
				commandSession = packet.session;
				commandBytes = packet.bytes.size();
				std::copy(packet.bytes.begin(), packet.bytes.end(), originalCommand.begin());
			} else if (commandPackets == 2 && dropFirstCommand) {
				check(packet.requestId == commandId && packet.session == commandSession,
				      "QML retry retains ID and session");
				check(packet.bytes.size() == commandBytes &&
				          std::equal(packet.bytes.begin(), packet.bytes.end(),
				                     originalCommand.begin()),
				      "QML retry retains exact frame bytes");
			}
		}
		const auto response = telemetry::integration::encoded::dispatchEncoded(
		    cf::model.view(), workspace, api.session(), tc::Input{copy}.first(packet.bytes.size()),
		    reply);
		check(bool(response), "native TEQ dispatcher response");
		++packets;
		if (!(command && parsed.header.endpoint == 1 && commandPackets == 1 && dropFirstCommand))
			check(adapter.onReply(tc::Input{reply}.first(response.written), packet.session,
			                      packet.requestId),
			      "retry checked response");
		return true;
	}

	void pump()
	{
		events();
		adapter.process(now);
		(void)send();
		adapter.process(now++);
	}

	void wait(unsigned count)
	{
		for (unsigned i = 0; i < 200 && finished.size() < qsizetype(count); ++i)
			pump();
		events();
		check(finished.size() == qsizetype(count), "retry callback exactly once per token");
	}

	QVariantMap value(const QString& token)
	{
		for (const auto& row : finished)
			if (row[0].toString() == token)
				return row[1].toMap();
		return {};
	}
};

static void qmlRetryingBackend()
{
	RetryingHarness h;
	const auto calls = client_example::device.calls;
	const auto read = invoke(h.api.device(), "refreshConfig");
	const auto command = invoke(h.api.device(), "reset");
	const auto service = invoke(h.api.edge(), "good");
	const auto full = invoke(h.api.device(), "ping");
	events();
	check(h.adapter.pending() == 3, "retry backend uses the same three-slot execution queue");
	check(h.value(full).value("errorKind").toString() == QStringLiteral("QueueFull"),
	      "retry QueueFull explicit");
	h.wait(4);
	check(h.value(read).value("success").toBool() && h.api.device()->configValid(),
	      "retry field read caches checked value");
	check(h.value(command).value("success").toBool(),
	      "retry command completes through generic adapter");
	check(h.value(service).value("success").toBool(), "retry zero-byte service completes");
	check(h.commandPackets == 2 && client_example::device.calls == calls + 2,
	      "lost encoded reply can execute command twice");
	auto config = h.value(read).value("value").toMap();
	config[QStringLiteral("gain")] = 2.5;
	const auto write = invoke(h.api.device(), "writeConfig", config);
	const auto accepted = invoke(h.api.edge(), "accept", QVariantMap{});
	h.wait(6);
	check(h.value(write).value("success").toBool() && !h.api.device()->configValid(),
	      "retry field write uses normal cache rules");
	check(h.value(accepted).value("accepted").toBool() &&
	          !h.value(accepted).value("success").toBool(),
	      "Accepted is terminal, not Executed");
	check(!h.adapter.nextPacket() && h.adapter.pending() == 0,
	      "no retry after checked endpoint result");
}

static void qmlRetryReset()
{
	RetryingHarness h;
	const auto calls = client_example::device.calls;
	const auto token = invoke(h.api.device(), "reset");
	events();
	check(h.send(), "reset case has one permitted encoded handoff");
	const auto old = h.api.session();
	h.api.resetSession();
	events();
	h.adapter.process(h.now++);
	events();
	check(h.value(token).value("errorKind").toString() == QStringLiteral("OutcomeUnknown"),
	      "reset after permitted retry request remains unknown");
	check(h.adapter.recoveryRequired() && !h.adapter.nextPacket(),
	      "reset blocks pending retransmission");
	check(client_example::device.calls == calls + 1, "reset does not retransmit mutation");
	check(!h.adapter.confirmSessionClosed(old + 1), "retry wrong recovery generation refused");
	check(h.adapter.confirmSessionClosed(old),
	      "test transport excludes old generation before confirmation");
	check(h.api.admitDescriptor(Harness::header()), "retry facade admits fresh generation");
	events();
	h.dropFirstCommand = false;
	const auto fresh = invoke(h.api.device(), "reset");
	h.wait(2);
	check(h.value(fresh).value("success").toBool() && client_example::device.calls == calls + 2,
	      "fresh user invocation, no recovery replay");
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	try {
		client_example::device.calls = 0;
		cf::edge.calls = 0;
		Harness h;
		const auto read = invoke(h.api.device(), "refreshConfig");
		const auto command = invoke(h.api.device(), "reset");
		const auto service = invoke(h.api.edge(), "good");
		const auto full = invoke(h.api.device(), "ping");
		events();
		check(h.adapter.pending() == 3, "three accepted requests in one DeviceClient queue");
		check(!h.native.active(), "enqueue does not start native I/O");
		check(h.result(full).value("errorKind").toString() == QStringLiteral("QueueFull"),
		      "fourth request explicitly QueueFull");
		check(client_example::device.calls == 0 && cf::edge.calls == 0,
		      "no hidden execution queue");
		h.wait(4);
		check(h.result(read).value("success").toBool(), "queued read success");
		check(h.result(command).value("success").toBool(), "queued command success");
		check(h.result(service).value("success").toBool(), "queued zero-byte service success");
		check(client_example::device.calls == 1 && cf::edge.calls == 1,
		      "exact native callback count");
		check(h.adapter.pending() == 0, "execution queue drained");
		check(packets > 6, "actual fragmented Resource exchange");
		check(h.finished[1][0].toString() == read && h.finished[2][0].toString() == command &&
		          h.finished[3][0].toString() == service,
		      "accepted request FIFO order");
		auto config = h.result(read).value("value").toMap();
		config[QStringLiteral("enabled")] = false;
		const auto write = invoke(h.api.device(), "writeConfig", config);
		config[QStringLiteral("enabled")] = true;
		h.wait(5);
		check(h.result(write).value("success").toBool(), "queued write success");
		check(!client_example::device.config.enabled, "queued value snapshot");
		check(!h.api.device()->configValid(), "successful write invalidates QML read cache");
		const auto old = h.api.session();
		h.api.resetSession();
		events();
		h.adapter.process(h.now++);
		check(h.adapter.recoveryRequired(), "API reset requires real remote session closure");
		check(!h.adapter.confirmSessionClosed(old + 1), "wrong recovery generation refused");
		check(h.pool.release(h.remote) == rpc::RpcSessionStatus::Ok,
		      "release actual old mailbox session");
		(void)h.pool.process(h.now);
		h.remote = h.pool.acquire({1, 2, 0}).session;
		check(h.pool.valid(h.remote), "fresh remote session");
		check(h.adapter.confirmSessionClosed(old), "confirm excluded and drained session");
		check(h.api.admitDescriptor(Harness::header()), "fresh observed schema admission");
		events();
		const auto fresh = invoke(h.api.device(), "ping");
		h.wait(6);
		check(h.result(fresh).value("success").toBool(), "fresh user invocation after recovery");
		check(client_example::device.calls == 3, "recovery never replayed old invocation");
		h.api.setRequestTimeoutMs(2);
		const auto expired = invoke(h.api.device(), "reset");
		events();
		check(h.adapter.pending() == 1, "queued request before deadline");
		QThread::msleep(10);
		h.adapter.process(h.now); // Stale simulated transport clock, fresh Qt deadline.
		QCoreApplication::sendPostedEvents(&h.api, QEvent::MetaCall); // Before timers.
		events();
		check(h.result(expired).value("errorKind").toString() == QStringLiteral("Timeout"),
		      "expired unsent queued request keeps Timeout ahead of Qt timer");
		check(client_example::device.calls == 3, "expired queued command never executed");
		handoffDeadlineRace();
		acceptedHandoffRemainsUnknown();
		qtDeadlineRefusesBeforeCore();
		knownQueuedReplyBeforeDelivery();
		qmlRetryingBackend();
		qmlRetryReset();
		std::printf("QML queued checks=%u packets=%u\n", checks, packets);
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "QML queued check=%u error=%s\n", checks, error.what());
		return 1;
	}
}
