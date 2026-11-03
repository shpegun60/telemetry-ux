/*
 * @file main.cpp
 * @brief Launch the runtime QML Inspector and qualify read/edit controls and transport.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "InspectorController.hpp"
#include "DeviceCheck.hpp"
#include "TcpDemoServer.hpp"
#include <QtCore/QCommandLineParser>
#include <QtCore/QFile>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuickControls2/QQuickStyle>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QQuickItem>
#include <algorithm>
#include <cstdio>

namespace {
QQuickItem* findVisual(QQuickItem* parent, const QString& objectName, const QString& scalar = {})
{
	if (parent->objectName() == objectName && parent->isVisible() && parent->width() > 0 &&
	    parent->height() > 0 &&
	    (scalar.isEmpty() || parent->property("scalarName").toString() == scalar))
		return parent;
	for (auto* child : parent->childItems())
		if (auto* found = findVisual(child, objectName, scalar))
			return found;
	return nullptr;
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	QCoreApplication::setApplicationName(QStringLiteral("Telemetry Inspector"));
	QCommandLineParser options;
	options.setApplicationDescription(
	    QStringLiteral("Runtime Telemetry Inspector with optional Field editing"));
	options.addHelpOption();
	options.addOption(
	    {QStringLiteral("check-device"),
	     QStringLiteral("Read and verify all Nucleo serial demo Fields without a window.")});
	options.addOption({QStringLiteral("report"), QStringLiteral("Device-check JSON output path."),
	                   QStringLiteral("file")});
	options.addOption({QStringLiteral("serial"),
	                   QStringLiteral("Connect by COBS/UART and discover the Descriptor."),
	                   QStringLiteral("port")});
	options.addOption({QStringLiteral("baud"), QStringLiteral("Serial baud rate."),
	                   QStringLiteral("bits-per-second"), QStringLiteral("115200")});
	options.addOption({QStringLiteral("tcp"),
	                   QStringLiteral("Connect by TCP and discover the device Descriptor."),
	                   QStringLiteral("host:port")});
	options.addOption({QStringLiteral("serve-demo"),
	                   QStringLiteral("Serve the demo on loopback TCP (0 chooses a free port)."),
	                   QStringLiteral("port")});
	options.addOption({QStringLiteral("smoke"),
	                   QStringLiteral("Read a real demo Field and verify the QML window.")});
	options.addOption(
	    {QStringLiteral("edit-smoke"),
	     QStringLiteral(
	         "Edit, apply and read back the writable demo through actual QML controls.")});
	options.addOption({QStringLiteral("screenshot"),
	                   QStringLiteral("Save the smoke-test window as a PNG."),
	                   QStringLiteral("file")});
	options.addOption({QStringLiteral("descriptor"),
	                   QStringLiteral("Open a Descriptor file without connecting a transport."),
	                   QStringLiteral("file")});
	options.addOption({QStringLiteral("write-demo-descriptor"),
	                   QStringLiteral("Export the demo Descriptor and exit."),
	                   QStringLiteral("file")});
	options.process(app);
	const bool editSmoke = options.isSet(QStringLiteral("edit-smoke"));
	if (editSmoke &&
	    (options.isSet(QStringLiteral("smoke")) || options.isSet(QStringLiteral("serial")) ||
	     options.isSet(QStringLiteral("tcp")) || options.isSet(QStringLiteral("descriptor")) ||
	     options.isSet(QStringLiteral("check-device")) ||
	     options.isSet(QStringLiteral("serve-demo")))) {
		std::fprintf(stderr, "--edit-smoke uses only the writable host demo\n");
		return 1;
	}
	if (options.isSet(QStringLiteral("check-device")) &&
	    (!options.isSet(QStringLiteral("serial")) || options.isSet(QStringLiteral("smoke")) ||
	     options.isSet(QStringLiteral("serve-demo")) ||
	     options.value(QStringLiteral("report")).isEmpty())) {
		std::fprintf(stderr,
		             "--check-device requires --serial and --report; run --smoke separately\n");
		return 1;
	}
	if (options.isSet(QStringLiteral("serve-demo"))) {
		bool valid = false;
		const auto port = options.value(QStringLiteral("serve-demo")).toUInt(&valid);
		inspector_qml::TcpDemoServer server;
		if (!valid || port > 65535 || !server.listen(static_cast<quint16>(port))) {
			std::fprintf(stderr, "Cannot start demo TCP server\n");
			return 1;
		}
		std::printf("Inspector TCP demo listening port=%u\n", unsigned(server.port()));
		std::fflush(stdout);
		return app.exec();
	}
	// Outlive the controller and both queued start triggers, including teardown.
	bool smokeReadStarted = false;
	inspector_qml::InspectorController controller;
	if (options.isSet(QStringLiteral("write-demo-descriptor"))) {
		QFile output(options.value(QStringLiteral("write-demo-descriptor")));
		const auto bytes = controller.demoDescriptor();
		return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() ? 0 : 1;
	}
	const auto destination = options.value(QStringLiteral("tcp"));
	const QUrl tcpAddress{destination.contains(QStringLiteral("://"))
	                          ? destination
	                          : QStringLiteral("tcp://") + destination};
	const auto tcpPort = tcpAddress.port(5577);
	bool baudValid = false;
	const auto baud = options.value(QStringLiteral("baud")).toInt(&baudValid);
	if (options.isSet(QStringLiteral("serial")) &&
	    (!baudValid || options.isSet(QStringLiteral("tcp")) ||
	     options.isSet(QStringLiteral("descriptor")))) {
		std::fprintf(stderr, "Choose exactly one transport and a valid baud rate\n");
		return 1;
	}
	const bool connected =
	    options.isSet(QStringLiteral("serial"))
	        ? controller.connectSerial(options.value(QStringLiteral("serial")), baud)
	    : options.isSet(QStringLiteral("tcp"))
	        ? (tcpAddress.scheme() == QStringLiteral("tcp") && !tcpAddress.host().isEmpty() &&
	           tcpPort > 0 && tcpPort <= 65535 &&
	           controller.connectTcp(tcpAddress.host(), static_cast<quint16>(tcpPort)))
	    : options.isSet(QStringLiteral("descriptor"))
	        ? controller.loadDescriptor(
	              QUrl::fromLocalFile(options.value(QStringLiteral("descriptor"))))
	    : editSmoke ? controller.connectWritableDemo()
	                : controller.connectDemo();
	if (!connected) {
		std::fprintf(stderr, "%s\n", qPrintable(controller.statusText()));
		return 1;
	}
	if (options.isSet(QStringLiteral("check-device")))
		return inspector_qml::checkSerialDevice(app, controller,
		                                        options.value(QStringLiteral("report")));
	bool qmlWarnings = false;
	QQmlApplicationEngine engine;
	QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError>&) {
		qmlWarnings = true;
	});
	engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
	engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Telemetry/Inspector/Main.qml")));
	if (engine.rootObjects().size() != 1)
		return 2;
	QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller,
	                 &inspector_qml::InspectorController::disconnectDevice);
	if (editSmoke) {
		QObject::connect(
		    &controller, &inspector_qml::InspectorController::readFinished, &app,
		    [&](bool published) {
			    if (!published) {
				    app.exit(20);
				    return;
			    }
			    QTimer::singleShot(150, &app, [&] {
				    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
				    auto* button =
				        window ? findVisual(window->contentItem(), QStringLiteral("editButton"))
				               : nullptr;
				    if (!button || !button->isEnabled() ||
				        !QMetaObject::invokeMethod(button, "clicked")) {
					    app.exit(21);
					    return;
				    }
				    QTimer::singleShot(150, &app, [&] {
					    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
					    auto* editor = window ? findVisual(window->contentItem(),
					                                       QStringLiteral("scalarEditor"),
					                                       QStringLiteral("Gain"))
					                          : nullptr;
					    auto* apply = window ? findVisual(window->contentItem(),
					                                      QStringLiteral("applyButton"))
					                         : nullptr;
					    if (!editor || !editor->setProperty("text", QStringLiteral("25")) ||
					        !QMetaObject::invokeMethod(editor, "editingFinished") || !apply ||
					        !apply->isEnabled() || !QMetaObject::invokeMethod(apply, "clicked"))
						    app.exit(22);
				    });
			    });
		    });
		QObject::connect(
		    &controller, &inspector_qml::InspectorController::applyFinished, &app,
		    [&](bool confirmed) {
			    if (!confirmed || controller.demoWriteCount() != 1 || controller.editing()) {
				    app.exit(23);
				    return;
			    }
			    QTimer::singleShot(150, &app, [&] {
				    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
				    const auto findGain = [&](auto&& self, QQuickItem* item) -> bool {
					    if (item->objectName() == QStringLiteral("valueRow") && item->isVisible() &&
					        item->property("name").toString() == QStringLiteral("Gain") &&
					        item->property("value").toString() == QStringLiteral("10"))
						    return true;
					    for (auto* child : item->childItems())
						    if (self(self, child))
							    return true;
					    return false;
				    };
				    if (!window || !findGain(findGain, window->contentItem()) || qmlWarnings) {
					    app.exit(24);
					    return;
				    }
				    if (options.isSet(QStringLiteral("screenshot")) &&
				        !window->grabWindow().save(options.value(QStringLiteral("screenshot")))) {
					    app.exit(25);
					    return;
				    }
				    std::printf(
				        "Inspector QML edit smoke: writes=1 refreshed=1 gain=10 controls=Edit,TextField,Apply\n");
				    app.exit(0);
			    });
		    });
		QTimer::singleShot(0, &controller, [&] {
			auto* model = controller.endpoints();
			const auto catalog = model->index(0, 0, model->index(0, 0));
			if (!controller.selectEndpoint(model->index(0, 0, catalog)) || !controller.refresh())
				app.exit(26);
		});
		QTimer::singleShot(15000, &app, [&] {
			app.exit(27);
		});
	}
	if (options.isSet(QStringLiteral("smoke"))) {
		QObject::connect(
		    &controller, &inspector_qml::InspectorController::readFinished, &app,
		    [&](bool published) {
			    if (!published || engine.rootObjects().size() != 1 ||
			        controller.values()->rowCount() != 1) {
				    app.exit(3);
				    return;
			    }
			    QTimer::singleShot(150, &app, [&] {
				    QList<QQuickItem*> endpointItems, valueItems;
				    const auto collect = [&](auto&& self, QQuickItem* item) -> void {
					    if (item->objectName() == QStringLiteral("endpointRow"))
						    endpointItems.push_back(item);
					    if (item->objectName() == QStringLiteral("valueRow"))
						    valueItems.push_back(item);
					    for (auto* child : item->childItems())
						    self(self, child);
				    };
				    auto* mainWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
				    if (mainWindow)
					    collect(collect, mainWindow->contentItem());
				    const auto visible = [](const QQuickItem* item) {
					    return item->isVisible() && item->width() > 0 && item->height() > 0;
				    };
				    const auto endpointRows =
				        std::count_if(endpointItems.begin(), endpointItems.end(), visible);
				    const auto valueRows =
				        std::count_if(valueItems.begin(), valueItems.end(), visible);
				    const bool voltage = std::any_of(
				        valueItems.begin(), valueItems.end(), [&](const QQuickItem* item) {
					        return visible(item) &&
					               item->property("name").toString() == QStringLiteral("Ua") &&
					               item->property("value").toString().startsWith(
					                   QStringLiteral("230.1"));
				        });
				    if (endpointRows == 0 || valueRows < 4 || !voltage) {
					    std::fprintf(
					        stderr,
					        "Inspector delegates missing: endpoints=%d values=%d voltage=%d\n",
					        int(endpointRows), int(valueRows), int(voltage));
					    app.exit(8);
					    return;
				    }
				    if (qmlWarnings) {
					    app.exit(7);
					    return;
				    }
				    if (options.isSet(QStringLiteral("screenshot"))) {
					    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
					    if (!window || !window->grabWindow().save(
					                       options.value(QStringLiteral("screenshot")))) {
						    app.exit(6);
						    return;
					    }
				    }
				    std::printf(
				        "Inspector QML smoke: published=1 roots=1 endpointRows=%d valueRows=%d\n",
				        int(endpointRows), int(valueRows));
				    app.exit(0);
			    });
		    });
		const auto startRead = [&] {
			if (!controller.connected() || smokeReadStarted)
				return;
			smokeReadStarted = true;
			auto* model = controller.endpoints();
			const auto fieldGroup = model->index(0, 0);
			const auto catalog = model->index(0, 0, fieldGroup);
			if (!controller.selectEndpoint(model->index(0, 0, catalog)) || !controller.refresh())
				app.exit(4);
		};
		QObject::connect(&controller, &inspector_qml::InspectorController::stateChanged, &app,
		                 startRead);
		QTimer::singleShot(0, &controller, startRead);
		QTimer::singleShot(15000, &app, [&app] {
			app.exit(5);
		});
	}
	return app.exec();
}
