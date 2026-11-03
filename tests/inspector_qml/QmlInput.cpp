/* Real key/mouse input against the shipped QML FieldWrite controls. MIT. */
#include "InspectorController.hpp"
#include <QtCore/QElapsedTimer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <cstdio>
#include <cstdlib>

namespace ui = inspector_qml;

namespace {
unsigned checks = 0;

void check(bool passed, int line)
{
	++checks;
	if (!passed) {
		std::fprintf(stderr, "Inspector QML physical input check %u failed at line %d\n", checks,
		             line);
		std::exit(1);
	}
}

#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)

template<class Predicate>
bool await(Predicate predicate, int timeout = 5000)
{
	QElapsedTimer elapsed;
	elapsed.start();
	while (!predicate() && elapsed.elapsed() < timeout) {
		QCoreApplication::processEvents();
		QTest::qWait(1);
	}
	return predicate();
}

QQuickItem* visual(QQuickItem* parent, const QString& objectName, const QString& scalarName = {})
{
	if (parent->isVisible() && parent->objectName() == objectName &&
	    (scalarName.isEmpty() || parent->property("scalarName").toString() == scalarName))
		return parent;
	for (auto* child : parent->childItems())
		if (auto* found = visual(child, objectName, scalarName))
			return found;
	return nullptr;
}

QQuickItem* control(QQuickWindow& window, const QString& name, const QString& scalarName = {})
{
	QQuickItem* item = nullptr;
	CHECK(await([&] {
		item = visual(window.contentItem(), name, scalarName);
		return item && item->width() > 0 && item->height() > 0;
	}));
	return item;
}

void click(QQuickWindow& window, QQuickItem& item)
{
	const auto position = item.mapToScene(QPointF{item.width() / 2, item.height() / 2}).toPoint();
	CHECK(position.x() >= 0 && position.x() < window.width() && position.y() >= 0 &&
	      position.y() < window.height());
	QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, position);
}

void type(QQuickWindow& window, const QString& name, const QByteArray& text)
{
	auto* editor = control(window, QStringLiteral("scalarEditor"), name);
	click(window, *editor);
	CHECK(await([&] {
		return editor->hasActiveFocus();
	}));
	QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClick(&window, Qt::Key_Backspace);
	// Qt 6.4 has keyClicks only for QWidget. Individual QWindow keyClick
	// events exercise the actual TextInput path without setting its text property.
	for (const char letter : text)
		QTest::keyClick(&window, letter);
	CHECK(editor->property("text").toString() == QString::fromLatin1(text));
	CHECK(editor->hasActiveFocus()); // No Enter, focus-out or signal invocation.
}

QModelIndex member(ui::ValueModel& values, const QString& name)
{
	const auto root = values.index(0, 0);
	if (values.canFetchMore(root))
		values.fetchMore(root);
	for (int row = 0; row != values.rowCount(root); ++row) {
		const auto index = values.index(row, 0, root);
		if (values.data(index, ui::ValueModel::NameRole).toString() == name)
			return index;
	}
	return {};
}

QString value(ui::ValueModel& values, const QString& name)
{
	const auto index = member(values, name);
	CHECK(index.isValid());
	return values.data(index, ui::ValueModel::ValueRole).toString();
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	ui::InspectorController controller;
	CHECK(controller.connectWritableDemo());
	QQmlApplicationEngine engine;
	bool warnings = false;
	QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
	                 [&](const QList<QQmlError>& errors) {
		                 warnings = true;
		                 for (const auto& error : errors)
			                 std::fprintf(stderr, "%s\n", qPrintable(error.toString()));
	                 });
	engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
	engine.load(QUrl::fromLocalFile(QStringLiteral(INSPECTOR_QML_SOURCE_FILE)));
	CHECK(engine.rootObjects().size() == 1);
	auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
	CHECK(window && QTest::qWaitForWindowExposed(window));
	window->requestActivate();
	auto& endpoints = *qobject_cast<ui::EndpointModel*>(controller.endpoints());
	auto& values = *qobject_cast<ui::ValueModel*>(controller.values());
	const auto catalog = endpoints.index(0, 0, endpoints.index(0, 0));
	CHECK(controller.selectEndpoint(endpoints.index(0, 0, catalog)));
	CHECK(controller.refresh());
	CHECK(await([&] {
		return !controller.busy();
	}));
	CHECK(value(values, QStringLiteral("Gain")) == QStringLiteral("1"));
	QSignalSpy applied{&controller, &ui::InspectorController::applyFinished};

	click(*window, *control(*window, QStringLiteral("editButton")));
	CHECK(controller.editing());
	type(*window, QStringLiteral("Gain"), QByteArrayLiteral("12"));
	auto* apply = control(*window, QStringLiteral("applyButton"));
	std::fprintf(stderr, "QML physical input first Apply: enabled=%d canApply=%d writes=%llu\n",
	             int(apply->isEnabled()), int(controller.canApply()),
	             static_cast<unsigned long long>(controller.demoWriteCount()));
	click(*window, *apply);
	CHECK(await([&] {
		return applied.count() == 1 && !controller.busy();
	}));
	CHECK(applied.last()[0].toBool() && controller.demoWriteCount() == 1 && !controller.editing());
	CHECK(value(values, QStringLiteral("Gain")) == QStringLiteral("10"));

	click(*window, *control(*window, QStringLiteral("editButton")));
	CHECK(controller.editing());
	type(*window, QStringLiteral("Gain"), QByteArrayLiteral("4.25"));
	type(*window, QStringLiteral("Counter"), QByteArrayLiteral("18446744073709551616"));
	CHECK(!controller.canApply() && !control(*window, QStringLiteral("applyButton"))->isEnabled());
	CHECK(controller.demoWriteCount() == 1 && applied.count() == 1);
	// Blurring an invalid editor must retain its raw input and keep Apply
	// disabled, instead of restoring a formatted role and silently clearing it.
	click(*window, *control(*window, QStringLiteral("scalarEditor"), QStringLiteral("Gain")));
	CHECK(control(*window, QStringLiteral("scalarEditor"), QStringLiteral("Counter"))
	          ->property("text")
	          .toString() == QStringLiteral("18446744073709551616"));
	CHECK(!controller.canApply() && controller.demoWriteCount() == 1 && applied.count() == 1);
	// Gain is already dirty. A final different leaf must also reach the Write
	// when Apply is clicked while that editor still owns keyboard focus.
	type(*window, QStringLiteral("Counter"), QByteArrayLiteral("18446744073709551614"));
	click(*window, *control(*window, QStringLiteral("applyButton")));
	CHECK(await([&] {
		return applied.count() == 2 && !controller.busy();
	}));
	CHECK(applied.last()[0].toBool() && controller.demoWriteCount() == 2 && !controller.editing());
	CHECK(value(values, QStringLiteral("Gain")) == QStringLiteral("4.25"));
	CHECK(value(values, QStringLiteral("Counter")) == QStringLiteral("18446744073709551614"));
	CHECK(!warnings);
	controller.disconnectDevice();
	CHECK(applied.count() == 2 && !controller.busy());
	std::printf(
	    "Inspector QML physical input checks=%u writes=2 noEnter=2 gain=4.25 counter=18446744073709551614 Qt=%s\n",
	    checks, qVersion());
	return 0;
}
