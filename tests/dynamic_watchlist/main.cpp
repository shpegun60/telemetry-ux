// Controlled completions and ticks: no event-loop timing assumptions. MIT.
#include <Telemetry/UX/DynamicWatchlist.hpp>
#include <Telemetry/UX/TrendHistory.hpp>
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
		return source;
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
		CHECK(busy && bool(completion));
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
		watch.remove(0);
		watch.clear();
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
	watch.remove(0);
	watch.clear();
	watch.setIndividual(true);
	CHECK(watch.count() == 65 && !watch.individual());
	peer.finish();
	watch.poll();
	events();
	CHECK(!watch.active() && peer.calls.size() == 1 && text(watch, 0).isEmpty());
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
	std::printf("Dynamic Watchlist checks=%u chunks=32,32,1 no_native_callback_signals=1\n",
	            checks);
}
