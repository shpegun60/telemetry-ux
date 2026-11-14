// Independent Qt panel over one borrowed native view. Ruslan Kovtun, codexAi. MIT.
#pragma once
#include "DynamicConfiguratorBinding.hpp"
#include "WatchSample.hpp"
#include "WatchFrame.hpp"
#include <QtCore/QAbstractItemModel>
#include <memory>

namespace telemetry::ux {
struct WatchlistSource;
struct BatchSource;
class EndpointPreviews;

class DynamicConfiguratorProvider final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel* endpoints READ endpoints CONSTANT)
	Q_PROPERTY(bool previewsSupported READ previewsSupported NOTIFY stateChanged)
	Q_PROPERTY(QVariantMap selectionInfo READ selectionInfo NOTIFY stateChanged)
	Q_PROPERTY(QAbstractItemModel* definitions READ definitions CONSTANT)
	Q_PROPERTY(QModelIndex definitionRootIndex READ definitionRootIndex NOTIFY stateChanged)
	Q_PROPERTY(QAbstractItemModel* values READ values CONSTANT)
	Q_PROPERTY(QAbstractItemModel* responses READ responses CONSTANT)
	Q_PROPERTY(QAbstractItemModel* batchItems READ batchItems CONSTANT)
	Q_PROPERTY(QAbstractItemModel* batchResponses READ batchResponses CONSTANT)
	Q_PROPERTY(bool batchSupported READ batchSupported NOTIFY stateChanged)
	Q_PROPERTY(bool batchPending READ batchPending NOTIFY stateChanged)
	Q_PROPERTY(bool canExecuteBatch READ canExecuteBatch NOTIFY stateChanged)
	Q_PROPERTY(int batchCount READ batchCount NOTIFY stateChanged)
	Q_PROPERTY(int batchSelection READ batchSelection NOTIFY stateChanged)
	Q_PROPERTY(QString batchStatus READ batchStatus NOTIFY stateChanged)
	Q_PROPERTY(QString batchResponseEndpoint READ batchResponseEndpoint NOTIFY stateChanged)
	Q_PROPERTY(QAbstractItemModel* watchlist READ watchlist CONSTANT)
	// The Watchlist's rows as shown: a list of whole paths or a tree grouped
	// by path (a WatchRows over watchlist). Made here, so a host needs no
	// registered type for the panel's two views.
	Q_PROPERTY(QObject* watchRows READ watchRows CONSTANT)
	Q_PROPERTY(bool watchReadManySupported READ watchReadManySupported NOTIFY stateChanged)
	Q_PROPERTY(bool watchRequiresExplicitIndividual READ watchRequiresExplicitIndividual CONSTANT)
	Q_PROPERTY(bool watchActive READ watchActive NOTIFY stateChanged)
	Q_PROPERTY(bool watchPending READ watchPending NOTIFY stateChanged)
	Q_PROPERTY(bool canAddWatch READ canAddWatch NOTIFY stateChanged)
	Q_PROPERTY(int watchCount READ watchCount NOTIFY stateChanged)
	// Some row is on this device; false when every row's path is one it lacks.
	Q_PROPERTY(bool watchReadable READ watchReadable NOTIFY stateChanged)
	Q_PROPERTY(QString watchStatus READ watchStatus NOTIFY stateChanged)
	Q_PROPERTY(bool canWriteWatch READ canWriteWatch NOTIFY stateChanged)
	// How the Watchlist shows its numbers: "original", "dec" or "hex" (WL-3).
	// The view's only: the recording, Trend and a write are the same in all.
	Q_PROPERTY(QString watchFormat READ watchFormat WRITE setWatchFormat NOTIFY stateChanged)
	Q_PROPERTY(bool watchWriting READ watchWriting NOTIFY stateChanged)
	Q_PROPERTY(QString watchWriteOutcome READ watchWriteOutcome NOTIFY stateChanged)
	Q_PROPERTY(
	    bool watchIndividual READ watchIndividual WRITE setWatchIndividual NOTIFY stateChanged)
	Q_PROPERTY(
	    int watchIntervalMs READ watchIntervalMs WRITE setWatchIntervalMs NOTIFY stateChanged)
	// The smallest interval accepted: 0 reads continuously at low priority.
	Q_PROPERTY(int watchIntervalMinimumMs READ watchIntervalMinimumMs CONSTANT)
	// The Device tree's watch boxes: a number that moves whenever a box may show
	// something else (the Watchlist's Fields or the tree changed), whether the
	// last change made through setWatchMany can be undone, and the most Fields
	// the Watchlist holds.
	Q_PROPERTY(int watchRevision READ watchRevision NOTIFY stateChanged)
	Q_PROPERTY(bool canUndoWatch READ canUndoWatch NOTIFY stateChanged)
	Q_PROPERTY(int watchLimit READ watchLimit CONSTANT)
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
	Q_INVOKABLE bool setPreviewDemand(QObject* view, const QVariantList& indexes);
	Q_INVOKABLE void clearPreviewDemand(QObject* view);
	QAbstractItemModel* values() const;
	QAbstractItemModel* responses() const;
	QAbstractItemModel* batchItems() const;
	QAbstractItemModel* batchResponses() const;
	bool batchSupported() const;
	bool batchPending() const;
	bool canExecuteBatch() const;
	int batchCount() const;
	int batchSelection() const;
	QString batchStatus() const;
	QString batchResponseEndpoint() const;
	Q_INVOKABLE bool canAddBatch(const QString& operation) const;
	Q_INVOKABLE bool addBatch(const QString& operation);
	Q_INVOKABLE bool removeBatch(int row);
	Q_INVOKABLE bool moveBatch(int row, int delta);
	Q_INVOKABLE bool clearBatch();
	Q_INVOKABLE bool selectBatch(int row);
	Q_INVOKABLE bool executeBatch();
	// Adding to the Batch by name or path (BA-1): a Field is read, a Command
	// runs and a Service is called - those that take no input; one that does
	// is selected for Details instead. checkBatchPath: {status: adds; partial
	// (half typed - Enter takes the first suggestion, path); choose; input;
	// missing; error; unsupported (no Batch on this device); permission
	// (changes are off); previous (items of an earlier connection held); busy;
	// full; or empty; message; path; operation;
	// item (the place it would take); also (an item that already has it, 0 if
	// none)}.
	Q_INVOKABLE QVariantMap checkBatchPath(const QString& text) const;
	// What may follow typed text: [{path, completion (what to put in the
	// field), kind, typeText, operation, input, note, container, next}].
	Q_INVOKABLE QVariantList batchPathCandidates(const QString& text) const;
	// Every line, in order, or none: {status: ok, refused, unsupported,
	// permission, previous, busy or full; added; first (the first new item);
	// items: [{path, operation, what}]; refused: [{line, path, reason}];
	// message}.
	Q_INVOKABLE QVariantMap addBatchPaths(const QStringList& lines);
	// The endpoint one line names, selected in the Device tree (for one that
	// takes input). False when the line names none, or several.
	Q_INVOKABLE bool selectBatchPath(const QString& text);
	QAbstractItemModel* watchlist() const;
	QObject* watchRows() const;
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
	// change, which undoWatch takes back: {status: ok, invalid or refused,
	// changed}.
	Q_INVOKABLE QVariantMap removeWatchRows(const QVariantList& rows);
	QString watchFormat() const;
	void setWatchFormat(const QString& format);
	// Gives rows a format of their own ("original", "dec", "hex"), or "" to
	// follow the Watchlist's; false, and nothing changed, for an unknown
	// format or a row that is not one.
	Q_INVOKABLE bool setWatchRowFormat(const QVariantList& rows, const QString& format);
	// The formats rows have of their own by path, {"device.Config.Gain":
	// "hex"}, as a project saves them; setWatchRowFormats puts them back, for
	// rows watched now and rows added later.
	Q_INVOKABLE QVariantMap watchRowFormats() const;
	// The Watchlist's "+" row (WL-4b). What a typed path would add, in words
	// for the line under it: {status: "adds", "watched", "missing", "error"
	// or "empty", message, adds, already, row (a watched value's row, or -1)}.
	Q_INVOKABLE QVariantMap checkWatchPath(const QString& text) const;
	// What may come next in a typed path, at most 50:
	// [{path, typeText, values, container, watched}], descriptor order.
	Q_INVOKABLE QVariantList watchPathCandidates(const QString& text) const;
	// Adds what each line names - a path ("device.Config.Gain", or a struct or
	// array for all its values) or a line copied from the Watchlist
	// ("path = value", "path<Tab>value") - as one change Undo takes back:
	// {status: ok, capacity or refused, added, already, missing: [paths],
	// refused: [{path, reason}]}.
	Q_INVOKABLE QVariantMap addWatchPaths(const QStringList& lines);
	// A saved Watchlist back in its order, as one change (WL-4c): every row
	// this device has, and the rest as rows that say they are not on it,
	// found again on a device that has them. {status, added, missing:
	// [positions of the rows not on this device]}.
	Q_INVOKABLE QVariantMap restoreWatchBindings(const QVariantList& bindings);
	Q_INVOKABLE bool setWatchRowFormats(const QVariantMap& formats);
	// While a row's value is typed (WL-3b): {preview: "= 0x40000000" - the
	// other form of what Enter would write, hex for a number and the value for
	// hex - or error: why it can't be, the type's range for a number past it}.
	// Both empty before the row's first reading, for an enum and for nothing typed.
	Q_INVOKABLE QVariantMap watchEditPreview(int row, const QString& text) const;
	Q_INVOKABLE void clearWatch();
	int watchRevision() const;
	bool canUndoWatch() const;
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
	// kept as a row by its path (WL-4c), saying why, once.
	Q_INVOKABLE QVariantList watchBindings() const;
	Q_INVOKABLE QVariantMap addWatchBindings(const QVariantList& bindings);
	// Watches, or stops watching, every Field the Watchlist can hold at or
	// under each index, in tree order, as one change: {status: ok, refused,
	// invalid, stale or capacity, changed, unchanged, fields, room}. At
	// capacity nothing changes; room says how many more fit.
	Q_INVOKABLE QVariantMap setWatchMany(const QVariantList& sourceIndexes, bool watched);
	// Undoes the last change setWatchMany made, while nothing else has changed
	// the Watchlist or the tree since: {status, changed}.
	Q_INVOKABLE QVariantMap undoWatch();
	Q_INVOKABLE void refreshWatch();
	Q_INVOKABLE void startWatch();
	Q_INVOKABLE void stopWatch();
	// Writes `text` to the watched Field in `row`, as Details would: its own
	// read, an exact draft, the write and a confirming read. Never moves the
	// Details selection. Refused without permission, for a Field that is not
	// writable, or while another row write is in flight.
	Q_INVOKABLE bool writeWatch(int row, const QString& text);
	bool canWriteWatch() const;
	bool watchWriting() const;
	QString watchWriteOutcome() const;
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

Q_SIGNALS:
	void stateChanged();
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
