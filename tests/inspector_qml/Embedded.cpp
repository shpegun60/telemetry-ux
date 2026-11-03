/* Explicit-provider Configurator inside an independent host window. MIT. */
#include "InspectorController.hpp"
#include <QtCore/QElapsedTimer>
#include <QtGui/QColor>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace ui = inspector_qml;

namespace {
unsigned checks = 0;

void check(bool passed, int line)
{
	++checks;
	if (!passed) {
		std::fprintf(stderr, "Configurator embedding check %u failed at line %d\n", checks, line);
		std::exit(1);
	}
}

#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)

template<class Predicate>
bool await(Predicate predicate)
{
	QElapsedTimer timer;
	timer.start();
	while (!predicate() && timer.elapsed() < 5000) {
		QCoreApplication::processEvents();
		QTest::qWait(1);
	}
	return predicate();
}

QQuickItem* visual(QQuickItem* root, const QString& name, const QString& scalar = {})
{
	if (root->isVisible() && root->objectName() == name &&
	    (scalar.isEmpty() || root->property("scalarName").toString() == scalar))
		return root;
	for (auto* child : root->childItems())
		if (auto* found = visual(child, name, scalar))
			return found;
	return nullptr;
}

QQuickItem* control(QQuickWindow& window, const QString& name, const QString& scalar = {})
{
	QQuickItem* item = nullptr;
	CHECK(await([&] {
		item = visual(window.contentItem(), name, scalar);
		return item && item->width() > 0 && item->height() > 0;
	}));
	return item;
}

void click(QQuickWindow& window, QQuickItem& item)
{
	// A watch-panel or editor transition can defer its parent layout.
	QCoreApplication::processEvents();
	std::vector<QQuickItem*> ancestors;
	for (auto* parent = &item; parent; parent = parent->parentItem())
		ancestors.push_back(parent);
	for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
		(*it)->ensurePolished();
	QCoreApplication::processEvents();
	const auto point = item.mapToScene(QPointF{item.width() / 2, item.height() / 2}).toPoint();
	CHECK(point.x() >= 0 && point.x() < window.width() && point.y() >= 0 &&
	      point.y() < window.height());
	CHECK(item.isEnabled());
	QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point);
}

QModelIndex endpoint(ui::EndpointModel& model, const QString& name, const QModelIndex& parent = {})
{
	for (int row = 0; row < model.rowCount(parent); ++row) {
		const auto index = model.index(row, 0, parent);
		if (model.data(index, ui::EndpointModel::IsEndpointRole).toBool() &&
		    model.data(index, ui::EndpointModel::NameRole).toString() == name)
			return index;
		if (const auto child = endpoint(model, name, index); child.isValid())
			return child;
	}
	return {};
}

QModelIndex member(ui::ValueModel& model, const QString& name)
{
	const auto root = model.index(0, 0);
	if (model.canFetchMore(root))
		model.fetchMore(root);
	for (int row = 0; row < model.rowCount(root); ++row) {
		const auto index = model.index(row, 0, root);
		if (model.data(index, ui::ValueModel::NameRole).toString() == name)
			return index;
	}
	return {};
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	if (app.arguments().contains(QStringLiteral("--missing-provider"))) {
		// Separate negative process: retain Qt's expected binding diagnostics.
		QQmlEngine missingEngine;
		QQmlComponent missing{&missingEngine};
		missing.setData("import Telemetry.UX\nConfigurator {}\n",
		                QUrl(QStringLiteral("qrc:/MissingProvider.qml")));
		std::unique_ptr<QObject> invalid{missing.create()};
		CHECK(!invalid);
		const auto errors = missing.errors();
		CHECK(errors.size() == 1);
		CHECK(errors.first().description().contains(
		    QStringLiteral("Required property provider was not initialized")));
		std::printf("Configurator missing provider checks=%u rejected=1 required=1\n", checks);
		return 0;
	}
	ui::InspectorController provider;
	CHECK(provider.connectWritableDemo());
	QQmlApplicationEngine engine;
	bool warnings = false;
	QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError>& errors) {
		warnings = true;
		for (const auto& error : errors)
			std::fprintf(stderr, "%s\n", qPrintable(error.toString()));
	});
	CHECK(!engine.rootContext()->contextProperty(QStringLiteral("controller")).isValid());
	engine.setInitialProperties(
	    {{QStringLiteral("deviceProvider"), QVariant::fromValue<QObject*>(&provider)}});
	engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Telemetry/EmbeddingHost/Embedded.qml")));
	CHECK(engine.rootObjects().size() == 1);
	auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
	CHECK(window && window->objectName() == QStringLiteral("embeddingHost"));
	CHECK(QTest::qWaitForWindowExposed(window));
	window->requestActivate();
	auto* view = control(*window, QStringLiteral("embeddedConfigurator"));
	CHECK(view->property("provider").value<QObject*>() == &provider);
	CHECK(control(*window, QStringLiteral("hostOnlyLabel")));
	CHECK(!visual(window->contentItem(), QStringLiteral("tcpHost")));
	CHECK(!visual(window->contentItem(), QStringLiteral("serialPort")));
	CHECK(!visual(window->contentItem(), QStringLiteral("writableDemoButton")));

	auto& endpoints = *qobject_cast<ui::EndpointModel*>(provider.endpoints());
	auto& values = *qobject_cast<ui::ValueModel*>(provider.values());
	const auto selected = endpoint(endpoints, QStringLiteral("Config"));
	CHECK(selected.isValid());
	CHECK(provider.selectEndpoint(selected));
	QSignalSpy read{&provider, &ui::InspectorController::readFinished};
	click(*window, *control(*window, QStringLiteral("refreshButton")));
	CHECK(await([&] {
		return read.count() == 1 && !provider.busy();
	}));
	CHECK(read.first().first().toBool());
	CHECK(member(values, QStringLiteral("Gain")).isValid());
	CHECK(control(*window, QStringLiteral("valueTree")));
	click(*window, *control(*window, QStringLiteral("editButton")));
	CHECK(provider.editing());
	auto* editor = control(*window, QStringLiteral("scalarEditor"), QStringLiteral("Gain"));
	click(*window, *editor);
	CHECK(await([&] {
		return editor->hasActiveFocus();
	}));
	QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClick(window, Qt::Key_Backspace);
	QTest::keyClick(window, Qt::Key_1);
	QTest::keyClick(window, Qt::Key_2);
	CHECK(editor->property("text").toString() == QStringLiteral("12"));
	CHECK(editor->hasActiveFocus());
	QSignalSpy applied{&provider, &ui::InspectorController::applyFinished};
	click(*window, *control(*window, QStringLiteral("applyButton")));
	CHECK(await([&] {
		return applied.count() == 1 && !provider.busy();
	}));
	CHECK(applied.first().first().toBool());
	CHECK(provider.demoWriteCount() == 1);
	CHECK(!provider.editing());
	const auto gain = member(values, QStringLiteral("Gain"));
	CHECK(gain.isValid());
	CHECK(values.data(gain, ui::ValueModel::ValueRole).toString() == QStringLiteral("10"));
	CHECK(control(*window, QStringLiteral("writeOutcomeLabel"))
	          ->property("text")
	          .toString()
	          .contains(QStringLiteral("applied"), Qt::CaseInsensitive));
	CHECK(!engine.rootContext()->contextProperty(QStringLiteral("controller")).isValid());
	CHECK(!warnings);
	// The reusable view has a dark palette even inside this light host. Check a
	// layout gap above the watch pane, where a transparent view would expose the
	// host's light surface behind its pale outcome/status labels.
	auto* watchPane = control(*window, QStringLiteral("watchlistPane"));
	const auto backgroundPoint =
	    watchPane->mapToScene(QPointF{watchPane->width() / 2, -4}).toPoint();
	CHECK(view->contains(view->mapFromScene(backgroundPoint)));
	QCoreApplication::processEvents();
	const auto backgroundImage = window->grabWindow();
	CHECK(!backgroundImage.isNull());
	const QPoint backgroundPixel{backgroundPoint.x() * backgroundImage.width() / window->width(),
	                             backgroundPoint.y() * backgroundImage.height() / window->height()};
	CHECK(backgroundImage.rect().contains(backgroundPixel) &&
	      backgroundImage.pixelColor(backgroundPixel) == QColor(0x11, 0x15, 0x1c));

	const auto arguments = app.arguments();
	const auto screenshot = arguments.indexOf(QStringLiteral("--screenshot"));
	if (screenshot >= 0) {
		CHECK(screenshot + 1 < arguments.size());
		QCoreApplication::processEvents();
		const auto image = window->grabWindow();
		CHECK(!image.isNull() && image.save(arguments[screenshot + 1]));
	}
	provider.disconnectDevice();
	CHECK(!provider.connected());
	CHECK(window->setProperty("viewLoaded", false));
	QCoreApplication::processEvents();
	QTest::qWait(1);
	CHECK(!visual(window->contentItem(), QStringLiteral("embeddedConfigurator")));
	CHECK(!warnings);
	std::printf(
	    "Configurator embedding checks=%u provider=explicit context=0 writes=1 normalized=10 unload=1 Qt=%s\n",
	    checks, qVersion());
}
