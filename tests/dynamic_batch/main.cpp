// Deterministic presentation fixture. It does not claim native execution. MIT.
#include <Telemetry/UX/DynamicBatch.hpp>
#include <Telemetry/UX/RequestArguments.hpp>
#include "../dynamic_qml_configurator/Fixture.hpp"
#include <QtTest/QAbstractItemModelTester>
#include <QtTest/QSignalSpy>
#include <utility>

namespace ux = telemetry::ux;
namespace cl = telemetry::client;
namespace f = dynamic_qml_fixture;

namespace {
struct Peer {
	bool current = true, busy = false, supported = true, permitted = true, blocked = false;
	bool otherBusy = false; // another panel's request (a polling Watchlist) in the session
	cl::Status refusal = cl::Status::Ok;
	quint32 session = 17;
	unsigned payloadBytes = 8;
	ux::BatchSource::Limits limits{32, 4096, 65536};
	std::shared_ptr<f::Backing> storage = std::make_shared<f::Backing>();
	ux::BatchSource::Completion completion;
	std::vector<ux::BatchRow> submitted;
	unsigned calls = 0, notifications = 0, finishes = 0;

	ux::BatchSource source()
	{
		ux::BatchSource source;
		source.storage = storage;
		source.current = [this] {
			return current;
		};
		source.busy = [this] {
			return busy || otherBusy;
		};
		source.session = [this] {
			return session;
		};
		source.schema = [this] {
			return storage->schema;
		};
		source.capability = [this]() -> std::optional<ux::BatchSource::Limits> {
			return supported ? std::optional{limits} : std::nullopt;
		};
		source.submit = [this](std::span<const ux::BatchSource::Request> requests,
		                       ux::BatchSource::Completion done) {
			++calls;
			CHECK(current && !busy && !completion && !requests.empty());
			if (refusal != cl::Status::Ok)
				return cl::Submission{refusal, {}};
			submitted.clear();
			for (const auto& request : requests) {
				ux::BatchRow row;
				row.id = request.id;
				row.operation = request.operation;
				row.request = QByteArray(reinterpret_cast<const char*>(request.payload.data()),
				                         qsizetype(request.payload.size()));
				submitted.push_back(std::move(row));
			}
			completion = std::move(done);
			busy = true;
			return cl::Submission{cl::Status::Ok, {}};
		};
		return source;
	}

	void attach(ux::DynamicBatch& batch)
	{
		batch.setNotify(
		    [this] {
			    ++notifications;
		    },
		    [this] {
			    return blocked;
		    },
		    [this] {
			    return permitted;
		    },
		    [this] {
			    ++finishes;
		    });
		batch.setSource(source());
	}

	void finish(cl::Status status = cl::Status::Ok, bool values = true)
	{
		CHECK(completion && busy);
		ux::BatchCompletion result;
		result.status = status;
		result.succeeded = status == cl::Status::Ok;
		result.valueStatus = values ? QStringLiteral("Ok") : QStringLiteral("InvalidPayload");
		result.count = result.succeeded ? int(submitted.size()) : 0;
		for (int i = 0; i < result.count; ++i) {
			auto& row = result.rows[std::size_t(i)];
			const auto& input = submitted[std::size_t(i)];
			row.id = input.id;
			row.operation = input.operation;
			const bool accepted = input.operation == cl::Operation::Command;
			row.result = {{QStringLiteral("status"), unsigned(cl::Status::Ok)},
			              {QStringLiteral("dispatch"), 0},
			              {QStringLiteral("endpointStatus"), accepted ? 1 : 0},
			              {QStringLiteral("succeeded"), true},
			              {QStringLiteral("accepted"), accepted},
			              {QStringLiteral("executed"), false},
			              {QStringLiteral("session"), session},
			              {QStringLiteral("valueStatus"),
			               values ? QStringLiteral("Ok") : QStringLiteral("InvalidPayload")}};
			row.hasValue = values && input.operation == cl::Operation::FieldRead;
			if (row.hasValue)
				row.payload = QByteArray(payloadBytes, payloadBytes == 8 ? char(0xff) : char(0));
		}
		auto done = std::exchange(completion, {});
		busy = false;
		*done = std::move(result);
	}
};

ux::BatchRow readRow(Peer& peer, quint32 id = 1)
{
	const auto endpoint = peer.storage->owner.bindField(id);
	CHECK(endpoint);
	ux::BatchRow row;
	row.operation = cl::Operation::FieldRead;
	row.id = id;
	row.session = peer.session;
	row.name = QStringLiteral("device.Serial");
	row.responseType = endpoint.endpoint.responseType();
	row.responseBytes = endpoint.endpoint.responseBytes();
	return row;
}

QVariant role(ux::DynamicBatch& batch, int row, int role)
{
	return batch.model()->data(batch.model()->index(row, 0), role);
}

void orderedOwnedResults()
{
	Peer peer;
	ux::DynamicBatch batch;
	peer.attach(batch);
	QAbstractItemModelTester tester(batch.model(),
	                                QAbstractItemModelTester::FailureReportingMode::Fatal);
	auto row = readRow(peer);
	CHECK(batch.supported() && batch.add(row) && batch.add(row));
	row.operation = cl::Operation::Command;
	row.responseBytes = 0;
	row.responseType = 0;
	char draft[] = "abc";
	row.request = QByteArray::fromRawData(draft, 3);
	CHECK(batch.add(row));
	draft[0] = 'z';
	CHECK(batch.move(2, -1) && batch.move(1, 1));
	CHECK(!batch.move(0, -1) && !batch.move(0, 2) && !batch.remove(9));
	CHECK(batch.count() == 3 && peer.calls == 0);
	CHECK(batch.canExecute() && batch.execute());
	CHECK(peer.calls == 1 && peer.submitted[2].request == QByteArray("abc"));
	CHECK(peer.submitted[0].id == peer.submitted[1].id);
	CHECK(batch.pending() && !batch.add(row) && !batch.remove(0) && !batch.move(1, -1) &&
	      !batch.clear() && !batch.select(0));
	QSignalSpy resets(batch.model(), &QAbstractItemModel::modelReset);
	const auto before = peer.notifications;
	peer.finish();
	CHECK(batch.pending() && resets.count() == 0 && peer.notifications == before &&
	      peer.finishes == 0);
	batch.poll();
	CHECK(!batch.pending() && peer.finishes == 1 && resets.count() == 1);
	CHECK(role(batch, 2, ux::BatchListModel::StatusRole)
	          .toString()
	          .contains(QStringLiteral("Accepted")));
	CHECK(!role(batch, 2, ux::BatchListModel::ResultRole)
	           .toMap()
	           .value(QStringLiteral("executed"))
	           .toBool());
	CHECK(batch.select(0) && batch.responses()->rowCount() == 1);
	CHECK(batch.responses()
	          ->data(batch.responses()->index(0, 0), ux::ValueModel::ValueRole)
	          .toString() == QStringLiteral("18446744073709551615"));
	CHECK(!batch.responses()
	           ->data(batch.responses()->index(0, 0), ux::ValueModel::EditableRole)
	           .toBool());
	const auto facts = role(batch, 2, ux::BatchListModel::ResultRole).toMap();
	peer.refusal = cl::Status::QueueFull;
	CHECK(!batch.execute() && !batch.pending() && batch.count() == 3);
	CHECK(role(batch, 2, ux::BatchListModel::ResultRole).toMap() == facts);
	CHECK(role(batch, 0, ux::BatchListModel::HasValueRole).toBool());
	CHECK(batch.status().contains(QStringLiteral("QueueFull")));
	peer.refusal = cl::Status::Ok;
	peer.permitted = false;
	CHECK(!batch.canExecute() && !batch.execute() && peer.calls == 2);
	CHECK(batch.remove(2) && batch.canExecute()); // A read-only Batch needs no mutation permission.
	CHECK(batch.execute());
	peer.finish(cl::Status::Ok, false);
	batch.poll();
	CHECK(!role(batch, 0, ux::BatchListModel::HasValueRole).toBool());
	CHECK(!role(batch, 0, ux::BatchListModel::ResultRole).toMap().isEmpty());
	CHECK(batch.responses()->rowCount() == 0);
}

void boundedAdmissionAndHistory()
{
	Peer peer;
	ux::DynamicBatch batch;
	peer.attach(batch);
	const auto row = readRow(peer);
	peer.limits = {32, 18, 42}; // Exactly two repeated U64 reads.
	CHECK(batch.add(row) && batch.add(row) && !batch.add(row) && batch.count() == 2 &&
	      peer.calls == 0);
	CHECK(batch.clear());
	peer.limits = {32, 4096, 65536};
	for (unsigned i = 0; i < 32; ++i)
		CHECK(batch.add(row));
	CHECK(!batch.add(row) && batch.count() == 32);
	CHECK(batch.execute());
	peer.current = false;
	batch.retire();
	CHECK(batch.pending() && !batch.supported() && !batch.clear());
	peer.finish(cl::Status::OutcomeUnknown);
	batch.poll();
	CHECK(!batch.pending() && batch.status().contains(QStringLiteral("OutcomeUnknown")) &&
	      peer.calls == 1);
	for (int i = 0; i < batch.count(); ++i) {
		CHECK(role(batch, i, ux::BatchListModel::ResultRole).toMap().isEmpty());
		CHECK(role(batch, i, ux::BatchListModel::HistoricalRole).toBool());
		CHECK(!role(batch, i, ux::BatchListModel::HasValueRole).toBool());
	}
	peer.current = true;
	peer.session = 18;
	batch.setSource(peer.source());
	CHECK(!batch.canExecute() && !batch.execute() &&
	      peer.calls == 1); // Never replay previous-session drafts.
	CHECK(batch.clear() && batch.count() == 0);
	CHECK(batch.add(readRow(peer)));
	CHECK(batch.execute());
	peer.finish();
	peer.current = false;
	batch.retire();
	batch.poll();
	CHECK(!role(batch, 0, ux::BatchListModel::HasValueRole).toBool());
	CHECK(role(batch, 0, ux::BatchListModel::ResultRole)
	          .toMap()
	          .value(QStringLiteral("succeeded"))
	          .toBool());
	CHECK(batch.responses()->rowCount() == 0);
}

void reentrantDetach()
{
	Peer peer;
	auto batch = std::make_unique<ux::DynamicBatch>();
	peer.attach(*batch);
	CHECK(batch->add(readRow(peer)));
	CHECK(batch->execute());
	peer.finish();
	QObject context;
	QObject::connect(batch->model(), &QAbstractItemModel::modelAboutToBeReset, &context, [&] {
		batch.reset();
	});
	batch->poll(); // The current call retains its models until reset notification returns.
	CHECK(!batch);
	CHECK(peer.finishes == 1);
	ux::DynamicBatch detached;
	peer.attach(detached);
	CHECK(detached.add(readRow(peer)));
	CHECK(detached.execute());
	detached.retire();
	peer.finish();
	detached.poll();
	CHECK(!role(detached, 0, ux::BatchListModel::HasValueRole).toBool());
}

void reentrantInvalidation()
{
	for (bool retire : {false, true}) {
		Peer peer;
		ux::DynamicBatch batch;
		peer.attach(batch);
		CHECK(batch.add(readRow(peer)) && batch.execute());
		peer.finish();
		batch.poll();
		CHECK(role(batch, 0, ux::BatchListModel::HasValueRole).toBool());
		QObject context;
		bool invalidated = false;
		QObject::connect(batch.responses(), &QAbstractItemModel::modelAboutToBeReset, &context,
		                 [&] {
			                 if (!std::exchange(invalidated, true)) {
				                 if (retire)
					                 batch.retire();
				                 else
					                 batch.invalidateValues();
			                 }
		                 });
		CHECK(batch.select(0));
		CHECK(invalidated && !role(batch, 0, ux::BatchListModel::HasValueRole).toBool());
		CHECK(batch.responses()->rowCount() == 0);
		CHECK(role(batch, 0, ux::BatchListModel::HistoricalRole).toBool() == retire);
		CHECK(!role(batch, 0, ux::BatchListModel::ResultRole).toMap().isEmpty());
	}
}

void externalModelInvalidation()
{
	Peer peer;
	ux::DynamicBatch batch;
	peer.attach(batch);
	const auto row = readRow(peer, 0);
	peer.payloadBytes = row.responseBytes;
	CHECK(batch.add(row) && batch.execute());
	peer.finish();
	batch.poll();
	auto* values = batch.responses();
	const auto root = values->index(0, 0);
	CHECK(root.isValid() && values->canFetchMore(root));
	QObject context;
	bool invalidated = false;
	QObject::connect(values, &QAbstractItemModel::rowsAboutToBeInserted, &context, [&] {
		if (!std::exchange(invalidated, true))
			batch.invalidateValues();
		CHECK(!role(batch, 0, ux::BatchListModel::HasValueRole).toBool());
	});
	values->fetchMore(root);
	f::events();
	CHECK(invalidated && values->rowCount() == 0);
	CHECK(!role(batch, 0, ux::BatchListModel::HasValueRole).toBool());
	CHECK(!role(batch, 0, ux::BatchListModel::ResultRole).toMap().isEmpty());
}
} // namespace

// A request of another panel in the session (a continuous Watchlist keeps one
// almost always) blocks neither editing the Batch nor running it: the session's
// FIFO orders them.
void busySessionKeepsBatch()
{
	Peer peer;
	ux::DynamicBatch batch;
	peer.attach(batch);
	peer.otherBusy = true;
	auto row = readRow(peer);
	CHECK(batch.add(row) && batch.add(row) && batch.move(1, -1) && batch.remove(1));
	CHECK(batch.canExecute() && batch.execute() && peer.calls == 1);
	peer.finish();
	batch.poll();
	CHECK(!batch.pending());
}

// Several rows at once (BA-1): all in one publication or none, and why not.
void severalRows()
{
	Peer peer;
	ux::DynamicBatch batch;
	peer.attach(batch);
	const auto row = readRow(peer);
	QSignalSpy resets(batch.model(), &QAbstractItemModel::modelReset);
	CHECK(batch.refusal(std::vector<ux::BatchRow>{row, row}) == ux::DynamicBatch::Refusal::None);
	CHECK(batch.add(std::vector<ux::BatchRow>{row, row}) && batch.count() == 2 && batch.selection() == 1);
	CHECK(resets.count() == 1);  // one change for both
	peer.limits = {32, 18, 42}; // Two repeated U64 reads fit; a third does not.
	CHECK(batch.refusal({&row, 1}) == ux::DynamicBatch::Refusal::Bytes && !batch.add(std::vector<ux::BatchRow>{row}) &&
	      batch.count() == 2 && !batch.canAdd(row));
	CHECK(batch.clear());
	peer.limits = {2, 4096, 65536};
	const std::vector<ux::BatchRow> three{row, row, row};
	CHECK(batch.refusal(three) == ux::DynamicBatch::Refusal::Items && !batch.add(three) && batch.count() == 0 &&
	      batch.itemLimit() == 2);
	auto stranger = row;
	stranger.session = peer.session + 1;
	const std::vector<ux::BatchRow> mixed{row, stranger};
	CHECK(batch.refusal(mixed) == ux::DynamicBatch::Refusal::Session);
	CHECK(batch.add(std::vector<ux::BatchRow>{row}) && batch.execute());
	CHECK(batch.refusal({&row, 1}) == ux::DynamicBatch::Refusal::Busy && !batch.add(std::vector<ux::BatchRow>{row}));
	peer.finish();
	batch.poll();
	CHECK(batch.refusal({}) == ux::DynamicBatch::Refusal::None && !batch.add(std::vector<ux::BatchRow>{}));
	// Items of an earlier connection: nothing joins them until they are cleared.
	peer.session += 1;
	batch.setSource(peer.source());
	const auto fresh = readRow(peer);
	CHECK(batch.count() == 1 && batch.refusal({&fresh, 1}) == ux::DynamicBatch::Refusal::Previous && !batch.canAdd(fresh));
	CHECK(batch.clear() && batch.canAdd(fresh));
}

// A Command's or Service's values typed as name=value (BA-2): every argument
// checked as Details would, its words, and the request's exact bytes. Config
// stands in for a request with many kinds of value; Inspect's Request has one.
void requestArguments()
{
	namespace ra = ux::request_arguments;
	using Status = ra::Item::Status;
	f::Backing backing;
	const auto& schema = backing.schema;
	const auto* config = schema.findField("device.Config");
	const auto* inspect = schema.findService("device.Inspect");
	CHECK(config && inspect);
	const auto bytesOf = [](const ra::Result& result) { return result.request.toHex(); };
	// The inputs, in order, with their types and choices.
	const auto inputs = ra::arguments(schema, config->typeId);
	CHECK(inputs.size() == 7 && inputs[0].name == "serial" && inputs[0].typeText == "u64" && inputs[0].scalar);
	CHECK(inputs[5].name == "mode" && inputs[5].typeText == "enum" && inputs[5].choices == QStringList({"Reverse", "Forward"}) &&
	      inputs[5].codes == QList<qint64>({-7, 12}));
	CHECK(inputs[6].name == "samples" && !inputs[6].scalar && inputs[6].typeText == "u16[300]");
	CHECK(ra::signatureText(schema, inspect->typeId) == "(multiplier: u32)");
	CHECK(ra::signatureText(schema, config->typeId) == "(serial: u64, offset: i64, gain: f32, precise: f64, …)");
	// The tail: names and values, spaces around = allowed.
	const auto given = ra::parse(u"a=1 b = 2  c= 3 d =4 5");
	CHECK(given.size() == 5 && given[0].name == "a" && given[0].text == "1" && given[1].name == "b" && given[1].text == "2" &&
	      given[2].name == "c" && given[2].text == "3" && given[3].name == "d" && given[3].text == "4" &&
	      given[4].name.isEmpty() && given[4].text == "5");
	// One argument: named, by its name in any case, or bare.
	for (const auto* tail : {u"multiplier=7", u"MULTIPLIER = 7", u"7", u"0x7"}) {
		const auto one = ra::check(schema, inspect->typeId, "device.Inspect", tail);
		CHECK(one.ok() && bytesOf(one) == "07000000" && one.items[0].canonical == "7");
	}
	// Nothing given: what it needs; a value that is not one; a name it has not.
	auto result = ra::check(schema, inspect->typeId, "device.Inspect", u"");
	CHECK(!result.ok() && result.items[0].status == Status::Missing &&
	      result.message == "device.Inspect needs multiplier (u32). Enter writes multiplier=; Ctrl+Enter fills it in Details.");
	result = ra::check(schema, inspect->typeId, "device.Inspect", u"multiplier=-1");
	CHECK(!result.ok() && result.items[0].status == Status::Error &&
	      result.message == "multiplier (u32) takes 0 to 4294967295 (0x00000000 to 0xFFFFFFFF), not \"-1\".");
	result = ra::check(schema, inspect->typeId, "device.Inspect", u"multiplier=");
	CHECK(result.message == "multiplier (u32) takes 0 to 4294967295 (0x00000000 to 0xFFFFFFFF).");
	result = ra::check(schema, inspect->typeId, "device.Inspect", u"multiplir=2");
	CHECK(!result.ok() && result.message == "device.Inspect has no argument multiplir. Did you mean multiplier?");
	result = ra::check(schema, inspect->typeId, "device.Inspect", u"size=2");
	CHECK(result.message == "device.Inspect has no argument size; it takes multiplier.");
	result = ra::check(schema, inspect->typeId, "device.Inspect", u"multiplier=2 multiplier=3");
	CHECK(result.message == "multiplier is given twice; keep one.");
	// Several values with no names: never paired by order behind the user's back.
	const auto gain = schema.findField("device.Config");
	CHECK(gain);
	result = ra::check(schema, config->typeId, "device.Config", u"1 2");
	CHECK(result.unnamed && !result.ok() && result.named.startsWith("device.Config serial=1 offset=2 gain="));
	// Many arguments: what is still missing; an enum by name or code; a
	// struct's array argument goes to Details.
	result = ra::check(schema, config->typeId, "device.Config", u"serial=1 offset=-2");
	CHECK(result.message == "device.Config needs gain (f32), precise (f64), limit (u32) and 1 more too. Enter writes gain=." ||
	      result.aggregate);
	CHECK(result.items[0].status == Status::Ok && result.items[1].status == Status::Ok && result.items[1].canonical == "-2");
	result = ra::check(schema, config->typeId, "device.Config", u"mode=forward");
	CHECK(result.items[5].status == Status::Ok && result.items[5].canonical == "Forward");
	result = ra::check(schema, config->typeId, "device.Config", u"mode=-7");
	CHECK(result.items[5].status == Status::Ok && result.items[5].canonical == "Reverse");
	result = ra::check(schema, config->typeId, "device.Config", u"mode=Fast");
	CHECK(result.items[5].status == Status::Error &&
	      result.message == "mode (enum) takes Reverse or Forward, or a code, not \"Fast\".");
	result = ra::check(schema, config->typeId, "device.Config",
	                   u"serial=1 offset=-2 gain=1.5 precise=0.25 limit=0x10 mode=Forward");
	CHECK(result.aggregate && !result.ok() &&
	      result.message == "device.Config takes samples (u16[300]), which is filled in Details. Enter selects it there "
	                        "with the values typed here; then Add selected draft.");
	// A request that takes no input.
	const auto* ping = schema.findService("device.Ping");
	CHECK(ping);
	result = ra::check(schema, ping->typeId, "device.Ping", u"");
	CHECK(result.ok() && result.request.isEmpty());
	result = ra::check(schema, ping->typeId, "device.Ping", u"now");
	CHECK(!result.ok() && result.message == "device.Ping takes no input; remove \"now\".");
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	requestArguments();
	orderedOwnedResults();
	boundedAdmissionAndHistory();
	reentrantDetach();
	reentrantInvalidation();
	externalModelInvalidation();
	busySessionKeepsBatch();
	severalRows();
	std::printf("Dynamic Batch checks=%u no_native_callback_signals=1 bounded_items=32\n",
	            f::checks);
}
