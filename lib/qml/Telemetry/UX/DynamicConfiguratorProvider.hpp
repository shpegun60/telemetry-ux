// Independent Qt panel over one borrowed native view. Ruslan Kovtun, codexAi. MIT.
#pragma once
#include "DynamicConfiguratorBinding.hpp"
#include "UndoHistory.hpp"
#include "WatchSample.hpp"
#include "WatchFrame.hpp"
#include <QtCore/QAbstractItemModel>
#include <memory>

namespace telemetry::ux {
struct WatchlistSource;
struct BatchSource;
class EndpointPreviews;

class DynamicConfiguratorProvider;

// The Batch's selected item as Details shows it (BA-4a): the right pane is an
// EndpointPanel over this, under Details' names. Its values are the item's
// request (a Command's arguments, a Service's request, the value a write
// writes), edited live into the item, or the value its read returned; its
// responses a Service's response. An item of an earlier connection shows
// nothing to edit. Made by the provider (batchItem), on its thread.
class BatchItemDetails final : public QObject {
	Q_OBJECT
	// The item's endpoint path, its number (1-based, 0 for none), its
	// operation (read, write, command, service) and its values in one line.
	Q_PROPERTY(QString selection READ selection NOTIFY stateChanged)
	Q_PROPERTY(int item READ item NOTIFY stateChanged)
	Q_PROPERTY(QString operation READ operation NOTIFY stateChanged)
	Q_PROPERTY(QString line READ line NOTIFY stateChanged)
	// {detail: "Service · (f32, f32) -> struct · 8-byte request", writable}
	Q_PROPERTY(QVariantMap selectionInfo READ selectionInfo NOTIFY stateChanged)
	Q_PROPERTY(QAbstractItemModel* values READ values CONSTANT)
	Q_PROPERTY(QAbstractItemModel* responses READ responses CONSTANT)
	Q_PROPERTY(QString serviceResponseEndpoint READ serviceResponseEndpoint NOTIFY stateChanged)
	// False for an item of an earlier connection.
	Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
	Q_PROPERTY(bool historical READ historical NOTIFY stateChanged)
	// The Batch is running.
	Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
	// The item is its own draft: never a Details draft.
	Q_PROPERTY(bool editing READ editing CONSTANT)
	Q_PROPERTY(bool commandSelected READ commandSelected NOTIFY stateChanged)
	Q_PROPERTY(bool serviceSelected READ serviceSelected NOTIFY stateChanged)
	Q_PROPERTY(bool allowMutations READ allowMutations WRITE setAllowMutations NOTIFY stateChanged)
	// A Field that can be read and written: the pane's Read | Write switch.
	Q_PROPERTY(bool switchable READ switchable NOTIFY stateChanged)
	// The item's values take text now.
	Q_PROPERTY(bool canEdit READ canEdit NOTIFY stateChanged)
	// The pane's status line, as DynamicConfiguratorProvider::detailsStatus.
	Q_PROPERTY(QVariantMap detailsStatus READ detailsStatus NOTIFY stateChanged)
	// A Field item's definition, as Details' provider has it: its row under
	// its group and its members from the schema, for its structure before
	// the Batch has read it. No row while the item is not a Field.
	Q_PROPERTY(QAbstractItemModel* definitions READ definitions CONSTANT)
	Q_PROPERTY(QModelIndex definitionRootIndex READ definitionRootIndex NOTIFY stateChanged)
	// What the values start from, under a write's table: "Starts from the
	// last value read." / "Starts at zero; …"; empty on other items.
	Q_PROPERTY(QString valueNote READ valueNote NOTIFY stateChanged)
public:
	explicit BatchItemDetails(DynamicConfiguratorProvider* provider);
	QString selection() const;
	int item() const;
	QString operation() const;
	QString line() const;
	QVariantMap selectionInfo() const;
	QAbstractItemModel* values() const;
	QAbstractItemModel* responses() const;
	QString serviceResponseEndpoint() const;
	bool connected() const;
	bool historical() const;
	bool busy() const;
	static constexpr bool editing() noexcept { return false; }
	bool commandSelected() const;
	bool serviceSelected() const;
	bool allowMutations() const;
	void setAllowMutations(bool allowed);
	bool switchable() const;
	bool canEdit() const;
	QVariantMap detailsStatus() const;
	QAbstractItemModel* definitions() const;
	QModelIndex definitionRootIndex() const;
	QString valueNote() const;
	// A value's text into the item, live; false for text that is no value.
	Q_INVOKABLE bool setScalarText(const QModelIndex& index, const QString& text);
	// A cell was left: the next change of it is a new step.
	Q_INVOKABLE void closeValue();
	// "read" or "write" for a Field item (Read | Write), as one step; a write
	// starts from the value last read, else from the type's initial value.
	Q_INVOKABLE bool setOperation(const QString& operation);

Q_SIGNALS:
	void stateChanged();
	void readFinished(bool published);

private:
	DynamicConfiguratorProvider* provider_;
};

class DynamicConfiguratorProvider final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel* endpoints READ endpoints CONSTANT)
	Q_PROPERTY(bool previewsSupported READ previewsSupported NOTIFY stateChanged)
	// The Device tree's previews (PV-2): how often a cycle reads the Fields in
	// view, 1000 to 10000 ms. One value for every tree on this connection.
	Q_PROPERTY(int previewIntervalMs READ previewIntervalMs WRITE setPreviewIntervalMs NOTIFY stateChanged)
	Q_PROPERTY(QVariantMap selectionInfo READ selectionInfo NOTIFY stateChanged)
	Q_PROPERTY(QAbstractItemModel* definitions READ definitions CONSTANT)
	Q_PROPERTY(QModelIndex definitionRootIndex READ definitionRootIndex NOTIFY stateChanged)
	Q_PROPERTY(QAbstractItemModel* values READ values CONSTANT)
	Q_PROPERTY(QAbstractItemModel* responses READ responses CONSTANT)
	Q_PROPERTY(QAbstractItemModel* batchItems READ batchItems CONSTANT)
	// The selected Batch item as Details shows it (a BatchItemDetails).
	Q_PROPERTY(QObject* batchItem READ batchItem CONSTANT)
	Q_PROPERTY(bool batchSupported READ batchSupported NOTIFY stateChanged)
	Q_PROPERTY(bool batchPending READ batchPending NOTIFY stateChanged)
	Q_PROPERTY(bool canExecuteBatch READ canExecuteBatch NOTIFY stateChanged)
	Q_PROPERTY(int batchCount READ batchCount NOTIFY stateChanged)
	Q_PROPERTY(int batchSelection READ batchSelection NOTIFY stateChanged)
	Q_PROPERTY(QString batchStatus READ batchStatus NOTIFY stateChanged)
	// The status line asks for attention: a run failed, or an item was not done.
	Q_PROPERTY(bool batchAttention READ batchAttention NOTIFY stateChanged)
	Q_PROPERTY(QAbstractItemModel* watchlist READ watchlist CONSTANT)
	// The Watchlist's rows as shown: a list of whole paths or a tree grouped
	// by path (a WatchRows over watchlist). Made here, so a host needs no
	// registered type for the panel's two views.
	Q_PROPERTY(QObject* watchRows READ watchRows CONSTANT)
	// Every watched value's last ten minutes of readings, and which of them
	// the live Plot shows (a WatchTraces, LP-1). It outlives the Watchlist's
	// rows: a value removed keeps its readings.
	Q_PROPERTY(QObject* watchTraces READ watchTraces CONSTANT)
	Q_PROPERTY(bool watchReadManySupported READ watchReadManySupported NOTIFY stateChanged)
	Q_PROPERTY(bool watchRequiresExplicitIndividual READ watchRequiresExplicitIndividual CONSTANT)
	Q_PROPERTY(bool watchActive READ watchActive NOTIFY stateChanged)
	Q_PROPERTY(bool watchPending READ watchPending NOTIFY stateChanged)
	Q_PROPERTY(bool canAddWatch READ canAddWatch NOTIFY stateChanged)
	Q_PROPERTY(int watchCount READ watchCount NOTIFY stateChanged)
	// Some row is on this device; false when every row's path is one it lacks.
	Q_PROPERTY(bool watchReadable READ watchReadable NOTIFY stateChanged)
	Q_PROPERTY(QString watchStatus READ watchStatus NOTIFY stateChanged)
	// How polling keeps pace with its interval (WL-6), as WatchPace: cycles,
	// polling, routine, values, readMany, batch, fieldRead, again, lastCycleMs,
	// medianCycleMs, medianPeriodMs, achievedPeriodMs, intervalMs,
	// skippedTicks, behind. Empty before a cycle has completed.
	Q_PROPERTY(QVariantMap watchPace READ watchPace NOTIFY stateChanged)
	Q_PROPERTY(bool canWriteWatch READ canWriteWatch NOTIFY stateChanged)
	// How the Watchlist shows its numbers: "original", "dec" or "hex" (WL-3).
	// The view's only: the recording, Trend and a write are the same in all.
	Q_PROPERTY(QString watchFormat READ watchFormat WRITE setWatchFormat NOTIFY stateChanged)
	// Watched rows shown in a format of their own, other than the Watchlist's
	// (WL-3c): the panel marks them by their count.
	Q_PROPERTY(int watchOwnFormatCount READ watchOwnFormatCount NOTIFY stateChanged)
	Q_PROPERTY(bool watchWriting READ watchWriting NOTIFY stateChanged)
	Q_PROPERTY(QString watchWriteOutcome READ watchWriteOutcome NOTIFY stateChanged)
	// What the outcome is (WL-5): "writing", "confirmed" (read back as
	// written), "differs" (the device reads back another value),
	// "unconfirmed" (written, not read back, or its reply lost), "refused",
	// or "" before any.
	Q_PROPERTY(QString watchWriteKind READ watchWriteKind NOTIFY stateChanged)
	// When the outcome came, in ms since the epoch (WL-9 C5); 0 while a
	// write runs and before any. The panel flashes the time once per outcome.
	Q_PROPERTY(double watchWriteAt READ watchWriteAt NOTIFY stateChanged)
	Q_PROPERTY(
	    bool watchIndividual READ watchIndividual WRITE setWatchIndividual NOTIFY stateChanged)
	Q_PROPERTY(
	    int watchIntervalMs READ watchIntervalMs WRITE setWatchIntervalMs NOTIFY stateChanged)
	// The smallest interval accepted: 0 reads continuously at low priority.
	Q_PROPERTY(int watchIntervalMinimumMs READ watchIntervalMinimumMs CONSTANT)
	// The Device tree's watch boxes: a number that moves whenever a box may show
	// something else (the Watchlist's Fields or the tree changed), and the most
	// Fields the Watchlist holds.
	Q_PROPERTY(int watchRevision READ watchRevision NOTIFY stateChanged)
	Q_PROPERTY(int watchLimit READ watchLimit CONSTANT)
	// Where the Watchlist's changes go as steps to undo (UR-1): rows added and
	// removed ("watch.rows", by path, with their places and own formats) and
	// number formats chosen ("watch.format"), the live Plot's curves
	// (watchTraces' "plot.*") and the Batch's items ("batch.items"). A host
	// makes one UndoHistory and gives it; null, the default, records nothing.
	Q_PROPERTY(telemetry::ux::UndoHistory* history READ history WRITE setHistory NOTIFY historyChanged)
	// The Watchlist takes changes (boxes, add, remove, Undo, Clear) while it
	// polls, between two reads. A provider without this property is locked
	// while polling.
	Q_PROPERTY(bool watchChangesWhilePolling READ watchChangesWhilePolling CONSTANT)
	Q_PROPERTY(QString selection READ selection NOTIFY stateChanged)
	Q_PROPERTY(QModelIndex selectedEndpointIndex READ selectedEndpointIndex NOTIFY stateChanged)
	Q_PROPERTY(QString serviceResponseEndpoint READ serviceResponseEndpoint NOTIFY stateChanged)
	Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
	Q_PROPERTY(bool editing READ editing NOTIFY stateChanged)
	Q_PROPERTY(bool selectionRequiresDiscard READ selectionRequiresDiscard NOTIFY stateChanged)
	Q_PROPERTY(bool allowMutations READ allowMutations WRITE setAllowMutations NOTIFY stateChanged)
	Q_PROPERTY(bool canEdit READ canEdit NOTIFY stateChanged)
	Q_PROPERTY(bool canApply READ canApply NOTIFY stateChanged)
	Q_PROPERTY(bool canRefresh READ canRefresh NOTIFY stateChanged)
	Q_PROPERTY(bool commandSelected READ commandSelected NOTIFY stateChanged)
	Q_PROPERTY(bool canExecute READ canExecute NOTIFY stateChanged)
	Q_PROPERTY(bool serviceSelected READ serviceSelected NOTIFY stateChanged)
	Q_PROPERTY(bool canCallService READ canCallService NOTIFY stateChanged)
	Q_PROPERTY(QString writeOutcome READ writeOutcome NOTIFY stateChanged)
	Q_PROPERTY(QString commandOutcome READ commandOutcome NOTIFY stateChanged)
	Q_PROPERTY(QString serviceOutcome READ serviceOutcome NOTIFY stateChanged)
	Q_PROPERTY(bool writeOutcomeNeedsAttention READ writeOutcomeNeedsAttention NOTIFY stateChanged)
	Q_PROPERTY(bool commandOutcomeNeedsAttention READ commandOutcomeNeedsAttention NOTIFY stateChanged)
	Q_PROPERTY(bool serviceOutcomeNeedsAttention READ serviceOutcomeNeedsAttention NOTIFY stateChanged)
	Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
	// Details' status line for the selection (family spec C, Details layout
	// §4): {text, failed, tip}. "Read 12:04:31 · took 9 ms", "Edited, not
	// applied · 2 changes", "Applied 12:05:02 · read back", "Executed
	// 12:05:10 · took 4 ms", "Executed 12:05:10 · accepted · took 4 ms",
	// "Called 12:05:21 · took 3 ms", "Reading…", "Not called yet", or "Read
	// failed 12:06:07 · <why>" with failed true. Empty with nothing selected.
	Q_PROPERTY(QVariantMap detailsStatus READ detailsStatus NOTIFY stateChanged)
	Q_PROPERTY(QString presentationError READ presentationError NOTIFY stateChanged)
public:
	explicit DynamicConfiguratorProvider(DynamicConfiguratorBinding* binding = nullptr,
	                                     QObject* parent = nullptr);
	~DynamicConfiguratorProvider() override;
	void setBinding(DynamicConfiguratorBinding* binding);
	QAbstractItemModel* endpoints() const;
	QVariantMap selectionInfo() const;
	QAbstractItemModel* definitions() const;
	QModelIndex definitionRootIndex() const;
	bool previewsSupported() const;
	int previewIntervalMs() const;
	void setPreviewIntervalMs(int ms);
	// Read the Fields in view now, without waiting for the interval.
	Q_INVOKABLE void readPreviewsNow();
	Q_INVOKABLE bool setPreviewDemand(QObject* view, const QVariantList& indexes);
	Q_INVOKABLE void clearPreviewDemand(QObject* view);
	QAbstractItemModel* values() const;
	QAbstractItemModel* responses() const;
	QAbstractItemModel* batchItems() const;
	QObject* batchItem() const;
	bool batchSupported() const;
	bool batchPending() const;
	bool canExecuteBatch() const;
	int batchCount() const;
	int batchSelection() const;
	QString batchStatus() const;
	bool batchAttention() const;
	Q_INVOKABLE bool removeBatch(int row);
	Q_INVOKABLE bool moveBatch(int row, int delta);
	// The item at row dragged to place to (BA-4c): one undo step.
	Q_INVOKABLE bool moveBatchTo(int row, int to);
	Q_INVOKABLE bool clearBatch();
	Q_INVOKABLE bool selectBatch(int row);
	Q_INVOKABLE bool executeBatch();
	// Adding to the Batch by name or path (BA-1): a Field is read (a
	// write-only one written), a Command runs and a Service is called, its
	// request at its initial values, set on the item (BA-4b).
	// checkBatchPath: {status: adds; partial (half typed - Enter takes the
	// first suggestion, path); choose; missing; error; unsupported (no Batch
	// on this device); permission
	// (changes are off); previous (items of an earlier connection held); busy;
	// full; or empty; message; path; operation;
	// item (the place it would take); also (an item that already has it, 0 if
	// none)}.
	Q_INVOKABLE QVariantMap checkBatchPath(const QString& text) const;
	// What may follow typed text, under the headings Fields, Commands and
	// Services, in that order (BA-4b): a heading is {heading: true, path (the
	// kind's word, "Commands"), completion ("command "), category, count
	// (found), shown}; a candidate is {path, completion (what to put in the
	// field), kind, kindCode, typeText, operation, input, note, section}.
	// Before them, in the Watchlist's grammar (WL-10a): the groups the text
	// goes on to, one level at a time, {path "demo/adc/", completion,
	// kindCode "group", typeText (what it holds, "2 Fields, 1 Command"),
	// values (its Fields), container true}; and inside a Field its members
	// and elements, {path, completion, kindCode "member", typeText, container,
	// next ("." or "[" into a container)}, each adding a read of the Field.
	// Empty text lists up to eight of each kind. A leading kind word ("field",
	// "command", "service", singular or plural; "read") lists that kind only.
	Q_INVOKABLE QVariantList batchPathCandidates(const QString& text) const;
	// Every line, in order, or none, as one step: {status: ok, refused,
	// unsupported, permission, previous, busy or full; added; first (the first
	// new item); items: [{path, operation, what}]; refused: [{line, path,
	// reason}]; message; serial (the step's, 0 when none was recorded)}.
	Q_INVOKABLE QVariantMap addBatchPaths(const QStringList& lines);
	// How many items addBatchPaths would add for these lines: a Field once
	// however many of its members are named, a group as its Fields.
	Q_INVOKABLE int batchItemCount(const QStringList& lines) const;
	QAbstractItemModel* watchlist() const;
	QObject* watchRows() const;
	QObject* watchTraces() const;
	bool watchReadManySupported() const;

	bool watchRequiresExplicitIndividual() const
	{
		return true;
	}

	bool watchActive() const;
	bool watchPending() const;
	bool canAddWatch() const;
	int watchCount() const;
	bool watchReadable() const;
	QString watchStatus() const;
	QVariantMap watchPace() const;
	bool watchIndividual() const;
	int watchIntervalMs() const;
	static constexpr int watchIntervalMinimumMs() noexcept { return 0; }
	void setWatchIndividual(bool individual);
	void setWatchIntervalMs(int milliseconds);
	Q_INVOKABLE bool addWatch();
	Q_INVOKABLE bool watchCandidate(const QModelIndex& index) const;
	Q_INVOKABLE QVariantMap addWatchMany(const QVariantList& sourceIndexes);
	Q_INVOKABLE void removeWatch(int row);
	// Stops watching the Watchlist's rows given (a tree node's values) as one
	// step: {status: ok, invalid or refused, changed, serial (the step's, 0
	// when none was recorded)}.
	Q_INVOKABLE QVariantMap removeWatchRows(const QVariantList& rows);
	QString watchFormat() const;
	// The Watchlist's format as a project or host sets it: no step.
	void setWatchFormat(const QString& format);
	// The Watchlist's format as the panel's user chooses it (WL-2c): one
	// step, merged with the next choice within two seconds. false for an
	// unknown format.
	Q_INVOKABLE bool chooseWatchFormat(const QString& format);
	// Gives rows a format of their own ("original", "dec", "hex"), or "" to
	// follow the Watchlist's, as one step; false, and nothing changed, for an
	// unknown format or a row that is not one.
	Q_INVOKABLE bool setWatchRowFormat(const QVariantList& rows, const QString& format);
	// The formats rows have of their own by path, {"device.Config.Gain":
	// "hex"}, as a project saves them; setWatchRowFormats puts them back, for
	// rows watched now and rows added later.
	Q_INVOKABLE QVariantMap watchRowFormats() const;
	// Whether any watched value is shown other than as Original: the
	// Watchlist's format, or a row's own that is not Original.
	Q_INVOKABLE bool watchFormatsDiffer() const;
	int watchOwnFormatCount() const;
	// Every value back to Original (WL-3b): the Watchlist's format and every
	// format of a row's own, those kept for paths not watched now too, as one
	// step. {status: ok, unchanged (nothing to reset) or refused, rows: the
	// watched rows that were shown in a format of their own, as
	// watchOwnFormatCount counts them, serial}.
	Q_INVOKABLE QVariantMap resetWatchFormats();
	// The Watchlist's "+" row (WL-4b). What a typed path would add, in words
	// for the line under it: {status: "adds", "watched", "missing", "error"
	// or "empty", message, adds, already, row (a watched value's row, or -1)}.
	Q_INVOKABLE QVariantMap checkWatchPath(const QString& text) const;
	// What may come next in a typed path, at most 50:
	// [{path, typeText, values, container, watched}], descriptor order.
	Q_INVOKABLE QVariantList watchPathCandidates(const QString& text) const;
	// Adds what each line names - a path ("device.Config.Gain", or a struct or
	// array for all its values) or a line copied from the Watchlist
	// ("path = value", "path<Tab>value") - as one step: {status: ok, capacity
	// or refused, added, already, missing: [paths], refused: [{path, reason}],
	// serial}.
	Q_INVOKABLE QVariantMap addWatchPaths(const QStringList& lines);
	// Drag and drop with nested groups (the designer's section 16). items are
	// the rows picked in the Device tree, [{kind, path}], kind "group",
	// "field", "member", "element", "command" or "service"; target is
	// "watchlist", "plot" or "batch". A group gives everything under it at any
	// depth; a row under a picked group or Field counts once; tree order.
	// The Watchlist and the Plot take values and leave Commands and Services
	// out; the Batch takes one item per Field, and a Command or a Service only
	// when it was picked by itself. All or nothing: no part of a drop that
	// does not fit is added.
	// dropPlan says what a drop would do, for the target's frame while it is
	// dragged over: {status: ok, held (everything watched already), refused
	// or empty; count (values, or Batch items); already; joins (values new
	// to the Watchlist, for the Plot); words ("Drop to add 312 values from
	// power · 6 already watched"); refusal (why not, in words); leftOut
	// ("3 Commands and 1 Service"); from; lines; values (the Plot's)}.
	Q_INVOKABLE QVariantMap dropPlan(const QVariantList& items, const QString& target) const;
	// Does it as one step - a drop on the Plot adds its values to the
	// Watchlist and plots them in one Undo: the plan with {added, serial,
	// message ("Added 312 values from power · 3 Commands and 1 Service are
	// not values"), rows (held: the Watchlist rows already holding it)}.
	Q_INVOKABLE QVariantMap dropOn(const QVariantList& items, const QString& target);
	// A saved Watchlist back in its order (WL-4c): every row this device has,
	// and the rest as rows that say they are not on it, found again on a
	// device that has them. A restore, never a step. {status, added, missing:
	// [positions of the rows not on this device]}.
	Q_INVOKABLE QVariantMap restoreWatchBindings(const QVariantList& bindings);
	Q_INVOKABLE bool setWatchRowFormats(const QVariantMap& formats);
	// While a row's value is typed (WL-3b): {preview: "= 0x40000000" - the
	// other form of what Enter would write, hex for a number and the value for
	// hex - or error: why it can't be, the type's range for a number past it}.
	// Both empty before the row's first reading, for an enum and for nothing typed.
	Q_INVOKABLE QVariantMap watchEditPreview(int row, const QString& text) const;
	// Every row out, as one step ("Clear the Watchlist") that puts them back
	// in their order with their formats.
	Q_INVOKABLE void clearWatch();
	int watchRevision() const;
	UndoHistory* history() const;
	void setHistory(UndoHistory* history);
	static constexpr int watchLimit() noexcept { return watchlistRowLimit; }
	static constexpr bool watchChangesWhilePolling() noexcept { return true; }
	// A tree row's watch box: -1 when nothing at or below it can be watched
	// (a Command, a Service, a Field the Watchlist cannot hold, a stale index);
	// otherwise Qt::Unchecked, Qt::PartiallyChecked or Qt::Checked as an int.
	Q_INVOKABLE int watchState(const QModelIndex& index) const;
	// Why a Field row's box is off while watchState is -1, in words: a
	// struct or array, one of its members, a write-only Field, or a group with
	// nothing the Watchlist can hold. Empty where the box is on, for Commands
	// and Services, and while the tree is changing.
	Q_INVOKABLE QString watchRefusal(const QModelIndex& index) const;
	// setWatchMany's map also holds `values` (a struct's or array's values were
	// asked) and, when adding, `adding`: how many rows it would add.
	// What a row's box counts, for its words: {watched, total, kind}, kind
	// "members", "elements", "values" (a struct or an array, or a group with
	// one below) or "fields"; empty where there is no box.
	Q_INVOKABLE QVariantMap watchTally(const QModelIndex& index) const;
	// The Watchlist as a project saves it: each row in order as {group, name}
	// (a whole Field) or {group, name, member} (a value inside one); empty
	// when it cannot tell. addWatchBindings puts such rows after the others in
	// one change: {status, added (values), duplicate, capacity, missing:
	// [positions of entries this device has no value for]}; each of those is
	// kept as a row by its path (WL-4c), saying why, once. A restore, never a
	// step.
	Q_INVOKABLE QVariantList watchBindings() const;
	Q_INVOKABLE QVariantMap addWatchBindings(const QVariantList& bindings);
	// Watches, or stops watching, every Field the Watchlist can hold at or
	// under each index, in tree order, as one step: {status: ok, refused,
	// invalid, stale or capacity, changed, unchanged, fields, room, serial}.
	// At capacity nothing changes; room says how many more fit.
	Q_INVOKABLE QVariantMap setWatchMany(const QVariantList& sourceIndexes, bool watched);
	Q_INVOKABLE void refreshWatch();
	Q_INVOKABLE void startWatch();
	Q_INVOKABLE void stopWatch();
	// Writes `text` to the watched Field in `row`, as Details would: its own
	// read, an exact draft, the write and a confirming read. Never moves the
	// Details selection. Refused without permission, for a Field that is not
	// writable, or while another row write is in flight.
	Q_INVOKABLE bool writeWatch(int row, const QString& text);
	// A text that is no value, left in its row by Tab or a click away (WL-9
	// C4), held there with its reason until a write of the row succeeds, Esc
	// on the row (dropWatchHeld), the row goes or another device attaches. A
	// refused write is held by the provider itself.
	Q_INVOKABLE bool holdWatchText(int row, const QString& text, const QString& reason);
	Q_INVOKABLE bool dropWatchHeld(int row);
	bool canWriteWatch() const;
	bool watchWriting() const;
	QString watchWriteOutcome() const;
	QString watchWriteKind() const;
	double watchWriteAt() const;
	QString selection() const;
	QModelIndex selectedEndpointIndex() const;
	QString serviceResponseEndpoint() const;
	bool connected() const;
	bool busy() const;
	bool editing() const;
	// Includes invalid user text; a host may ask before changing endpoints.
	bool selectionRequiresDiscard() const;
	bool allowMutations() const;
	void setAllowMutations(bool allowed);
	bool canEdit() const;
	bool canApply() const;
	bool canRefresh() const;
	bool commandSelected() const;
	bool canExecute() const;
	bool serviceSelected() const;
	bool canCallService() const;
	QString writeOutcome() const;
	QString commandOutcome() const;
	QString serviceOutcome() const;
	bool writeOutcomeNeedsAttention() const;
	bool commandOutcomeNeedsAttention() const;
	bool serviceOutcomeNeedsAttention() const;
	QString statusText() const;
	QVariantMap detailsStatus() const;
	Q_INVOKABLE bool setPresentation(const QByteArray& json);
	QString presentationError() const;
	Q_INVOKABLE bool selectEndpoint(const QModelIndex& index);
	Q_INVOKABLE bool refresh();
	Q_INVOKABLE bool beginEdit();
	Q_INVOKABLE void cancelEdit();
	Q_INVOKABLE bool apply();
	Q_INVOKABLE bool executeCommand();
	Q_INVOKABLE bool callService();
	Q_INVOKABLE bool setScalarText(const QModelIndex& index, const QString& text);
	// Asks every Device tree on this provider to show a group and make it
	// its current row (Details' breadcrumb): its path in the one grammar,
	// "metrology/harmonics". No I/O.
	Q_INVOKABLE void revealGroup(const QString& path);

Q_SIGNALS:
	void stateChanged();
	void historyChanged();
	void revealGroupRequested(const QString& path);
	// Direct owner-thread publication of one accepted terminal Watch request.
	// Owns exact bytes; Current describes source validity, not atomic UI commit.
	void watchCompleted(const telemetry::ux::WatchFrame& frame);
	void readFinished(bool published);
	void applyFinished(bool confirmed);
	void commandFinished(bool executed, bool accepted);
	void serviceFinished(bool succeeded, bool published);
	void batchFinished();
	// A write from a Watchlist row finished; `confirmed` when the device
	// accepted it and a fresh read published the value.
	void watchWriteFinished(bool confirmed);

private:
	friend class QtConnectionBinding;
	friend class BatchItemDetails;
	friend class ConnectionViews;
	void setEndpointPreviews(EndpointPreviews* previews);
	void setWatchSource(WatchlistSource source);
	void pollWatch();
	void setBatchSource(BatchSource source);
	void pollBatch();
	bool canSetPresentation() const;
	void setPresentationOverlay(const PresentationOverlay& overlay);
	// restoreWatchBindings (replace) and addWatchBindings (after the rows).
	QVariantMap putWatchBindings(const QVariantList& bindings, bool replace);
	struct Impl;
	// A model notification may delete the provider. In-flight methods retain the
	// models until their structural notification returns; native leases are host-owned.
	std::shared_ptr<Impl> d;
};
} // namespace telemetry::ux
