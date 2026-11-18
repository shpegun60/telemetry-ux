// One explicit ordered request; Qt owns presentation, the Session owns execution. MIT.
#include "DynamicBatch.hpp"
#include "ScalarEditor.hpp"
#include "SchemaText.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>
#include <QtCore/QTime>
#include <algorithm>
#include <initializer_list>
#include <utility>

namespace telemetry::ux {
namespace {
QString operationName(client::Operation op)
{
	switch (op) {
		case client::Operation::FieldRead:
			return QStringLiteral("read");
		case client::Operation::FieldWrite:
			return QStringLiteral("write");
		case client::Operation::Command:
			return QStringLiteral("command");
		case client::Operation::Service:
			return QStringLiteral("service");
		default:
			return {};
	}
}

QString batchSaid(const char* text)
{
	return QCoreApplication::translate("DynamicBatch", text);
}

// The status line's words for a run that failed as a whole (BA-4c).
QString statusText(client::Status status)
{
	if (status == client::Status::OutcomeUnknown)
		return batchSaid("The reply was lost: the items may have run. Check the device before executing again.");
	if (status == client::Status::QueueFull)
		return batchSaid("The device was busy and ran nothing. Execute the Batch again in a moment.");
	if (status == client::Status::Timeout)
		return batchSaid("No reply in time. The items may have run; nothing is sent again by itself.");
	if (status == client::Status::Cancelled)
		return batchSaid("The Batch was cancelled before its reply came.");
	if (status == client::Status::Unsupported)
		return batchSaid("This connection has no Batch.");
	return batchSaid("The Batch failed (status %1).").arg(static_cast<unsigned>(status));
}

// An item's word when the run failed as a whole: no checked outcome.
QString itemStatusText(client::Status status)
{
	if (status == client::Status::OutcomeUnknown)
		return batchSaid("Outcome unknown");
	if (status == client::Status::QueueFull)
		return batchSaid("Not sent");
	if (status == client::Status::Timeout)
		return batchSaid("No reply");
	if (status == client::Status::Cancelled)
		return batchSaid("Cancelled");
	if (status == client::Status::Unsupported)
		return batchSaid("Unavailable");
	return batchSaid("Failed");
}

// A checked item outcome counts as done.
bool itemDone(const QVariantMap& result)
{
	return result.value(QStringLiteral("succeeded")).toBool() || result.value(QStringLiteral("executed")).toBool() ||
	       result.value(QStringLiteral("accepted")).toBool();
}

QString itemText(client::Operation operation, const QVariantMap& result)
{
	if (result.value(QStringLiteral("accepted")).toBool())
		return QStringLiteral("Accepted - execution is not confirmed");
	if (result.value(QStringLiteral("executed")).toBool())
		return QStringLiteral("Executed");
	if (result.value(QStringLiteral("succeeded")).toBool())
		return QStringLiteral("Completed");
	if (result.value(QStringLiteral("dispatch")).toUInt() != 0)
		return QStringLiteral("Dispatch refused");
	const auto code = result.value(QStringLiteral("endpointStatus")).toUInt();
	const auto name = [code](std::initializer_list<const char*> names) {
		return code < names.size() ? QString::fromLatin1(*(names.begin() + code))
		                           : QStringLiteral("Endpoint refused");
	};
	if (operation == client::Operation::Service)
		return name({"Ok", "InvalidArgument", "Unavailable", "Busy", "Failed"});
	if (operation == client::Operation::FieldWrite)
		return name({"Applied", "NotFound", "ReadOnly", "InvalidValue", "Busy", "Unavailable"});
	if (operation == client::Operation::Command)
		return name({"Executed", "Accepted", "NotFound", "Unavailable", "ArgumentCountMismatch",
		             "InvalidValue", "Busy", "Failed"});
	return QStringLiteral("Read refused");
}

// Whether a row's outcome is a failure (BA-4c): a refusal of the device, an
// unreadable value, or a run with no checked outcome. A draft, an item being
// sent and an item done are not.
bool failed(const BatchRow& row)
{
	const auto& result = row.result;
	if (result.isEmpty())
		return row.status != QStringLiteral("Draft") && row.status != QStringLiteral("Pending") && !row.sent.isNull();
	const auto valueStatus = result.value(QStringLiteral("valueStatus")).toString();
	if (valueStatus == QStringLiteral("InvalidPayload") || valueStatus == QStringLiteral("TypeMismatch"))
		return true;
	if (result.value(QStringLiteral("accepted")).toBool() || result.value(QStringLiteral("executed")).toBool() ||
	    result.value(QStringLiteral("succeeded")).toBool())
		return false;
	if (result.value(QStringLiteral("dispatch")).toUInt() != 0)
		return true;
	const auto code = result.value(QStringLiteral("endpointStatus")).toUInt();
	if (row.operation == client::Operation::Command)
		return code > 1;
	if (row.operation == client::Operation::Service || row.operation == client::Operation::FieldWrite)
		return code != 0;
	return true;
}

// The Outcome column's words (Details layout §8): "–" before a run, the
// kind's word when done, the failure's in short; ", changed since" when the
// values changed after the run.
QString outcomeWords(const BatchRow& row)
{
	if (row.historical)
		return batchSaid("earlier connection");
	if (row.status == QStringLiteral("Draft") || row.sent.isNull())
		return QString(QChar(0x2013));
	if (row.status == QStringLiteral("Pending"))
		return batchSaid("Sending…");
	QString word;
	const auto& result = row.result;
	if (failed(row)) {
		const auto why = DynamicBatch::why(row);
		word = why.refused ? batchSaid("Refused") : why.outcome;
	} else if (result.value(QStringLiteral("accepted")).toBool()) {
		word = batchSaid("Accepted, not confirmed");
	} else {
		switch (row.operation) {
			case client::Operation::FieldRead:
				word = batchSaid("Read");
				break;
			case client::Operation::FieldWrite:
				word = batchSaid("Written");
				break;
			case client::Operation::Command:
				word = batchSaid("Executed");
				break;
			default:
				word = batchSaid("Done");
				break;
		}
	}
	return row.sent != row.request ? batchSaid("%1, changed since").arg(word) : word;
}
} // namespace

bool DynamicBatch::itemFailed(const BatchRow& row)
{
	return failed(row);
}

DynamicBatch::Why DynamicBatch::why(const BatchRow& row)
{
	Why out;
	const auto& result = row.result;
	if (result.isEmpty()) {
		// The run failed as a whole: the item's word says how ("No reply").
		out.words = row.status.toLower();
		out.sentence = batchSaid("has no checked outcome");
		out.outcome = row.status == batchSaid("No checked outcome") ? batchSaid("No outcome") : row.status;
		return out;
	}
	const auto valueStatus = result.value(QStringLiteral("valueStatus")).toString();
	if (valueStatus == QStringLiteral("InvalidPayload") || valueStatus == QStringLiteral("TypeMismatch")) {
		out.words = batchSaid("the reply could not be read");
		out.sentence = batchSaid("returned a value that could not be read");
		out.outcome = batchSaid("Unreadable");
		return out;
	}
	out.refused = true;
	if (result.value(QStringLiteral("dispatch")).toUInt() != 0) {
		out.words = batchSaid("refused by the device");
		out.sentence = batchSaid("was refused");
		return out;
	}
	const auto code = result.value(QStringLiteral("endpointStatus")).toUInt();
	const auto set = [&out](const char* words, const char* sentence) {
		out.words = batchSaid(words);
		out.sentence = batchSaid(sentence);
	};
	switch (row.operation) {
		case client::Operation::Command:
			if (code == 4 || code == 5)
				set("arguments refused", "refused its arguments");
			else if (code == 6)
				set("the device was busy", "found the device busy");
			else if (code == 3)
				set("unavailable", "was unavailable");
			else if (code == 7)
				set("failed on the device", "failed on the device");
			else if (code == 2)
				set("not found on the device", "was not found on the device");
			else
				set("refused by the device", "was refused");
			break;
		case client::Operation::Service:
			if (code == 1)
				set("request values refused", "refused its request values");
			else if (code == 3)
				set("the device was busy", "found the device busy");
			else if (code == 2)
				set("unavailable", "was unavailable");
			else if (code == 4)
				set("failed on the device", "failed on the device");
			else
				set("refused by the device", "was refused");
			break;
		case client::Operation::FieldWrite:
			if (code == 3)
				set("value refused", "refused its value");
			else if (code == 2)
				set("read-only on the device", "is read-only on the device");
			else if (code == 4)
				set("the device was busy", "found the device busy");
			else if (code == 5)
				set("unavailable", "was unavailable");
			else if (code == 1)
				set("not found on the device", "was not found on the device");
			else
				set("refused by the device", "was refused");
			break;
		default:
			set("read refused", "was not read");
			break;
	}
	return out;
}

int BatchListModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : int(rows_.size());
}

QVariant BatchListModel::data(const QModelIndex& index, int role) const
{
	if (index.model() != this || index.column() != 0 || index.row() < 0 ||
	    index.row() >= int(rows_.size()))
		return {};
	const auto& row = rows_[std::size_t(index.row())];
	switch (role) {
		case Qt::DisplayRole:
		case NameRole:
			return row.name;
		case OperationRole:
			return operationName(row.operation);
		case EndpointIdRole:
			return row.id;
		case StatusRole:
			return row.status;
		case HasValueRole:
			return row.hasValue && (!valueGuard_ || valueGuard_());
		case HistoricalRole:
			return row.historical;
		case ResultRole:
			return row.result;
		case LineRole:
			return row.line;
		case ChangedRole:
			return !row.sent.isNull() && row.sent != row.request;
		case AtRole:
			return row.at;
		case UidRole:
			return QString::number(row.uid);
		case FailedRole:
			return failed(row);
		case OutcomeRole:
			return outcomeWords(row);
		default:
			return {};
	}
}

QHash<int, QByteArray> BatchListModel::roleNames() const
{
	return {{NameRole, "name"},     {OperationRole, "operation"}, {EndpointIdRole, "endpointId"},
	        {StatusRole, "status"}, {HasValueRole, "hasValue"},   {HistoricalRole, "historical"},
	        {ResultRole, "result"},  {LineRole, "line"},           {ChangedRole, "changed"},
	        {AtRole, "at"},          {UidRole, "uid"},             {FailedRole, "failed"},
	        {OutcomeRole, "outcome"}};
}

bool BatchListModel::changing() const noexcept
{
	return changing_;
}

const std::vector<BatchRow>& BatchListModel::rows() const noexcept
{
	return rows_;
}

void BatchListModel::replace(std::vector<BatchRow> rows)
{
	if (changing_)
		return;
	changing_ = true;
	beginResetModel();
	rows_ = std::move(rows);
	endResetModel();
	changing_ = false;
}

void BatchListModel::update(int row, BatchRow value)
{
	if (changing_ || row < 0 || row >= int(rows_.size()))
		return;
	changing_ = true;
	rows_[std::size_t(row)] = std::move(value);
	const auto at = index(row, 0);
	Q_EMIT dataChanged(at, at);
	changing_ = false;
}

void BatchListModel::setValueGuard(std::function<bool()> guard)
{
	valueGuard_ = std::move(guard);
}

struct DynamicBatch::Impl : std::enable_shared_from_this<DynamicBatch::Impl> {
	struct Request {
		std::shared_ptr<void> storage;
		quint32 session = 0;
		std::vector<BatchRow> rows;
		std::optional<BatchCompletion> completion;
	};

	BatchSource source;
	BatchListModel model;
	ValueModel responses;
	QObject deferredContext;
	std::array<std::byte, 65536> responseMemory{};
	integration::inspector::SnapshotStore snapshot;
	std::shared_ptr<Request> request;
	std::function<void()> notify, finished;
	std::function<bool()> blocked, allowed;
	// The history the Batch's steps go to, the target they put rows back
	// through, the next row's uid, and the last step's serial.
	QPointer<UndoHistory> history;
	std::unique_ptr<UndoFunctions> target;
	quint64 nextUid = 1;
	int lastSerial = 0;
	int selected = -1;
	bool publishing = false, invalidationPending = false, pendingHistorical = false,
	     invalidationQueued = false, retired = true;
	// The status line: empty while there is nothing to say (BA-4c).
	QString status;
	// The status line asks for attention: a run that failed, an item not done.
	bool attention = false;
	// The selected item's request (BA-4a): its bytes as a store, an editor's
	// draft over them, and its values as Details shows them. A value's steps
	// merge while its cell stays open: editSerial moves when it is left.
	std::array<std::byte, 4096> requestMemory{}, draftMemory{}, commitMemory{};
	integration::inspector::SnapshotStore requestStore;
	integration::workbench::RequestEditor editor{draftMemory, commitMemory};
	ValueModel values;
	int editSerial = 0;

	Impl()
	    : snapshot(responseMemory, {this, +[](const void* p) noexcept {
		                                const auto& state = *static_cast<const Impl*>(p);
		                                return state.current() && !state.invalidationPending;
	                                }}),
	      requestStore(requestMemory, {this, +[](const void* p) noexcept {
		                               const auto& state = *static_cast<const Impl*>(p);
		                               return state.current() && !state.invalidationPending;
	                               }})
	{
		model.setValueGuard([this] {
			return current() && !invalidationPending;
		});
	}

	bool current() const
	{
		return !retired && source.storage && source.current && source.current();
	}

	bool editable() const
	{
		// A busy session (another panel's request, a polling Watchlist) is no
		// reason to refuse: the FIFO orders requests and reports QueueFull.
		return current() && !request && !publishing && !invalidationPending && !model.changing() &&
		       !responses.changing() && !values.changing() && (!blocked || !blocked());
	}

	bool permitted() const
	{
		return allowed && allowed();
	}

	void signal()
	{
		if (notify)
			notify();
		// Whether a step can be taken may have changed with the request.
		if (history)
			history->refresh();
	}

	// A row as a "batch.items" fragment: its place and the row itself.
	static QVariantMap fragment(int at, const BatchRow& row)
	{
		return {{QStringLiteral("at"), at}, {QStringLiteral("row"), QVariant::fromValue(row)}};
	}

	static QString key(const BatchRow& row)
	{
		return QString::number(row.uid);
	}

	int record(const QString& text, QVariantMap before, QVariantMap after, const QString& mergeKey = {})
	{
		lastSerial = 0;
		if (!history || !target)
			return 0;
		QVector<UndoPart> parts;
		parts.push_back(UndoPart{target.get(), QStringLiteral("batch.items"), std::move(before), std::move(after)});
		lastSerial = history->record(text, std::move(parts), mergeKey);
		return lastSerial;
	}

	// Rows added at the end, from first on, as one step.
	void recordAdded(std::size_t first)
	{
		const auto& rows = model.rows();
		QVariantMap after;
		for (auto at = first; at < rows.size(); ++at)
			after.insert(key(rows[at]), fragment(int(at), rows[at]));
		const auto count = rows.size() - first;
		record(count == 1 ? QCoreApplication::translate("DynamicBatch", "Add %1 to the Batch").arg(rows[first].name)
		                  : QCoreApplication::translate("DynamicBatch", "Add %L1 items to the Batch").arg(count),
		       {}, std::move(after));
	}

	// A row's request changed in place (BA-4a): the line follows, and with
	// words one "batch.request" step, merged by mergeKey while it stays.
	bool putRequest(int at, const QByteArray& bytes, const QString& words, const QString& mergeKey)
	{
		if (!rowEditable(at) || !source.schema)
			return false;
		auto row = model.rows()[std::size_t(at)];
		if (row.request == bytes)
			return true;
		const auto schema = source.schema();
		if (row.requestType != 0 &&
		    !schema.view(row.requestType, runtime::Input{reinterpret_cast<const std::byte*>(bytes.constData()),
		                                                 std::size_t(bytes.size())}))
			return false;
		const auto before = row.request;
		row.request = bytes;
		row.line = DynamicBatch::lineOf(schema, row.requestType, bytes);
		const auto uid = key(row);
		publishing = true;
		model.update(at, std::move(row));
		finishPublication();
		if (at == selected)
			staleResponse();
		if (!words.isEmpty() && history && target) {
			QVector<UndoPart> parts;
			parts.push_back(UndoPart{target.get(), QStringLiteral("batch.request"),
			                         {{uid, QVariantMap{{QStringLiteral("request"), before}}}},
			                         {{uid, QVariantMap{{QStringLiteral("request"), bytes}}}}});
			lastSerial = history->record(words, std::move(parts), mergeKey, 0);
		}
		signal();
		return true;
	}

	// A "batch.request" step's requests back by uid; the item is selected.
	UndoOutcome applyRequest(const QVariantMap& state, const QStringList& keys)
	{
		if (!undoBlocked().isEmpty())
			return {UndoOutcome::Kind::Blocked, {}};
		int applied = 0;
		QStringList notes;
		for (const auto& uid : keys) {
			const auto& rows = model.rows();
			const auto found = std::find_if(rows.begin(), rows.end(), [&uid](const BatchRow& row) { return key(row) == uid; });
			if (found == rows.end()) {
				notes << QCoreApplication::translate("DynamicBatch", "the item is no longer in the Batch");
				continue;
			}
			const auto at = int(found - rows.begin());
			if (!rowEditable(at)) {
				notes << QCoreApplication::translate("DynamicBatch", "the item is from an earlier connection");
				continue;
			}
			if (!putRequest(at, state.value(uid).toMap().value(QStringLiteral("request")).toByteArray(), {}, {}))
				continue;
			selected = at;
			++applied;
		}
		if (applied > 0) {
			showItem();
			signal();
		}
		const auto note = notes.join(QStringLiteral("; "));
		if (applied == 0)
			return {UndoOutcome::Kind::Skipped, note};
		return {note.isEmpty() ? UndoOutcome::Kind::Done : UndoOutcome::Kind::Partial, note};
	}

	// Why a step on the Batch waits now: a Batch request is in flight.
	QString undoBlocked() const
	{
		if (request)
			return QCoreApplication::translate("DynamicBatch", "Wait for the Batch to finish.");
		if (publishing || model.changing() || responses.changing() || values.changing())
			return QCoreApplication::translate("DynamicBatch", "The Batch is changing; try again in a moment.");
		return {};
	}

	// A step's rows back by uid: those it says are gone, taken out; those it
	// says are there, moved to their places when held, put back when not -
	// whatever the connection: a row of another session, or with no session,
	// comes back historical, never to be sent again.
	UndoOutcome apply(const QVariantMap& state, const QStringList& keys)
	{
		if (!undoBlocked().isEmpty())
			return {UndoOutcome::Kind::Blocked, {}};
		auto rows = model.rows();
		const auto find = [&rows](const QString& uid) {
			return std::find_if(rows.begin(), rows.end(), [&uid](const BatchRow& row) { return key(row) == uid; });
		};
		int applied = 0;
		QStringList gone;
		QVector<QPair<int, BatchRow>> in;
		for (const auto& uid : keys) {
			const auto held = find(uid);
			if (!state.contains(uid)) {
				if (held != rows.end()) {
					rows.erase(held);
					++applied;
				} else {
					gone << uid;
				}
				continue;
			}
			const auto fragment = state.value(uid).toMap();
			auto row = fragment.value(QStringLiteral("row")).value<BatchRow>();
			if (held != rows.end()) {
				// Held: moved, with what it holds now - unless the step made it
				// another operation (Read | Write), which puts the row back whole.
				const auto stepped = std::move(row);
				row = *held;
				if (stepped.operation != row.operation) {
					const bool historical = row.historical;
					row = stepped;
					row.historical = historical;
				}
				rows.erase(held);
			} else {
				const bool session = current() && source.session && row.session == source.session();
				row.historical = row.historical || !session;
				if (row.historical) {
					row.hasValue = false;
					row.response.clear();
				}
			}
			in.push_back({fragment.value(QStringLiteral("at")).toInt(), std::move(row)});
		}
		if (rows.size() + std::size_t(in.size()) > 32)
			return {UndoOutcome::Kind::Skipped,
			        QCoreApplication::translate("DynamicBatch", "the Batch holds 32 items at most")};
		std::sort(in.begin(), in.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		for (auto& [at, row] : in) {
			rows.insert(rows.begin() + std::clamp(at, 0, int(rows.size())), std::move(row));
			++applied;
		}
		if (applied > 0) {
			publishing = true;
			selected = -1;
			responses.clear();
			clearItem();
			snapshot.invalidate();
			model.replace(std::move(rows));
			finishPublication();
			signal();
		}
		const auto note = gone.isEmpty() ? QString{}
		                  : gone.size() == 1
		                      ? QCoreApplication::translate("DynamicBatch", "1 item is no longer in the Batch")
		                      : QCoreApplication::translate("DynamicBatch", "%L1 items are no longer in the Batch")
		                            .arg(gone.size());
		if (applied == 0)
			return {UndoOutcome::Kind::Skipped, note};
		return {note.isEmpty() ? UndoOutcome::Kind::Done : UndoOutcome::Kind::Partial, note};
	}

	bool budgets(const BatchRow* candidate = nullptr) const
	{
		return budget(candidate, candidate ? 1 : 0) == DynamicBatch::Refusal::None;
	}

	// The item and byte budgets with candidates added: None, Items or Bytes;
	// Session when the device says nothing of its limits.
	DynamicBatch::Refusal budget(const BatchRow* candidates, qsizetype count) const
	{
		const auto limits = current() && source.capability ? source.capability() : std::nullopt;
		if (!limits)
			return DynamicBatch::Refusal::Session;
		quint64 tx = 2, rx = 2;
		const auto add = [&](const BatchRow& row) {
			tx += 8 + quint64(row.request.size());
			rx += 12 + quint64(row.responseBytes);
		};
		for (const auto& row : model.rows())
			add(row);
		for (qsizetype at = 0; at < count; ++at)
			add(candidates[at]);
		const auto items = model.rows().size() + std::size_t(count);
		if (items > 32 || items > limits->items)
			return DynamicBatch::Refusal::Items;
		if (tx > 4096 || tx > limits->requestBytes || rx > responseMemory.size() || rx > limits->replyBytes)
			return DynamicBatch::Refusal::Bytes;
		return DynamicBatch::Refusal::None;
	}

	void clearItem()
	{
		values.clear();
		editor.clear();
		requestStore.invalidate();
	}

	// An item that may change now: not while the Batch runs, never one of an
	// earlier connection.
	bool rowEditable(int at) const
	{
		return !publishing && rowChangeable(at);
	}

	// The same, while a publication of the Batch's own is under way.
	bool rowChangeable(int at) const
	{
		if (!current() || request || invalidationPending || at < 0 || at >= int(model.rows().size()))
			return false;
		const auto& row = model.rows()[std::size_t(at)];
		return !row.historical && source.session && row.session == source.session();
	}

	static descriptor::Category categoryOf(const BatchRow& row)
	{
		return row.operation == client::Operation::Command   ? descriptor::Category::Command
		       : row.operation == client::Operation::Service ? descriptor::Category::Service
		                                                     : descriptor::Category::Field;
	}

	// The selected item as Details shows it: a request's values (editable
	// while the item may change), the value a read returned, and a Service's
	// response.
	void showItem()
	{
		clearItem();
		showResponse();
		if (invalidationPending || !current() || selected < 0 || selected >= int(model.rows().size()))
			return;
		const auto& row = model.rows()[std::size_t(selected)];
		if (row.historical || !source.session || row.session != source.session() || !source.schema)
			return;
		const auto schema = source.schema();
		values.setSchemaMetadata(schema);
		// A Field's value keeps its root row, a write's too; a Command's
		// and a Service's request is a section's.
		const bool field = row.operation == client::Operation::FieldRead ||
		                   row.operation == client::Operation::FieldWrite;
		values.setSection(field ? ValueModel::Section::Value : ValueModel::Section::Request);
		if (row.operation == client::Operation::FieldRead) {
			// The value read is the item's values; it has no response. Before
			// its run the pane shows the Field's definition (the provider's).
			if (row.hasValue && snapshot.isCurrent()) {
				responses.clear();
				values.resetSnapshot(snapshot);
			}
			return;
		}
		if (row.requestType == 0 || std::size_t(row.request.size()) > requestMemory.size())
			return;
		if (requestStore.admit(schema, row.session) != integration::inspector::ModelStatus::Ok ||
		    requestStore.selectEndpoint(row.id, row.requestType, categoryOf(row)) !=
		        integration::inspector::ModelStatus::Ok)
			return;
		const auto identity = requestStore.snapshotInfo();
		const client::Input bytes{reinterpret_cast<const std::byte*>(row.request.constData()),
		                          std::size_t(row.request.size())};
		if (requestStore.publish(bytes, identity.admission, identity.revision) !=
		    integration::inspector::ModelStatus::Ok)
			return;
		if (rowChangeable(selected) && editor.begin(requestStore) == runtime::Status::Ok)
			values.resetDraft(editor);
		else
			values.resetSnapshot(requestStore);
		// What was sent last, for the unsent marks.
		if (!row.sent.isNull())
			values.setBaseline(sentTexts(schema, row));
	}

	// The sent request's values by path, as the values model writes them.
	QHash<QString, QString> sentTexts(const runtime::RuntimeSchema& schema, const BatchRow& row)
	{
		QHash<QString, QString> texts;
		std::array<std::byte, 4096> memory{};
		integration::inspector::SnapshotStore store{memory};
		if (std::size_t(row.sent.size()) > memory.size() || store.admit(schema, row.session) != integration::inspector::ModelStatus::Ok ||
		    store.selectEndpoint(row.id, row.requestType, categoryOf(row)) != integration::inspector::ModelStatus::Ok)
			return texts;
		const auto identity = store.snapshotInfo();
		const client::Input bytes{reinterpret_cast<const std::byte*>(row.sent.constData()), std::size_t(row.sent.size())};
		if (store.publish(bytes, identity.admission, identity.revision) != integration::inspector::ModelStatus::Ok)
			return texts;
		ValueModel sentValues;
		sentValues.resetSnapshot(store);
		// Every row a person may have opened: the first 128 of each level.
		QVector<QModelIndex> stack{QModelIndex{}};
		while (!stack.isEmpty()) {
			const auto parent = stack.takeLast();
			if (parent.isValid() && sentValues.canFetchMore(parent))
				sentValues.fetchMore(parent);
			for (int at = 0, count = sentValues.rowCount(parent); at < count; ++at)
				stack.push_back(sentValues.index(at, 0, parent));
		}
		return sentValues.texts();
	}

	void showResponse()
	{
		responses.clear();
		snapshot.invalidate();
		if (invalidationPending || !current() || selected < 0 ||
		    selected >= int(model.rows().size()))
			return;
		const auto& row = model.rows()[std::size_t(selected)];
		if (row.historical || row.session != source.session() || !source.schema)
			return;
		const auto schema = source.schema();
		if (!row.hasValue) {
			// A Service's response before its run: its structure with "–".
			if (row.operation == client::Operation::Service && schema.type(row.responseType))
				responses.resetShape(schema, row.responseType, ValueModel::Section::Response);
			return;
		}
		if (snapshot.admit(schema, row.session) != integration::inspector::ModelStatus::Ok)
			return;
		const auto category = row.operation == client::Operation::Service
		                          ? descriptor::Category::Service
		                          : descriptor::Category::Field;
		if (snapshot.selectEndpoint(row.id, row.responseType, category) !=
		    integration::inspector::ModelStatus::Ok)
			return;
		const auto identity = snapshot.snapshotInfo();
		const auto bytes =
		    client::Input{reinterpret_cast<const std::byte*>(row.response.constData()),
		                  std::size_t(row.response.size())};
		if (snapshot.publish(bytes, identity.admission, identity.revision) ==
		    integration::inspector::ModelStatus::Ok)
			responses.resetSnapshot(snapshot);
		staleResponse();
	}

	// A Service's response from a run whose request changed since: muted,
	// with the note under it.
	void staleResponse()
	{
		if (selected < 0 || selected >= int(model.rows().size()))
			return;
		const auto& row = model.rows()[std::size_t(selected)];
		const bool changed = row.operation == client::Operation::Service && row.hasValue && !row.sent.isNull() &&
		                     row.sent != row.request && responses.rowCount() > 0 && !responses.shape();
		responses.setStale(changed, changed ? batchSaid("From the run at %1; the values changed since.").arg(row.at)
		                                    : QString{});
	}

	void deferInvalidation()
	{
		if (invalidationQueued)
			return;
		invalidationQueued = true;
		const auto weak = weak_from_this();
		QMetaObject::invokeMethod(
		    &deferredContext,
		    [weak] {
			    if (const auto p = weak.lock()) {
				    p->invalidationQueued = false;
				    if (p->publishing || p->model.changing() || p->responses.changing() || p->values.changing()) {
					    p->deferInvalidation();
					    return;
				    }
				    p->finishPublication();
			    }
		    },
		    Qt::QueuedConnection);
	}

	void finishPublication()
	{
		publishing = false;
		if (std::exchange(invalidationPending, false))
			invalidate(std::exchange(pendingHistorical, false));
	}

	void invalidate(bool historical)
	{
		if (publishing || model.changing() || responses.changing() || values.changing()) {
			invalidationPending = true;
			pendingHistorical |= historical;
			deferInvalidation();
			return;
		}
		if (std::none_of(model.rows().begin(), model.rows().end(), [historical](const auto& row) {
			    return row.hasValue || !row.response.isEmpty() || (historical && !row.historical);
		    }))
			return;
		publishing = true;
		auto rows = model.rows();
		for (auto& row : rows) {
			row.hasValue = false;
			row.response.clear();
			row.historical |= historical;
		}
		responses.clear();
		clearItem();
		snapshot.invalidate();
		model.replace(std::move(rows));
		// Items of an earlier connection, said on the status line (BA-4c).
		if (historical && !request && !attention) {
			status = batchSaid("These items are from an earlier connection and are never sent again. Clear them to start a new Batch.");
			attention = false;
		}
		finishPublication();
		signal();
	}
};

DynamicBatch::DynamicBatch() : d(std::make_shared<Impl>())
{
	const std::weak_ptr<Impl> weak = d;
	d->target = std::make_unique<UndoFunctions>(
	    [weak](const QString& kind, const QVariantMap& state, const QStringList& keys) {
		    const auto p = weak.lock();
		    if (!p)
			    return UndoOutcome{UndoOutcome::Kind::Skipped, {}};
		    return kind == QStringLiteral("batch.request") ? p->applyRequest(state, keys) : p->apply(state, keys);
	    },
	    [weak] {
		    const auto p = weak.lock();
		    return p ? p->undoBlocked() : QString{};
	    });
}

void DynamicBatch::setHistory(UndoHistory* history)
{
	d->history = history;
}

UndoHistory* DynamicBatch::history() const
{
	return d->history;
}

int DynamicBatch::lastSerial() const
{
	return d->lastSerial;
}

DynamicBatch::~DynamicBatch() = default;

void DynamicBatch::setNotify(std::function<void()> notify, std::function<bool()> blocked,
                             std::function<bool()> allowed, std::function<void()> finished)
{
	d->notify = std::move(notify);
	d->blocked = std::move(blocked);
	d->allowed = std::move(allowed);
	d->finished = std::move(finished);
}

void DynamicBatch::setSource(BatchSource source)
{
	auto p = d;
	p->invalidate(true);
	p->source = std::move(source);
	p->retired = false;
	p->signal();
}

void DynamicBatch::retire()
{
	auto p = d;
	if (p->retired)
		return;
	p->retired = true;
	p->invalidate(true);
}

void DynamicBatch::invalidateValues()
{
	auto p = d;
	p->invalidate(false);
}

bool DynamicBatch::supported() const
{
	return d->current() && d->source.capability && d->source.capability().has_value();
}

runtime::RuntimeSchema DynamicBatch::schema() const
{
	return d->current() && d->source.schema ? d->source.schema() : runtime::RuntimeSchema{};
}

bool DynamicBatch::pending() const
{
	return bool(d->request);
}

bool DynamicBatch::active() const
{
	return pending() || d->publishing || d->model.changing() || d->responses.changing() || d->values.changing();
}

bool DynamicBatch::canAdd(const BatchRow& row) const
{
	return refusal({&row, 1}) == Refusal::None;
}

bool DynamicBatch::add(BatchRow row)
{
	auto p = d;
	if (!canAdd(row))
		return false;
	row.request = QByteArray(row.request.constData(), row.request.size());
	if (row.line.isEmpty())
		row.line = lineOf(schema(), row.requestType, row.request);
	row.uid = p->nextUid++;
	p->lastSerial = 0;
	p->publishing = true;
	auto rows = p->model.rows();
	const auto first = rows.size();
	rows.push_back(std::move(row));
	p->selected = int(rows.size()) - 1;
	p->responses.clear();
	p->clearItem();
	p->snapshot.invalidate();
	p->model.replace(std::move(rows));
	p->showItem();
	// The Batch as it is now has not run: the status line says so.
	p->status.clear();
	p->attention = false;
	p->finishPublication();
	p->recordAdded(first);
	p->signal();
	return true;
}

DynamicBatch::Refusal DynamicBatch::refusal(std::span<const BatchRow> rows) const
{
	if (!d->editable())
		return Refusal::Busy;
	if (!d->source.session)
		return Refusal::Session;
	const auto session = d->source.session();
	for (const auto& row : rows)
		if (row.session != session)
			return Refusal::Session;
	// Items of an earlier connection are never sent again; until they are
	// cleared nothing joins them, or the Batch could not be executed.
	for (const auto& row : d->model.rows())
		if (row.session != session)
			return Refusal::Previous;
	for (const auto& row : rows)
		if (row.operation != client::Operation::FieldRead && !d->permitted())
			return Refusal::Permission;
	return d->budget(rows.data(), qsizetype(rows.size()));
}

int DynamicBatch::itemLimit() const
{
	const auto limits = d->current() && d->source.capability ? d->source.capability() : std::nullopt;
	return limits ? int(std::min(32u, limits->items)) : 0;
}

bool DynamicBatch::add(std::vector<BatchRow> rows)
{
	auto p = d;
	if (rows.empty() || refusal(rows) != Refusal::None)
		return false;
	p->lastSerial = 0;
	p->publishing = true;
	auto all = p->model.rows();
	const auto first = all.size();
	for (auto& row : rows) {
		row.request = QByteArray(row.request.constData(), row.request.size());
		if (row.line.isEmpty())
			row.line = lineOf(schema(), row.requestType, row.request);
		row.uid = p->nextUid++;
		all.push_back(std::move(row));
	}
	p->selected = int(all.size()) - 1;
	p->responses.clear();
	p->clearItem();
	p->snapshot.invalidate();
	p->model.replace(std::move(all));
	p->showItem();
	p->status.clear();
	p->attention = false;
	p->finishPublication();
	p->recordAdded(first);
	p->signal();
	return true;
}

bool DynamicBatch::remove(int row)
{
	auto p = d;
	if (!p->editable() || row < 0 || row >= count())
		return false;
	p->lastSerial = 0;
	p->publishing = true;
	auto rows = p->model.rows();
	const auto taken = rows[std::size_t(row)];
	rows.erase(rows.begin() + row);
	p->selected = -1;
	p->responses.clear();
	p->clearItem();
	p->snapshot.invalidate();
	p->model.replace(std::move(rows));
	p->finishPublication();
	p->record(QCoreApplication::translate("DynamicBatch", "Remove %1 from the Batch").arg(taken.name),
	          {{Impl::key(taken), Impl::fragment(row, taken)}}, {});
	p->signal();
	return true;
}

bool DynamicBatch::move(int row, int delta)
{
	if (delta != -1 && delta != 1)
		return false;
	return moveTo(row, row + delta);
}

bool DynamicBatch::moveTo(int row, int to)
{
	auto p = d;
	if (!p->editable() || row < 0 || row >= count() || to < 0 || to >= count() || to == row)
		return false;
	p->lastSerial = 0;
	p->publishing = true;
	auto rows = p->model.rows();
	const auto moved = rows[std::size_t(row)];
	rows.erase(rows.begin() + row);
	rows.insert(rows.begin() + to, moved);
	// The selection follows its item; the items between close up.
	if (p->selected == row)
		p->selected = to;
	else if (row < to && p->selected > row && p->selected <= to)
		--p->selected;
	else if (to < row && p->selected >= to && p->selected < row)
		++p->selected;
	p->model.replace(std::move(rows));
	p->finishPublication();
	// One item moved again within two seconds is one step; back where it
	// started, none. Only the moved item's place is kept: putting it back
	// moves the others back too.
	p->record(QCoreApplication::translate("DynamicBatch", "Move %1 to item %L2 in the Batch").arg(moved.name).arg(to + 1),
	          {{Impl::key(moved), Impl::fragment(row, moved)}}, {{Impl::key(moved), Impl::fragment(to, moved)}},
	          QStringLiteral("batch.move:") + Impl::key(moved));
	p->signal();
	return true;
}

bool DynamicBatch::clear()
{
	auto p = d;
	if (p->request || p->publishing || p->model.changing() || p->responses.changing() || p->values.changing() ||
	    (p->blocked && p->blocked()))
		return false;
	p->lastSerial = 0;
	QVariantMap before;
	const auto& held = p->model.rows();
	for (std::size_t at = 0; at < held.size(); ++at)
		before.insert(Impl::key(held[at]), Impl::fragment(int(at), held[at]));
	p->publishing = true;
	p->selected = -1;
	p->responses.clear();
	p->clearItem();
	p->snapshot.invalidate();
	p->model.replace({});
	p->status.clear();
	p->attention = false;
	p->finishPublication();
	if (!before.isEmpty())
		p->record(QCoreApplication::translate("DynamicBatch", "Clear the Batch"), std::move(before), {});
	p->signal();
	return true;
}

bool DynamicBatch::select(int row)
{
	auto p = d;
	if (p->request || p->publishing || p->model.changing() || p->responses.changing() || p->values.changing() || row < 0 ||
	    row >= count())
		return false;
	p->publishing = true;
	p->selected = row;
	p->showItem();
	p->finishPublication();
	p->signal();
	return true;
}

bool DynamicBatch::canExecute() const
{
	const auto p = d;
	if (!p->editable() || !count() || !p->budgets() || !p->source.submit || !p->source.session)
		return false;
	for (const auto& row : p->model.rows())
		if (row.session != p->source.session() || row.historical ||
		    (row.operation != client::Operation::FieldRead && !p->permitted()))
			return false;
	return true;
}

bool DynamicBatch::execute()
{
	auto p = d;
	if (!canExecute())
		return false;
	auto request = std::make_shared<Impl::Request>();
	request->storage = p->source.storage;
	request->session = p->source.session();
	request->rows = p->model.rows();
	std::array<BatchSource::Request, 32> inputs{};
	for (std::size_t i = 0; i < request->rows.size(); ++i) {
		const auto& row = request->rows[i];
		inputs[i] = {row.operation,
		             row.id,
		             {reinterpret_cast<const std::byte*>(row.request.constData()),
		              std::size_t(row.request.size())}};
	}
	p->publishing = true;
	p->request = request;
	const auto submitted = p->source.submit(std::span{inputs}.first(request->rows.size()),
	                                        BatchSource::Completion{request, &request->completion});
	if (!submitted) {
		p->request.reset();
		p->status = statusText(submitted.status);
		p->attention = true;
		p->finishPublication();
		p->signal();
		return false;
	}
	const auto at = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
	for (auto& row : request->rows) {
		row.sent = row.request.isNull() ? QByteArray("") : row.request;
		row.at = at;
	}
	auto rows = request->rows;
	for (auto& row : rows) {
		row.status = QStringLiteral("Pending");
		row.result.clear();
		row.response.clear();
		row.hasValue = false;
	}
	// The selected item's tables stay as they were while the Batch runs.
	const auto valuesShown = p->values.shownTexts();
	const auto responsesShown = p->responses.shownTexts();
	const bool valuesHeld = p->values.rowCount() > 0;
	p->responses.clear();
	p->clearItem();
	p->snapshot.invalidate();
	p->model.replace(std::move(rows));
	if (p->selected >= 0 && p->selected < int(p->model.rows().size())) {
		const auto& row = p->model.rows()[std::size_t(p->selected)];
		const auto schema = p->source.schema ? p->source.schema() : runtime::RuntimeSchema{};
		const bool field = row.operation == client::Operation::FieldRead ||
		                   row.operation == client::Operation::FieldWrite;
		const auto type = row.operation == client::Operation::FieldRead ? row.responseType : row.requestType;
		if (valuesHeld && schema.type(type))
			p->values.resetShape(schema, type, field ? ValueModel::Section::Value : ValueModel::Section::Request,
			                     valuesShown);
		if (row.operation == client::Operation::Service && schema.type(row.responseType))
			p->responses.resetShape(schema, row.responseType, ValueModel::Section::Response, responsesShown);
	}
	p->status = request->rows.size() == 1 ? batchSaid("Sending 1 item in one request…")
	                                        : batchSaid("Sending %L1 items in one request…").arg(request->rows.size());
	p->attention = false;
	p->finishPublication();
	p->signal();
	return true;
}

void DynamicBatch::poll()
{
	auto p = d;
	if (p->publishing || p->model.changing() || p->responses.changing() || p->values.changing())
		return;
	if (!p->request || !p->request->completion)
		return;
	const auto request = p->request;
	const auto completion = std::move(*request->completion);
	p->publishing = true;
	const bool current = p->current() && p->source.storage == request->storage &&
	                     p->source.session && p->source.session() == request->session;
	auto rows = request->rows;
	bool checked = completion.count == int(rows.size());
	for (std::size_t i = 0; checked && i < rows.size(); ++i)
		checked = completion.rows[i].id == rows[i].id &&
		          completion.rows[i].operation == rows[i].operation;
	for (std::size_t i = 0; i < rows.size(); ++i) {
		auto& row = rows[i];
		row.response.clear();
		row.hasValue = false;
		row.result.clear();
		row.historical = !current;
		if (checked) {
			const auto& result = completion.rows[i];
			row.result = result.result;
			row.status = itemText(row.operation, row.result);
			const auto valueStatus = row.result.value(QStringLiteral("valueStatus")).toString();
			if (valueStatus == QStringLiteral("InvalidPayload") ||
			    valueStatus == QStringLiteral("TypeMismatch"))
				row.status += QStringLiteral(" - invalid response value");
			else if (!current || valueStatus == QStringLiteral("Stale"))
				row.status += QStringLiteral(" - value is stale");
			if (current && result.hasValue) {
				row.response = result.payload;
				row.hasValue = true;
			}
		} else
			row.status = completion.status == client::Status::Ok ? batchSaid("No checked outcome")
			                                                      : itemStatusText(completion.status);
	}
	p->model.replace(std::move(rows));
	p->request.reset();
	// The values the item pane showed while it ran, for the cells that changed.
	p->values.noteUpdate();
	p->responses.noteUpdate();
	p->showItem();
	p->values.dropUpdate();
	p->responses.dropUpdate();
	// "Executed 14:02:11 · 3 of 3 done", or the first item not done and why.
	if (completion.status == client::Status::Ok && completion.succeeded && checked) {
		const auto& held = p->model.rows();
		int done = 0, first = -1;
		for (std::size_t i = 0; i < held.size(); ++i)
			if (itemDone(held[i].result))
				++done;
			else if (first < 0)
				first = int(i);
		const auto at = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
		p->status = batchSaid("Executed %1 · %L2 of %L3 done").arg(at).arg(done).arg(int(held.size()));
		if (first >= 0)
			p->status += batchSaid(" · item %L1 %2").arg(first + 1).arg(why(held[std::size_t(first)]).sentence);
		if (completion.valueStatus == QStringLiteral("InvalidPayload") ||
		    completion.valueStatus == QStringLiteral("TypeMismatch"))
			p->status += batchSaid(" · some values in the reply could not be read");
		p->attention = first >= 0;
	} else {
		p->status = completion.status == client::Status::Ok ? batchSaid("The reply checked no item; the items may have run.")
		                                                    : statusText(completion.status);
		p->attention = true;
	}
	p->finishPublication();
	p->signal();
	if (p->finished)
		p->finished();
}

int DynamicBatch::count() const
{
	return int(d->model.rows().size());
}

int DynamicBatch::selection() const
{
	return d->selected;
}

QString DynamicBatch::status() const
{
	// Never empty: what to do with an empty Batch, and that it has not run.
	if (!d->status.isEmpty())
		return d->status;
	return d->model.rows().empty()
	           ? batchSaid("Add Fields, Commands and Services; Execute Batch sends them in one request, in order.")
	           : batchSaid("Not executed yet");
}

bool DynamicBatch::attention() const
{
	return d->attention;
}

QString DynamicBatch::responseEndpoint() const
{
	return d->selected >= 0 && d->selected < count()
	           ? d->model.rows()[std::size_t(d->selected)].name
	           : QString{};
}

BatchListModel* DynamicBatch::model() const
{
	return &d->model;
}

ValueModel* DynamicBatch::responses() const
{
	return &d->responses;
}

ValueModel* DynamicBatch::values() const
{
	return &d->values;
}

const BatchRow* DynamicBatch::selectedRow() const
{
	return d->selected >= 0 && d->selected < count() ? &d->model.rows()[std::size_t(d->selected)] : nullptr;
}

bool DynamicBatch::canEdit() const
{
	return d->rowEditable(d->selected) && d->editor.current();
}

bool DynamicBatch::setScalarText(const QModelIndex& index, const QString& text)
{
	auto p = d;
	if (!canEdit())
		return false;
	const auto node = p->values.node(index);
	if (!node)
		return false;
	const auto info = p->editor.info(node.value);
	if (!info)
		return false;
	bool parsed = false;
	const auto status = detail::setScalarText(p->editor, node.value, info.value.code, text, parsed);
	if (!parsed || status != runtime::Status::Ok)
		return false;
	std::array<std::byte, 4096> out{};
	const auto copied = p->editor.copyDraft(out);
	if (!copied)
		return false;
	const QByteArray bytes(reinterpret_cast<const char*>(out.data()), qsizetype(copied.value));
	p->values.updateScalar(index);
	const auto at = p->selected;
	const auto uid = p->model.rows()[std::size_t(at)].uid;
	const auto name = p->values.data(index, ValueModel::NameRole).toString();
	return p->putRequest(at, bytes,
	                     QCoreApplication::translate("DynamicBatch", "Set %1 of item %L2").arg(name).arg(at + 1),
	                     QStringLiteral("batch.request/%1/%2/%3").arg(uid).arg(index.internalId()).arg(p->editSerial));
}

void DynamicBatch::closeValue()
{
	++d->editSerial;
}

bool DynamicBatch::setRequest(int row, const QByteArray& bytes)
{
	auto p = d;
	if (!p->rowEditable(row))
		return false;
	const auto uid = p->model.rows()[std::size_t(row)].uid;
	if (!p->putRequest(row, bytes,
	                   QCoreApplication::translate("DynamicBatch", "Set the values of item %L1").arg(row + 1),
	                   QStringLiteral("batch.request/%1/all/%2").arg(uid).arg(++p->editSerial)))
		return false;
	if (p->selected == row)
		p->showItem();
	p->signal();
	return true;
}

bool DynamicBatch::replace(int row, BatchRow value, const QString& words)
{
	auto p = d;
	if (!p->rowEditable(row))
		return false;
	const auto before = p->model.rows()[std::size_t(row)];
	value.uid = before.uid;
	value.request = QByteArray(value.request.constData(), value.request.size());
	value.line = lineOf(schema(), value.requestType, value.request);
	p->publishing = true;
	p->model.update(row, value);
	p->selected = row;
	p->showItem();
	p->finishPublication();
	p->record(words, {{Impl::key(before), Impl::fragment(row, before)}}, {{Impl::key(value), Impl::fragment(row, value)}});
	p->signal();
	return true;
}

QString DynamicBatch::lineOf(const runtime::RuntimeSchema& schema, TypeId type, const QByteArray& bytes)
{
	if (type == 0 || bytes.isEmpty() || !schema.valid())
		return {};
	const auto view = schema.view(type, runtime::Input{reinterpret_cast<const std::byte*>(bytes.constData()),
	                                                   std::size_t(bytes.size())});
	if (!view)
		return {};
	const auto one = [&schema](const runtime::DynamicValueView& value) {
		const auto* shown = schema.type(value.typeId());
		if (shown && (shown->kind == TypeKind::Scalar || shown->kind == TypeKind::Enum))
			return schema_text::valueText(schema, value).text;
		return schema_text::summaryText(schema, value);
	};
	const auto* root = schema.type(type);
	if (!root || root->kind != TypeKind::Struct)
		return one(view.value);
	QStringList parts;
	const auto members = schema.members(type);
	for (std::uint32_t at = 0; at < members.size(); ++at)
		if (const auto member = view.value.member(at))
			parts << schema_text::text(members[at].name) + QLatin1Char('=') + one(member.value);
	return parts.join(QLatin1Char(' '));
}
} // namespace telemetry::ux
