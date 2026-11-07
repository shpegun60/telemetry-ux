// Installed connection owner and two panels; no copied pump/bootstrap/DTO. MIT.
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <integration/qt/ConnectionSession.hpp>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuickControls2/QQuickStyle>

int main(int argc, char** argv)
{
	QGuiApplication application(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	telemetry::integration::qt::ConnectionSession connection;
	telemetry::ux::QtConnectionBinding presentation{&connection};
	presentation.setMutationsAllowed(true); // Host policy; each panel asks for opt-in too.
	auto* configuration = presentation.createProvider();
	auto* measurements = presentation.createProvider();
	QQmlApplicationEngine engine; // Destroy QML before the shared native owner.
	engine.rootContext()->setContextProperty(QStringLiteral("connection"), &connection);
	engine.rootContext()->setContextProperty(QStringLiteral("configuration"), configuration);
	engine.rootContext()->setContextProperty(QStringLiteral("measurements"), measurements);
	engine.load(QUrl{QStringLiteral("qrc:/Main.qml")});
	if (engine.rootObjects().isEmpty())
		return 1;
	return application.exec();
}
