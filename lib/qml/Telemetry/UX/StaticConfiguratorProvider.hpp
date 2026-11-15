// Qt presentation over a caller-owned generated API. Ruslan Kovtun, codexAi. MIT.
#pragma once

#include "StaticConfiguratorBinding.hpp"
#include "UndoHistory.hpp"
#include "WatchSample.hpp"
#include <QtCore/QAbstractItemModel>
#include <memory>

namespace telemetry::ux {

// The host keeps its generated Api and transport alive on this object's thread.
// One provider serializes its foreground actions through that Api's existing FIFO.
class StaticConfiguratorProvider final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel* endpoints READ endpoints CONSTANT)
	Q_PROPERTY(QAbstractItemModel* values READ values CONSTANT)
	Q_PROPERTY(QAbstractItemModel* responses READ responses CONSTANT)
	Q_PROPERTY(QAbstractItemModel* watchlist READ watchlist CONSTANT)
	// The Watchlist's rows as shown: a list of whole paths or a tree grouped
	// by path (a WatchRows over watchlist). Made here, so a host needs no
	// registered type for the panel's two views.
	Q_PROPERTY(QObject* watchRows READ watchRows CONSTANT)
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
	Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
	Q_PROPERTY(QString presentationError READ presentationError NOTIFY stateChanged)
	// Explicit Individual FieldRead scheduling over the host's existing API/FIFO.
	Q_PROPERTY(bool watchReadManySupported READ watchReadManySupported CONSTANT)
	Q_PROPERTY(bool watchActive READ watchActive NOTIFY stateChanged)
	Q_PROPERTY(bool watchPending READ watchPending NOTIFY stateChanged)
	Q_PROPERTY(bool canAddWatch READ canAddWatch NOTIFY stateChanged)
	Q_PROPERTY(int watchCount READ watchCount NOTIFY stateChanged)
	Q_PROPERTY(QString watchStatus READ watchStatus NOTIFY stateChanged)
	Q_PROPERTY(
	    bool watchIndividual READ watchIndividual WRITE setWatchIndividual NOTIFY stateChanged)
	Q_PROPERTY(
	    int watchIntervalMs READ watchIntervalMs WRITE setWatchIntervalMs NOTIFY stateChanged)
	// The smallest interval accepted: 0 reads continuously at low priority.
	Q_PROPERTY(int watchIntervalMinimumMs READ watchIntervalMinimumMs CONSTANT)
	// The Device tree's watch boxes, as DynamicConfiguratorProvider has them.
	Q_PROPERTY(int watchRevision READ watchRevision NOTIFY stateChanged)
	Q_PROPERTY(int watchLimit READ watchLimit CONSTANT)
	// Where the Watchlist's changes go as steps to undo (UR-1), as
	// DynamicConfiguratorProvider's: rows added and removed ("watch.rows", by
	// path, with their places). Null, the default, records nothing.
	Q_PROPERTY(telemetry::ux::UndoHistory* history READ history WRITE setHistory NOTIFY historyChanged)
public:
	explicit StaticConfiguratorProvider(StaticConfiguratorBinding binding,
	                                    QObject* parent = nullptr);
	~StaticConfiguratorProvider() override;
	QAbstractItemModel* endpoints() const;
	QAbstractItemModel* values() const;
	QAbstractItemModel* responses() const;
	QAbstractItemModel* watchlist() const;
	QObject* watchRows() const;
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
	QString statusText() const;
	Q_INVOKABLE bool setPresentation(const QByteArray& json);
	QString presentationError() const;
	Q_INVOKABLE void selectEndpoint(const QModelIndex& index);
	Q_INVOKABLE void refresh();
	Q_INVOKABLE void beginEdit();
	Q_INVOKABLE void cancelEdit();
	Q_INVOKABLE void apply();
	Q_INVOKABLE void executeCommand();
	Q_INVOKABLE void callService();
	Q_INVOKABLE bool setScalarText(const QModelIndex& index, const QString& text);

	// Retain the former constant property reader for source compatibility.
	bool noWatch() const
	{
		return false;
	}

	bool watchReadManySupported() const
	{
		return false;
	}

	bool watchActive() const;
	bool watchPending() const;
	bool canAddWatch() const;
	int watchCount() const;
	QString watchStatus() const;

	bool watchIndividual() const
	{
		return true;
	}

	int watchIntervalMs() const;
	static constexpr int watchIntervalMinimumMs() noexcept { return 0; }
	void setWatchIndividual(bool individual);
	void setWatchIntervalMs(int interval);
	Q_INVOKABLE bool addWatch();
	Q_INVOKABLE bool watchCandidate(const QModelIndex& index) const;
	Q_INVOKABLE QVariantMap addWatchMany(const QVariantList& sourceIndexes);
	Q_INVOKABLE void removeWatch(int row);
	// The rows given out as one step, as DynamicConfiguratorProvider's:
	// {status: ok, invalid or refused, changed, serial}.
	Q_INVOKABLE QVariantMap removeWatchRows(const QVariantList& rows);
	// Every row out, as one step ("Clear the Watchlist").
	Q_INVOKABLE void clearWatch();
	int watchRevision() const;
	UndoHistory* history() const;
	void setHistory(UndoHistory* history);
	static constexpr int watchLimit() noexcept { return watchlistRowLimit; }
	// The same contracts as DynamicConfiguratorProvider's.
	Q_INVOKABLE int watchState(const QModelIndex& index) const;
	Q_INVOKABLE QVariantMap setWatchMany(const QVariantList& sourceIndexes, bool watched);
	Q_INVOKABLE void refreshWatch();
	Q_INVOKABLE void startWatch();
	Q_INVOKABLE void stopWatch();

Q_SIGNALS:
	void stateChanged();
	void historyChanged();
	void readFinished(bool published);

private:
	struct Impl;
	std::unique_ptr<Impl> d;
};

} // namespace telemetry::ux
