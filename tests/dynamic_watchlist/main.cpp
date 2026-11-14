// Controlled completions and ticks: no event-loop timing assumptions. MIT.
#include <Telemetry/UX/DynamicWatchlist.hpp>
#include <Telemetry/UX/TrendHistory.hpp>
#include <Telemetry/UX/WatchKey.hpp>
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <algorithm>
#include <QtCore/QCoreApplication>
#include <QtCore/QByteArray>
#include <QtCore/QEvent>
#include <QtTest/QAbstractItemModelTester>
#include <QtTest/QSignalSpy>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>
#include <utility>

namespace ux = telemetry::ux;
namespace cl = telemetry::client;
namespace rt = telemetry::runtime;

// A device whose Field 1 is a struct: device.Config holds 302 values, gain
// first, enabled second, then samples[0] to samples[299]. Field 0 is a
// scalar. Ids 2 to 99 stay the Peer's whole 8-byte Fields.
namespace members {
struct Config {
	float gain = 1.25f;
	bool enabled = true;
	std::array<std::uint16_t, 300> samples{};
};
struct Device {
	std::uint32_t counter() const noexcept { return 7; }
	Config config() const noexcept { return {}; }
};
inline Device device;
inline constexpr telemetry::FieldTable fields{
    telemetry::field<&Device::counter>("Counter", device),
    telemetry::field<&Device::config>("Config", device)};
inline constexpr telemetry::FieldCatalogTable catalogs{telemetry::group("device", fields)};
inline constexpr telemetry::ServiceCatalogTable<> services{};
inline constexpr telemetry::Model model{catalogs, telemetry::emptyCommands, services};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();
constexpr quint32 config = 1, leaves = 302;

struct Schema {
	std::array<std::byte, descriptorBytes.size()> bytes = descriptorBytes;
	std::array<rt::RuntimeType, 32> types{};
	std::array<rt::RuntimeMember, 32> members{};
	std::array<rt::RuntimeEnumEntry, 4> enums{};
	std::array<rt::RuntimeCatalog, 4> catalogs{};
	std::array<rt::RuntimeEndpoint, 8> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
	rt::RuntimeSchema schema;
	Schema()
	{
		const auto parsed = rt::RuntimeSchema::parse(bytes, workspace);
		if (!parsed) std::abort();
		schema = parsed.schema;
	}
};
} // namespace members

namespace {
unsigned checks = 0;
#define CHECK(...)                                                                                 \
	do {                                                                                           \
		++checks;                                                                                  \
		if (!(__VA_ARGS__)) {                                                                      \
			std::fprintf(stderr, "Dynamic Watchlist check %u line %d: %s\n", checks, __LINE__,     \
			             #__VA_ARGS__);                                                            \
			std::abort();                                                                          \
		}                                                                                          \
	} while (false)

void events()
{
	QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
}

ux::WatchValueBits bits(ux::WatchScalar scalar, quint8 width, quint64 number, bool enumeration = false)
{
	ux::WatchValueBits result;
	result.type = {1, scalar, width, enumeration};
	std::array<std::byte, 8> bytes{};
	for (unsigned i = 0; i < width; ++i) bytes[i] = std::byte((number >> (i * 8)) & 0xff);
	CHECK(result.assign(bytes.data(), width));
	bytes.fill(std::byte{0x55}); // The model must own the exact payload, including unplottable values.
	return result;
}

void typedEditors()
{
	ux::WatchlistModel model;
	bool current = true;
	model.bind([&] { return current; });
	CHECK(model.add(0, QStringLiteral("Typed")));
	const auto row = model.index(0, 0);
	const auto data = [&](int role) { return model.data(row, role); };
	CHECK(data(ux::WatchlistModel::EditorTextRole).toString().isEmpty());
	CHECK(!data(ux::WatchlistModel::CategoricalRole).toBool());
	// Legacy sources may omit bytes; their existing sample contract is unchanged.
	CHECK(model.setResult(0, "Ok", "legacy display", ux::WatchSample::ofSigned(12)));
	CHECK(data(ux::WatchlistModel::SampleValueRole).toDouble() == 12 &&
	      data(ux::WatchlistModel::EditorTextRole).toString().isEmpty() &&
	      !data(ux::WatchlistModel::CategoricalRole).toBool());
	CHECK(model.setResult(0, "Ok", "true (display decoration)", ux::WatchSample::ofUnsigned(1),
	                      bits(ux::WatchScalar::Boolean, 1, 1)));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "true" &&
	      data(ux::WatchlistModel::EditorKindRole) == "bool" &&
	      data(ux::WatchlistModel::CategoricalRole).toBool());
	const auto boolean = data(ux::WatchlistModel::EditorChoicesRole).toList();
	CHECK(boolean.size() == 2 && boolean[0].toMap().value("label") == "false (0)" &&
	      boolean[0].toMap().value("text") == "false" &&
	      boolean[1].toMap().value("label") == "true (1)" &&
	      boolean[1].toMap().value("text") == "true");
	CHECK(model.setResult(0, "Ok", "false", ux::WatchSample::ofUnsigned(0),
	                      bits(ux::WatchScalar::Boolean, 1, 0)));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "false");
	CHECK(model.setResult(0, "Ok", "rounded display must not be parsed", {},
	                      bits(ux::WatchScalar::U64, 8, UINT64_MAX)));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "18446744073709551615" &&
	      !data(ux::WatchlistModel::SampleValueRole).isValid() &&
	      !data(ux::WatchlistModel::CategoricalRole).toBool());
	CHECK(model.setResult(0, "Ok", "rounded display", {},
	                      bits(ux::WatchScalar::S64, 8, UINT64_C(0x8000000000000000))));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "-9223372036854775808");
	CHECK(model.setResult(0, "Ok", "Negative (-1)", ux::WatchSample::ofSigned(-1),
	                      bits(ux::WatchScalar::S8, 1, 0xff, true)));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "-1" &&
	      data(ux::WatchlistModel::EditorKindRole) == "enum" &&
	      data(ux::WatchlistModel::CategoricalRole).toBool() &&
	      data(ux::WatchlistModel::EditorChoicesRole).toList().isEmpty());
	CHECK(model.setResult(0, "Ok", "unknown enum", {}, bits(ux::WatchScalar::U64, 8, UINT64_MAX, true)));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "18446744073709551615" &&
	      data(ux::WatchlistModel::CategoricalRole).toBool() &&
	      !data(ux::WatchlistModel::SampleExactRole).toBool());
	CHECK(model.setResult(0, "Ok", "0.1", ux::WatchSample::ofFloating(0.1f),
	                      bits(ux::WatchScalar::F32, 4, 0x3dcccccd)));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "0.100000001" &&
	      !data(ux::WatchlistModel::CategoricalRole).toBool());
	CHECK(model.setResult(0, "Ok", "negative zero", ux::WatchSample::ofFloating(-0.0),
	                      bits(ux::WatchScalar::F64, 8, UINT64_C(0x8000000000000000))));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "-0");
	CHECK(model.setResult(0, "Failed", "No reading", {}));
	CHECK(data(ux::WatchlistModel::EditorTextRole).toString().isEmpty());
	CHECK(model.setResult(0, "Ok", "large enum", {}, bits(ux::WatchScalar::U64, 8, UINT64_MAX, true)));
	current = false;
	CHECK(data(ux::WatchlistModel::EditorTextRole).toString().isEmpty() &&
	      !data(ux::WatchlistModel::EditorChoicesRole).isValid() &&
	      !data(ux::WatchlistModel::CategoricalRole).toBool());
}

QString text(ux::DynamicWatchlist& watch, int row)
{
	auto* model = watch.model();
	return model->data(model->index(row, 0), ux::WatchlistModel::TextRole).toString();
}

struct Peer {
	bool current = true, busy = false, supported = true, inCallback = false;
	ux::WatchlistSource::Limits limits{32, 130, 514};
	std::vector<std::vector<std::uint32_t>> calls;
	ux::WatchlistSource::Completion completion;
	std::vector<std::uint32_t> pending;
	std::shared_ptr<int> lease = std::make_shared<int>(0);
	// Set to read struct members: the schema and the member reads asked.
	std::shared_ptr<members::Schema> schema;
	std::vector<std::pair<quint32, std::vector<std::uint32_t>>> memberCalls;
	quint32 memberField = 0;

	ux::WatchlistSource source()
	{
		ux::WatchlistSource source;
		source.storage = lease;
		source.current = [this] {
			return current;
		};
		source.busy = [this] {
			return busy;
		};
		source.capability = [this]() -> std::optional<ux::WatchlistSource::Limits> {
			return supported ? std::optional{limits} : std::nullopt;
		};
		source.fieldBytes = [](quint32 id) -> std::optional<unsigned> {
			return id < 100 ? std::optional<unsigned>{8} : std::nullopt;
		};
		source.readMany = [this](std::span<const std::uint32_t> ids,
		                         ux::WatchlistSource::Completion done) {
			return submit(ids, std::move(done));
		};
		source.readField = [this](quint32 id, ux::WatchlistSource::Completion done) {
			return submit(std::span<const std::uint32_t>{&id, 1}, std::move(done));
		};
		if (schema) {
			source.schema = [this] { return schema->schema; };
			source.readMembers = [this](quint32 field, std::span<const std::uint32_t> leaves,
			                            ux::WatchlistSource::Completion done) -> cl::Submission {
				CHECK(current && !busy && !completion && !leaves.empty() && leaves.size() <= 1024);
				CHECK(std::is_sorted(leaves.begin(), leaves.end()));
				memberField = field;
				pending.assign(leaves.begin(), leaves.end());
				memberCalls.push_back({field, pending});
				completion = std::move(done);
				busy = true;
				return {cl::Status::Ok, {}};
			};
		}
		return source;
	}

	// Answers a member read: each value is its own ordinal as a u16. skew
	// answers the first value as another leaf, as a confused device would.
	void finishMembers(cl::Status status = cl::Status::Ok, bool skew = false)
	{
		CHECK(busy && bool(completion) && memberField != 0);
		ux::WatchlistCompletion result;
		result.status = status;
		result.native.value = status == cl::Status::Ok ? 0 : 2;
		result.completed = ux::WatchTimestamp::now();
		result.count = int(pending.size());
		result.members.resize(pending.size());
		for (std::size_t i = 0; i < pending.size(); ++i) {
			auto& row = result.members[i];
			row.id = memberField;
			row.leaf = pending[i] + 1;
			row.itemStatus = row.valueStatus = status == cl::Status::Ok ? 0 : 2;
			row.value.type = {7, ux::WatchScalar::U16, 2, false};
			const std::array<std::byte, 2> bytes{std::byte(pending[i] & 0xff), std::byte(pending[i] >> 8)};
			CHECK(row.value.assign(bytes.data(), 2));
			row.status = QStringLiteral("Ok");
			row.text = QString::number(pending[i]);
			row.sample = ux::WatchSample::ofUnsigned(pending[i]);
		}
		if (skew)
			result.members[0].leaf += 1;
		auto done = std::exchange(completion, {});
		busy = false;
		memberField = 0;
		*done = std::move(result);
	}

	cl::Submission submit(std::span<const std::uint32_t> ids, ux::WatchlistSource::Completion done)
	{
		CHECK(current && !busy && !completion && !ids.empty() && ids.size() <= 32);
		pending.assign(ids.begin(), ids.end());
		calls.push_back(pending);
		completion = std::move(done);
		busy = true;
		return {cl::Status::Ok, {}};
	}

	void finish(cl::Status status = cl::Status::Ok, bool malformed = false)
	{
		CHECK(busy && bool(completion) && memberField == 0);
		inCallback = true;
		ux::WatchlistCompletion result;
		result.status = status;
		result.native.client = qint32(status);
		result.native.value = status == cl::Status::Ok ? 0 : 2;
		result.completed = ux::WatchTimestamp::now();
		result.count = int(pending.size());
		for (int row = 0; row < result.count; ++row) {
			result.rows[row].id = pending[row];
			result.rows[row].status = QStringLiteral("Ok");
			auto& exact = result.rows[row];
			exact.itemStatus = status == cl::Status::Ok ? 0 : -1;
			exact.valueStatus = status == cl::Status::Ok ? 0 : 2;
			const auto id = pending[std::size_t(row)];
			exact.value.type = {id, id == 2 ? ux::WatchScalar::S64 : id == 3 ? ux::WatchScalar::S16
			    : id == 4 || id == 5 ? ux::WatchScalar::F32 : id == 6 ? ux::WatchScalar::F64 : ux::WatchScalar::U64,
			    quint8(id == 3 ? 2 : id == 4 || id == 5 ? 4 : 8), id == 3};
			const quint64 bits = id == 1 ? UINT64_MAX : id == 2 || id == 6 ? UINT64_C(0x8000000000000000)
			    : id == 3 ? UINT64_C(0xff85) : id == 4 ? UINT64_C(0x7fc12345)
			    : id == 5 ? UINT64_C(0x80000000) : id;
			std::array<std::byte, 8> borrowed{};
			for (unsigned i = 0; i != borrowed.size(); ++i) borrowed[i] = std::byte((bits >> (8 * i)) & 0xff);
			if (status == cl::Status::Ok) CHECK(exact.value.assign(borrowed.data(), exact.value.type.width));
			borrowed.fill(std::byte{0x55}); // RX borrow expires before the scheduler sees it.
			result.rows[row].text = pending[row] == 1   ? QStringLiteral("18446744073709551615")
			                        : pending[row] == 2 ? QStringLiteral("-9223372036854775808")
			                        : pending[row] == 3 ? QStringLiteral("-123 (unknown)")
			                                            : QString::number(pending[row]);
			// The typed facts a real binding takes from the same value (sampleOf).
			result.rows[row].sample =
			    pending[row] == 1   ? ux::WatchSample::ofUnsigned(18446744073709551615ULL)
			    : pending[row] == 2 ? ux::WatchSample::ofSigned(std::numeric_limits<long long>::min())
			    : pending[row] == 3 ? ux::WatchSample::ofSigned(-123)
			                        : ux::WatchSample::ofUnsigned(pending[row]);
		}
		if (malformed)
			result.rows[0].id = 999;
		auto done = std::exchange(completion, {});
		busy = false;
		*done = std::move(result); // Deliberately no watch.poll() while callback active.
		inCallback = false;
	}
};

void fill(ux::DynamicWatchlist& watch, int count)
{
	for (int id = 1; id <= count; ++id)
		CHECK(watch.add(quint32(id), QStringLiteral("catalog.Field%1").arg(id)));
}

void bulkPublication()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	QSignalSpy inserted(watch.model(), &QAbstractItemModel::rowsInserted);
	QVector<QPair<quint32, QString>> fields;
	for (quint32 i = 1; i <= 65; ++i)
		fields.push_back({i, QStringLiteral("field%1").arg(i)});
	fields.push_back({1, QStringLiteral("duplicate")});
	fields.push_back({100, QStringLiteral("unsupported")});
	const auto result = watch.addMany(fields);
	CHECK(result.value("status") == "ok" && result.value("added").toInt() == 65);
	CHECK(result.value("duplicate").toInt() == 1 && result.value("unsupported").toInt() == 1);
	CHECK(watch.count() == 65 && inserted.count() == 1 && peer.calls.empty());
	CHECK(watch.begin(false));
	for (int size : {32, 32, 1}) {
		CHECK(peer.pending.size() == size_t(size));
		peer.finish(); watch.poll(); events();
	}
	CHECK(!watch.active());
	CHECK(frames.size() == 3 && frames[0].count == 32 && frames[1].count == 32 && frames[2].count == 1);
	for (std::size_t i = 0; i != frames.size(); ++i) {
		CHECK(frames[i].sequence == qint64(i + 1) && frames[i].cycle == frames[0].cycle && frames[i].portion == qint64(i));
		CHECK(frames[i].phase == ux::WatchCompletionPhase::Current && frames[i].exact);
	}
	CHECK(frames[0].rows[0].id == 1 && frames[1].rows[0].id == 33 && frames[2].rows[0].id == 65);

	QObject otherOwner;
	Peer other;
	ux::DynamicWatchlist stale(&otherOwner);
	auto source = other.source();
	source.fieldBytes = [&other](quint32) -> std::optional<unsigned> {
		other.current = false;
		return 4;
	};
	stale.setSource(std::move(source));
	CHECK(stale.addMany({{1, "one"}}).value("status") == "stale");
	CHECK(stale.count() == 0 && other.calls.empty());

	QObject retireOwner;
	Peer retirePeer;
	ux::DynamicWatchlist retiring(&retireOwner);
	retiring.setSource(retirePeer.source());
	QObject::connect(retiring.model(), &QAbstractItemModel::rowsAboutToBeInserted, &retireOwner,
	                 [&] { retiring.retire(); });
	CHECK(retiring.addMany({{1, "one"}, {2, "two"}}).value("added").toInt() == 2);
	events();
	CHECK(retiring.count() == 0 && retirePeer.calls.empty());

	QPointer<QObject> doomed = new QObject;
	QObject observer;
	Peer deathPeer;
	ux::DynamicWatchlist orphaned(doomed);
	orphaned.setSource(deathPeer.source());
	QObject::connect(orphaned.model(), &QAbstractItemModel::rowsAboutToBeInserted, &observer,
	                 [&] { delete doomed.data(); });
	const auto destruction = orphaned.addMany({{1, "one"}});
	CHECK(!doomed && destruction.value("added").toInt() == 1 && deathPeer.calls.empty());
}

// One Watchlist holds at most watchlistRowLimit Fields. Past it nothing more is
// added, in bulk or one at a time, and the bulk result counts the rest as
// "capacity".
void rowLimit()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	auto source = peer.source();
	source.fieldBytes = [](quint32) -> std::optional<unsigned> { return 4; };
	watch.setSource(std::move(source));
	QVector<QPair<quint32, QString>> fields;
	for (quint32 i = 1; i <= quint32(ux::watchlistRowLimit) + 5; ++i)
		fields.push_back({i, QStringLiteral("field%1").arg(i)});
	const auto result = watch.addMany(fields);
	CHECK(result.value("status") == "ok");
	CHECK(result.value("added").toInt() == ux::watchlistRowLimit);
	CHECK(result.value("capacity").toInt() == 5);
	CHECK(watch.count() == ux::watchlistRowLimit);
	CHECK(!watch.canAdd(5000) && !watch.add(5000, QStringLiteral("over")));
	const auto over = watch.addMany({{6000, QStringLiteral("over")}});
	CHECK(over.value("added").toInt() == 0 && over.value("capacity").toInt() == 1);
	CHECK(watch.count() == ux::watchlistRowLimit && peer.calls.empty());
}

// After a write while polling, the Watchlist reads again without waiting for
// its timer. A Field this cycle already read, or had in flight while the write
// waited, is read first; one not read yet waits for its turn. With no Field
// named (a Command, Service or Batch) the next cycle follows this one at once,
// and between cycles a wake starts the next one now.
void wakeAfterWrites()
{
	const auto step = [](ux::DynamicWatchlist& watch, Peer& peer) {
		peer.finish();
		watch.poll();
		for (int i = 0; i < 4; ++i)
			events();
	};
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 65);
	watch.setInterval(60000); // the timer never fires during this test
	CHECK(watch.begin(true));
	CHECK(peer.pending.size() == 32 && peer.pending.front() == 1);
	step(watch, peer);
	CHECK(peer.pending.size() == 32 && peer.pending.front() == 33); // in flight
	watch.wake(5u);  // read earlier in this cycle
	watch.wake(40u); // in flight: read before the write
	watch.wake(65u); // not read yet: its turn comes after the write
	watch.wake(5u);  // named twice, read once
	step(watch, peer);
	CHECK(peer.pending == std::vector<std::uint32_t>{5});
	step(watch, peer);
	CHECK(peer.pending == std::vector<std::uint32_t>{40});
	CHECK(text(watch, 4) == QStringLiteral("5"));
	step(watch, peer);
	CHECK(peer.pending == std::vector<std::uint32_t>{65});
	step(watch, peer);
	CHECK(!peer.busy && !watch.pending()); // complete; the 60 s timer has not fired
	const auto calls = peer.calls.size();
	watch.wake();
	for (int i = 0; i < 4; ++i)
		events();
	CHECK(peer.calls.size() == calls + 1 && peer.pending.front() == 1);
	step(watch, peer); // 33..64 in flight
	watch.wake();      // a Command: whatever it changed is read in the next cycle
	step(watch, peer); // 65
	CHECK(peer.pending == std::vector<std::uint32_t>{65});
	step(watch, peer); // complete, and the next cycle follows at once
	CHECK(peer.busy && peer.pending.size() == 32 && peer.pending.front() == 1);
	step(watch, peer);
	step(watch, peer);
	step(watch, peer);
	CHECK(!peer.busy); // no further cycle without a new wake or tick
	watch.stop();
	// Not polling: a wake reads nothing.
	const auto stopped = peer.calls.size();
	watch.wake(5u);
	watch.wake();
	for (int i = 0; i < 4; ++i)
		events();
	CHECK(peer.calls.size() == stopped);
}

// Interval 0 reads continuously, at low priority: each cycle follows the
// last without the timer, and nothing is sent while other work is in flight.
void continuousLowPriority()
{
	const auto settle = [] {
		for (int i = 0; i < 4; ++i)
			events();
	};
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 40);
	watch.setInterval(0);
	CHECK(watch.interval() == 0);
	watch.setInterval(-1);
	CHECK(watch.interval() == 0);
	CHECK(watch.begin(true));
	CHECK(peer.pending.size() == 32);
	peer.finish();
	watch.poll();
	settle();
	CHECK(peer.pending.size() == 8 && peer.pending.front() == 33);
	peer.finish();
	peer.busy = true; // another panel's request is in flight
	watch.poll();
	settle();
	CHECK(peer.calls.size() == 2); // nothing sent beside it
	peer.busy = false;
	watch.poll(); // its completion progresses the session
	settle();
	CHECK(peer.calls.size() == 3 && peer.pending.size() == 32 && peer.pending.front() == 1);
	watch.stop();
	peer.finish();
	watch.poll();
	settle();
	CHECK(!watch.active() && peer.calls.size() == 3);
}

// A tick that finds the session busy (another panel's request) starts the
// cycle anyway and sends when the session is free, instead of being skipped;
// a wake between cycles does the same. Stop leaves no rerun behind.
void busySessionTicks()
{
	const auto settle = [] {
		for (int i = 0; i < 4; ++i)
			events();
	};
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 3);
	watch.setInterval(60000);
	CHECK(watch.begin(true));
	peer.finish();
	watch.poll();
	settle();
	CHECK(!peer.busy && !watch.pending()); // first cycle complete
	peer.busy = true;                      // another panel's request
	watch.tick();
	CHECK(!watch.pending() && peer.calls.size() == 1);
	peer.busy = false;
	watch.poll(); // that request's completion progresses the session
	settle();
	CHECK(peer.calls.size() == 2 && peer.busy); // the cycle went out
	peer.finish();
	watch.poll();
	settle();
	peer.busy = true; // a write from another panel is still queued
	watch.dataChanged();
	settle();
	CHECK(peer.calls.size() == 2);
	peer.busy = false;
	watch.poll();
	settle();
	CHECK(peer.calls.size() == 3); // read again after the write, not after a minute
	watch.wake(); // a Command during this cycle: a rerun is due
	watch.stop();
	peer.finish();
	watch.poll();
	settle();
	CHECK(watch.begin(true));
	peer.finish();
	watch.poll();
	settle();
	CHECK(!peer.busy && peer.calls.size() == 4); // no leftover rerun after the restart
	watch.stop();
}

// Start while a Read once runs turns that cycle into the first one of polling
// rather than being refused; the timer then repeats it.
void startDuringReadOnce()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 3);
	CHECK(watch.begin(false) && peer.calls.size() == 1);
	CHECK(watch.begin(true) && watch.active());
	peer.finish();
	watch.poll();
	for (int i = 0; i < 4; ++i)
		events();
	CHECK(watch.active() && !watch.pending()); // polling, between cycles
	watch.tick();
	CHECK(peer.calls.size() == 2 && watch.pending());
	watch.stop();
}

void trendCompletions()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	CHECK(watch.add(4, QStringLiteral("Field4")));
	ux::TrendHistory trend;
	trend.setSource(watch.model());
	trend.setRow(0);
	for (int attempt = 0; attempt < 2; ++attempt) {
		CHECK(watch.begin(false));
		peer.finish();
		watch.poll(); events();
	}
	CHECK(trend.points().size() == 2);
	CHECK(!trend.points().last().toMap().value("breakBefore").toBool());
	CHECK(trend.points().last().toMap().value("value").toDouble() == 4.0);
	const auto* model = watch.model();
	const auto first = model->index(0, 0);
	CHECK(model->data(first, ux::WatchlistModel::SampleValueRole).toDouble() == 4.0);
	CHECK(model->data(first, ux::WatchlistModel::SampleExactRole).toBool());
	CHECK(model->data(first, ux::WatchlistModel::StatusCodeRole).toInt() ==
	      int(ux::WatchStatus::Ok));
	watch.setIndividual(true);
	CHECK(watch.begin(false));
	peer.finish(); watch.poll(); events();
	CHECK(trend.points().size() == 3 && peer.calls.size() == 3);
	static_cast<ux::WatchlistModel*>(watch.model())->invalidateValues();
	CHECK(watch.model()->data(watch.model()->index(0, 0), ux::WatchlistModel::SampleTextRole).toString().isEmpty());
	CHECK(watch.begin(false));
	peer.finish(); watch.poll(); events();
	CHECK(trend.points().size() == 4 && trend.points().last().toMap().value("breakBefore").toBool());
}

void chunks()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	QAbstractItemModelTester tester(watch.model(),
	                                QAbstractItemModelTester::FailureReportingMode::Fatal);
	fill(watch, 65);
	CHECK(watch.count() == 65 && peer.calls.empty());
	CHECK(!watch.add(1, QStringLiteral("duplicate")) && !watch.canAdd(100));
	CHECK(watch.begin(false) && watch.active() && watch.pending());
	QSignalSpy changed(watch.model(), &QAbstractItemModel::dataChanged);
	for (int batch = 0; batch < 3; ++batch) {
		CHECK(peer.calls.size() == std::size_t(batch + 1));
		CHECK(peer.calls.back().size() == std::size_t(batch == 2 ? 1 : 32));
		for (int tick = 0; tick < 50; ++tick)
			watch.tick();
		CHECK(peer.calls.size() == std::size_t(batch + 1));
		// The read mode and the interval wait for the cycle (rows may change
		// meanwhile, fix 8; removeDuringRead and clearWhilePolling check that).
		watch.setIndividual(true);
		watch.setInterval(1);
		CHECK(watch.count() == 65 && !watch.individual() && watch.interval() == 1000);
		const auto before = changed.count();
		peer.finish();
		CHECK(watch.pending() &&
		      changed.count() == before); // No Qt publication in native callback.
		watch.poll();
		CHECK(!watch.pending());
		events();
	}
	CHECK(!watch.active() && watch.status().contains(QStringLiteral("complete")));
	CHECK(text(watch, 0) == QStringLiteral("18446744073709551615"));
	CHECK(text(watch, 1) == QStringLiteral("-9223372036854775808"));
	CHECK(text(watch, 2) == QStringLiteral("-123 (unknown)"));
	CHECK(text(watch, 64) == QStringLiteral("65"));
	CHECK(peer.calls[0].front() == 1 && peer.calls[1].front() == 33 && peer.calls[2].front() == 65);
	watch.setInterval(1);
	CHECK(watch.interval() == 1);
	watch.setInterval(60000);
	CHECK(watch.interval() == 60000);
	watch.setInterval(60001);
	CHECK(watch.interval() == 60000);
	watch.setInterval(-1);
	CHECK(watch.interval() == 60000);
	watch.setInterval(0); // continuous
	CHECK(watch.interval() == 0);
}

void budgetsAndFallback()
{
	QObject owner;
	Peer peer;
	peer.limits = {32, 10, 34}; // Both request and reply permit only two U64 fields.
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 5);
	CHECK(watch.begin(false));
	for (int count : {2, 2, 1}) {
		CHECK(peer.pending.size() == std::size_t(count));
		peer.finish();
		watch.poll();
		events();
	}
	CHECK(!watch.active() && peer.calls.size() == 3);
	peer.supported = false;
	CHECK(!watch.supported() && !watch.individual());
	CHECK(!watch.begin(false) && peer.calls.size() == 3); // No automatic fan-out.
	watch.setIndividual(true);
	CHECK(watch.individual());
	CHECK(watch.begin(false));
	for (int i = 0; i < 5; ++i) {
		CHECK(peer.pending.size() == 1);
		peer.finish();
		watch.poll();
		events();
	}
	CHECK(!watch.active() && peer.calls.size() == 8);
	watch.setIndividual(false);
	peer.supported = true;
	peer.limits.replyBytes = 17;
	CHECK(!watch.begin(false) && peer.calls.size() == 8);
	CHECK(watch.status().contains(QStringLiteral("byte budget")));
}

void drainAndStale()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 65);
	CHECK(watch.begin(true));
	std::weak_ptr<int> lease = peer.lease;
	watch.stop();
	CHECK(watch.active() && watch.pending());
	// Rows change while the read drains (fix 8); the read mode does not.
	watch.remove(0);
	CHECK(watch.count() == 64);
	watch.clear();
	watch.setIndividual(true);
	CHECK(watch.count() == 0 && !watch.individual());
	peer.finish();
	watch.poll();
	events();
	CHECK(!watch.active() && peer.calls.size() == 1);
	fill(watch, 65);
	CHECK(text(watch, 0).isEmpty());
	CHECK(watch.begin(false));
	peer.current = false;
	CHECK(text(watch, 0).isEmpty()); // Guard works before the owner publishes reset.
	watch.retire();
	CHECK(watch.count() == 0 && watch.pending() && watch.active());
	peer.lease.reset();
	CHECK(!lease.expired());
	peer.finish();
	watch.poll();
	events();
	CHECK(!watch.active() && peer.calls.size() == 2);
	Peer fresh;
	watch.setSource(fresh.source());
	fill(watch, 2);
	CHECK(lease.expired());
	CHECK(watch.begin(false));
	fresh.finish(cl::Status::Ok, true);
	watch.poll();
	events();
	CHECK(!watch.active() && text(watch, 0).isEmpty() && text(watch, 1).isEmpty());
}

void schedulingAndReentrancy()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	bool blocked = false;
	watch.setNotify(
	    [] {
	    },
	    [&] {
		    return blocked;
	    });
	watch.setSource(peer.source());
	fill(watch, 1);
	CHECK(watch.begin(true));
	peer.finish();
	watch.poll();
	events();
	CHECK(watch.active() && !watch.pending());
	// Ticks while the session is busy start one cycle that waits: nothing
	// accumulates, and it goes out on the session's next progress once neither
	// the session nor this panel holds it.
	peer.busy = true;
	for (int i = 0; i < 100; ++i)
		watch.tick();
	CHECK(peer.calls.size() == 1 && !watch.pending());
	peer.busy = false;
	blocked = true;
	watch.poll();
	events();
	CHECK(peer.calls.size() == 1);
	blocked = false;
	watch.poll();
	events();
	CHECK(peer.calls.size() == 2 && watch.pending());
	bool attempted = false;
	QObject::connect(watch.model(), &QAbstractItemModel::dataChanged, &owner, [&] {
		attempted = true;
		watch.clear();
		watch.remove(0);
		watch.setIndividual(true);
		CHECK(watch.count() == 1 && !watch.individual());
	});
	peer.finish();
	watch.poll();
	events();
	CHECK(attempted && peer.calls.size() == 2);
	watch.stop();
	CHECK(!watch.active());
	CHECK(watch.begin(false));
	{
		auto removed = std::make_unique<ux::DynamicWatchlist>(&owner);
		removed->setSource(peer.source());
	}
	peer.finish();
	watch.poll();
	events();
	CHECK(!watch.active());
	CHECK(watch.begin(false));
	QObject::connect(watch.model(), &QAbstractItemModel::dataChanged, &owner, [&] {
		peer.current = false;
		watch.retire(); // Defer the structural reset until model publication returns.
	});
	peer.finish();
	watch.poll();
	events();
	CHECK(watch.count() == 0 && !watch.active());
}

void replaceSourceDuringPublication()
{
	QObject owner;
	Peer old, fresh;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(old.source());
	fill(watch, 2);
	CHECK(watch.begin(false));
	bool replaced = false;
	QObject::connect(watch.model(), &QAbstractItemModel::dataChanged, &owner, [&] {
		if (replaced)
			return;
		replaced = true;
		watch.setSource(fresh.source());
		CHECK(text(watch, 0).isEmpty() && text(watch, 1).isEmpty());
		CHECK(watch.active()); // Reset/attachment must settle after model publication.
	});
	old.finish();
	watch.poll();
	CHECK(replaced && text(watch, 0).isEmpty() && text(watch, 1).isEmpty());
	CHECK(fresh.calls.empty());
	events();
	CHECK(!watch.active() && watch.count() == 0);
	fill(watch, 1);
	CHECK(watch.begin(false));
	fresh.finish();
	watch.poll();
	events();
	CHECK(!watch.active() && fresh.calls.size() == 1);
	CHECK(text(watch, 0) == QStringLiteral("18446744073709551615"));
}

QByteArray raw(const ux::WatchValueBits& value)
{
	return QByteArray(reinterpret_cast<const char*>(value.bytes.data()), value.size);
}

void exactCompletionFrames()
{
	QObject owner;
	Peer old, fresh;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(old.source());
	fill(watch, 6);
	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) {
		CHECK(!old.inCallback && !fresh.inCallback);
		frames.push_back(frame);
	});
	CHECK(watch.begin(false));
	old.finish();
	CHECK(frames.empty()); // filling the native completion slot publishes nothing
	watch.poll(); events();
	CHECK(frames.size() == 1 && frames[0].count == 6 && frames[0].exact && frames[0].correlated);
	CHECK(frames[0].phase == ux::WatchCompletionPhase::Current && frames[0].operation == ux::WatchOperation::ReadMany);
	CHECK(frames[0].sequence == 1 && frames[0].cycle == 1 && frames[0].portion == 0 && frames[0].sourceBinding > 0);
	CHECK(frames[0].completed.monotonicNs >= frames[0].startedNs);
	CHECK(raw(frames[0].rows[0].value) == QByteArray::fromHex("ffffffffffffffff"));
	CHECK(raw(frames[0].rows[1].value) == QByteArray::fromHex("0000000000000080"));
	CHECK(frames[0].rows[2].value.type.enumeration && frames[0].rows[2].value.type.scalar == ux::WatchScalar::S16 &&
	      raw(frames[0].rows[2].value) == QByteArray::fromHex("85ff"));
	CHECK(raw(frames[0].rows[3].value) == QByteArray::fromHex("4523c17f"));
	CHECK(raw(frames[0].rows[4].value) == QByteArray::fromHex("00000080"));
	CHECK(raw(frames[0].rows[5].value) == QByteArray::fromHex("0000000000000080"));
	watch.setIndividual(true);
	CHECK(watch.begin(false));
	for (int i = 0; i != 6; ++i) { old.finish(); watch.poll(); events(); }
	CHECK(frames.size() == 7 && !watch.active());
	for (int i = 0; i != 6; ++i) {
		const auto& frame = frames[std::size_t(i + 1)];
		CHECK(frame.operation == ux::WatchOperation::FieldRead && frame.count == 1 && frame.rows[0].id == quint32(i + 1));
		CHECK(frame.sequence == i + 2 && frame.cycle == 2 && frame.portion == i && frame.exact);
	}
	watch.setIndividual(false);
	CHECK(watch.begin(false));
	const auto slot = old.completion;
	old.finish(cl::Status::Timeout);
	(*slot)->count = 0; // a real aggregate failure is allowed to return no items
	watch.poll(); events();
	CHECK(frames.size() == 8 && frames.back().phase == ux::WatchCompletionPhase::Failed && frames.back().count == 6);
	CHECK(frames.back().native.client == qint32(cl::Status::Timeout) && !frames.back().correlated);
	for (int i = 0; i != 6; ++i) CHECK(frames.back().rows[std::size_t(i)].id == quint32(i + 1) &&
	    frames.back().rows[std::size_t(i)].value.size == 0 && frames.back().rows[std::size_t(i)].value.type.valid());
	CHECK(watch.begin(false));
	watch.stop();
	old.finish(); watch.poll(); events();
	CHECK(frames.size() == 9 && frames.back().phase == ux::WatchCompletionPhase::Drained && !watch.active());
	CHECK(watch.begin(false));
	watch.setSource(fresh.source());
	old.finish(); watch.poll(); events();
	CHECK(frames.size() == 10 && frames.back().phase == ux::WatchCompletionPhase::Drained && watch.count() == 0);
	const auto oldBinding = frames.back().sourceBinding;
	fill(watch, 1);
	CHECK(watch.begin(false));
	fresh.finish(); watch.poll(); events();
	CHECK(frames.size() == 11 && frames.back().phase == ux::WatchCompletionPhase::Current && frames.back().sourceBinding > oldBinding);
	CHECK(watch.begin(false));
	const auto unavailable = fresh.completion;
	fresh.finish();
	(*unavailable)->rows[0].itemStatus = 3;
	(*unavailable)->rows[0].valueStatus = 2;
	(*unavailable)->rows[0].value.size = 0;
	(*unavailable)->rows[0].status = QStringLiteral("Unavailable");
	(*unavailable)->rows[0].text.clear();
	(*unavailable)->rows[0].sample = {};
	watch.poll(); events();
	CHECK(frames.size() == 12 && frames.back().phase == ux::WatchCompletionPhase::Current && frames.back().exact);
	CHECK(frames.back().rows[0].itemStatus == 3 && frames.back().rows[0].valueStatus == 2 && frames.back().rows[0].value.size == 0);
	CHECK(watch.begin(false));
	fresh.finish(cl::Status::Ok, true); watch.poll(); events();
	CHECK(frames.size() == 13 && frames.back().phase == ux::WatchCompletionPhase::Failed && !frames.back().correlated);
	CHECK(frames.back().rows[0].id == 1 && frames.back().rows[0].value.size == 0);
	// One direct bounded recorder can halt the next cycle on backpressure. It
	// cannot recursively consume or start another request from this publication.
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) {
		frames.push_back(frame);
		watch.stop();
		CHECK(!watch.begin(true));
		watch.poll();
	});
	CHECK(watch.begin(true));
	fresh.finish(); watch.poll(); events();
	CHECK(frames.size() == 14 && !watch.active() && fresh.calls.size() == 4);
	const auto frameCount = frames.size();
	watch.poll(); events();
	CHECK(frames.size() == frameCount);
	// Destruction in a public terminal observer cannot continue with row writes.
	auto doomedOwner = std::make_unique<QObject>();
	auto doomed = std::make_unique<ux::DynamicWatchlist>(doomedOwner.get());
	Peer peer;
	doomed->setSource(peer.source()); fill(*doomed, 1);
	int destroyedFrames = 0;
	doomed->setCompletionObserver([&](const ux::WatchFrame&) {
		++destroyedFrames;
		doomedOwner.reset();
		doomed.reset();
	});
	CHECK(doomed->begin(false)); peer.finish(); doomed->poll();
	CHECK(!doomedOwner && !doomed && destroyedFrames == 1 && peer.calls.size() == 1);
}
} // namespace

// A Watchlist entry as a host names it; its type and write scope are the
// Watchlist's to fill in.
ux::WatchEntry entryOf(ux::WatchKey key, const QString& name, const QString& member = {})
{
	ux::WatchEntry entry;
	entry.key = key;
	entry.name = name;
	entry.member = member;
	return entry;
}

// The Config values of the members fixture, as the Device tree would name them.
QVector<ux::WatchEntry> configEntries(std::initializer_list<quint32> ordinals)
{
	QVector<ux::WatchEntry> entries;
	for (const auto ordinal : ordinals)
		entries.push_back(entryOf(ux::watch_key::member(members::config, ordinal),
		                          QStringLiteral("device.Config #%1").arg(ordinal)));
	return entries;
}

// One cycle's reads: whole Fields together in ReadMany portions, every value
// of one struct in one read, wherever its rows are, in ascending order.
void memberPlan()
{
	QObject owner;
	Peer peer;
	peer.schema = std::make_shared<members::Schema>();
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	auto* model = watch.model();
	QVector<ux::WatchEntry> rows{entryOf(ux::watch_key::whole(2), "device.Two")};
	rows += configEntries({5});
	rows.push_back(entryOf(ux::watch_key::whole(3), "device.Three"));
	rows += configEntries({0});
	rows.push_back(entryOf(ux::watch_key::whole(4), "device.Four"));
	// Refused: past the last value, a scalar Field's "member", a wrong member text.
	rows += configEntries({members::leaves});
	rows.push_back(entryOf(ux::watch_key::member(0, 0), "device.Counter"));
	rows.push_back(entryOf(ux::watch_key::member(members::config, 1), "device.Config.enabled", "gain"));
	rows += configEntries({5});
	const auto added = watch.addEntries(rows);
	CHECK(added.value("status") == "ok" && added.value("added").toInt() == 5);
	CHECK(added.value("unsupported").toInt() == 3 && added.value("duplicate").toInt() == 1);
	CHECK(watch.held().contains(ux::watch_key::member(members::config, 5)) && watch.held().contains(2));
	const auto memberRow = model->index(1, 0);
	CHECK(model->data(memberRow, ux::WatchlistModel::MemberRole).toString() == "samples[3]");
	CHECK(model->data(memberRow, ux::WatchlistModel::WatchKeyRole).toULongLong() ==
	      ux::watch_key::member(members::config, 5));
	CHECK(model->data(memberRow, ux::WatchlistModel::EndpointIdRole).toUInt() == members::config);
	CHECK(model->data(memberRow, ux::WatchlistModel::TypeTextRole).toString() == "u16");
	CHECK(model->data(model->index(3, 0), ux::WatchlistModel::TypeTextRole).toString() == "f32");
	CHECK(model->data(model->index(0, 0), ux::WatchlistModel::MemberRole).toString().isEmpty());
	// Each step's type for the tree's nodes: the Field's, then down to the value's.
	CHECK(model->data(memberRow, ux::WatchlistModel::PathTypesRole).toStringList() ==
	      QStringList({"struct (3)", "u16[300]", "u16"}));
	CHECK(model->data(model->index(3, 0), ux::WatchlistModel::PathTypesRole).toStringList() ==
	      QStringList({"struct (3)", "f32"}));
	CHECK(model->data(model->index(0, 0), ux::WatchlistModel::PathTypesRole).toStringList().size() <= 1);  // the Peer's whole Fields are not in its schema

	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	CHECK(watch.begin(false));
	CHECK(peer.calls.size() == 1 && peer.calls[0] == std::vector<std::uint32_t>{2, 3, 4});
	CHECK(watch.status() == "Reading values 1-3 of 5");
	peer.finish(); watch.poll(); events();
	CHECK(peer.memberCalls.size() == 1 && peer.memberCalls[0].first == members::config &&
	      peer.memberCalls[0].second == std::vector<std::uint32_t>{0, 5});
	peer.finishMembers(); watch.poll(); events();
	CHECK(!watch.active() && watch.status() == "ReadMany complete - 5 values");
	CHECK(model->data(model->index(1, 0), ux::WatchlistModel::TextRole).toString() == "5");
	CHECK(model->data(model->index(3, 0), ux::WatchlistModel::TextRole).toString() == "0");
	CHECK(model->data(model->index(3, 0), ux::WatchlistModel::SampleValueRole).toDouble() == 0.0);
	CHECK(frames.size() == 2 && frames[1].operation == ux::WatchOperation::FieldRead && frames[1].count == 2);
	CHECK(frames[1].rows[0].id == members::config && frames[1].rows[0].leaf == 1 && frames[1].rows[1].leaf == 6);
	CHECK(frames[0].rows[0].leaf == 0 && frames[1].phase == ux::WatchCompletionPhase::Current && frames[1].exact);
	CHECK(frames[1].sequence == frames[0].sequence + 1 && frames[1].portion == 1);

	// Individual mode: one FieldRead per whole Field, the struct still one read.
	peer.calls.clear(); peer.memberCalls.clear();
	watch.setIndividual(true);
	CHECK(watch.begin(false));
	for (const quint32 id : {2u, 3u, 4u}) {
		CHECK(peer.calls.size() == 1 + std::size_t(id - 2) && peer.calls.back() == std::vector<std::uint32_t>{id});
		if (id == 2) {
			// The struct's read comes where its first row is: after Field 2.
			peer.finish(); watch.poll(); events();
			CHECK(peer.memberCalls.size() == 1);
			peer.finishMembers(); watch.poll(); events();
		} else {
			peer.finish(); watch.poll(); events();
		}
	}
	CHECK(!watch.active() && watch.status() == "Individual FieldRead complete - 5 values");

	// Taken out by key, put back in place.
	const auto removed = watch.removeMany({ux::watch_key::member(members::config, 0), 3});
	CHECK(removed.size() == 2 && removed[0].row == 2 && removed[1].row == 3 && watch.count() == 3);
	CHECK(removed[1].entry.member == "gain" && removed[1].entry.type != 0);
	CHECK(watch.restore(removed) && watch.count() == 5 &&
	      model->data(model->index(3, 0), ux::WatchlistModel::MemberRole).toString() == "gain");
}

// All 302 values of one struct: one read, published as ten frames of at most
// 32 rows with consecutive sequences, the same cycle and portion.
void memberFanOut()
{
	QObject owner;
	Peer peer;
	peer.schema = std::make_shared<members::Schema>();
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	QVector<ux::WatchEntry> all;
	for (quint32 n = 0; n < members::leaves; ++n)
		all += configEntries({n});
	CHECK(watch.addEntries(all).value("added").toInt() == int(members::leaves));
	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	CHECK(watch.begin(false));
	CHECK(peer.calls.empty() && peer.memberCalls.size() == 1 && peer.memberCalls[0].second.size() == members::leaves);
	CHECK(watch.status() == "Reading values 1-302 of 302");
	peer.finishMembers(); watch.poll(); events();
	CHECK(!watch.active() && peer.memberCalls.size() == 1 && frames.size() == 10);
	for (std::size_t k = 0; k != frames.size(); ++k) {
		const auto& frame = frames[k];
		CHECK(frame.sequence == frames[0].sequence + qint64(k) && frame.cycle == frames[0].cycle &&
		      frame.portion == 0 && frame.startedNs == frames[0].startedNs);
		CHECK(frame.operation == ux::WatchOperation::FieldRead && frame.phase == ux::WatchCompletionPhase::Current &&
		      frame.correlated && frame.exact && !frame.urgent);
		CHECK(frame.count == (k < 9 ? 32 : 14));
		for (int i = 0; i < frame.count; ++i)
			CHECK(frame.rows[std::size_t(i)].id == members::config &&
			      frame.rows[std::size_t(i)].leaf == quint32(k * 32 + std::size_t(i) + 1));
	}
	auto* model = watch.model();
	CHECK(model->data(model->index(301, 0), ux::WatchlistModel::TextRole).toString() == "301");
	CHECK(model->data(model->index(301, 0), ux::WatchlistModel::MemberRole).toString() == "samples[299]");
	CHECK(model->data(model->index(0, 0), ux::WatchlistModel::StatusRole).toString() == "Ok");
	// A second cycle numbers its frames on.
	CHECK(watch.begin(false));
	peer.finishMembers(); watch.poll(); events();
	CHECK(frames.size() == 20 && frames[10].sequence == frames[9].sequence + 1 && frames[10].cycle == frames[9].cycle + 1);
}

// A struct written while polling is read again before the cycle goes on, as
// one read of its watched values; a struct not yet reached is not.
void memberUrgent()
{
	QObject owner;
	Peer peer;
	peer.schema = std::make_shared<members::Schema>();
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	auto rows = configEntries({1, 0});
	rows.push_back(entryOf(ux::watch_key::whole(2), "device.Two"));
	CHECK(watch.addEntries(rows).value("added").toInt() == 3);
	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	CHECK(watch.begin(true));
	CHECK(peer.memberCalls.size() == 1 && peer.memberCalls[0].second == std::vector<std::uint32_t>{0, 1});
	watch.wake(2);               // not reached yet: read in its turn
	watch.wake(members::config); // in flight: sent before the write, so read again
	watch.wake(members::config); // once is enough
	peer.finishMembers(); watch.poll(); events();
	CHECK(peer.calls.empty() && peer.memberCalls.size() == 2 &&
	      peer.memberCalls[1].second == std::vector<std::uint32_t>{0, 1});
	CHECK(watch.status() == "Reading a written Field again");
	peer.finishMembers(); watch.poll(); events();
	CHECK(peer.calls.size() == 1 && peer.calls[0] == std::vector<std::uint32_t>{2} && peer.memberCalls.size() == 2);
	CHECK(frames.size() == 2 && !frames[0].urgent && frames[1].urgent &&
	      frames[1].phase == ux::WatchCompletionPhase::Current);
	CHECK(frames[1].count == 2 && frames[1].rows[0].leaf == 1 && frames[1].rows[1].leaf == 2);
	peer.finish(); watch.poll(); events();
	CHECK(frames.size() == 3 && !frames[2].urgent && !peer.busy);
	watch.stop();
	events();
}

// What stops a cycle at a struct's read: no member reads on this connection,
// a reply that names other values, a failed read.
void memberFailure()
{
	{
		QObject owner;
		Peer peer;
		peer.schema = std::make_shared<members::Schema>();
		auto source = peer.source();
		source.readMembers = {};
		ux::DynamicWatchlist watch(&owner);
		watch.setSource(std::move(source));
		const auto added = watch.addEntries(configEntries({0, 1}));
		CHECK(added.value("added").toInt() == 0 && added.value("unsupported").toInt() == 2);
	}
	for (const bool skew : {true, false}) {
		QObject owner;
		Peer peer;
		peer.schema = std::make_shared<members::Schema>();
		ux::DynamicWatchlist watch(&owner);
		watch.setSource(peer.source());
		auto rows = configEntries({0, 1});
		rows.push_back(entryOf(ux::watch_key::whole(2), "device.Two"));
		CHECK(watch.addEntries(rows).value("added").toInt() == 3);
		std::vector<ux::WatchFrame> frames;
		watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
		CHECK(watch.begin(true));
		peer.finishMembers(skew ? cl::Status::Ok : cl::Status::CodecError, skew);
		watch.poll(); events();
		CHECK(!watch.active() && peer.calls.empty() && watch.status().startsWith("Watch read failed"));
		CHECK(frames.size() == 1 && frames[0].phase == ux::WatchCompletionPhase::Failed);
		CHECK(frames[0].correlated == !skew);
		auto* model = watch.model();
		CHECK(model->data(model->index(0, 0), ux::WatchlistModel::StatusRole).toString() == "Stale");
	}
}

// Fix 8: the Watchlist takes changes while it polls. A change enters the
// list at once; the read in flight finishes as it was asked and is applied
// to the rows still there; the plan follows at the next portion.
void removeDuringRead()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 3);
	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	CHECK(watch.begin(true));
	const auto asked = watch.membership();
	const auto removed = watch.removeMany({2});
	CHECK(removed.size() == 1 && watch.count() == 2 && watch.active() && watch.membership() != asked);
	peer.finish(); watch.poll(); events();
	CHECK(frames.size() == 1 && frames[0].phase == ux::WatchCompletionPhase::Current && frames[0].correlated &&
	      frames[0].count == 3 && frames[0].membership == asked);
	CHECK(text(watch, 0) == "18446744073709551615" && text(watch, 1) == "-123 (unknown)");
	CHECK(watch.active() && !watch.status().startsWith("Watch read failed"));
	// The next cycle reads the rows there now.
	watch.tick();
	CHECK(peer.calls.size() == 2 && peer.calls[1] == std::vector<std::uint32_t>{1, 3});
	peer.finish(); watch.poll(); events();
	CHECK(frames.size() == 2 && frames[1].membership == watch.membership());
	watch.stop();
	events();
}

void addDuringCycle()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 40);
	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	CHECK(watch.begin(true));
	CHECK(peer.calls.size() == 1 && peer.calls[0].size() == 32);
	CHECK(watch.add(50, QStringLiteral("catalog.Field50")) && watch.count() == 41);
	// The new row waits for the next portion boundary; its value is empty.
	CHECK(text(watch, 40).isEmpty());
	peer.finish(); watch.poll(); events();
	// A new cycle over the rows shown now: it starts again from the first row.
	CHECK(peer.calls.size() == 2 && peer.calls[1].size() == 32 && peer.calls[1][0] == 1);
	CHECK(frames.size() == 1 && frames[0].phase == ux::WatchCompletionPhase::Current);
	peer.finish(); watch.poll(); events();
	CHECK(peer.calls.size() == 3 && peer.calls[2].size() == 9 && peer.calls[2].back() == 50);
	peer.finish(); watch.poll(); events();
	CHECK(frames.size() == 3 && frames[2].cycle == frames[1].cycle && frames[1].cycle == frames[0].cycle + 1);
	CHECK(text(watch, 40) == "50");
	watch.stop();
	events();
}

void addBetweenCycles()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 2);
	CHECK(watch.begin(true));
	peer.finish(); watch.poll(); events();
	CHECK(peer.calls.size() == 1 && watch.active() && !watch.pending());
	// Polling between two cycles: an addition is read now, not at the tick.
	const auto added = watch.addEntries({entryOf(ux::watch_key::whole(3), QStringLiteral("catalog.Field3"))});
	CHECK(added.value("added").toInt() == 1);
	for (int step = 0; step < 4; ++step) events(); // notice, then start, then send
	CHECK(peer.calls.size() == 2 && peer.calls[1] == std::vector<std::uint32_t>{1, 2, 3});
	peer.finish(); watch.poll(); events();
	// A removal waits for the timer.
	watch.remove(0);
	events();
	CHECK(peer.calls.size() == 2 && watch.count() == 2);
	watch.tick();
	CHECK(peer.calls.size() == 3 && peer.calls[2] == std::vector<std::uint32_t>{2, 3});
	peer.finish(); watch.poll(); events();
	watch.stop();
	events();
}

void memberChangeDuringRead()
{
	QObject owner;
	Peer peer;
	peer.schema = std::make_shared<members::Schema>();
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	CHECK(watch.addEntries(configEntries({0, 1})).value("added").toInt() == 2);
	CHECK(watch.begin(true));
	CHECK(peer.memberCalls.size() == 1 && peer.memberCalls[0].second == std::vector<std::uint32_t>{0, 1});
	CHECK(watch.removeMany({ux::watch_key::member(members::config, 1)}).size() == 1);
	// A value added to the struct joins its read from the next cycle on.
	CHECK(watch.addEntries(configEntries({5})).value("added").toInt() == 1);
	peer.finishMembers(); watch.poll(); events();
	CHECK(text(watch, 0) == "0" && text(watch, 1).isEmpty() && !watch.status().startsWith("Watch read failed"));
	CHECK(peer.memberCalls.size() == 2 && peer.memberCalls[1].second == std::vector<std::uint32_t>{0, 5});
	peer.finishMembers(); watch.poll(); events();
	CHECK(text(watch, 1) == "5");
	watch.stop();
	events();
}

void clearWhilePolling()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 3);
	CHECK(watch.begin(true));
	watch.clear();
	CHECK(watch.count() == 0 && watch.active());
	peer.finish(); watch.poll(); events();
	// Nothing to read: polling idles without spinning and says so.
	CHECK(watch.active() && !watch.pending() && peer.calls.size() == 1);
	CHECK(watch.status() == "Nothing to read. Add a value, or Stop.");
	watch.tick();
	events();
	CHECK(peer.calls.size() == 1);
	CHECK(watch.add(7, QStringLiteral("catalog.Field7")));
	for (int step = 0; step < 4; ++step) events();
	CHECK(peer.calls.size() == 2 && peer.calls[1] == std::vector<std::uint32_t>{7});
	peer.finish(); watch.poll(); events();
	watch.stop();
	events();
}

void urgentAfterChange()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 40);
	CHECK(watch.begin(true));
	watch.wake(1);   // in flight: would be read again before the cycle goes on
	watch.remove(39);
	peer.finish(); watch.poll(); events();
	// The change replans the cycle, which reads every row: no separate read of 1.
	CHECK(peer.calls.size() == 2 && peer.calls[1].size() == 32 && peer.calls[1][0] == 1);
	peer.finish(); watch.poll(); events();
	CHECK(peer.calls.size() == 3 && peer.calls[2].size() == 7);
	peer.finish(); watch.poll(); events();
	watch.stop();
	events();
}

void readOnceReplans()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 40);
	CHECK(watch.begin(false));
	CHECK(watch.add(60, QStringLiteral("catalog.Field60")));
	peer.finish(); watch.poll(); events();
	// Read once reads the rows shown when it finishes, the added one too.
	CHECK(peer.calls.size() == 2 && peer.calls[1][0] == 1);
	peer.finish(); watch.poll(); events();
	CHECK(peer.calls.size() == 3 && peer.calls[2].back() == 60);
	peer.finish(); watch.poll(); events();
	CHECK(!watch.active() && text(watch, 40) == "60");
}

void changeInsideCompletion()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 2);
	bool refused = false;
	watch.setCompletionObserver([&](const ux::WatchFrame&) {
		refused = !watch.add(9, QStringLiteral("catalog.Field9")) && watch.removeMany({1}).isEmpty() &&
		          watch.addEntries({entryOf(ux::watch_key::whole(8), QStringLiteral("catalog.Field8"))})
		                  .value("status") == "refused";
	});
	CHECK(watch.begin(false));
	peer.finish(); watch.poll(); events();
	CHECK(refused && watch.count() == 2);
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	typedEditors();
	exactCompletionFrames();
	bulkPublication();
	rowLimit();
	wakeAfterWrites();
	continuousLowPriority();
	busySessionTicks();
	startDuringReadOnce();
	trendCompletions();
	chunks();
	budgetsAndFallback();
	drainAndStale();
	schedulingAndReentrancy();
	replaceSourceDuringPublication();
	memberPlan();
	memberFanOut();
	memberUrgent();
	memberFailure();
	removeDuringRead();
	addDuringCycle();
	addBetweenCycles();
	memberChangeDuringRead();
	clearWhilePolling();
	urgentAfterChange();
	readOnceReplans();
	changeInsideCompletion();
	std::printf("Dynamic Watchlist checks=%u chunks=32,32,1 member_frames=10 no_native_callback_signals=1\n",
	            checks);
}
