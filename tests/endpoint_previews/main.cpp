// Controlled clock/native completions over actual parsed descriptor bytes. MIT.
#include <Telemetry/UX/EndpointPreviews.hpp>
#include "../dynamic_qml_configurator/Fixture.hpp"
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QTimer>
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
	// Set: the device has ReadMany or Batch, with these budgets.
	bool many = false, batch = false, refuseOnce = false;
	ux::read_portions::Budget manyBudget{32, 130, 514}, batchBudget{32, 4096, 65536};
	QList<QList<quint32>> manyCalls, batchCalls;
	QList<quint32> asked;
	ux::EndpointPreviewSource::ManyCompletion manyDone;

	ux::EndpointPreviewSource source() {
		ux::EndpointPreviewSource out;
		out.backing = backing;
		out.current = [this] { return admitted; };
		out.busy = [this] { return busy; };
		out.schema = [this] { return schema; };
		out.session = [this] { return session; };
		out.watched = [this](quint32 id) { return watched.contains(id); };
		const auto several = [this](QList<QList<quint32>>& calls) {
			return [this, &calls](std::span<const std::uint32_t> ids, ux::EndpointPreviewSource::ManyCompletion done) {
				CHECK(admitted && !busy && !completion && !manyDone && !ids.empty());
				asked = QList<quint32>(ids.begin(), ids.end());
				calls.append(asked);
				if (refusal != cl::Status::Ok) return cl::Submission{refusal, {}};
				manyDone = std::move(done);
				busy = true;
				return cl::Submission{cl::Status::Ok, {}};
			};
		};
		if (many) {
			out.readManyBudget = [this] { return std::optional<ux::read_portions::Budget>{manyBudget}; };
			out.readMany = several(manyCalls);
		}
		if (batch) {
			out.batchBudget = [this] { return std::optional<ux::read_portions::Budget>{batchBudget}; };
			out.readBatch = several(batchCalls);
		}
		out.read = [this](quint32 id, ux::EndpointPreviewSource::Completion done) {
			CHECK(admitted && !busy && !completion && !manyDone);
			calls.append(id);
			if (refusal != cl::Status::Ok) {
				const auto refused = refusal;
				if (refuseOnce) refusal = cl::Status::Ok;
				return cl::Submission{refused, {}};
			}
			completion = std::move(done);
			busy = true;
			if (onRead) onRead();
			return cl::Submission{cl::Status::Ok, {}};
		};
		return out;
	}
	// Answers several Fields: each value as value(id) gives it.
	void finishMany(const std::function<QByteArray(quint32)>& value, cl::Status status = cl::Status::Ok) {
		CHECK(manyDone && busy);
		inCallback = true;
		auto done = std::exchange(manyDone, {});
		busy = false;
		ux::EndpointPreviewManyResult result;
		result.status = status;
		for (auto id : asked)
			result.items.emplace_back(id, ux::EndpointPreviewResult{cl::Status::Ok, true, value(id), {}});
		*done = std::move(result);
		inCallback = false;
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

// One cycle reads every Field in view, one request at a time and only while
// the session is idle, and its values land together when the last reply is
// in; the next cycle comes an interval later; a value is stale after three
// intervals; a failed read keeps the last value, stale, with its reason.
void cycleLandsTogether() {
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
	peer.busy = true; // foreground and Watch own priority at the common FIFO.
	previews.poll();
	CHECK(peer.calls.isEmpty());
	peer.busy = false;
	previews.poll();
	CHECK(peer.calls == QList<quint32>{0} && previews.pending() && state(previews, 0) == "Reading");
	for (unsigned n = 0; n < 20; ++n) previews.poll();
	CHECK(peer.calls.size() == 1);
	peer.finish(encoded(fixture::device.config));
	previews.poll();
	// The next Field at once; nothing lands before the last reply.
	CHECK(peer.calls == QList<quint32>({0, 1}) && !hasValue(previews, 0) && !hasValue(previews, 1));
	QByteArray borrowed = encoded(UINT64_MAX);
	peer.finish(borrowed);
	borrowed.fill(char(0x55));
	previews.poll();
	CHECK(hasValue(previews, 0) && hasValue(previews, 1) && state(previews, 0) == "Current" &&
	      state(previews, 1) == "Current");
	CHECK(previews.value(1, peer.type(1), 0) == "18446744073709551615" && previews.info(1).value("ageMs").toLongLong() == 0);
	// A folded struct reads as its first members, as values.bin writes it.
	const auto summary = previews.value(0, peer.type(0), 0);
	CHECK(summary.startsWith(QLatin1Char('{')) && summary.endsWith(QLatin1Char('}')) && summary.contains(QStringLiteral("…")));
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
	// The next cycle an interval after the last landed.
	CHECK(previews.interval() == ux::EndpointPreviews::intervalDefaultMs);
	peer.now = 1999;
	previews.poll();
	CHECK(peer.calls.size() == 2);
	peer.now = 2000;
	previews.poll();
	CHECK(peer.calls.size() == 3 && peer.calls.last() == 0);
	// Stale after three intervals.
	peer.now = 5999;
	CHECK(state(previews, 0) == "Current");
	peer.now = 6000;
	CHECK(state(previews, 0) == "Stale");
	// A failed read: the last value stays, stale, and says why.
	peer.finish({}, false);
	previews.poll();
	peer.finish(encoded(std::uint64_t{7}));
	previews.poll();
	CHECK(hasValue(previews, 0) && state(previews, 0) == "Stale" && previews.value(1, peer.type(1), 0) == "7");
	// The interval is the host's, within its bounds.
	previews.setInterval(10);
	CHECK(previews.interval() == ux::EndpointPreviews::intervalMinimumMs);
	previews.setInterval(60000);
	CHECK(previews.interval() == ux::EndpointPreviews::intervalMaximumMs);
}

// As few requests as the device allows: scalars by ReadMany within its item
// and reply budgets, structs and arrays whole in one Batch, a scalar in the
// ReadMany open beside them; a Batch carries scalars too where there is no
// ReadMany; with neither, a FieldRead each. All land together.
void portionsByBudget() {
	const auto parsed = std::make_shared<bounds_fixture::Parsed>();
	const auto u64 = [](quint32) { return encoded(UINT64_MAX); };
	{
		Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
		peer.many = peer.batch = true;
		ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
		QObject view;
		QList<quint32> ids;
		for (quint32 id = 0; id < 40; ++id) ids.append(id);
		ids += {bounds_fixture::blockId(0), bounds_fixture::blockId(1), bounds_fixture::boolId};
		CHECK(previews.setDemand(&view, ids));
		previews.poll();
		QList<quint32> first;
		for (quint32 id = 0; id < 32; ++id) first.append(id);
		CHECK(peer.manyCalls.size() == 1 && peer.manyCalls[0] == first && peer.calls.isEmpty());
		peer.finishMany(u64);
		previews.poll();
		QList<quint32> second;
		for (quint32 id = 32; id < 40; ++id) second.append(id);
		second.append(bounds_fixture::boolId);
		CHECK(peer.manyCalls.size() == 2 && peer.manyCalls[1] == second && !hasValue(previews, 0));
		peer.finishMany([](quint32 id) { return id == bounds_fixture::boolId ? QByteArray(1, '\1') : encoded(UINT64_MAX); });
		previews.poll();
		CHECK(peer.batchCalls.size() == 1 &&
		      peer.batchCalls[0] == QList<quint32>({bounds_fixture::blockId(0), bounds_fixture::blockId(1)}) &&
		      !hasValue(previews, 39));
		peer.finishMany([](quint32 id) { return QByteArray(8192, char(id & 0xff)); });
		previews.poll();
		CHECK(hasValue(previews, 0) && hasValue(previews, 39) && hasValue(previews, bounds_fixture::boolId) &&
		      hasValue(previews, bounds_fixture::blockId(1)) && peer.calls.isEmpty());
		CHECK(previews.value(bounds_fixture::boolId, peer.type(bounds_fixture::boolId), 0) == "true");
	}
	{
		// A small reply budget: six eight-byte values a ReadMany.
		Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
		peer.many = true; peer.manyBudget = {32, 130, 100};
		ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
		QObject view;
		QList<quint32> ids;
		for (quint32 id = 0; id < 40; ++id) ids.append(id);
		CHECK(previews.setDemand(&view, ids));
		QList<int> sizes;
		previews.poll();
		while (peer.manyDone) {
			sizes.append(int(peer.manyCalls.last().size()));
			peer.finishMany(u64);
			previews.poll();
		}
		CHECK(sizes == QList<int>({6, 6, 6, 6, 6, 6, 4}) && hasValue(previews, 39));
	}
	{
		// Batch without ReadMany: the scalars in one Batch.
		Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
		peer.batch = true;
		ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
		QObject view;
		CHECK(previews.setDemand(&view, {0, 1, 2}));
		previews.poll();
		CHECK(peer.batchCalls.size() == 1 && peer.batchCalls[0] == QList<quint32>({0, 1, 2}));
		peer.finishMany(u64);
		previews.poll();
		CHECK(hasValue(previews, 2) && peer.calls.isEmpty());
	}
	{
		// Neither: a FieldRead each, landing once.
		Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
		ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
		QObject view;
		CHECK(previews.setDemand(&view, {0, 1, 2}));
		previews.poll();
		for (int n = 0; n < 3; ++n) {
			CHECK(peer.calls.size() == n + 1 && !hasValue(previews, 0));
			peer.finish(encoded(UINT64_MAX));
			previews.poll();
		}
		CHECK(hasValue(previews, 0) && hasValue(previews, 2) && peer.calls == QList<quint32>({0, 1, 2}));
	}
}

// A refusal the queue may lift (busy, full) tries the same request again; any
// other leaves those Fields unread this cycle and the cycle goes on. A reply
// that does not decode as the Field's type is not a value.
void refusalAndPayloadValidation() {
	const auto parsed = std::make_shared<bounds_fixture::Parsed>();
	Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
	ux::EndpointPreviews previews(nullptr, [&] { return peer.now; });
	bind(previews, peer);
	QObject view;
	CHECK(previews.setDemand(&view, {bounds_fixture::emptyId}));
	peer.refusal = cl::Status::QueueFull;
	previews.poll();
	CHECK(peer.calls.size() == 1 && !previews.pending() && state(previews, bounds_fixture::emptyId) == "Reading");
	peer.refusal = cl::Status::Ok;
	previews.poll();
	CHECK(peer.calls.size() == 2 && previews.pending());
	peer.finish({}, true);
	previews.poll();
	CHECK(hasValue(previews, bounds_fixture::emptyId));
	CHECK(previews.value(bounds_fixture::emptyId, peer.type(bounds_fixture::emptyId), 0) == "{}");
	// Refused for good: unread, with the reason; the next Field is read.
	CHECK(previews.setDemand(&view, {bounds_fixture::modeId, 3}));
	peer.now = 500;
	peer.refusal = cl::Status::NotAdmitted;
	peer.refuseOnce = true;
	previews.poll();
	CHECK(peer.calls.size() == 4 && previews.pending());
	peer.finish(encoded(std::uint64_t{3}));
	previews.poll();
	CHECK(!hasValue(previews, bounds_fixture::modeId) && state(previews, bounds_fixture::modeId) == "Unavailable" &&
	      previews.info(bounds_fixture::modeId).value("reason") == "Not read: the request was refused." &&
	      previews.value(3, peer.type(3), 0) == "3");
	// A value that is not the Field's type.
	CHECK(previews.setDemand(&view, {bounds_fixture::boolId}));
	peer.now = 1000;
	previews.poll(); peer.finish(QByteArray(1, char(2))); previews.poll();
	CHECK(!hasValue(previews, bounds_fixture::boolId) &&
	      previews.info(bounds_fixture::boolId).value("reason") == "Invalid preview payload.");
	CHECK(previews.setDemand(&view, {4}));
	peer.now = 1500;
	previews.poll(); peer.finish(QByteArray(9, '\0')); previews.poll();
	CHECK(!hasValue(previews, 4) && previews.info(4).value("reason") == "Invalid preview payload.");
	CHECK(previews.setDemand(&view, {5}));
	peer.now = 2000;
	previews.poll(); peer.finish(encoded(UINT64_MAX), true, cl::Status::TransportError); previews.poll();
	CHECK(!hasValue(previews, 5));
}

// At most 64 Fields and 256 KiB: the Fields past the limit say so.
void cacheBoundsAndPreflight() {
	const auto parsed = std::make_shared<bounds_fixture::Parsed>();
	{
		Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
		peer.many = true;
		ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
		QObject view;
		CHECK(previews.setDemand(&view, {bounds_fixture::oversizedId}));
		previews.poll();
		CHECK(peer.calls.isEmpty() && peer.manyCalls.isEmpty() && previews.cachedFields() == 0);
		CHECK(state(previews, bounds_fixture::oversizedId) == "Open Details");
		QList<quint32> ids; for (quint32 id = 0; id < 65; ++id) ids.append(id);
		CHECK(previews.setDemand(&view, ids));
		peer.now = 500;
		previews.poll();
		while (peer.manyDone) { peer.finishMany([](quint32) { return encoded(UINT64_MAX); }); previews.poll(); }
		CHECK(peer.manyCalls.size() == 2 && !peer.manyCalls[1].contains(64));
		CHECK(previews.cachedFields() == 64 && previews.cachedBytes() == 64 * 8 && state(previews, 64) == "Preview limit");
		// Out of view, room is made for it.
		CHECK(previews.setDemand(&view, {64}));
		peer.now = 1000;
		previews.poll();
		peer.finishMany([](quint32) { return encoded(UINT64_MAX); });
		previews.poll();
		CHECK(hasValue(previews, 64) && previews.cachedFields() <= 64);
	}
	{
		Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
		peer.batch = true; peer.batchBudget = {32, 4096, 300000};
		ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
		QObject view;
		QList<quint32> ids;
		for (unsigned n = 0; n < 33; ++n) ids.append(bounds_fixture::blockId(n));
		CHECK(previews.setDemand(&view, ids));
		previews.poll();
		CHECK(peer.batchCalls.size() == 1 && peer.batchCalls[0].size() == 32);
		peer.finishMany([](quint32 id) { return QByteArray(8192, char(id & 0xff)); });
		previews.poll();
		CHECK(peer.calls == QList<quint32>{bounds_fixture::blockId(32)});
		peer.finish(QByteArray(8192, '\0'));
		previews.poll();
		CHECK(previews.cachedBytes() == ux::EndpointPreviews::payloadLimit &&
		      state(previews, bounds_fixture::blockId(32)) == "Preview limit");
	}
}

// Demand, epochs and sources: a reply for a Field no longer in view does not
// land; values invalidated mid-cycle are dropped and the next cycle waits its
// interval; a retired source drains its last request; a Field newly in view is
// read soon, but never sooner than half a second after the last cycle.
void demandLifecycleAndEpochs() {
	const auto parsed = std::make_shared<bounds_fixture::Parsed>();
	Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
	ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
	QObject view;
	CHECK(previews.setDemand(&view, {0})); previews.poll();
	previews.clearDemand(&view);
	CHECK(previews.pending()); // accepted native read must drain normally.
	peer.finish(encoded(UINT64_MAX)); previews.poll();
	CHECK(!hasValue(previews, 0) && !previews.pending());
	CHECK(previews.setDemand(&view, {0}));
	peer.now = 300;
	previews.poll();
	CHECK(peer.calls.size() == 1); // half a second after the last cycle
	peer.now = 500;
	previews.poll();
	CHECK(peer.calls.size() == 2);
	peer.finish(encoded(std::uint64_t{42})); previews.poll();
	CHECK(previews.value(0, peer.type(0), 0) == "42");
	// Invalidated mid-cycle: dropped, and the next cycle waits its interval.
	peer.now = 2500;
	previews.poll(); CHECK(previews.pending());
	previews.invalidateValues();
	CHECK(!hasValue(previews, 0) && previews.value(0, peer.type(0), 0).isEmpty());
	peer.finish(encoded(UINT64_MAX)); previews.poll();
	CHECK(!hasValue(previews, 0) && !previews.pending() && peer.calls.size() == 3);
	peer.now = 4499; previews.poll(); CHECK(peer.calls.size() == 3);
	peer.now = 4500; previews.poll(); CHECK(previews.pending());
	peer.finish(encoded(std::uint64_t{7})); previews.poll();
	CHECK(previews.value(0, peer.type(0), 0) == "7");
	// Retired with a request out: it drains, and nothing stays cached.
	peer.now = 6500; previews.poll(); CHECK(previews.pending());
	previews.retire();
	CHECK(!previews.current() && previews.pending() && previews.cachedFields() == 0);
	peer.finish(encoded(UINT64_MAX)); previews.poll();
	CHECK(!previews.pending() && previews.value(0, peer.type(0), 0).isEmpty());
	// A new source reads at once.
	previews.setSource(peer.source());
	CHECK(previews.setDemand(&view, {0}));
	auto ephemeral = std::make_unique<QObject>();
	CHECK(previews.setDemand(ephemeral.get(), {1})); ephemeral.reset();
	previews.poll(); CHECK(peer.calls.last() == 0);
	peer.finish(encoded(std::uint64_t{8})); previews.poll();
	CHECK(!previews.pending() && previews.value(0, peer.type(0), 0) == "8");
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

// Repeated invalidations (a run of writes) do not turn into a run of reads:
// each drops the cycle, and the next waits its interval.
void invalidationCannotBurstRequests() {
	const auto parsed = std::make_shared<bounds_fixture::Parsed>();
	Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
	ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
	QObject view; CHECK(previews.setDemand(&view, {0}));
	previews.poll(); CHECK(peer.calls.size() == 1);
	peer.finish(encoded(UINT64_MAX)); previews.poll();
	for (const qint64 time : {1, 100, 1999}) {
		peer.now = time; previews.invalidateValues(); previews.poll();
		CHECK(peer.calls.size() == 1 && !previews.pending());
	}
	peer.now = 3998; previews.poll(); CHECK(peer.calls.size() == 1);
	peer.now = 3999; previews.poll(); CHECK(peer.calls.size() == 2);
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
	for (unsigned n = 0; n < 7; ++n) {
		auto invalid = frame;
		if (n == 0) invalid.correlated = false;
		if (n == 1) invalid.phase = ux::WatchCompletionPhase::Drained;
		if (n == 2) ++invalid.source.nativeSession;
		if (n == 3) ++invalid.source.fingerprint;
		if (n == 4) ++invalid.source.schemaGeneration;
		if (n == 5) invalid.rows[0].value.type.typeId = peer.type(bounds_fixture::modeId);
		if (n == 6) invalid.rows[0].leaf = 1; // a value inside a Field never warms the Field's preview
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

// The session runs only when it has work, so its progress is no clock: the
// previews arm a timer of their own, for the next cycle falling due and for
// the next whole second (the ages shown), and only while Fields are in view
// and the source is current (SDK-2b).
void timerArmsOnlyWhileInView() {
	const auto parsed = std::make_shared<bounds_fixture::Parsed>();
	Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
	ux::EndpointPreviews previews(nullptr, [&] { return peer.now; }); bind(previews, peer);
	CHECK(previews.nextPollInMs() == -1);
	QObject view;
	CHECK(previews.setDemand(&view, {0}));
	// Nothing is sent from setDemand itself; the first cycle goes at the next turn.
	CHECK(previews.nextPollInMs() == 0 && peer.calls.isEmpty());
	for (int turn = 0; turn < 100 && peer.calls.isEmpty(); ++turn)
		QCoreApplication::processEvents();
	CHECK(peer.calls.size() == 1 && previews.pending());
	// A reply on its way is polled by the progress that brings it; the ages' tick stays.
	const auto tick = previews.nextPollInMs();
	CHECK(tick > 0 && tick <= 1000);
	peer.now = 250;
	peer.finish(encoded(UINT64_MAX)); previews.poll();
	CHECK(hasValue(previews, 0));
	// Landed at 250 ms: the ages move on at 1000, the next cycle comes at 2250.
	CHECK(previews.nextPollInMs() > 650 && previews.nextPollInMs() <= 750);
	peer.now = 1900; previews.poll();
	CHECK(previews.nextPollInMs() > 50 && previews.nextPollInMs() <= 100);
	peer.now = 2100; previews.poll();
	CHECK(previews.nextPollInMs() > 100 && previews.nextPollInMs() <= 150);
	// Due while the session is busy: the progress that frees it polls; no spin.
	peer.busy = true; peer.now = 2250; previews.poll();
	CHECK(peer.calls.size() == 1 && previews.nextPollInMs() > 650 && previews.nextPollInMs() <= 750);
	peer.busy = false; previews.poll();
	CHECK(peer.calls.size() == 2);
	peer.finish(encoded(UINT64_MAX)); previews.poll();
	// A shorter interval brings the next cycle nearer at once.
	peer.now = 2300; previews.setInterval(ux::EndpointPreviews::intervalMinimumMs);
	CHECK(previews.nextPollInMs() > 650 && previews.nextPollInMs() <= 700);
	// Out of view, or the source not current: off.
	previews.clearDemand(&view);
	CHECK(previews.nextPollInMs() == -1);
	CHECK(previews.setDemand(&view, {0}) && previews.nextPollInMs() >= 0);
	peer.admitted = false; previews.poll();
	CHECK(previews.nextPollInMs() == -1);
	peer.admitted = true;
	previews.retire();
	CHECK(previews.nextPollInMs() == -1);
}

// With the real clock and nothing else to move it, a preview is read again
// an interval after it landed, by its own timer alone.
void rereadsOnItsOwnTimer() {
	const auto parsed = std::make_shared<bounds_fixture::Parsed>();
	Peer peer; peer.schema = parsed->schema; peer.backing = parsed;
	ux::EndpointPreviews previews; bind(previews, peer);
	previews.setInterval(ux::EndpointPreviews::intervalMinimumMs);
	// Runs the loop as a program does, waking at least every 5 ms.
	const auto loop = [](const std::function<bool()>& done, int limit) {
		QElapsedTimer deadline; deadline.start();
		QTimer wake; wake.setTimerType(Qt::PreciseTimer); wake.start(5);
		while (!done() && deadline.elapsed() < limit)
			QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
		return done();
	};
	QObject view; CHECK(previews.setDemand(&view, {0}));
	CHECK(loop([&] { return peer.calls.size() == 1; }, 1000));
	// The progress owner takes the reply in, as on the session's progress.
	peer.finish(encoded(UINT64_MAX)); previews.poll();
	CHECK(hasValue(previews, 0));
	QElapsedTimer landed; landed.start();
	CHECK(loop([&] { return peer.calls.size() == 2; }, 3000));
	const auto again = landed.elapsed();
	std::printf("Endpoint preview read again %lld ms after it landed (interval %d ms)\n",
	            static_cast<long long>(again), previews.interval());
	CHECK(again >= ux::EndpointPreviews::intervalMinimumMs - 20 && again < ux::EndpointPreviews::intervalMinimumMs + 300);
	peer.finish(encoded(std::uint64_t{5})); previews.poll();
	CHECK(previews.value(0, peer.type(0), 0) == "5");
	previews.clearDemand(&view);
	CHECK(previews.nextPollInMs() == -1);
	const auto calls = peer.calls.size();
	CHECK(!loop([&] { return peer.calls.size() != calls; }, 1200));
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
	cycleLandsTogether();
	portionsByBudget();
	refusalAndPayloadValidation();
	cacheBoundsAndPreflight();
	demandLifecycleAndEpochs();
	invalidationCannotBurstRequests();
	watchWarmingAndCorrelation();
	timerArmsOnlyWhileInView();
	rereadsOnItsOwnTimer();
	detachedCallbackAndReentrancy();
	CHECK(fixture::warnings == 0);
	std::printf("Endpoint preview checks=%u warnings=%u Qt=%s\n", fixture::checks, fixture::warnings, qVersion());
}
