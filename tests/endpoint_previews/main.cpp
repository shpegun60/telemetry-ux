// Controlled clock/native completions over actual parsed descriptor bytes. MIT.
#include <Telemetry/UX/EndpointPreviews.hpp>
#include "../dynamic_qml_configurator/Fixture.hpp"
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <functional>
#include <utility>

namespace ux = telemetry::ux;
namespace cl = telemetry::client;
namespace rt = telemetry::runtime;
namespace fixture = dynamic_qml_fixture;

namespace bounds_fixture {
// This model is used only to create honest schema sizes and identities. No
// getter is invoked by these source-controlled cache/scheduling checks.
struct Owner {
	std::array<std::uint8_t, 8192> large{};
	std::array<std::uint8_t, 8193> oversized{};
	std::uint64_t scalar() const noexcept { return UINT64_MAX; }
	const auto& block() const noexcept { return large; }
	const auto& tooLarge() const noexcept { return oversized; }
	fixture::Empty empty() const noexcept { return {}; }
	fixture::Mode mode() const noexcept { return fixture::Mode::Reverse; }
	bool flag() const noexcept { return true; }
};
inline Owner owner;
inline constexpr auto names = [] {
	std::array<std::array<char, 5>, 70> result{};
	for (unsigned n = 0; n < result.size(); ++n)
		result[n] = {'F', char('0' + n / 10), char('0' + n % 10), '\0', '\0'};
	return result;
}();
template<std::size_t... I>
constexpr auto scalars(std::index_sequence<I...>) {
	return telemetry::FieldTable{telemetry::field<&Owner::scalar>(names[I].data(), owner)...};
}
template<std::size_t... I>
constexpr auto blocks(std::index_sequence<I...>) {
	return telemetry::FieldTable{telemetry::field<&Owner::block>(names[I].data(), owner)...};
}
inline constexpr auto scalarFields = scalars(std::make_index_sequence<70>{});
inline constexpr auto blockFields = blocks(std::make_index_sequence<40>{});
inline constexpr telemetry::FieldTable specialFields{
    telemetry::field<&Owner::empty>("Empty", owner),
    telemetry::field<&Owner::tooLarge>("Oversized", owner),
    telemetry::field<&Owner::mode>("Mode", owner),
    telemetry::field<&Owner::flag>("Bool", owner)};
inline constexpr telemetry::FieldCatalogTable fields{
    telemetry::group("scalars", scalarFields), telemetry::group("blocks", blockFields),
    telemetry::group("special", specialFields)};
inline constexpr telemetry::ServiceCatalogTable<> noServices{};
inline constexpr telemetry::Model model{fields, telemetry::emptyCommands, noServices};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();
struct Parsed {
	std::array<std::byte, descriptorBytes.size()> bytes = descriptorBytes;
	std::array<rt::RuntimeType, 32> types{};
	std::array<rt::RuntimeMember, 8> members{};
	std::array<rt::RuntimeEnumEntry, 16> enums{};
	std::array<rt::RuntimeCatalog, 8> catalogs{};
	std::array<rt::RuntimeEndpoint, 128> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
	rt::RuntimeSchema schema;
	Parsed() {
		const auto parsed = rt::RuntimeSchema::parse(bytes, workspace);
		CHECK(parsed);
		schema = parsed.schema;
	}
};
constexpr quint32 blockId(unsigned n) { return 0x10000u + n; }
constexpr quint32 emptyId = 0x20000u, oversizedId = 0x20001u,
                  modeId = 0x20002u, boolId = 0x20003u;
} // namespace bounds_fixture

namespace {
template<class T>
QByteArray encoded(const T& value) {
	QByteArray bytes(qsizetype(telemetry::wireSize<T>), '\0');
	CHECK(telemetry::encode(value, std::span<std::byte>{
	          reinterpret_cast<std::byte*>(bytes.data()), std::size_t(bytes.size())}) ==
	      telemetry::CodecStatus::Ok);
	return bytes;
}

struct Peer {
	rt::RuntimeSchema schema;
	qint64 now = 0;
	bool admitted = true, busy = false, inCallback = false;
	quint32 session = 17;
	cl::Status refusal = cl::Status::Ok;
	QSet<quint32> watched;
	QList<quint32> calls;
	ux::EndpointPreviewSource::Completion completion;
	std::function<void()> onRead;
	std::shared_ptr<void> backing;

	ux::EndpointPreviewSource source() {
		ux::EndpointPreviewSource out;
		out.backing = backing;
		out.current = [this] { return admitted; };
		out.busy = [this] { return busy; };
		out.schema = [this] { return schema; };
		out.session = [this] { return session; };
		out.watched = [this](quint32 id) { return watched.contains(id); };
		out.read = [this](quint32 id, ux::EndpointPreviewSource::Completion done) {
			CHECK(admitted && !busy && !completion);
			calls.append(id);
			if (refusal != cl::Status::Ok) return cl::Submission{refusal, {}};
			completion = std::move(done);
			busy = true;
			if (onRead) onRead();
			return cl::Submission{cl::Status::Ok, {}};
		};
		return out;
	}
	void finish(QByteArray bytes, bool hasValue = true, cl::Status status = cl::Status::Ok) {
		CHECK(completion && busy);
		inCallback = true;
		auto done = std::exchange(completion, {});
		busy = false;
		*done = ux::EndpointPreviewResult{status, hasValue, std::move(bytes), {}};
		inCallback = false;
	}
	telemetry::TypeId type(quint32 id) const {
		const auto fields = schema.endpoints(telemetry::descriptor::Category::Field);
		const auto field = std::find_if(fields.begin(), fields.end(),
		                                [id](const auto& item) { return item.id == id; });
		CHECK(field != fields.end());
		return field->typeId;
	}
};

QString state(ux::EndpointPreviews& previews, quint32 id) {
	return previews.info(id).value(QStringLiteral("state")).toString();
}
bool hasValue(ux::EndpointPreviews& previews, quint32 id) {
	return previews.info(id).value(QStringLiteral("hasValue")).toBool();
}
void bind(ux::EndpointPreviews& previews, Peer& peer) {
	QObject::connect(&previews, &ux::EndpointPreviews::changed, &previews,
	                 [&peer] { CHECK(!peer.inCallback); });
	previews.setSource(peer.source());
	CHECK(previews.current());
}

void schedulingAndOwnedValues() {
	auto parsed = std::make_shared<fixture::Backing>();
	Peer peer;
	peer.schema = parsed->schema;
	peer.backing = parsed;
	ux::EndpointPreviews previews(nullptr, [&] { return peer.now; });
	bind(previews, peer);
	QObject view;
	// ID zero is a real root Config Field. Metadata/data access do not submit.
	CHECK(previews.setDemand(&view, {0, 1, 0, 0xffffffffu}));
	CHECK(peer.calls.isEmpty() && state(previews, 0) == "Queued");
	CHECK(!previews.info(0).isEmpty() && previews.value(0, peer.type(0), 0).isEmpty());
	CHECK(peer.calls.isEmpty());
	peer.busy = true; // foreground and Watch own priority at the common FIFO.
	previews.poll();
	CHECK(peer.calls.isEmpty());
	peer.busy = false;
	previews.poll();
	CHECK(peer.calls == QList<quint32>{0} && previews.pending());
	CHECK(state(previews, 0) == "Reading");
	for (unsigned n = 0; n < 20; ++n) previews.poll();
	CHECK(peer.calls.size() == 1);
	const auto config = encoded(fixture::device.config);
	peer.finish(config);
	CHECK(state(previews, 0) == "Reading" && !hasValue(previews, 0));
	previews.poll();
	CHECK(hasValue(previews, 0) && state(previews, 0) == "Current");
	// A folded struct reads as its first members, as values.bin writes it.
	const auto summary = previews.value(0, peer.type(0), 0);
	CHECK(summary.startsWith(QLatin1Char('{')) && summary.endsWith(QLatin1Char('}')) && summary.contains(QStringLiteral("…")));
	const auto* structure = peer.schema.type(peer.type(0));
	CHECK(structure && structure->memberCount == 7);
	bool serial = false, mode = false;
	for (const auto& member : peer.schema.members(peer.type(0))) {
		if (member.name == "serial") {
			CHECK(previews.value(0, member.typeId, member.wireOffset) == "18446744073709551615");
			serial = true;
		}
		if (member.name == "mode") {
			CHECK(previews.value(0, member.typeId, member.wireOffset) == "-123 (no name)");
			mode = true;
		}
	}
	CHECK(serial && mode);
	peer.now = 499;
	previews.poll();
	CHECK(peer.calls.size() == 1);
	peer.now = 500;
	previews.poll();
	CHECK(peer.calls == QList<quint32>({0, 1}));
	QByteArray borrowed = encoded(UINT64_MAX);
	peer.finish(borrowed);
	borrowed.fill(char(0x55));
	previews.poll();
	CHECK(previews.value(1, peer.type(1), 0) == "18446744073709551615");
	CHECK(previews.info(1).value("ageMs").toLongLong() == 0);
	peer.now = 4999;
	previews.poll();
	CHECK(peer.calls.size() == 2 && state(previews, 0) == "Current");
	peer.now = 5000;
	CHECK(state(previews, 0) == "Stale");
	// A scan at 4999 consumes the global spacing interval, even if TTL excludes all.
	previews.poll();
	CHECK(peer.calls.size() == 2);
	peer.now = 5499;
	previews.poll();
	CHECK(peer.calls.size() == 3 && peer.calls.last() == 0);
	peer.finish({}, false);
	previews.poll();
	CHECK(state(previews, 0) == "Unavailable" && !hasValue(previews, 0));
	CHECK(previews.value(0, peer.type(0), 0).isEmpty());
}

void refusalAndPayloadValidation() {
	const auto parsed = std::make_shared<bounds_fixture::Parsed>();
	Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
	ux::EndpointPreviews previews(nullptr, [&] { return peer.now; });
	bind(previews, peer);
	QObject view;
	CHECK(previews.setDemand(&view, {bounds_fixture::emptyId}));
	peer.refusal = cl::Status::QueueFull;
	previews.poll();
	CHECK(peer.calls.size() == 1 && !previews.pending());
	CHECK(state(previews, bounds_fixture::emptyId) == "Queued");
	CHECK(!previews.info(bounds_fixture::emptyId).value("reason").toString().isEmpty());
	peer.now = 499; previews.poll(); CHECK(peer.calls.size() == 1);
	peer.now = 500; peer.refusal = cl::Status::Ok; previews.poll();
	CHECK(peer.calls.size() == 2 && previews.pending());
	peer.finish({}, true);
	previews.poll();
	CHECK(hasValue(previews, bounds_fixture::emptyId));
	CHECK(previews.value(bounds_fixture::emptyId, peer.type(bounds_fixture::emptyId), 0) == "{}");
	previews.invalidateValues();
	peer.now += 500; previews.poll(); peer.finish({}, false); previews.poll();
	CHECK(!hasValue(previews, bounds_fixture::emptyId));
	CHECK(state(previews, bounds_fixture::emptyId) == "Unavailable");
	CHECK(previews.setDemand(&view, {bounds_fixture::boolId}));
	peer.now += 500; previews.poll(); peer.finish(QByteArray(1, char(2))); previews.poll();
	CHECK(!hasValue(previews, bounds_fixture::boolId));
	CHECK(previews.info(bounds_fixture::boolId).value("reason") == "Invalid preview payload.");
	CHECK(previews.setDemand(&view, {0}));
	peer.now += 500; previews.poll(); peer.finish(QByteArray(9, '\0')); previews.poll();
	CHECK(!hasValue(previews, 0));
	CHECK(previews.info(0).value("reason") == "Invalid preview payload.");
	previews.invalidateValues(); peer.now += 500; previews.poll();
	peer.finish(encoded(UINT64_MAX), true, cl::Status::TransportError); previews.poll();
	CHECK(!hasValue(previews, 0) && previews.cachedBytes() == 0);
}

void cacheBoundsAndPreflight() {
	const auto parsed = std::make_shared<bounds_fixture::Parsed>();
	{
		Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
		ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
		QObject view;
		CHECK(previews.setDemand(&view, {bounds_fixture::oversizedId}));
		previews.poll(); CHECK(peer.calls.isEmpty() && previews.cachedFields() == 0);
		CHECK(state(previews, bounds_fixture::oversizedId) == "Open Details");
		QList<quint32> ids; for (quint32 id = 0; id < 65; ++id) ids.append(id);
		CHECK(previews.setDemand(&view, ids));
		for (unsigned n = 0; n < 64; ++n) {
			peer.now += 500; previews.poll();
			CHECK(peer.calls.size() == int(n + 1));
			peer.finish(encoded(UINT64_MAX)); previews.poll();
			CHECK(previews.cachedFields() <= ux::EndpointPreviews::fieldLimit);
		}
		CHECK(previews.cachedFields() == 64 && previews.cachedBytes() == 64 * 8);
		peer.now += 500; previews.poll();
		// Refresh of an existing demanded Field is allowed; the 65th must never
		// reach read() while all 64 cache slots belong to live demand.
		CHECK(!peer.calls.contains(64));
		if (peer.completion) { peer.finish(encoded(UINT64_MAX)); previews.poll(); }
		CHECK(state(previews, 64) == "Preview limit");
		CHECK(previews.setDemand(&view, {64}));
		for (unsigned n = 0; n < 65 && !peer.calls.contains(64); ++n) {
			peer.now += 500; previews.poll();
			if (peer.completion) { peer.finish(encoded(UINT64_MAX)); previews.poll(); }
		}
		CHECK(peer.calls.contains(64) && previews.cachedFields() <= 64 && hasValue(previews, 64));
	}
	{
		Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
		ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
		QObject view;
		QList<quint32> ids;
		for (unsigned n = 0; n < 33; ++n) ids.append(bounds_fixture::blockId(n));
		CHECK(previews.setDemand(&view, ids));
		for (unsigned n = 0; n < 32; ++n) {
			peer.now += 500; previews.poll(); CHECK(peer.calls.size() == int(n + 1));
			peer.finish(QByteArray(8192, char(n))); previews.poll();
			CHECK(previews.cachedBytes() <= ux::EndpointPreviews::payloadLimit);
		}
		CHECK(previews.cachedBytes() == ux::EndpointPreviews::payloadLimit);
		peer.now += 500; previews.poll();
		CHECK(!peer.calls.contains(bounds_fixture::blockId(32)));
		if (peer.completion) { peer.finish(QByteArray(8192, '\0')); previews.poll(); }
		CHECK(previews.cachedBytes() == ux::EndpointPreviews::payloadLimit);
		CHECK(previews.setDemand(&view, {bounds_fixture::blockId(32)}));
		peer.now += 500; previews.poll();
		CHECK(peer.calls.last() == bounds_fixture::blockId(32));
		peer.finish(QByteArray(8192, '\0')); previews.poll();
		CHECK(previews.cachedBytes() <= ux::EndpointPreviews::payloadLimit &&
		      hasValue(previews, bounds_fixture::blockId(32)));
	}
}

void demandLifecycleAndEpochs() {
	const auto parsed = std::make_shared<bounds_fixture::Parsed>();
	Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
	ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
	QObject view;
	CHECK(previews.setDemand(&view, {0})); previews.poll();
	previews.clearDemand(&view);
	CHECK(previews.pending()); // accepted native read must drain normally.
	CHECK(previews.setDemand(&view, {0}));
	peer.finish(encoded(UINT64_MAX)); previews.poll();
	CHECK(!hasValue(previews, 0)); // an old visibility lease cannot warm a new one.
	CHECK(state(previews, 0) != "Reading" || previews.pending());
	peer.now = 5000; previews.poll(); peer.finish(encoded(std::uint64_t{42})); previews.poll();
	CHECK(previews.value(0, peer.type(0), 0) == "42");
	peer.now = 10000; previews.poll(); CHECK(previews.pending());
	previews.invalidateValues();
	CHECK(!hasValue(previews, 0) && previews.value(0, peer.type(0), 0).isEmpty());
	peer.finish(encoded(UINT64_MAX)); previews.poll();
	// Draining an invalidated read does not bypass the global spacing budget.
	CHECK(!hasValue(previews, 0));
	CHECK(!previews.pending());
	peer.now += 500; previews.poll();
	CHECK(previews.pending()); peer.finish(encoded(std::uint64_t{7})); previews.poll();
	CHECK(previews.value(0, peer.type(0), 0) == "7");
	peer.now += 5000; previews.poll();
	previews.retire();
	CHECK(!previews.current() && previews.pending() && previews.cachedFields() == 0);
	peer.finish(encoded(UINT64_MAX)); previews.poll();
	CHECK(!previews.pending() && previews.value(0, peer.type(0), 0).isEmpty());
	previews.setSource(peer.source());
	CHECK(previews.setDemand(&view, {0}));
	auto ephemeral = std::make_unique<QObject>();
	CHECK(previews.setDemand(ephemeral.get(), {1})); ephemeral.reset();
	peer.now += 500; previews.poll(); CHECK(peer.calls.last() == 0);
	peer.finish(encoded(std::uint64_t{8})); previews.poll();
	peer.now += 500; previews.poll(); CHECK(peer.calls.last() == 0);
	// Oversized lease updates fail atomically and preserve existing demand.
	CHECK(!previews.setDemand(&view, QList<quint32>(257, 1)));
	CHECK(!previews.setDemand(nullptr, {0}));
	std::array<std::unique_ptr<QObject>, 16> owners;
	for (unsigned n = 0; n < 15; ++n) {
		owners[n] = std::make_unique<QObject>(); CHECK(previews.setDemand(owners[n].get(), {1}));
	}
	owners[15] = std::make_unique<QObject>();
	CHECK(!previews.setDemand(owners[15].get(), {1}));
	owners[0].reset(); CHECK(previews.setDemand(owners[15].get(), {1}));
}

void invalidationCannotBurstRequests() {
	const auto parsed = std::make_shared<bounds_fixture::Parsed>();
	Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
	ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
	QObject view; CHECK(previews.setDemand(&view, {0}));
	previews.poll(); CHECK(peer.calls.size() == 1);
	peer.finish(encoded(UINT64_MAX)); previews.poll();
	for (const qint64 time : {1, 100, 499}) {
		peer.now = time; previews.invalidateValues(); previews.poll();
		CHECK(peer.calls.size() == 1 && !previews.pending());
	}
	peer.now = 500; previews.poll(); CHECK(peer.calls.size() == 2);
	peer.finish(encoded(UINT64_MAX)); previews.poll();
	peer.now = 501; previews.setSource(peer.source());
	CHECK(previews.setDemand(&view, {0})); previews.poll();
	CHECK(peer.calls.size() == 2 && !previews.pending());
	peer.now = 999; previews.poll(); CHECK(peer.calls.size() == 2);
	peer.now = 1000; previews.poll(); CHECK(peer.calls.size() == 3 && previews.pending());
	peer.finish(encoded(UINT64_MAX)); previews.poll();
}

ux::WatchFrame frameFor(Peer& peer, quint32 id, quint64 bits,
                       ux::WatchScalar scalar = ux::WatchScalar::U64, unsigned width = 8,
                       bool enumeration = false) {
	ux::WatchFrame frame;
	frame.startedNs = ux::WatchTimestamp::now().monotonicNs;
	frame.completed = ux::WatchTimestamp::now();
	frame.source = {peer.session, peer.schema.fingerprint(), peer.schema.generation()};
	frame.phase = ux::WatchCompletionPhase::Current;
	frame.correlated = true; frame.exact = true; frame.count = 1;
	frame.native = {0, 0, 0, 0, 0};
	auto& row = frame.rows[0]; row.id = id; row.itemStatus = 0; row.valueStatus = 0;
	row.value.type = {peer.type(id), scalar, quint8(width), enumeration};
	std::array<std::byte, 8> data{};
	for (unsigned n = 0; n < width; ++n) data[n] = std::byte((bits >> (8 * n)) & 255);
	CHECK(row.value.assign(data.data(), width));
	return frame;
}

void watchWarmingAndCorrelation() {
	const auto parsed = std::make_shared<bounds_fixture::Parsed>();
	Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
	peer.watched = {0, bounds_fixture::modeId};
	ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
	QObject view; CHECK(previews.setDemand(&view, {0, bounds_fixture::modeId}));
	previews.poll(); CHECK(peer.calls.isEmpty());
	auto frame = frameFor(peer, 0, UINT64_MAX);
	previews.ingest(frame);
	CHECK(hasValue(previews, 0) && previews.value(0, peer.type(0), 0) == "18446744073709551615");
	auto enumeration = frameFor(peer, bounds_fixture::modeId, 0xfff9, ux::WatchScalar::S16, 2, true);
	previews.ingest(enumeration);
	CHECK(previews.value(bounds_fixture::modeId, peer.type(bounds_fixture::modeId), 0) == "Reverse (-7)");
	previews.invalidateValues();
	frame.startedNs = 0; // Explicit old monotonic epoch; no elapsed-time assumption.
	previews.ingest(frame); CHECK(!hasValue(previews, 0));
	frame = frameFor(peer, 0, 42);
	for (unsigned n = 0; n < 6; ++n) {
		auto invalid = frame;
		if (n == 0) invalid.correlated = false;
		if (n == 1) invalid.phase = ux::WatchCompletionPhase::Drained;
		if (n == 2) ++invalid.source.nativeSession;
		if (n == 3) ++invalid.source.fingerprint;
		if (n == 4) ++invalid.source.schemaGeneration;
		if (n == 5) invalid.rows[0].value.type.typeId = peer.type(bounds_fixture::modeId);
		previews.ingest(invalid); CHECK(!hasValue(previews, 0));
	}
	// The public C++ cache boundary must not turn failed/unvalidated facts into
	// successful readings, even when a caller supplies nonempty stale bits.
	for (unsigned n = 0; n < 9; ++n) {
		auto invalid = frame;
		if (n == 0) invalid.exact = false;
		if (n == 1) invalid.native.client = qint32(cl::Status::TransportError);
		if (n == 2) invalid.native.dispatch = 1;
		if (n == 3) invalid.native.endpoint = 1;
		if (n == 4) invalid.native.codec = 1;
		if (n == 5) invalid.native.value = 1;
		if (n == 6) invalid.presentationStatus = qint32(cl::Status::CodecError);
		if (n == 7) invalid.rows[0].itemStatus = 1;
		if (n == 8) invalid.rows[0].valueStatus = 1;
		previews.ingest(invalid); CHECK(!hasValue(previews, 0));
	}
	previews.ingest(frame); CHECK(previews.value(0, peer.type(0), 0) == "42");
	frame.rows[0].value.size = 0; frame.rows[0].valueStatus = 2;
	previews.ingest(frame); CHECK(!hasValue(previews, 0) && state(previews, 0) == "Unavailable");
	previews.clearDemand(&view);
	previews.ingest(frameFor(peer, 0, UINT64_MAX)); CHECK(!hasValue(previews, 0));
	CHECK(peer.calls.isEmpty());
}

void detachedCallbackAndReentrancy() {
	const auto parsed = std::make_shared<bounds_fixture::Parsed>();
	{
		Peer peer; peer.schema = parsed->schema;
		auto storage = std::make_shared<int>(42); std::weak_ptr<int> weak = storage;
		peer.backing = storage; storage.reset();
		auto previews = std::make_unique<ux::EndpointPreviews>(nullptr, [&] { return peer.now; });
		bind(*previews, peer); QObject view; CHECK(previews->setDemand(&view, {0}));
		previews->poll(); peer.backing.reset();
		previews->retire(); CHECK(!weak.expired());
		peer.finish(encoded(UINT64_MAX)); previews->poll();
		CHECK(weak.expired());
	}
	{
		Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
		auto previews = std::make_unique<ux::EndpointPreviews>(nullptr, [&] { return peer.now; });
		bind(*previews, peer); QObject view; CHECK(previews->setDemand(&view, {0}));
		peer.onRead = [&] { previews.reset(); };
		previews->poll(); CHECK(!previews && peer.completion);
		peer.finish(encoded(UINT64_MAX)); // no QObject is captured by the native completion.
	}
	{
		Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
		ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
		QObject view; CHECK(previews.setDemand(&view, {0}));
		peer.onRead = [&] { previews.retire(); };
		previews.poll(); CHECK(!previews.current() && previews.pending());
		peer.finish(encoded(UINT64_MAX)); previews.poll(); CHECK(!previews.pending());
	}
}
} // namespace

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	qInstallMessageHandler(+[](QtMsgType type, const QMessageLogContext&, const QString& message) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
			++fixture::warnings;
			std::fprintf(stderr, "Qt preview warning: %s\n", qPrintable(message));
		}
	});
	schedulingAndOwnedValues();
	refusalAndPayloadValidation();
	cacheBoundsAndPreflight();
	demandLifecycleAndEpochs();
	invalidationCannotBurstRequests();
	watchWarmingAndCorrelation();
	detachedCallbackAndReentrancy();
	CHECK(fixture::warnings == 0);
	std::printf("Endpoint preview checks=%u warnings=%u Qt=%s\n", fixture::checks, fixture::warnings, qVersion());
}
