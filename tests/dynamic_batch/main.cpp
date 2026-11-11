// Deterministic presentation fixture. It does not claim native execution. MIT.
#include <Telemetry/UX/DynamicBatch.hpp>
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

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	orderedOwnedResults();
	boundedAdmissionAndHistory();
	reentrantDetach();
	reentrantInvalidation();
	externalModelInvalidation();
	busySessionKeepsBatch();
	std::printf("Dynamic Batch checks=%u no_native_callback_signals=1 bounded_items=32\n",
	            f::checks);
}
