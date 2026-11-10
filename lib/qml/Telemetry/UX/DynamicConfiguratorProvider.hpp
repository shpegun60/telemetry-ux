// Independent Qt panel over one borrowed native view. Ruslan Kovtun, codexAi. MIT.
#pragma once
#include "DynamicConfiguratorBinding.hpp"
#include <QtCore/QAbstractItemModel>
#include <memory>

namespace telemetry::ux {
struct WatchlistSource;

class DynamicConfiguratorProvider final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel* endpoints READ endpoints CONSTANT)
	Q_PROPERTY(QAbstractItemModel* values READ values CONSTANT)
	Q_PROPERTY(QAbstractItemModel* responses READ responses CONSTANT)
	Q_PROPERTY(QAbstractItemModel* watchlist READ watchlist CONSTANT)
	Q_PROPERTY(bool watchReadManySupported READ watchReadManySupported NOTIFY stateChanged)
	Q_PROPERTY(bool watchRequiresExplicitIndividual READ watchRequiresExplicitIndividual CONSTANT)
	Q_PROPERTY(bool watchActive READ watchActive NOTIFY stateChanged)
	Q_PROPERTY(bool watchPending READ watchPending NOTIFY stateChanged)
	Q_PROPERTY(bool canAddWatch READ canAddWatch NOTIFY stateChanged)
	Q_PROPERTY(int watchCount READ watchCount NOTIFY stateChanged)
	Q_PROPERTY(QString watchStatus READ watchStatus NOTIFY stateChanged)
	Q_PROPERTY(
	    bool watchIndividual READ watchIndividual WRITE setWatchIndividual NOTIFY stateChanged)
	Q_PROPERTY(
	    int watchIntervalMs READ watchIntervalMs WRITE setWatchIntervalMs NOTIFY stateChanged)
	Q_PROPERTY(QString selection READ selection NOTIFY stateChanged)
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
	void setWatchIndividual(bool individual);
	void setWatchIntervalMs(int milliseconds);
	Q_INVOKABLE bool addWatch();
	Q_INVOKABLE void removeWatch(int row);
	Q_INVOKABLE void clearWatch();
	Q_INVOKABLE void refreshWatch();
	Q_INVOKABLE void startWatch();
	Q_INVOKABLE void stopWatch();
	QString selection() const;
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

private:
	friend class QtConnectionBinding;
	friend class ConnectionViews;
	void setWatchSource(WatchlistSource source);
	void pollWatch();
	bool canSetPresentation() const;
	void setPresentationOverlay(const PresentationOverlay& overlay);
	struct Impl;
	// A model notification may delete the provider. In-flight methods retain the
	// models until their structural notification returns; native leases are host-owned.
	std::shared_ptr<Impl> d;
};
} // namespace telemetry::ux
