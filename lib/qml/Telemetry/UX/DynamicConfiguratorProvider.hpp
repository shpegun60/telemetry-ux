// Independent Qt panel over one borrowed native view. Ruslan Kovtun, codexAi. MIT.
#pragma once
#include "DynamicConfiguratorBinding.hpp"
#include "WatchSample.hpp"
#include <QtCore/QAbstractItemModel>
#include <memory>

namespace telemetry::ux {
struct WatchlistSource;
struct BatchSource;

class DynamicConfiguratorProvider final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel* endpoints READ endpoints CONSTANT)
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
	Q_PROPERTY(bool watchReadManySupported READ watchReadManySupported NOTIFY stateChanged)
	Q_PROPERTY(bool watchRequiresExplicitIndividual READ watchRequiresExplicitIndividual CONSTANT)
	Q_PROPERTY(bool watchActive READ watchActive NOTIFY stateChanged)
	Q_PROPERTY(bool watchPending READ watchPending NOTIFY stateChanged)
	Q_PROPERTY(bool canAddWatch READ canAddWatch NOTIFY stateChanged)
	Q_PROPERTY(int watchCount READ watchCount NOTIFY stateChanged)
	Q_PROPERTY(QString watchStatus READ watchStatus NOTIFY stateChanged)
	Q_PROPERTY(bool canWriteWatch READ canWriteWatch NOTIFY stateChanged)
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
	Q_PROPERTY(QString selection READ selection NOTIFY stateChanged)
	Q_PROPERTY(QModelIndex selectedEndpointIndex READ selectedEndpointIndex NOTIFY stateChanged)
	Q_PROPERTY(QString serviceResponseEndpoint READ serviceResponseEndpoint NOTIFY stateChanged)
	Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
	Q_PROPERTY(bool editing READ editing NOTIFY stateChanged)
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
	Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
	Q_PROPERTY(QString presentationError READ presentationError NOTIFY stateChanged)
public:
	explicit DynamicConfiguratorProvider(DynamicConfiguratorBinding* binding = nullptr,
	                                     QObject* parent = nullptr);
	~DynamicConfiguratorProvider() override;
	void setBinding(DynamicConfiguratorBinding* binding);
	QAbstractItemModel* endpoints() const;
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
	QAbstractItemModel* watchlist() const;
	bool watchReadManySupported() const;

	bool watchRequiresExplicitIndividual() const
	{
		return true;
	}

	bool watchActive() const;
	bool watchPending() const;
	bool canAddWatch() const;
	int watchCount() const;
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
	Q_INVOKABLE void clearWatch();
	int watchRevision() const;
	bool canUndoWatch() const;
	static constexpr int watchLimit() noexcept { return watchlistRowLimit; }
	// A tree row's watch box: -1 when nothing at or below it can be watched
	// (a Command, a Service, a Field the Watchlist cannot hold, a stale index);
	// otherwise Qt::Unchecked, Qt::PartiallyChecked or Qt::Checked as an int.
	Q_INVOKABLE int watchState(const QModelIndex& index) const;
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
	void setWatchSource(WatchlistSource source);
	void pollWatch();
	void setBatchSource(BatchSource source);
	void pollBatch();
	bool canSetPresentation() const;
	void setPresentationOverlay(const PresentationOverlay& overlay);
	struct Impl;
	// A model notification may delete the provider. In-flight methods retain the
	// models until their structural notification returns; native leases are host-owned.
	std::shared_ptr<Impl> d;
};
} // namespace telemetry::ux
