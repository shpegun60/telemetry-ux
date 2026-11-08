// Automatic presentation binding for an owned SDK Qt session. MIT.
#pragma once
#include "DynamicConfiguratorProvider.hpp"
#include "PresentationOverlay.hpp"
#include <integration/qt/ConnectionSession.hpp>
#include <QtCore/QPointer>

namespace telemetry::ux {
class ConnectionViews;

// This object adds no client, timer, discovery or transport. The session owns
// one shared view registry, which survives removal of an individual UI adapter
// or panel until accepted native work has drained.
class QtConnectionBinding final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
	Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
	Q_PROPERTY(QString presentationError READ presentationError NOTIFY stateChanged)
	Q_PROPERTY(
	    bool mutationsAllowed READ mutationsAllowed WRITE setMutationsAllowed NOTIFY stateChanged)
public:
	explicit QtConnectionBinding(integration::qt::ConnectionSession* session,
	                             QObject* parent = nullptr);
	~QtConnectionBinding() override;
	Q_INVOKABLE DynamicConfiguratorProvider* createProvider(QObject* parent = nullptr);
	bool connected() const;
	QString lastError() const;
	Q_INVOKABLE bool setPresentation(const QByteArray& json);
	QString presentationError() const;
	bool mutationsAllowed() const;
	void setMutationsAllowed(bool allowed);
Q_SIGNALS:
	void stateChanged();
	void operationFinished(QVariantMap facts);

private:
	QPointer<integration::qt::ConnectionSession> session_;
	QPointer<QObject> views_;
	PresentationOverlay presentation_;
	QString presentationError_;
	bool presentationChanging_ = false;
	QList<QPointer<DynamicConfiguratorProvider>> providers_;
};
} // namespace telemetry::ux
