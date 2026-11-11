// Installed connection owner and two panels; no copied pump/bootstrap/DTO. MIT.
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <Telemetry/UX/TrendHistory.hpp>
#include <integration/qt/ConnectionSession.hpp>
#include <QtCore/QCommandLineParser>
#include <QtCore/QFile>
#include <QtCore/QDebug>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuickControls2/QQuickStyle>

int main(int argc, char** argv)
{
	QGuiApplication application(argc, argv);
	QCommandLineParser arguments;
	arguments.setApplicationDescription(
	    QStringLiteral("Two Telemetry panels sharing one connection"));
	arguments.addHelpOption();
	arguments.addOption({QStringLiteral("presentation"),
	                     QStringLiteral("Optional presentation JSON for this Model"),
	                     QStringLiteral("file")});
	arguments.process(application);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	telemetry::integration::qt::ConnectionSession connection;
	telemetry::ux::QtConnectionBinding presentation{&connection};
	if (arguments.isSet(QStringLiteral("presentation"))) {
		QFile file{arguments.value(QStringLiteral("presentation"))};
		if (!file.open(QIODevice::ReadOnly)) {
			qCritical().noquote() << file.errorString();
			return 1;
		}
		if (!presentation.setPresentation(file.readAll())) {
			qCritical().noquote() << presentation.presentationError();
			return 1;
		}
	}
	presentation.setMutationsAllowed(true); // Host policy; each panel asks for opt-in too.
	auto* configuration = presentation.createProvider();
	auto* measurements = presentation.createProvider();
	telemetry::ux::TrendHistory trendHistory;
	QQmlApplicationEngine engine; // Destroy QML before the shared native owner.
	engine.rootContext()->setContextProperty(QStringLiteral("connection"), &connection);
	engine.rootContext()->setContextProperty(QStringLiteral("configuration"), configuration);
	engine.rootContext()->setContextProperty(QStringLiteral("measurements"), measurements);
	engine.rootContext()->setContextProperty(QStringLiteral("trendHistory"), &trendHistory);
	engine.load(QUrl{QStringLiteral("qrc:/Main.qml")});
	if (engine.rootObjects().isEmpty())
		return 1;
	return application.exec();
}
