// Deterministic presentation fixture. It does not claim native execution. MIT.
#include <Telemetry/UX/DynamicBatch.hpp>
#include <Telemetry/UX/RequestArguments.hpp>
#include "../dynamic_qml_configurator/Fixture.hpp"
#include "../helpers/UndoKinds.hpp"
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
	// The status line in words (BA-4c).
	CHECK(batch.status() == QStringLiteral("Sending 3 items in one request…") && !batch.attention());
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
	// "Executed 14:02:11 · 3 of 3 done", or the first item not done and why.
	CHECK(batch.status().startsWith(QStringLiteral("Executed ")) && batch.status().contains(QStringLiteral(" of 3 done")) &&
	      batch.attention() == !batch.status().endsWith(QStringLiteral(" \u00b7 3 of 3 done")));
	CHECK(!batch.status().contains(QStringLiteral("Executed at")) && !batch.status().endsWith(QLatin1Char('.')));
	// The Outcome column in words: a read "Read", an accepted Command
	// "Accepted, not confirmed".
	CHECK(role(batch, 0, ux::BatchListModel::OutcomeRole).toString() == QStringLiteral("Read"));
	CHECK(role(batch, 2, ux::BatchListModel::OutcomeRole).toString() == QStringLiteral("Accepted, not confirmed"));
	CHECK(role(batch, 2, ux::BatchListModel::StatusRole)
	          .toString()
	          .contains(QStringLiteral("Accepted")));
	CHECK(!role(batch, 2, ux::BatchListModel::ResultRole)
	           .toMap()
	           .value(QStringLiteral("executed"))
	           .toBool());
	// A read's value is the item's values (BA-4a); it has no response.
	CHECK(batch.select(0) && batch.values()->rowCount() == 1 && batch.responses()->rowCount() == 0);
	CHECK(batch.values()
	          ->data(batch.values()->index(0, 0), ux::ValueModel::ValueRole)
	          .toString() == QStringLiteral("18446744073709551615"));
	CHECK(!batch.values()
	           ->data(batch.values()->index(0, 0), ux::ValueModel::EditableRole)
	           .toBool());
	const auto facts = role(batch, 2, ux::BatchListModel::ResultRole).toMap();
	peer.refusal = cl::Status::QueueFull;
	CHECK(!batch.execute() && !batch.pending() && batch.count() == 3);
	CHECK(role(batch, 2, ux::BatchListModel::ResultRole).toMap() == facts);
	CHECK(role(batch, 0, ux::BatchListModel::HasValueRole).toBool());
	CHECK(batch.status() == QStringLiteral("The device was busy and ran nothing. Execute the Batch again in a moment.") &&
	      batch.attention());
	peer.refusal = cl::Status::Ok;
	peer.permitted = false;
	CHECK(!batch.canExecute() && !batch.execute() && peer.calls == 2);
	CHECK(batch.remove(2) && batch.canExecute()); // A read-only Batch needs no mutation permission.
	CHECK(batch.execute());
	peer.finish(cl::Status::Ok, false);
	batch.poll();
	CHECK(!role(batch, 0, ux::BatchListModel::HasValueRole).toBool());
	CHECK(!role(batch, 0, ux::BatchListModel::ResultRole).toMap().isEmpty());
	CHECK(batch.values()->rowCount() == 0);
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
	CHECK(!batch.pending() &&
	      batch.status() == QStringLiteral("The reply was lost: the items may have run. Check the device before executing again.") &&
	      batch.attention() && peer.calls == 1);
	CHECK(role(batch, 0, ux::BatchListModel::StatusRole).toString() == QStringLiteral("Outcome unknown"));
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
	CHECK(batch.values()->rowCount() == 0);
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
		QObject::connect(batch.values(), &QAbstractItemModel::modelAboutToBeReset, &context,
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
		CHECK(batch.values()->rowCount() == 0);
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
	auto* values = batch.values();
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

// A Command's or Service's request as its arguments, for the "+" row's
// right column. Config stands in for a request with many kinds of value;
// Inspect's Request has one. The typed values (BA-2) are gone: values are set
// on the item (BA-4b).
void requestArguments()
{
	namespace ra = ux::request_arguments;
	f::Backing backing;
	const auto& schema = backing.schema;
	const auto* config = schema.findField("device.Config");
	const auto* inspect = schema.findService("device.Inspect");
	CHECK(config && inspect);
	// The inputs, in order, with their types and choices.
	const auto inputs = ra::arguments(schema, config->typeId);
	CHECK(inputs.size() == 7 && inputs[0].name == "serial" && inputs[0].typeText == "u64" && inputs[0].scalar);
	CHECK(inputs[5].name == "mode" && inputs[5].typeText == "enum" && inputs[5].choices == QStringList({"Reverse", "Forward"}) &&
	      inputs[5].codes == QList<qint64>({-7, 12}));
	CHECK(inputs[6].name == "samples" && !inputs[6].scalar && inputs[6].typeText == "u16[300]");
	CHECK(ra::signatureText(schema, inspect->typeId) == "(multiplier: u32)");
	CHECK(ra::signatureText(schema, config->typeId) == "(serial: u64, offset: i64, gain: f32, precise: f64, …)");
	CHECK(ra::valueType(schema, inputs[5].type).enumeration && !ra::valueType(schema, inputs[6].type).valid());
	// A request that takes no input has no arguments.
	const auto* ping = schema.findService("device.Ping");
	CHECK(ping && ra::signatureText(schema, ping->typeId) == "()");
}

// The Batch's steps (UR-1): add, remove, move and Clear held to UndoKinds'
// property - done, undone and redone the same, across a disconnect and a
// device with another session - rows by uid with their places; rows put
// back with no device, or after a reconnect, come back historical; moves of
// one item within two seconds are one step; Execute is never a step, and a
// step waits while a Batch is in flight.
// A Service item of device.Inspect, its request at its initial values.
ux::BatchRow serviceRow(Peer& peer)
{
	const auto* inspect = peer.storage->schema.findService("device.Inspect");
	CHECK(inspect);
	const auto bound = peer.storage->owner.bindService(inspect->id);
	CHECK(bound);
	ux::BatchRow row;
	row.operation = cl::Operation::Service;
	row.id = inspect->id;
	row.session = peer.session;
	row.name = QStringLiteral("device.Inspect");
	row.requestType = bound.endpoint.requestType();
	row.responseType = bound.endpoint.responseType();
	row.responseBytes = bound.endpoint.responseBytes();
	row.request = QByteArray(qsizetype(bound.endpoint.requestBytes()), '\0');
	return row;
}

// The selected item's value of that name, its values loaded.
QModelIndex itemValue(ux::DynamicBatch& batch, const QString& name)
{
	auto* values = batch.values();
	const auto root = values->index(0, 0);
	if (values->canFetchMore(root))
		values->fetchMore(root);
	for (int at = 0; at < values->rowCount(root); ++at) {
		const auto index = values->index(at, 0, root);
		if (values->data(index, ux::ValueModel::NameRole).toString() == name)
			return index;
	}
	return {};
}

// A value typed and then typed back to what it was when its cell was entered
// leaves no step; a real change stays one step, and the next cell visit that
// ends where it began adds none either.
void itemValueBackToStart()
{
	Peer peer;
	ux::DynamicBatch batch;
	peer.attach(batch);
	ux::UndoHistory history;
	qint64 now = 0;
	history.setClock([&now] { return now; });
	batch.setHistory(&history);
	CHECK(batch.add(serviceRow(peer)));
	const auto multiplier = itemValue(batch, QStringLiteral("multiplier"));
	CHECK(multiplier.isValid() && batch.canEdit());
	const auto base = history.count();
	CHECK(batch.setScalarText(multiplier, QStringLiteral("7")) && history.count() == base + 1);
	CHECK(batch.setScalarText(multiplier, QStringLiteral("0")) && history.count() == base);
	batch.closeValue();
	CHECK(history.count() == base);
	CHECK(batch.setScalarText(multiplier, QStringLiteral("5")) && history.count() == base + 1);
	batch.closeValue();
	// Entered at 5: 9 then 5 again is no step; 9 then 6 is one.
	CHECK(batch.setScalarText(multiplier, QStringLiteral("9")) && history.count() == base + 2);
	CHECK(batch.setScalarText(multiplier, QStringLiteral("5")) && history.count() == base + 1);
	CHECK(batch.setScalarText(multiplier, QStringLiteral("6")) && history.count() == base + 2);
	batch.closeValue();
	CHECK(history.undo() && history.count() == base + 2);
}

// BA-4a: the selected item as Details shows it - its request's values,
// edited live into the item as one step per value, the line from its bytes,
// what Execute sent and what changed since, and Read | Write as one step.
void itemValues()
{
	Peer peer;
	ux::DynamicBatch batch;
	peer.attach(batch);
	ux::UndoHistory history;
	qint64 now = 0;
	history.setClock([&now] { return now; });
	batch.setHistory(&history);
	const auto line = [&batch](int row) { return role(batch, row, ux::BatchListModel::LineRole).toString(); };
	// The status line is never empty: what to do with an empty Batch, then
	// that it has not run.
	CHECK(batch.status() == QStringLiteral("Add Fields, Commands and Services; Execute Batch sends them in one request, in order."));
	CHECK(batch.add(serviceRow(peer)));
	CHECK(batch.status() == QStringLiteral("Not executed yet"));
	// Before its run the item's outcome is "–", and its response shows its
	// whole structure with "–" in every value.
	CHECK(role(batch, 0, ux::BatchListModel::OutcomeRole).toString() == QStringLiteral("\u2013"));
	CHECK(batch.responses()->property("shape").toBool() && batch.responses()->rowCount() == 1);
	{
		auto* responses = batch.responses();
		const auto root = responses->index(0, 0);
		CHECK(responses->rowCount(root) == 7);
		for (int at = 0; at < responses->rowCount(root); ++at) {
			const auto index = responses->index(at, 0, root);
			if (!responses->data(index, ux::ValueModel::AggregateRole).toBool())
				CHECK(responses->data(index, ux::ValueModel::ValueRole).toString() == QStringLiteral("\u2013"));
		}
	}
	// A new item is shown at once, its line made from its bytes.
	CHECK(batch.selection() == 0 && batch.selectedRow() && line(0) == QStringLiteral("multiplier=0"));
	auto multiplier = itemValue(batch, QStringLiteral("multiplier"));
	CHECK(multiplier.isValid() && batch.canEdit() &&
	      batch.values()->data(multiplier, ux::ValueModel::EditableRole).toBool());
	// Live: a value is in the item as it is typed; its keystrokes are one step.
	CHECK(batch.setScalarText(multiplier, QStringLiteral("7")) && line(0) == QStringLiteral("multiplier=7"));
	CHECK(history.undoText() == QStringLiteral("Set multiplier of item 1"));
	const auto steps = history.count();
	CHECK(batch.setScalarText(multiplier, QStringLiteral("70")) && history.count() == steps);
	CHECK(batch.values()->data(multiplier, ux::ValueModel::EditorTextRole).toString() == QStringLiteral("70"));
	// Text that is no value leaves the item as it was.
	CHECK(!batch.setScalarText(multiplier, QStringLiteral("abc")) && line(0) == QStringLiteral("multiplier=70"));
	// The cell left, the next change is a step of its own.
	batch.closeValue();
	CHECK(batch.setScalarText(multiplier, QStringLiteral("71")) && history.count() == steps + 1);
	CHECK(history.undo() && line(0) == QStringLiteral("multiplier=70"));
	CHECK(history.undo() && line(0) == QStringLiteral("multiplier=0") && batch.selection() == 0);
	CHECK(history.redo() && line(0) == QStringLiteral("multiplier=70"));
	// Bytes of another shape are refused; the right ones go in whole.
	CHECK(!batch.setRequest(0, QByteArray("\x01", 1)) && line(0) == QStringLiteral("multiplier=70"));
	CHECK(batch.setRequest(0, QByteArray::fromHex("05000000")) && line(0) == QStringLiteral("multiplier=5"));
	// Before a run nothing is "changed since".
	CHECK(!role(batch, 0, ux::BatchListModel::ChangedRole).toBool());
	// Execute sends each item as it stands, and keeps what it sent.
	CHECK(batch.execute() && peer.submitted.size() == 1 && peer.submitted[0].request.toHex() == "05000000");
	CHECK(!batch.canEdit());
	// While it runs the item's tables stay as they were, taking no text.
	CHECK(batch.values()->property("shape").toBool() && batch.responses()->property("shape").toBool());
	multiplier = itemValue(batch, QStringLiteral("multiplier"));
	CHECK(batch.values()->data(multiplier, ux::ValueModel::ValueRole).toString() == QStringLiteral("5"));
	CHECK(!batch.values()->data(multiplier, ux::ValueModel::EditableRole).toBool());
	CHECK(!batch.setScalarText(multiplier, QStringLiteral("6")) && !batch.setRequest(0, QByteArray::fromHex("06000000")));
	peer.finish();
	batch.poll();
	f::events();
	CHECK(!role(batch, 0, ux::BatchListModel::ChangedRole).toBool() &&
	      !role(batch, 0, ux::BatchListModel::AtRole).toString().isEmpty());
	CHECK(role(batch, 0, ux::BatchListModel::OutcomeRole).toString() == QStringLiteral("Done"));
	// This fixture's Service returns no value: its response keeps its
	// structure, "–" in every value.
	CHECK(batch.status().endsWith(QStringLiteral(" \u00b7 1 of 1 done")) && batch.responses()->property("shape").toBool());
	// A change after the run: changed since, and the value is marked unsent.
	CHECK(batch.select(0));
	multiplier = itemValue(batch, QStringLiteral("multiplier"));
	CHECK(batch.setScalarText(multiplier, QStringLiteral("6")));
	CHECK(role(batch, 0, ux::BatchListModel::ChangedRole).toBool() &&
	      batch.values()->data(multiplier, ux::ValueModel::UnsentRole).toBool());
	// The outcome says the values changed since the run; a structure with
	// no values has nothing to go stale.
	CHECK(role(batch, 0, ux::BatchListModel::OutcomeRole).toString() == QStringLiteral("Done, changed since"));
	CHECK(!batch.responses()->property("stale").toBool());
	CHECK(batch.setScalarText(multiplier, QStringLiteral("5")) && !role(batch, 0, ux::BatchListModel::ChangedRole).toBool() &&
	      !batch.values()->data(multiplier, ux::ValueModel::UnsentRole).toBool());
	// A read made a write and back: one step each, the same uid, undone whole.
	CHECK(batch.add(readRow(peer)) && batch.selection() == 1 && line(1).isEmpty());
	const auto uid = batch.model()->rows()[1].uid;
	auto write = readRow(peer);
	write.operation = cl::Operation::FieldWrite;
	write.requestType = write.responseType;
	write.request = QByteArray(qsizetype(write.responseBytes), '\0');
	CHECK(batch.replace(1, write, QStringLiteral("Make item 2 a write")));
	CHECK(batch.model()->rows()[1].uid == uid && batch.model()->rows()[1].operation == cl::Operation::FieldWrite &&
	      history.undoText() == QStringLiteral("Make item 2 a write") && batch.canEdit());
	CHECK(history.undo() && batch.model()->rows()[1].operation == cl::Operation::FieldRead &&
	      batch.model()->rows()[1].uid == uid);
	CHECK(history.redo() && batch.model()->rows()[1].operation == cl::Operation::FieldWrite);
	// Items of an earlier connection keep their values: nothing to edit.
	++peer.session;
	batch.setSource(peer.source());
	CHECK(batch.select(0) && !batch.canEdit() && batch.values()->rowCount() == 0 &&
	      !batch.setRequest(0, QByteArray::fromHex("01000000")));
}

void batchUndoKinds()
{
	Peer peer;
	ux::DynamicBatch batch;
	peer.attach(batch);
	ux::UndoHistory history;
	qint64 now = 0;
	history.setClock([&now] { return now; });
	batch.setHistory(&history);
	CHECK(batch.history() == &history);
	const auto rows = [&batch] {
		QStringList all;
		for (const auto& row : batch.model()->rows())
			all << QStringLiteral("%1:%2:%3").arg(row.uid).arg(row.name).arg(int(row.operation));
		return QVariant(all);
	};
	const auto named = [&peer](const char* name) {
		auto row = readRow(peer);
		row.name = QString::fromLatin1(name);
		return row;
	};
	const auto away = [&] {
		peer.current = false;
		batch.retire();
	};
	// A reconnect is a new session, as an admission makes one.
	const auto back = [&] {
		peer.current = true;
		++peer.session;
		batch.setSource(peer.source());
	};
	const auto start = [&] {
		if (!peer.current)
			back();
		CHECK(batch.clear());
		CHECK(batch.add(std::vector<ux::BatchRow>{named("device.A"), named("device.B"), named("device.C")}));
		history.clear();
		now += 10000;
	};
	const auto otherSession = [&] {
		++peer.session;
		batch.setSource(peer.source());
	};
	const auto done = [](const QString& outcome, const QString&) { return outcome == QStringLiteral("done"); };
	std::vector<undo_kinds::Case> cases;
	const auto add = [&](const char* name, std::function<void()> act) {
		cases.push_back({QStringLiteral("batch.items"), QString::fromLatin1(name), &history, start, std::move(act), rows,
		                 away, back, otherSession, done});
	};
	add("add an item", [&] { CHECK(batch.add(named("device.D"))); });
	add("add items", [&] { CHECK(batch.add(std::vector<ux::BatchRow>{named("device.D"), named("device.E")})); });
	add("remove an item", [&] { CHECK(batch.remove(1)); });
	add("move an item twice", [&] {
		CHECK(batch.move(0, 1));
		now += 500;
		CHECK(batch.move(1, 1));
	});
	add("drag an item", [&] { CHECK(batch.moveTo(0, 2)); });
	add("clear", [&] { CHECK(batch.clear()); });
	// A value set in an item (BA-4a): the request comes back. An item of an
	// earlier connection is never changed again: its step is skipped.
	const auto startRequest = [&] {
		if (!peer.current)
			back();
		CHECK(batch.clear());
		CHECK(batch.add(serviceRow(peer)));
		history.clear();
		now += 10000;
	};
	const auto requests = [&batch] {
		QStringList all;
		for (const auto& row : batch.model()->rows())
			all << QStringLiteral("%1:%2:%3").arg(row.uid).arg(QString::fromLatin1(row.request.toHex())).arg(row.line);
		return QVariant(all);
	};
	cases.push_back({QStringLiteral("batch.request"), QStringLiteral("set a value"), &history, startRequest,
	                 [&] { CHECK(batch.setScalarText(itemValue(batch, QStringLiteral("multiplier")), QStringLiteral("9"))); },
	                 requests, {}, {}, otherSession,
	                 [](const QString& outcome, const QString& note) {
		                 return outcome == QStringLiteral("skipped") &&
		                        note == QStringLiteral("the item is from an earlier connection");
	                 }});
	const auto report = undo_kinds::run(QStringLiteral("dynamic_batch"), cases);
	CHECK(report.cases == 7 && report.disconnects == 6 && report.descriptors == 7);

	// The words, each with its serial.
	start();
	CHECK(batch.add(named("device.D")) && batch.lastSerial() == history.undoSerial() &&
	      history.undoText() == QStringLiteral("Add device.D to the Batch"));
	CHECK(batch.add(std::vector<ux::BatchRow>{named("device.E"), named("device.F")}) &&
	      history.undoText() == QStringLiteral("Add 2 items to the Batch"));
	CHECK(batch.remove(0) && history.undoText() == QStringLiteral("Remove device.A from the Batch"));
	CHECK(batch.move(0, 1) && history.undoText() == QStringLiteral("Move device.B to item 2 in the Batch"));
	now += 500;
	// Moved back where it started within two seconds: no step is left.
	const auto steps = history.count();
	CHECK(batch.move(1, -1) && history.count() == steps - 1);
	// A drag (BA-4c): one item to another place, the others closing up, as
	// one step; Undo puts every item back.
	now += 10000;
	{
		const auto order = [&batch] {
			QStringList all;
			for (const auto& row : batch.model()->rows())
				all << row.name;
			return all;
		};
		const auto before = order();
		CHECK(before == QStringList({"device.B", "device.C", "device.D", "device.E", "device.F"}));
		CHECK(!batch.moveTo(0, 0) && !batch.moveTo(0, 5) && !batch.moveTo(-1, 2));
		const auto dragSteps = history.count();
		CHECK(batch.moveTo(0, 2) && history.count() == dragSteps + 1 &&
		      history.undoText() == QStringLiteral("Move device.B to item 3 in the Batch"));
		CHECK(order() == QStringList({"device.C", "device.D", "device.B", "device.E", "device.F"}));
		CHECK(history.undo() && order() == before);
		now += 10000;
		CHECK(batch.moveTo(4, 1) && history.count() == dragSteps + 1 &&
		      order() == QStringList({"device.B", "device.F", "device.C", "device.D", "device.E"}));
		CHECK(history.undo() && order() == before);
	}
	CHECK(batch.clear() && history.undoText() == QStringLiteral("Clear the Batch") && batch.count() == 0);
	CHECK(history.undo() && batch.count() == 5);
	// Rows put back after a reconnect come back historical: never sent again.
	CHECK(batch.clear());
	away();
	back();
	CHECK(history.undo() && batch.count() == 5 &&
	      role(batch, 0, ux::BatchListModel::HistoricalRole).toBool() && !batch.canExecute());
	// Execute is never a step; a step waits while the Batch is in flight.
	CHECK(batch.clear());
	CHECK(batch.add(named("device.G")));
	const auto serial = history.undoSerial();
	CHECK(batch.canExecute() && batch.execute());
	CHECK(history.undoSerial() == serial && !history.canUndo() &&
	      history.blockedReason() == QStringLiteral("Wait for the Batch to finish.") && !history.undo());
	peer.finish();
	batch.poll();
	f::events();
	CHECK(history.canUndo() && history.blockedReason().isEmpty() && history.undo() && batch.count() == 0);
	// Without a history nothing is recorded.
	batch.setHistory(nullptr);
	CHECK(batch.add(named("device.H")) && batch.lastSerial() == 0);
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	requestArguments();
	itemValues();
	itemValueBackToStart();
	orderedOwnedResults();
	boundedAdmissionAndHistory();
	reentrantDetach();
	reentrantInvalidation();
	externalModelInvalidation();
	busySessionKeepsBatch();
	severalRows();
	batchUndoKinds();
	std::printf("Dynamic Batch checks=%u no_native_callback_signals=1 bounded_items=32\n",
	            f::checks);
}
