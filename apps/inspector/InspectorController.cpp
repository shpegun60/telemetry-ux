/*
 * @file InspectorController.cpp
 * @brief Own descriptor generations and drive the real Encoded peer from the Qt event loop.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "InspectorController.hpp"
#include "DemoPeer.hpp"
#include "TcpTransport.hpp"
#ifdef INSPECTOR_ENABLE_SERIAL
#include <integration/qt/SerialTransport.hpp>
#include <QtSerialPort/QSerialPortInfo>
#endif
#include <integration/encoded/Backend.hpp>
#include <integration/read_many/Workbench.hpp>
#include <telemetry/result/EndpointStatus.hpp>
#include <telemetry/result/ServiceResult.hpp>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QPointer>
#include <QtNetwork/QHostAddress>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <charconv>
#include <limits>
#include <optional>
#include <vector>

namespace inspector_qml {
namespace cl = telemetry::client;
namespace rt = telemetry::runtime;
namespace im = telemetry::integration::inspector;
namespace wb = telemetry::integration::workbench;
namespace rm = telemetry::integration::read_many;

namespace {
bool mutationPacket(const cl::Packet& packet) noexcept
{
	const auto parsed = telemetry::protocol::encoded::parse(packet.bytes);
	return packet.mayExecute && parsed &&
	       (parsed.header.operation == telemetry::protocol::encoded::Operation::FieldWrite ||
	        parsed.header.operation == telemetry::protocol::encoded::Operation::Command ||
	        parsed.header.operation == telemetry::protocol::encoded::Operation::Service);
}

QString statusName(cl::Status status)
{
	switch (status) {
		case cl::Status::Ok:
			return QStringLiteral("Ok");
		case cl::Status::QueueFull:
			return QStringLiteral("QueueFull");
		case cl::Status::PayloadFull:
			return QStringLiteral("PayloadFull");
		case cl::Status::NotAdmitted:
			return QStringLiteral("NotAdmitted");
		case cl::Status::ReadOnly:
			return QStringLiteral("ReadOnly");
		case cl::Status::Closed:
			return QStringLiteral("Closed");
		case cl::Status::Cancelled:
			return QStringLiteral("Cancelled");
		case cl::Status::Timeout:
			return QStringLiteral("Timeout");
		case cl::Status::SessionLost:
			return QStringLiteral("SessionLost");
		case cl::Status::OutcomeUnknown:
			return QStringLiteral("OutcomeUnknown");
		case cl::Status::MalformedReply:
			return QStringLiteral("MalformedReply");
		case cl::Status::TransportError:
			return QStringLiteral("TransportError");
		case cl::Status::CodecError:
			return QStringLiteral("CodecError");
		case cl::Status::BufferTooSmall:
			return QStringLiteral("BufferTooSmall");
		case cl::Status::InvalidArgument:
			return QStringLiteral("InvalidArgument");
		case cl::Status::Busy:
			return QStringLiteral("Busy");
		case cl::Status::RecoveryRequired:
			return QStringLiteral("RecoveryRequired");
		case cl::Status::IdsExhausted:
			return QStringLiteral("IdsExhausted");
		case cl::Status::Unsupported:
			return QStringLiteral("Unsupported");
	}
	return QStringLiteral("Unknown");
}

QString commandStatusName(std::uint8_t status)
{
	switch (static_cast<telemetry::CommandResult>(status)) {
		case telemetry::CommandResult::Executed:
			return QStringLiteral("Executed");
		case telemetry::CommandResult::Accepted:
			return QStringLiteral("Accepted");
		case telemetry::CommandResult::NotFound:
			return QStringLiteral("Not found");
		case telemetry::CommandResult::Unavailable:
			return QStringLiteral("Unavailable");
		case telemetry::CommandResult::ArgumentCountMismatch:
			return QStringLiteral("Argument count mismatch");
		case telemetry::CommandResult::InvalidValue:
			return QStringLiteral("Invalid argument value");
		case telemetry::CommandResult::Busy:
			return QStringLiteral("Busy");
		case telemetry::CommandResult::Failed:
			return QStringLiteral("Failed");
	}
	return QStringLiteral("Unknown endpoint status %1").arg(status);
}

QString serviceStatusName(std::uint8_t status)
{
	switch (static_cast<telemetry::ServiceStatus>(status)) {
		case telemetry::ServiceStatus::Ok:
			return QStringLiteral("Ok");
		case telemetry::ServiceStatus::InvalidArgument:
			return QStringLiteral("InvalidArgument");
		case telemetry::ServiceStatus::Unavailable:
			return QStringLiteral("Unavailable");
		case telemetry::ServiceStatus::Busy:
			return QStringLiteral("Busy");
		case telemetry::ServiceStatus::Failed:
			return QStringLiteral("Failed");
	}
	return QStringLiteral("Unknown endpoint status %1").arg(status);
}

template<class T>
bool parseScalar(const QString& text, T& value)
{
	const auto input = text.trimmed().toUtf8();
	if constexpr (std::is_same_v<T, bool>) {
		if (input == "true" || input == "1")
			value = true;
		else if (input == "false" || input == "0")
			value = false;
		else
			return false;
		return true;
	} else {
		const auto parsed =
		    std::from_chars(input.constData(), input.constData() + input.size(), value);
		return parsed.ec == std::errc{} && parsed.ptr == input.constData() + input.size();
	}
}
} // namespace

struct InspectorController::State {
	struct WatchCompletion {
		struct Row {
			quint32 id = 0;
			QString status, text;
		};

		quint64 revision = 0;
		cl::Status status = cl::Status::Ok;
		std::array<Row, 32> rows{};
		int count = 0;
	};

	struct Reply {
		std::vector<std::byte> bytes;
		quint32 session = 0, id = 0, due = 0;
	};

	DemoPeer peer;
	QElapsedTimer elapsed;
	QByteArray descriptor;
	std::vector<rt::RuntimeType> types;
	std::vector<rt::RuntimeMember> members;
	std::vector<rt::RuntimeEnumEntry> enums;
	std::vector<rt::RuntimeCatalog> catalogs;
	std::vector<rt::RuntimeEndpoint> endpoints;
	std::unique_ptr<rt::RuntimeSchemaWorkspace> storage;
	rt::RuntimeSchema schema;
	std::array<std::byte, 65568> tx{}, rx{}, port{}, response{};
	cl::Clock clock{this, +[](void* owner) noexcept {
		                return static_cast<State*>(owner)->now();
	                }};
	telemetry::integration::encoded::EncodedBackend backend{tx, rx, clock, 30000, true};
	std::array<std::byte, 65536> serviceStorage{};
	Workbench inspector{backend, clock, serviceStorage};
	std::vector<Reply> replies;
	std::optional<im::ReadResult> completed;
	std::optional<wb::ApplyResult> applied;
	std::optional<im::OperationResult> commanded;
	std::optional<wb::ServiceResult> serviced;
	std::optional<rm::wire::Capability> capability;
	std::optional<WatchCompletion> watched;
	bool applying = false, mutationAuthorized = false;
	bool executing = false;
	bool calling = false;
	bool demo = false;
	bool serial = false;
	bool remote = false, remoteAccepted = false;
	quint32 remoteRequestId = 0;

	State(QByteArray bytes, bool isDemo, bool writableDemo = false, bool readManyDemo = false)
	    : peer(writableDemo, readManyDemo), descriptor(std::move(bytes)), demo(isDemo)
	{
		elapsed.start();
	}

	quint32 now() const noexcept
	{
		return static_cast<quint32>(elapsed.elapsed());
	}

	cl::Input input() const noexcept
	{
		return {reinterpret_cast<const std::byte*>(descriptor.constData()),
		        static_cast<std::size_t>(descriptor.size())};
	}

	QString initialize(quint32 session, bool admit = true)
	{
		rt::RuntimeSchemaWorkspace sizing{{}, {}, {}, {}, {}};
		const auto required = rt::RuntimeSchema::parse(input(), sizing);
		if (!required && required.status != rt::Status::Capacity)
			return QStringLiteral("Descriptor rejected (status %1, error %2)")
			    .arg(static_cast<unsigned>(required.status))
			    .arg(static_cast<unsigned>(required.error));
		types.resize(required.requirements.types);
		members.resize(required.requirements.members);
		enums.resize(required.requirements.enumEntries);
		catalogs.resize(required.requirements.catalogs);
		endpoints.resize(required.requirements.endpoints);
		storage = std::make_unique<rt::RuntimeSchemaWorkspace>(types, members, enums, catalogs,
		                                                       endpoints);
		const auto parsed = rt::RuntimeSchema::parse(input(), *storage);
		if (!parsed)
			return QStringLiteral("Descriptor rejected (status %1, error %2)")
			    .arg(static_cast<unsigned>(parsed.status))
			    .arg(static_cast<unsigned>(parsed.error));
		schema = parsed.schema;
		return admit ? admitSession(session) : QString{};
	}

	QString admitSession(quint32 session)
	{
		const auto admitted = inspector.admit(schema, input().first(64), session);
		return admitted == cl::Status::Ok ? QString{} : statusName(admitted);
	}

	void closeAndDrain()
	{
		// The port is already closed (the demo releases TX synchronously). Settle
		// only complete, already-owned replies whose delay
		// has elapsed. onReply still enforces the operation deadline. Never pump
		// here: retirement must not authorize another physical handoff. A readback
		// queued by a write callback is closed below before it can be transmitted.
		for (const auto& reply : replies)
			if (cl::expired(now(), reply.due))
				(void)inspector.onReply(reply.bytes, reply.session, reply.id);
		replies.clear();
		inspector.close();
		for (unsigned i = 0; i < 16 && !inspector.drained(); ++i)
			inspector.process(now());
		if (!inspector.drained())
			std::abort();
	}

	~State()
	{
		closeAndDrain();
	}
};

InspectorController::InspectorController(QObject* parent) : QObject(parent)
{
	timer_.setInterval(5);
	connect(&timer_, &QTimer::timeout, this, &InspectorController::pump);
	timer_.start();
}

InspectorController::~InspectorController()
{
	timer_.stop();
	updating_ = true;
	retire();
}

InspectorController::Completion InspectorController::retire()
{
	++epoch_;
	retireWatch(true);
	values_.clear();
	responses_.clear();
	endpoints_.clear();
	selection_.clear();
	connected_ = false;
	busy_ = false;
	writesEnabled_ = false;
	invalidEditors_.clear();
	connecting_ = false;
	if (transport_) {
		transport_->close();
		if (!transport_->locallyQuiescent())
			std::abort();
		transport_.reset();
	}
	Completion completion;
	if (state_) {
		state_->closeAndDrain();
		watchPending_ = false;
		completion.applied = consumeApply(*state_, false);
		completion.command = consumeCommand(*state_, false);
		completion.service = consumeService(*state_, false);
		if (state_->remote && state_->inspector.recoveryRequired()) {
			// Local abort releases all Qt-owned TX/RX storage. It does not prove
			// that a peer finished an already handed-off request. Preserve that
			// obligation until the same single-client server admits fresh bootstrap.
			if (retiredState_)
				std::abort();
			retiredSerial_ = state_->serial;
			retiredSerialPort_ = serialPort_;
			retiredSerialBaud_ = serialBaud_;
			retiredPeerHost_ = peerHost_;
			retiredPeerPort_ = peerPort_;
			retiredPeerAddress_ = peerAddress_;
			retiredState_ = std::move(state_);
		}
	}
	state_.reset();
	return completion;
}

std::optional<bool> InspectorController::consumeApply(State& state, bool displaySnapshot)
{
	if (!state.applying || !state.applied)
		return {};
	const auto applied = *state.applied;
	state.applied.reset();
	state.applying = false;
	busy_ = false;
	// The terminal operation result survives retirement of its schema/cache.
	// A readback completed before Disconnect is still a historical confirmation.
	const bool confirmed = applied.writeCompleted && applied.write.succeeded() &&
	                       applied.refreshCompleted && applied.refresh.operation.succeeded() &&
	                       applied.refresh.published;
	const bool publish = confirmed && displaySnapshot && state.inspector.isCurrent();
	if (publish)
		values_.resetSnapshot(state.inspector);
	else
		values_.clear();
	if (confirmed) {
		writeOutcome_ = QStringLiteral("Applied · confirmed by a fresh Field read.");
		status_ = publish
		              ? QStringLiteral("Write applied · displayed values are the device readback.")
		              : QStringLiteral("Write applied · readback completed · no cached values.");
	} else if (applied.write.status == cl::Status::OutcomeUnknown) {
		writeOutcome_ =
		    QStringLiteral("OutcomeUnknown · the write may have executed. It will not be retried.");
		status_ = writeOutcome_;
	} else if (applied.writeCompleted && applied.write.succeeded()) {
		const auto reason =
		    applied.refreshCompleted
		        ? QStringLiteral("%1 · dispatch %2 · endpoint %3 · value %4 · model %5")
		              .arg(statusName(applied.refresh.operation.status))
		              .arg(applied.refresh.operation.dispatch)
		              .arg(applied.refresh.operation.endpointStatus)
		              .arg(static_cast<unsigned>(applied.refresh.valueStatus))
		              .arg(static_cast<unsigned>(applied.refresh.modelStatus))
		        : QStringLiteral("submission %1").arg(statusName(applied.refreshSubmission));
		writeOutcome_ = QStringLiteral("Write applied · readback unconfirmed (%1).").arg(reason);
		status_ = writeOutcome_;
	} else {
		writeOutcome_ = QStringLiteral("Write finished: %1 · dispatch %2 · endpoint %3.")
		                    .arg(statusName(applied.write.status))
		                    .arg(applied.write.dispatch)
		                    .arg(applied.write.endpointStatus);
		status_ = writeOutcome_;
	}
	const auto fatal = [](cl::Status value) {
		return value == cl::Status::TransportError || value == cl::Status::MalformedReply ||
		       value == cl::Status::SessionLost || value == cl::Status::OutcomeUnknown;
	};
	if (displaySnapshot && state.remote && transport_ &&
	    (state.inspector.recoveryRequired() || fatal(applied.write.status) ||
	     (applied.refreshCompleted && fatal(applied.refresh.operation.status)))) {
		invalidateSession(state);
	}
	return confirmed;
}

std::optional<std::pair<bool, bool>> InspectorController::consumeCommand(State& state, bool live)
{
	if (!state.executing || !state.commanded)
		return {};
	const auto result = *state.commanded;
	state.commanded.reset();
	state.executing = false;
	busy_ = false;
	const bool executed = result.executed(), accepted = result.accepted();
	if (executed)
		commandOutcome_ = QStringLiteral("Executed · the endpoint confirmed execution.");
	else if (accepted)
		commandOutcome_ = QStringLiteral("Accepted · execution has not been confirmed.");
	else if (result.status == cl::Status::OutcomeUnknown)
		commandOutcome_ = QStringLiteral(
		    "OutcomeUnknown · the Command may have executed. It will not be retried.");
	else if (result.status == cl::Status::Ok && result.dispatch == 0)
		commandOutcome_ =
		    QStringLiteral("Command result: %1.").arg(commandStatusName(result.endpointStatus));
	else
		commandOutcome_ = QStringLiteral("Command finished: %1 · dispatch %2 · endpoint %3.")
		                      .arg(statusName(result.status))
		                      .arg(result.dispatch)
		                      .arg(result.endpointStatus);
	status_ = commandOutcome_;
	const bool fatal = result.status == cl::Status::TransportError ||
	                   result.status == cl::Status::MalformedReply ||
	                   result.status == cl::Status::SessionLost ||
	                   result.status == cl::Status::OutcomeUnknown;
	if (live && state.remote && transport_ && (state.inspector.recoveryRequired() || fatal)) {
		invalidateSession(state);
	} else if (live && state.inspector.isCurrent()) {
		values_.resetSnapshot(state.inspector);
	}
	return std::pair{executed, accepted};
}

std::optional<std::pair<bool, bool>> InspectorController::consumeService(State& state, bool live)
{
	if (!state.calling || !state.serviced)
		return {};
	const auto result = *state.serviced;
	state.serviced.reset();
	state.calling = false;
	busy_ = false;
	const bool succeeded = result.operation.succeeded();
	const bool published = succeeded && result.published;
	if (succeeded && published)
		serviceOutcome_ = QStringLiteral("Completed · response published.");
	else if (succeeded &&
	         result.valueStatus == telemetry::integration::dynamic::ValueStatus::NoValue)
		serviceOutcome_ = QStringLiteral("Completed · no response value (Void).");
	else if (succeeded)
		serviceOutcome_ = QStringLiteral("Completed · response unavailable (value %1 · model %2).")
		                      .arg(static_cast<unsigned>(result.valueStatus))
		                      .arg(static_cast<unsigned>(result.modelStatus));
	else if (result.operation.status == cl::Status::OutcomeUnknown)
		serviceOutcome_ = QStringLiteral(
		    "OutcomeUnknown · the Service may have executed. It will not be retried.");
	else {
		const auto reason =
		    result.operation.status == cl::Status::Ok && result.operation.dispatch == 0
		        ? serviceStatusName(result.operation.endpointStatus)
		        : statusName(result.operation.status);
		serviceOutcome_ =
		    QStringLiteral(
		        "Service finished: %1 · dispatch %2 · endpoint %3 · value %4 · model %5.")
		        .arg(reason)
		        .arg(result.operation.dispatch)
		        .arg(result.operation.endpointStatus)
		        .arg(static_cast<unsigned>(result.valueStatus))
		        .arg(static_cast<unsigned>(result.modelStatus));
	}
	status_ = serviceOutcome_;
	if (live && state.inspector.isCurrent()) {
		values_.resetSnapshot(state.inspector);
		responses_.resetServiceResponse(state.inspector);
	}
	const auto outcome = result.operation.status;
	const bool fatal = outcome == cl::Status::TransportError ||
	                   outcome == cl::Status::MalformedReply ||
	                   outcome == cl::Status::SessionLost || outcome == cl::Status::OutcomeUnknown;
	if (live && state.remote && transport_ && (state.inspector.recoveryRequired() || fatal)) {
		invalidateSession(state);
	}
	return std::pair{succeeded, published};
}

void InspectorController::notifyState(Completion completion, std::optional<bool> read)
{
	// A terminal Apply/Command belongs to the accepted operation, not a connection epoch.
	// Deliver it before stateChanged slots can replace the connection and erase
	// the sole notification. Never access this object after a slot destroys it.
	const auto epoch = epoch_;
	QPointer<InspectorController> alive{this};
	if (completion.applied)
		Q_EMIT applyFinished(*completion.applied);
	if (!alive || epoch_ != epoch)
		return;
	if (completion.command)
		Q_EMIT commandFinished(completion.command->first, completion.command->second);
	if (!alive || epoch_ != epoch)
		return;
	if (completion.service)
		Q_EMIT serviceFinished(completion.service->first, completion.service->second);
	if (!alive || epoch_ != epoch)
		return;
	Q_EMIT stateChanged();
	if (alive && epoch_ == epoch && read)
		Q_EMIT readFinished(*read);
}

bool InspectorController::replaceDescriptor(QByteArray bytes, bool demo, bool remote,
                                            bool writableDemo, bool readManyDemo)
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing())
		return false;
	if (session_ == std::numeric_limits<quint32>::max()) {
		status_ = QStringLiteral("Session IDs exhausted; restart the application.");
		Q_EMIT stateChanged();
		return false;
	}
	// Validate into a separate generation first. A malformed file does not destroy
	// the current peer, selected field or published snapshot.
	auto candidate = std::make_unique<State>(std::move(bytes), demo, writableDemo, readManyDemo);
	candidate->remote = remote;
	candidate->serial = remote && serial_;
	const auto error = candidate->initialize(session_ + 1, !remote);
	if (!error.isEmpty()) {
		status_ = error;
		Q_EMIT stateChanged();
		return false;
	}
	quint32 admittedSession = session_ + 1;
	if (readManyDemo)
		candidate->capability =
		    rm::wire::Capability{admittedSession, candidate->schema.fingerprint()};
#ifdef INSPECTOR_ENABLE_SERIAL
	if (remote && serial_ && transport_) {
		const auto capabilityBytes =
		    static_cast<telemetry::integration::qt::SerialTransport*>(transport_.get())
		        ->readManyCapability();
		if (!capabilityBytes.isEmpty()) {
			const auto parsed = rm::wire::parseCapability(
			    {reinterpret_cast<const std::byte*>(capabilityBytes.constData()),
			     static_cast<std::size_t>(capabilityBytes.size())});
			if (!parsed || parsed.value.fingerprint != candidate->schema.fingerprint()) {
				status_ = QStringLiteral("TRM1 does not match the received Descriptor.");
				Q_EMIT stateChanged();
				return false;
			}
			candidate->capability = parsed.value;
			admittedSession = parsed.value.session;
		}
	}
#endif
	if (remote) {
		if (!transport_ || !transport_->ready())
			return false;
		const auto address = transport_->peerAddress();
		if (retiredState_) {
			if (retiredState_->capability &&
			    (!candidate->capability ||
			     candidate->capability->session <= retiredState_->inspector.session())) {
				status_ = QStringLiteral(
				    "A newer TRM1 session is required to retire the previous batch.");
				Q_EMIT stateChanged();
				return false;
			}
			if (retiredState_->serial && retiredState_->mutationAuthorized) {
				status_ = QStringLiteral(
				    "The read-only UART bootstrap cannot confirm an earlier write. Application reset proof is required.");
				Q_EMIT stateChanged();
				return false;
			}
			if (serial_ != retiredSerial_ || address != retiredPeerAddress_ ||
			    (serial_ ? serialPort_ != retiredSerialPort_ || serialBaud_ != retiredSerialBaud_
			             : peerHost_ != retiredPeerHost_ || peerPort_ != retiredPeerPort_)) {
				status_ = QStringLiteral("The previous device still requires session cleanup.");
				Q_EMIT stateChanged();
				return false;
			}
			// TCP retires the old socket before accepting another client. The serial
			// demo has one exclusive, sequential host and synchronous read-only
			// dispatch; it cannot process the fresh bootstrap before prior RX and
			// physical TX/COBS leases finish. Both profiles require the complete
			// validated Descriptor transcript from the same peer, never local close
			// or one uncorrelated Resource reply as a remote cleanup proof.
			if (!retiredState_->inspector.confirmQuiescence(
			        {retiredState_->inspector.session(), true, true, true})) {
				status_ = QStringLiteral("Could not confirm the previous device session cleanup.");
				Q_EMIT stateChanged();
				return false;
			}
			retiredState_.reset();
			retiredPeerHost_.clear();
			retiredPeerAddress_.clear();
			retiredPeerPort_ = 0;
			retiredSerialPort_.clear();
			retiredSerialBaud_ = 0;
			retiredSerial_ = false;
		}
		const auto admission = candidate->admitSession(admittedSession);
		if (!admission.isEmpty()) {
			status_ = admission;
			Q_EMIT stateChanged();
			return false;
		}
		peerAddress_ = address;
	}
	updating_ = true;
	Completion retiredApply;
	if (remote) {
		retireWatch(true);
		values_.clear();
		responses_.clear();
		endpoints_.clear();
		selection_.clear();
		if (state_) {
			state_->closeAndDrain();
			retiredApply.applied = consumeApply(*state_, false);
			retiredApply.command = consumeCommand(*state_, false);
			retiredApply.service = consumeService(*state_, false);
			state_.reset();
		}
	} else {
		retiredApply = retire();
	}
	++session_;
	state_ = std::move(candidate);
	// Any previous State has completed closeAndDrain before replacement.
	watchPending_ = false;
	watchlist_.bind(state_->inspector);
	endpoints_.rebuild(state_->inspector);
	connected_ = demo || remote;
	writesEnabled_ = demo && writableDemo;
	connecting_ = false;
	status_ =
	    remote && serial_
	        ? QStringLiteral("Serial connected - Descriptor loaded - read-only - COBS/Encoded")
	    : remote
	        ? QStringLiteral("TCP connected · Descriptor loaded · read-only · Encoded transport")
	    : demo && writableDemo
	        ? QStringLiteral("Writable demo connected · explicit Field editing · Encoded transport")
	    : demo ? QStringLiteral("Demo connected · read-only · Encoded transport")
	           : QStringLiteral("Descriptor file · metadata only · no transport connected");
	updating_ = false;
	notifyState(retiredApply);
	return true;
}

QByteArray InspectorController::demoDescriptor() const
{
	DemoPeer peer;
	const auto bytes = peer.descriptor();
	return {reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())};
}

bool InspectorController::connectDemo()
{
	return replaceDescriptor(demoDescriptor(), true);
}

bool InspectorController::connectWritableDemo()
{
	DemoPeer peer{true};
	const auto bytes = peer.descriptor();
	return replaceDescriptor(
	    {reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())}, true,
	    false, true);
}

bool InspectorController::connectReadManyDemo()
{
	DemoPeer peer{false, true};
	const auto bytes = peer.descriptor();
	return replaceDescriptor(
	    {reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())}, true,
	    false, false, true);
}

bool InspectorController::connectTcp(const QString& host, quint16 port)
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing() ||
	    host.trimmed().isEmpty() || port == 0)
		return false;
	updating_ = true;
	const auto retiredApply = retire();
	auto normalized = host.trimmed().toLower();
	QHostAddress numeric;
	if (numeric.setAddress(normalized))
		normalized = numeric.toString().toLower();
	if (retiredState_ &&
	    (retiredSerial_ || normalized != retiredPeerHost_ || port != retiredPeerPort_)) {
		status_ =
		    QStringLiteral("Reconnect %1:%2 first to finish the previous TCP session cleanup.")
		        .arg(retiredPeerHost_)
		        .arg(retiredPeerPort_);
		updating_ = false;
		notifyState(retiredApply);
		return false;
	}
	serial_ = false;
	peerHost_ = normalized;
	peerPort_ = port;
	connecting_ = true;
	status_ = QStringLiteral("Connecting TCP and loading Descriptor…");
	transport_ = std::make_unique<TcpTransport>();
	auto* tcpPortAdapter = static_cast<TcpTransport*>(transport_.get());
	transport_->setWriteChunkBytes(tcpWriteChunk_);
	bindTransportSignals();
	tcpPortAdapter->connectTo(peerHost_, peerPort_);
	updating_ = false;
	notifyState(retiredApply);
	return true;
}

void InspectorController::bindTransportSignals()
{
	const auto epoch = epoch_;
	connect(
	    transport_.get(), &telemetry::integration::qt::DeviceTransport::descriptorReceived, this,
	    [this, epoch](QByteArray bytes) {
		    transportDescriptor(epoch, std::move(bytes));
	    },
	    Qt::QueuedConnection);
	connect(
	    transport_.get(), &telemetry::integration::qt::DeviceTransport::packetAccepted, this,
	    [this, epoch](quint32 session, quint32 id) {
		    if (epoch == epoch_ && state_ && state_->remote &&
		        session == state_->inspector.session() && id == state_->remoteRequestId)
			    state_->remoteAccepted = true;
	    },
	    Qt::QueuedConnection);
	connect(
	    transport_.get(), &telemetry::integration::qt::DeviceTransport::packetReceived, this,
	    [this, epoch](const QByteArray& bytes, quint32 session, quint32 id) {
		    if (epoch != epoch_ || !state_ || !state_->remote ||
		        session != state_->inspector.session() || id != state_->remoteRequestId ||
		        !state_->replies.empty())
			    return;
		    const auto* first = reinterpret_cast<const std::byte*>(bytes.constData());
		    state_->replies.push_back({{first, first + bytes.size()}, session, id, state_->now()});
	    },
	    Qt::QueuedConnection);
	connect(
	    transport_.get(), &telemetry::integration::qt::DeviceTransport::transportError, this,
	    [this, epoch](const QString& error) {
		    transportFailed(epoch, error);
	    },
	    Qt::QueuedConnection);
}

bool InspectorController::serialAvailable() const noexcept
{
#ifdef INSPECTOR_ENABLE_SERIAL
	return true;
#else
	return false;
#endif
}

QStringList InspectorController::serialPorts() const
{
	QStringList ports;
#ifdef INSPECTOR_ENABLE_SERIAL
	for (const auto& port : QSerialPortInfo::availablePorts())
		ports.push_back(port.systemLocation());
#endif
	return ports;
}

bool InspectorController::connectSerial(const QString& port, qint32 baud)
{
#ifndef INSPECTOR_ENABLE_SERIAL
	Q_UNUSED(port);
	Q_UNUSED(baud);
	status_ =
	    QStringLiteral("Enable INSPECTOR_ENABLE_SERIAL with an installed SDK Qt serial transport.");
	Q_EMIT stateChanged();
	return false;
#else
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing() ||
	    port.trimmed().isEmpty() ||
	    baud < qint32(telemetry::integration::qt::SerialTransport::minBaudRate()) ||
	    baud > qint32(telemetry::integration::qt::SerialTransport::maxBaudRate()))
		return false;
	updating_ = true;
	const auto retiredApply = retire();
	const auto name = port.trimmed();
	if (retiredState_ &&
	    (!retiredSerial_ || name != retiredSerialPort_ || baud != retiredSerialBaud_)) {
		status_ = QStringLiteral("Reconnect the previous device first to finish session cleanup.");
		updating_ = false;
		notifyState(retiredApply);
		return false;
	}
	serial_ = true;
	serialPort_ = name;
	serialBaud_ = baud;
	connecting_ = true;
	status_ = QStringLiteral("Opening serial port and loading Descriptor...");
	transport_ = std::make_unique<telemetry::integration::qt::SerialTransport>();
	transport_->setWriteChunkBytes(serialWriteChunk_);
	auto* serial = static_cast<telemetry::integration::qt::SerialTransport*>(transport_.get());
	serial->setReadManyDiscovery(readManyDiscovery_);
	bindTransportSignals();
	serial->connectTo(name, baud);
	updating_ = false;
	notifyState(retiredApply);
	return true;
#endif
}

void InspectorController::setSerialWriteChunkBytes(int bytes) noexcept
{
	serialWriteChunk_ = std::max(0, bytes);
	if (serial_ && transport_)
		transport_->setWriteChunkBytes(serialWriteChunk_);
}

void InspectorController::transportDescriptor(quint64 epoch, QByteArray bytes)
{
	if (epoch != epoch_ || !transport_)
		return;
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing()) {
		QTimer::singleShot(0, this, [this, epoch, bytes = std::move(bytes)] {
			transportDescriptor(epoch, bytes);
		});
		return;
	}
	QPointer<InspectorController> alive{this};
	const bool admitted = replaceDescriptor(std::move(bytes), false, true);
	if (alive && epoch == epoch_ && !admitted)
		transportFailed(epoch, status_);
}

void InspectorController::transportFailed(quint64 epoch, QString error)
{
	if (epoch != epoch_)
		return;
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing()) {
		QTimer::singleShot(0, this, [this, epoch, error = std::move(error)] {
			transportFailed(epoch, error);
		});
		return;
	}
	// The transport preserves complete packets received before EOF. Apply them
	// before closing the logical operation; onReply/process still enforce its
	// deadline. The socket is already closed, so pump cannot authorize new TX.
	if (state_ && state_->remote && !state_->applying && !state_->executing && !state_->calling &&
	    !state_->replies.empty()) {
		QPointer<InspectorController> alive{this};
		pump();
		if (!alive || epoch != epoch_)
			return;
	}
	const bool readPending =
	    busy_ && state_ && !state_->applying && !state_->executing && !state_->calling;
	updating_ = true;
	const auto applied = retire();
	status_ = (serial_ ? QStringLiteral("Serial: %1") : QStringLiteral("TCP: %1")).arg(error);
	updating_ = false;
	notifyState(applied, readPending ? std::optional<bool>{false} : std::nullopt);
}

bool InspectorController::canRefresh() const noexcept
{
	return connected_ && !busy_ && !editing() && state_ && !commandSelected() &&
	       !serviceSelected() && state_->inspector.snapshotInfo().selected;
}

bool InspectorController::writeProfileSupported() const noexcept
{
	return connected_ && state_ && !state_->serial && (state_->remote || state_->demo);
}

bool InspectorController::editing() const noexcept
{
	return state_ && !commandSelected() && !serviceSelected() && state_->inspector.editing();
}

bool InspectorController::commandSelected() const noexcept
{
	return state_ && state_->inspector.commandSelected();
}

bool InspectorController::canExecute() const noexcept
{
	return writeProfileSupported() && writesEnabled_ && !busy_ && invalidEditors_.isEmpty() &&
	       state_->inspector.canExecute();
}

QString InspectorController::commandOutcome() const
{
	if (commandOutcome_.isEmpty())
		return {};
	const auto outcome =
	    QStringLiteral("Last Command %1 · %2").arg(commandOutcomeEndpoint_, commandOutcome_);
	if (!commandOutcomeSession_ || commandOutcomeSession_ == session_)
		return outcome;
	return QStringLiteral("Previous session %1 (current session %2) · %3")
	    .arg(commandOutcomeSession_)
	    .arg(session_)
	    .arg(outcome);
}

bool InspectorController::serviceSelected() const noexcept
{
	return state_ && state_->inspector.serviceSelected();
}

bool InspectorController::canCallService() const noexcept
{
	return writeProfileSupported() && writesEnabled_ && !busy_ && invalidEditors_.isEmpty() &&
	       state_->inspector.canCallService();
}

QString InspectorController::serviceOutcome() const
{
	if (serviceOutcome_.isEmpty())
		return {};
	const auto outcome =
	    QStringLiteral("Last Service %1 · %2").arg(serviceOutcomeEndpoint_, serviceOutcome_);
	if (!serviceOutcomeSession_ || serviceOutcomeSession_ == session_)
		return outcome;
	return QStringLiteral("Previous session %1 (current session %2) · %3")
	    .arg(serviceOutcomeSession_)
	    .arg(session_)
	    .arg(outcome);
}

bool InspectorController::canEdit() const noexcept
{
	return writeProfileSupported() && writesEnabled_ && !busy_ && !commandSelected() &&
	       !serviceSelected() && state_->inspector.canEdit();
}

bool InspectorController::canApply() const noexcept
{
	return writeProfileSupported() && writesEnabled_ && !busy_ && invalidEditors_.isEmpty() &&
	       state_->inspector.canApply();
}

void InspectorController::setWritesEnabled(bool enabled)
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing() || busy_)
		return;
	if (enabled && !writeProfileSupported()) {
		status_ = QStringLiteral(
		    "This transport profile supports read-only inspection. Descriptor write capability is still shown.");
		Q_EMIT stateChanged();
		return;
	}
	updating_ = true;
	if (!enabled && editing()) {
		state_->inspector.cancelEdit();
		invalidEditors_.clear();
		values_.resetSnapshot(state_->inspector);
	}
	writesEnabled_ = enabled;
	updating_ = false;
	Q_EMIT stateChanged();
}

bool InspectorController::beginEdit()
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing() ||
	    !canEdit())
		return false;
	const auto status = state_->inspector.beginEdit();
	if (status != rt::Status::Ok) {
		status_ = QStringLiteral("Could not start editing (runtime %1).")
		              .arg(static_cast<unsigned>(status));
		Q_EMIT stateChanged();
		return false;
	}
	updating_ = true;
	invalidEditors_.clear();
	values_.resetSnapshot(state_->inspector);
	status_ = QStringLiteral(
	    "Editing a local draft · Apply writes once and reads back the actual Field.");
	updating_ = false;
	Q_EMIT stateChanged();
	return true;
}

void InspectorController::cancelEdit()
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing() ||
	    !editing() || busy_)
		return;
	updating_ = true;
	state_->inspector.cancelEdit();
	invalidEditors_.clear();
	values_.resetSnapshot(state_->inspector);
	status_ = QStringLiteral("Draft discarded · device was not changed.");
	updating_ = false;
	Q_EMIT stateChanged();
}

bool InspectorController::setScalarText(const QModelIndex& index, const QString& text)
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing() ||
	    (!editing() && !commandSelected() && !serviceSelected()) || busy_)
		return false;
	const auto node = values_.node(index);
	if (!node)
		return false;
	const auto info = (commandSelected() || serviceSelected())
	                      ? state_->inspector.editorInfo(node.value)
	                      : state_->inspector.info(node.value);
	if (!info || (info.value.kind != telemetry::TypeKind::Scalar &&
	              info.value.kind != telemetry::TypeKind::Enum))
		return false;
	// Focusing and leaving a field is not an edit. In particular the displayed
	// NaN label does not encode its sign/payload/quiet bit, so never reparse it
	// unless the operator actually changes the text.
	if (text == values_.data(index, ValueModel::EditorTextRole).toString()) {
		invalidEditors_.remove(index.internalId());
		status_ = QStringLiteral("Draft value unchanged · original wire bits retained.");
		Q_EMIT stateChanged();
		return true;
	}
	rt::Status status = rt::Status::TypeMismatch;
	bool parsed = false;
	const auto set = [&]<class T>() {
		T number{};
		if ((parsed = parseScalar(text, number)))
			status = state_->inspector.setScalar(node.value, number);
	};
	using Code = telemetry::ScalarCode;
	switch (info.value.code) {
		case Code::Bool:
			set.template operator()<bool>();
			break;
		case Code::U8:
			set.template operator()<std::uint8_t>();
			break;
		case Code::S8:
			set.template operator()<std::int8_t>();
			break;
		case Code::U16:
			set.template operator()<std::uint16_t>();
			break;
		case Code::S16:
			set.template operator()<std::int16_t>();
			break;
		case Code::U32:
			set.template operator()<std::uint32_t>();
			break;
		case Code::S32:
			set.template operator()<std::int32_t>();
			break;
		case Code::U64:
			set.template operator()<std::uint64_t>();
			break;
		case Code::S64:
			set.template operator()<std::int64_t>();
			break;
		case Code::F32:
			set.template operator()<float>();
			break;
		case Code::F64:
			set.template operator()<double>();
			break;
	}
	if (!parsed || status != rt::Status::Ok) {
		invalidEditors_.insert(index.internalId());
		status_ =
		    parsed ? QStringLiteral("Draft edit refused (runtime %1).")
		                 .arg(static_cast<unsigned>(status))
		           : QStringLiteral("Invalid value for the exact scalar type · draft unchanged.");
		Q_EMIT stateChanged();
		return false;
	}
	updating_ = true;
	invalidEditors_.remove(index.internalId());
	values_.updateScalar(index);
	status_ = QStringLiteral("Draft changed · not yet written to the device.");
	updating_ = false;
	Q_EMIT stateChanged();
	return true;
}

bool InspectorController::apply()
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing() ||
	    !canApply())
		return false;
	const auto submitted = state_->inspector.apply(
	    [state = state_.get()](const wb::ApplyResult& result) noexcept {
		    state->applied = result;
	    },
	    static_cast<quint32>(readTimeout_), static_cast<quint32>(readTimeout_));
	if (!submitted) {
		status_ = QStringLiteral("Apply refused: %1.").arg(statusName(submitted.status));
		Q_EMIT stateChanged();
		return false;
	}
	updating_ = true;
	state_->applying = true;
	writeOutcomeSession_ = state_->inspector.session();
	busy_ = true;
	retireWatch(false);
	values_.clear();
	responses_.clear();
	writeOutcome_ = QStringLiteral("Pending · waiting for write result and readback.");
	status_ = QStringLiteral("Writing once, then refreshing from the device…");
	updating_ = false;
	Q_EMIT stateChanged();
	return true;
}

bool InspectorController::executeCommand()
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing() ||
	    !canExecute())
		return false;
	const auto submitted = state_->inspector.execute(
	    [state = state_.get()](const im::OperationResult& result) noexcept {
		    state->commanded = result;
	    },
	    static_cast<quint32>(readTimeout_));
	if (!submitted) {
		status_ = QStringLiteral("Execute refused: %1.").arg(statusName(submitted.status));
		Q_EMIT stateChanged();
		return false;
	}
	updating_ = true;
	state_->executing = true;
	commandOutcomeSession_ = state_->inspector.session();
	commandOutcomeEndpoint_ = selection_;
	busy_ = true;
	retireWatch(false);
	values_.clear();
	responses_.clear();
	commandOutcome_ = QStringLiteral("Pending · waiting for the Command result.");
	status_ = QStringLiteral("Sending the Command once · no automatic replay or readback.");
	updating_ = false;
	Q_EMIT stateChanged();
	return true;
}

bool InspectorController::callService()
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing() ||
	    !canCallService())
		return false;
	const auto submitted = state_->inspector.callService(
	    [state = state_.get()](const wb::ServiceResult& result) noexcept {
		    state->serviced = result;
	    },
	    static_cast<quint32>(readTimeout_));
	if (!submitted) {
		status_ = QStringLiteral("Call refused: %1.").arg(statusName(submitted.status));
		Q_EMIT stateChanged();
		return false;
	}
	updating_ = true;
	state_->calling = true;
	serviceOutcomeSession_ = state_->inspector.session();
	serviceOutcomeEndpoint_ = selection_;
	busy_ = true;
	retireWatch(false);
	values_.clear();
	responses_.clear();
	serviceOutcome_ = QStringLiteral("Pending · waiting for the Service result.");
	status_ = QStringLiteral("Calling the Service once · no automatic replay.");
	updating_ = false;
	Q_EMIT stateChanged();
	return true;
}

bool InspectorController::loadDescriptor(const QUrl& path)
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing())
		return false;
	QFile file(path.isLocalFile() ? path.toLocalFile() : path.toString());
	if (!file.open(QIODevice::ReadOnly)) {
		status_ = QStringLiteral("Cannot open Descriptor file: %1").arg(file.errorString());
		Q_EMIT stateChanged();
		return false;
	}
	if (file.size() < 64 || file.size() > telemetry::Limits::maxDescriptorBytes) {
		status_ = QStringLiteral("Descriptor file size is outside the supported bounds.");
		Q_EMIT stateChanged();
		return false;
	}
	const auto expected = file.size();
	const auto bytes = file.readAll();
	if (bytes.size() != expected) {
		status_ = QStringLiteral("Could not read the complete Descriptor file.");
		Q_EMIT stateChanged();
		return false;
	}
	return replaceDescriptor(bytes, false);
}

bool InspectorController::openDescriptorPath(const QString& path)
{
	return loadDescriptor(QUrl::fromLocalFile(QDir::fromNativeSeparators(path)));
}

bool InspectorController::selectEndpoint(const QModelIndex& index)
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing() ||
	    !state_ || editing() || state_->applying || state_->executing || state_->calling)
		return false;
	const auto endpoint = endpoints_.endpoint(index);
	if (endpoint.isEmpty())
		return false;
	if (endpoint.value(QStringLiteral("category")).toInt() == 3) {
		const auto binding =
		    state_->inspector.bindService(endpoint.value(QStringLiteral("id")).toUInt());
		if (!binding || state_->inspector.selectService(binding.endpoint) != cl::Status::Ok)
			return false;
		updating_ = true;
		invalidEditors_.clear();
		values_.resetSnapshot(state_->inspector);
		responses_.resetServiceResponse(state_->inspector);
		selection_ = endpoint.value(QStringLiteral("path")).toString();
		status_ = QStringLiteral(
		    "Service Request · client-initialized zero arguments · Call submits one request.");
		updating_ = false;
		Q_EMIT stateChanged();
		return true;
	}
	if (endpoint.value(QStringLiteral("category")).toInt() == 2) {
		const auto binding =
		    state_->inspector.bindCommand(endpoint.value(QStringLiteral("id")).toUInt());
		const auto prepared =
		    binding ? state_->inspector.selectCommand(binding.endpoint) : binding.status;
		if (prepared != cl::Status::Ok) {
			status_ =
			    QStringLiteral("Cannot prepare Command arguments: %1.").arg(statusName(prepared));
			Q_EMIT stateChanged();
			return false;
		}
		updating_ = true;
		invalidEditors_.clear();
		values_.resetSnapshot(state_->inspector);
		responses_.resetServiceResponse(state_->inspector);
		selection_ = endpoint.value(QStringLiteral("path")).toString();
		status_ = QStringLiteral(
		    "Command Request · client-initialized zero arguments · Execute sends once.");
		updating_ = false;
		Q_EMIT stateChanged();
		return true;
	}
	if (endpoint.value(QStringLiteral("category")).toInt() != 1 ||
	    !endpoint.value(QStringLiteral("readable")).toBool()) {
		updating_ = true;
		state_->inspector.clearSelection();
		values_.clear();
		responses_.resetServiceResponse(state_->inspector);
		selection_ = endpoint.value(QStringLiteral("path")).toString();
		status_ = QStringLiteral("Metadata only · this endpoint cannot be read as a Field.");
		updating_ = false;
		Q_EMIT stateChanged();
		return true;
	}
	const auto binding = state_->inspector.bindField(endpoint.value(QStringLiteral("id")).toUInt());
	if (!binding || state_->inspector.selectField(binding.endpoint) != cl::Status::Ok)
		return false;
	updating_ = true;
	values_.clear();
	responses_.resetServiceResponse(state_->inspector);
	selection_ = endpoint.value(QStringLiteral("path")).toString();
	status_ = connected_ ? QStringLiteral("Field selected · press Refresh to read it")
	                     : QStringLiteral("Field metadata · no transport connected");
	updating_ = false;
	Q_EMIT stateChanged();
	return true;
}

bool InspectorController::refresh()
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing() ||
	    !state_ || !connected_ || busy_ || editing() || commandSelected() || serviceSelected())
		return false;
	auto readTimeout = static_cast<quint32>(readTimeout_);
#ifdef INSPECTOR_ENABLE_SERIAL
	if (state_->serial) {
		const auto* type = state_->schema.type(state_->inspector.snapshotInfo().type);
		if (!type ||
		    type->wireBytes > telemetry::integration::qt::SerialTransport::maxBodyBytes() - 32u) {
			status_ = QStringLiteral("Selected Field exceeds the serial frame payload limit.");
			Q_EMIT stateChanged();
			return false;
		}
		if (!readTimeoutExplicit_)
			readTimeout = telemetry::integration::qt::SerialTransport::exchangeTimeoutMs(
			    32u, 32u + type->wireBytes, static_cast<std::uint32_t>(serialBaud_), 3000);
	}
#endif
	const auto submitted = state_->inspector.refresh(
	    [state = state_.get()](const im::ReadResult& result) noexcept {
		    // Never emit Qt signals inside a core callback: slots may replace the
		    // session or destroy this controller. Publish after returning from core.
		    state->completed = result;
	    },
	    readTimeout);
	if (!submitted) {
		status_ = QStringLiteral("Read refused: %1 (model %2)")
		              .arg(statusName(submitted.status))
		              .arg(static_cast<unsigned>(submitted.modelStatus));
		Q_EMIT stateChanged();
		return false;
	}
	updating_ = true;
	values_.clear();
	busy_ = true;
	status_ = QStringLiteral("Reading…");
	updating_ = false;
	Q_EMIT stateChanged();
	return true;
}

bool InspectorController::canAddWatch() const noexcept
{
	if (updating_ || watchActive() || !connected_ || !state_ || !state_->inspector.isCurrent() ||
	    watchlist_.count() >= 32 || commandSelected() || serviceSelected())
		return false;
	const auto selected = state_->inspector.snapshotInfo();
	const auto* type = selected.selected ? state_->schema.type(selected.type) : nullptr;
	if (!type || type->wireBytes == 0 || type->wireBytes > 8 ||
	    (type->kind != telemetry::TypeKind::Scalar && type->kind != telemetry::TypeKind::Enum))
		return false;
	for (int row = 0; row < watchlist_.count(); ++row)
		if (watchlist_.id(row) == selected.fieldId)
			return false;
	return true;
}

bool InspectorController::addWatch()
{
	if (!canAddWatch() || watchlist_.changing())
		return false;
	updating_ = true;
	const auto added = watchlist_.add(state_->inspector.snapshotInfo().fieldId, selection_);
	if (added)
		++watchRevision_;
	updating_ = false;
	Q_EMIT stateChanged();
	return added;
}

bool InspectorController::removeWatch(int row)
{
	if (updating_ || watchlist_.changing() || watchActive())
		return false;
	updating_ = true;
	const auto removed = watchlist_.remove(row);
	if (removed)
		++watchRevision_;
	updating_ = false;
	Q_EMIT stateChanged();
	return removed;
}

void InspectorController::clearWatch()
{
	if (updating_ || watchlist_.changing() || watchActive())
		return;
	updating_ = true;
	retireWatch(true);
	if (state_)
		watchlist_.bind(state_->inspector);
	updating_ = false;
	Q_EMIT stateChanged();
}

void InspectorController::retireWatch(bool clear)
{
	watchRunning_ = watchCycle_ = false;
	// Revoking admission is callback-free. Preserve the accepted-read flag
	// until consumeWatch sees its completion or closeAndDrain really finishes.
	watchCursor_ = 0;
	++watchRevision_;
	if (clear)
		watchlist_.clear();
	else
		watchlist_.invalidateValues();
	watchStatus_ = QStringLiteral("Stopped - no current watch values.");
}

void InspectorController::invalidateSession(State& state)
{
	// Called only after backend processing returned, under the owner update
	// guard. Notify live QML role caches as well as guarding future data() calls.
	if (transport_)
		transport_->close();
	state.inspector.invalidate();
	retireWatch(false);
	connected_ = false;
	values_.clear();
	responses_.clear();
	endpoints_.clear();
	selection_.clear();
}

void InspectorController::setWatchIntervalMs(int milliseconds)
{
	if (updating_ || watchlist_.changing() || watchActive())
		return;
	watchIntervalMs_ = std::clamp(milliseconds, 100, 10000);
	if (state_)
		watchDue_ = state_->now() + quint32(watchIntervalMs_);
	Q_EMIT stateChanged();
}

void InspectorController::setWatchIndividual(bool individual)
{
	if (updating_ || watchActive() || watchIndividual_ == individual)
		return;
	watchIndividual_ = individual;
	++watchRevision_;
	Q_EMIT stateChanged();
}

void InspectorController::setReadManyDiscovery(bool enabled)
{
	if (updating_ || connecting_ || readManyDiscovery_ == enabled)
		return;
	readManyDiscovery_ = enabled;
	Q_EMIT stateChanged();
}

bool InspectorController::startWatch()
{
	if (updating_ || watchlist_.changing() || watchActive() || !state_ || !connected_ ||
	    !state_->inspector.isCurrent() || watchlist_.count() == 0)
		return false;
	watchCycle_ = true;
	watchCursor_ = 0;
	if (!submitWatch()) {
		watchCycle_ = false;
		Q_EMIT stateChanged();
		return false;
	}
	watchRunning_ = true;
	watchDue_ = state_->now() + quint32(watchIntervalMs_);
	Q_EMIT stateChanged();
	return true;
}

void InspectorController::stopWatch()
{
	if (updating_ || watchlist_.changing())
		return;
	watchRunning_ = false;
	watchCycle_ = false;
	++watchRevision_;
	// A read already authorized to the transport is drained, not replayed or
	// cancelled into unnecessary session recovery. Its superseded result is ignored.
	watchStatus_ = watchPending_ ? QStringLiteral("Stopping - draining the current read.")
	                             : QStringLiteral("Stopped.");
	Q_EMIT stateChanged();
}

bool InspectorController::refreshWatch()
{
	if (updating_ || watchlist_.changing() || watchActive() || !state_ || !connected_ ||
	    !state_->inspector.isCurrent() || watchlist_.count() == 0)
		return false;
	watchCycle_ = true;
	watchCursor_ = 0;
	const auto accepted = submitWatch();
	if (!accepted)
		watchCycle_ = false;
	Q_EMIT stateChanged();
	return accepted;
}

bool InspectorController::submitWatch()
{
	if (!state_ || !connected_ || watchPending_ || busy_ || watchlist_.count() == 0)
		return false;
	auto& state = *state_;
	const auto revision = watchRevision_;
	auto timeout = quint32(readTimeout_);
	cl::Submission submitted;
	if (!watchIndividual_) {
		if (!state.capability) {
			watchStatus_ = QStringLiteral(
			    "ReadMany unavailable. Explicitly select Individual FieldRead, or reconnect with TRM1 discovery.");
			return false;
		}
		std::array<std::uint32_t, 32> ids{};
		for (int row = 0; row < watchlist_.count(); ++row)
			ids[row] = watchlist_.id(row);
		const auto binding =
		    rm::bindReadMany(state.inspector, state.backend, *state.capability,
		                     std::span<const std::uint32_t>{ids}.first(watchlist_.count()));
		if (!binding) {
			watchStatus_ = QStringLiteral("ReadMany refused: %1").arg(statusName(binding.status));
			return false;
		}
#ifdef INSPECTOR_ENABLE_SERIAL
		if (state.serial && !readTimeoutExplicit_)
			timeout = telemetry::integration::qt::SerialTransport::exchangeTimeoutMs(
			    34u + 4u * watchlist_.count(), 32u + binding.binding.responseBytes(),
			    quint32(serialBaud_), 3000);
#endif
		submitted = rm::readMany(
		    state.inspector, binding.binding,
		    [owner = &state, revision](const rm::ReadManyResult& result) noexcept {
			    State::WatchCompletion completion;
			    completion.revision = revision;
			    completion.status = result.operation.status;
			    if (result.operation.succeeded() && result.valueStatus != rm::ValueStatus::Ok)
				    completion.status = cl::Status::CodecError;
			    else if (result.operation.status == cl::Status::Ok && !result.operation.succeeded())
				    completion.status = cl::Status::CodecError;
			    completion.count = result.count;
			    for (int row = 0; row < completion.count; ++row) {
				    const auto& item = result.items[row];
				    auto& out = completion.rows[row];
				    out.id = item.id;
				    switch (item.status) {
					    case rm::wire::ItemStatus::Ok:
						    out.status =
						        item.hasValue() ? QStringLiteral("Ok") : QStringLiteral("Stale");
						    break;
					    case rm::wire::ItemStatus::NotFound:
						    out.status = QStringLiteral("NotFound");
						    break;
					    case rm::wire::ItemStatus::UnsupportedType:
						    out.status = QStringLiteral("UnsupportedType");
						    break;
					    case rm::wire::ItemStatus::Unavailable:
						    out.status = QStringLiteral("Unavailable");
						    break;
					    case rm::wire::ItemStatus::ReadError:
						    out.status = QStringLiteral("ReadError");
						    break;
				    }
				    if (item.hasValue())
					    out.text = WatchlistModel::formatValue(item.value);
			    }
			    // Only owned strings cross the callback boundary; no borrowed DTO view.
			    owner->watched = std::move(completion);
		    },
		    timeout);
	} else {
		const auto id = watchlist_.id(watchCursor_);
		const auto binding = state.inspector.bindField(id);
		if (!binding) {
			watchStatus_ = QStringLiteral("FieldRead refused: %1").arg(statusName(binding.status));
			return false;
		}
#ifdef INSPECTOR_ENABLE_SERIAL
		if (state.serial && !readTimeoutExplicit_)
			timeout = telemetry::integration::qt::SerialTransport::exchangeTimeoutMs(
			    32u, 40u, quint32(serialBaud_), 3000);
#endif
		submitted = rm::readFieldValue(
		    state.inspector, binding.endpoint,
		    [owner = &state, revision,
		     id](const telemetry::integration::dynamic::DynamicResult& result) noexcept {
			    State::WatchCompletion completion;
			    completion.revision = revision;
			    completion.status = result.operation.status;
			    completion.count = 1;
			    if (result.operation.succeeded() && !result.hasValue())
				    completion.status = cl::Status::CodecError;
			    auto& out = completion.rows[0];
			    out.id = id;
			    out.status = !result.operation.succeeded()
			                     ? QStringLiteral("%1 (dispatch %2, endpoint %3)")
			                           .arg(result.operation.status == cl::Status::Ok
			                                    ? QStringLiteral("Field refused")
			                                    : statusName(result.operation.status))
			                           .arg(result.operation.dispatch)
			                           .arg(result.operation.endpointStatus)
			                 : result.hasValue() ? QStringLiteral("Ok")
			                                     : QStringLiteral("NoValue");
			    if (result.hasValue())
				    out.text = WatchlistModel::formatValue(result.value);
			    owner->watched = std::move(completion);
		    },
		    timeout);
	}
	if (!submitted) {
		watchStatus_ = QStringLiteral("Watch read refused: %1").arg(statusName(submitted.status));
		return false;
	}
	watchPending_ = true;
	watchStatus_ = watchIndividual_
	                   ? QStringLiteral("Reading individual Field %1 of %2...")
	                         .arg(watchCursor_ + 1)
	                         .arg(watchlist_.count())
	                   : QStringLiteral("ReadMany pending - %1 Fields").arg(watchlist_.count());
	return true;
}

bool InspectorController::consumeWatch(State& state)
{
	if (!state.watched)
		return false;
	const auto completion = std::move(*state.watched);
	state.watched.reset();
	watchPending_ = false;
	if (!state.inspector.isCurrent() || completion.revision != watchRevision_) {
		watchCycle_ = false;
		watchStatus_ = QStringLiteral("Stopped - superseded read drained.");
	} else if (completion.status != cl::Status::Ok) {
		watchRunning_ = watchCycle_ = false;
		watchlist_.invalidateValues();
		watchStatus_ = QStringLiteral("Watch read: %1").arg(statusName(completion.status));
	} else {
		for (int item = 0; item < completion.count; ++item)
			for (int row = 0; row < watchlist_.count(); ++row)
				if (watchlist_.id(row) == completion.rows[item].id)
					watchlist_.setResult(row, completion.rows[item].status,
					                     completion.rows[item].text);
		if (!watchIndividual_ || ++watchCursor_ >= watchlist_.count()) {
			watchCycle_ = false;
			watchStatus_ = QStringLiteral("%1 complete - %2 Fields - session %3")
			                   .arg(watchIndividual_ ? QStringLiteral("Individual FieldRead cycle")
			                                         : QStringLiteral("ReadMany"))
			                   .arg(watchlist_.count())
			                   .arg(state.inspector.session());
		}
	}
	const bool fatal = completion.status == cl::Status::TransportError ||
	                   completion.status == cl::Status::MalformedReply ||
	                   completion.status == cl::Status::SessionLost;
	if (state.remote && transport_ && (state.inspector.recoveryRequired() || fatal)) {
		invalidateSession(state);
		if (completion.status != cl::Status::Ok)
			watchStatus_ = QStringLiteral("Watch read: %1").arg(statusName(completion.status));
	}
	return true;
}

void InspectorController::pump()
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing() ||
	    !state_)
		return;
	updating_ = true;
	auto& state = *state_;
	const auto now = state.now();
	state.inspector.process(now);
	if (state.remoteAccepted) {
		state.remoteAccepted = false;
		(void)state.inspector.onSendAccepted(state.inspector.session(), state.remoteRequestId);
	}
	const auto packet = state.inspector.nextPacket();
	if (state.remote && transport_ && packet && transport_->canStage() &&
	    transport_->stage(packet)) {
		if (state.inspector.prepareHandoff(packet.session, packet.requestId)) {
			if (mutationPacket(packet))
				state.mutationAuthorized = true;
			state.remoteRequestId = packet.requestId;
			if (!transport_->sendStaged())
				state.inspector.invalidate();
		} else {
			transport_->discardStaged();
		}
	}
	if (state.demo && packet && packet.bytes.size() <= state.port.size()) {
		std::copy(packet.bytes.begin(), packet.bytes.end(), state.port.begin());
		if (state.inspector.prepareHandoff(packet.session, packet.requestId) &&
		    state.inspector.onSendAccepted(packet.session, packet.requestId)) {
			if (mutationPacket(packet))
				state.mutationAuthorized = true;
			const auto written = state.peer.dispatch(
			    cl::Input{state.port}.first(packet.bytes.size()), state.response, packet.session);
			if (written && !dropReplies_)
				state.replies.push_back({{state.response.begin(), state.response.begin() + written},
				                         packet.session,
				                         packet.requestId,
				                         now + static_cast<quint32>(replyDelay_)});
		}
	}
	for (auto i = state.replies.begin(); i != state.replies.end();) {
		if (!cl::expired(now, i->due)) {
			++i;
			continue;
		}
		(void)state.inspector.onReply(i->bytes, i->session, i->id);
		i = state.replies.erase(i);
	}
	state.inspector.process(now);
	const auto completion = state.completed;
	state.completed.reset();
	const bool stale = !state.inspector.isCurrent();
	bool notify = false, published = false;
	if (stale) {
		if (connected_ || !selection_.isEmpty() || endpoints_.rowCount()) {
			retireWatch(false);
			values_.clear();
			responses_.clear();
			endpoints_.clear();
			selection_.clear();
			connected_ = false;
			status_ = QStringLiteral("Schema or session changed · reconnect to read again");
			notify = true;
		}
	}
	if (completion) {
		busy_ = false;
		published = !stale && completion->published && completion->operation.succeeded();
		if (published) {
			values_.resetSnapshot(state.inspector);
			status_ = QStringLiteral("Read complete · %1 bytes")
			              .arg(state.inspector.snapshotInfo().bytes);
		} else if (!stale) {
			status_ = QStringLiteral("Read finished: %1 · dispatch %2 · endpoint %3 · value %4")
			              .arg(statusName(completion->operation.status))
			              .arg(completion->operation.dispatch)
			              .arg(completion->operation.endpointStatus)
			              .arg(static_cast<unsigned>(completion->valueStatus));
		}
		const auto outcome = completion->operation.status;
		const bool fatalTransport =
		    outcome == cl::Status::TransportError || outcome == cl::Status::MalformedReply ||
		    outcome == cl::Status::SessionLost || outcome == cl::Status::OutcomeUnknown;
		if (state.remote && transport_ && (state.inspector.recoveryRequired() || fatalTransport)) {
			invalidateSession(state);
		}
		notify = true;
	}
	const auto applied = consumeApply(state, true);
	const auto commanded = consumeCommand(state, true);
	const auto serviced = consumeService(state, true);
	notify = consumeWatch(state) || notify;
	if (watchRunning_ && cl::expired(now, watchDue_)) {
		watchDue_ = now + quint32(watchIntervalMs_);
		if (!watchPending_ && !watchCycle_) {
			watchCycle_ = true;
			watchCursor_ = 0;
		}
	}
	if (watchCycle_ && !watchPending_ && !busy_) {
		if (submitWatch())
			notify = true;
		else {
			watchCycle_ = false;
			notify = true;
		}
	}
	notify = notify || applied.has_value() || commanded.has_value() || serviced.has_value();
	// All backend calls have returned before application signals can run slots.
	updating_ = false;
	if (notify)
		notifyState({applied, commanded, serviced},
		            completion ? std::optional<bool>{published} : std::nullopt);
}

void InspectorController::disconnectDevice()
{
	if (updating_ || values_.changing() || responses_.changing() || endpoints_.changing())
		return;
	updating_ = true;
	const auto applied = retire();
	status_ = QStringLiteral("Disconnected · no cached values");
	updating_ = false;
	notifyState(applied);
}

void InspectorController::setReplyDelay(int milliseconds) noexcept
{
	replyDelay_ = std::clamp(milliseconds, 0, 60000);
}

void InspectorController::setDropReplies(bool drop) noexcept
{
	dropReplies_ = drop;
}

void InspectorController::setRejectWrites(bool reject) noexcept
{
	if (state_ && state_->demo)
		state_->peer.setRejectWrites(reject);
}

quint64 InspectorController::demoWriteCount() const noexcept
{
	return state_ && state_->demo ? state_->peer.writeCount() : 0;
}

quint64 InspectorController::demoCommandCount() const noexcept
{
	return state_ && state_->demo ? state_->peer.commandCount() : 0;
}

quint64 InspectorController::demoExecutedCount() const noexcept
{
	return state_ && state_->demo ? state_->peer.executedCount() : 0;
}

quint64 InspectorController::demoAcceptedCount() const noexcept
{
	return state_ && state_->demo ? state_->peer.acceptedCount() : 0;
}

quint64 InspectorController::demoCommandCounter() const noexcept
{
	return state_ && state_->demo ? state_->peer.lastCommandCounter() : 0;
}

float InspectorController::demoCommandGain() const noexcept
{
	return state_ && state_->demo ? state_->peer.lastCommandGain() : 0;
}

quint64 InspectorController::demoServiceCount() const noexcept
{
	return state_ && state_->demo ? state_->peer.serviceCount() : 0;
}

void InspectorController::setReadTimeout(int milliseconds) noexcept
{
	readTimeout_ = std::clamp(milliseconds, 1, 60000);
	readTimeoutExplicit_ = true;
}

void InspectorController::setTcpWriteChunkBytes(int bytes) noexcept
{
	tcpWriteChunk_ = std::max(0, bytes);
	if (!serial_ && transport_)
		transport_->setWriteChunkBytes(tcpWriteChunk_);
}

Workbench& InspectorController::inspector() noexcept
{
	if (!state_)
		std::abort();
	return state_->inspector;
}

} // namespace inspector_qml
