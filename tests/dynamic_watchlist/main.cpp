// Controlled completions and ticks: no event-loop timing assumptions. MIT.
#include <Telemetry/UX/DynamicWatchlist.hpp>
#include <Telemetry/UX/TrendHistory.hpp>
#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtTest/QAbstractItemModelTester>
#include <QtTest/QSignalSpy>
#include <cstdio>
#include <cstdlib>
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

QString text(ux::DynamicWatchlist& watch, int row)
{
	auto* model = watch.model();
	return model->data(model->index(row, 0), ux::WatchlistModel::TextRole).toString();
}

struct Peer {
	bool current = true, busy = false, supported = true;
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
		ux::WatchlistCompletion result;
		result.status = status;
		result.count = int(pending.size());
		for (int row = 0; row < result.count; ++row) {
			result.rows[row].id = pending[row];
			result.rows[row].status = QStringLiteral("Ok");
			result.rows[row].text = pending[row] == 1   ? QStringLiteral("18446744073709551615")
			                        : pending[row] == 2 ? QStringLiteral("-9223372036854775808")
			                        : pending[row] == 3 ? QStringLiteral("-123 (unknown)")
			                                            : QString::number(pending[row]);
		}
		if (malformed)
			result.rows[0].id = 999;
		auto done = std::exchange(completion, {});
		busy = false;
		*done = std::move(result); // Deliberately no watch.poll() while callback active.
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
	watch.setInterval(0);
	CHECK(watch.interval() == 60000);
	watch.setInterval(60001);
	CHECK(watch.interval() == 60000);
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
	peer.busy = true;
	for (int i = 0; i < 100; ++i)
		watch.tick();
	CHECK(peer.calls.size() == 1 && !watch.pending());
	peer.busy = false;
	blocked = true;
	watch.tick();
	CHECK(peer.calls.size() == 1);
	blocked = false;
	watch.tick();
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
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bulkPublication();
	trendCompletions();
	chunks();
	budgetsAndFallback();
	drainAndStale();
	schedulingAndReentrancy();
	replaceSourceDuringPublication();
	std::printf("Dynamic Watchlist checks=%u chunks=32,32,1 no_native_callback_signals=1\n",
	            checks);
}
