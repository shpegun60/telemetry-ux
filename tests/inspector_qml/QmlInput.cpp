/* Real key/mouse input against the shipped QML FieldWrite controls. MIT. */
#include "InspectorController.hpp"
#include <QtCore/QElapsedTimer>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
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

QQuickItem* textItem(QQuickItem* parent, const QString& text)
{
	if (parent->isVisible() && parent->property("text").toString() == text)
		return parent;
	for (auto* child : parent->childItems())
		if (auto* found = textItem(child, text))
			return found;
	return nullptr;
}

bool inside(QQuickItem& item, QQuickItem& viewport)
{
	const auto bounds = item.mapRectToItem(&viewport, QRectF{0, 0, item.width(), item.height()});
	return bounds.left() >= -0.5 && bounds.top() >= -0.5 &&
	       bounds.right() <= viewport.width() + 0.5 &&
	       bounds.bottom() <= viewport.height() + 0.5;
}

bool visibleValue(QQuickItem* parent, QQuickItem& viewport, const QString& name,
                  const QString& value)
{
	if (parent->isVisible() && parent->objectName() == QStringLiteral("valueRow") &&
	    parent->property("name").toString() == name &&
	    parent->property("value").toString() == value && inside(*parent, viewport))
		return true;
	for (auto* child : parent->childItems())
		if (visibleValue(child, viewport, name, value))
			return true;
	return false;
}

void defaultLayout(QQuickWindow& window)
{
	QQuickItem* endpoints = nullptr;
	QQuickItem* values = nullptr;
	CHECK(await([&] {
		endpoints = visual(window.contentItem(), QStringLiteral("endpointTree"));
		values = visual(window.contentItem(), QStringLiteral("valueTree"));
		return endpoints && values;
	}));
	QCoreApplication::processEvents();
	std::fprintf(stderr, "Inspector default layout: window=%dx%d endpoints=%.1f values=%.1f\n",
	             window.width(), window.height(), double(endpoints->height()),
	             double(values->height()));
	CHECK(window.width() == 1080 && window.height() == 720);
	// Four endpoint rows and the Config root plus both editable scalar leaves
	// must fit after Apply publishes its additional outcome text.
	CHECK(endpoints->height() >= 4 * 38 && values->height() >= 3 * 40);
	CHECK(await([&] {
		return visibleValue(values, *values, QStringLiteral("Gain"), QStringLiteral("10")) &&
		       visibleValue(values, *values, QStringLiteral("Counter"),
		                    QStringLiteral("18446744073709551615"));
	}));
}

void click(QQuickWindow& window, QQuickItem& item)
{
	QCoreApplication::processEvents();
	const auto polish = [](auto&& self, QQuickItem* node) -> void {
		if (node->parentItem())
			self(self, node->parentItem());
		node->ensurePolished();
	};
	polish(polish, &item);
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

QModelIndex endpoint(ui::EndpointModel& endpoints, const QString& name,
                     const QModelIndex& parent = {})
{
	for (int row = 0; row < endpoints.rowCount(parent); ++row) {
		const auto index = endpoints.index(row, 0, parent);
		if (endpoints.data(index, ui::EndpointModel::IsEndpointRole).toBool() &&
		    endpoints.data(index, ui::EndpointModel::NameRole).toString() == name)
			return index;
		if (const auto child = endpoint(endpoints, name, index); child.isValid())
			return child;
	}
	return {};
}

void watchLayout(QQuickWindow& window, ui::InspectorController& controller)
{
	CHECK(controller.connectDemo());
	auto& endpoints = *qobject_cast<ui::EndpointModel*>(controller.endpoints());
	CHECK(controller.selectEndpoint(endpoint(endpoints, QStringLiteral("Counter"))));
	click(window, *control(window, QStringLiteral("addWatchButton")));
	CHECK(controller.watchCount() == 1);
	click(window, *control(window, QStringLiteral("refreshWatchButton")));
	const auto message = QStringLiteral(
	    "ReadMany unavailable. Explicitly select Individual FieldRead, or reconnect with TRM1 discovery.");
	CHECK(controller.watchStatus() == message && !controller.watchPending());
	QQuickItem* status = nullptr;
	CHECK(await([&] {
		status = textItem(window.contentItem(), message);
		return status && status->width() > 0 && status->height() > 0;
	}));
	auto* pane = control(window, QStringLiteral("watchlistPane"));
	QCoreApplication::processEvents();
	std::fprintf(stderr,
	             "Inspector watch layout: width=%.1f height=%.1f textWidth=%.1f textHeight=%.1f truncated=%d\n",
	             double(status->width()), double(status->height()),
	             status->property("contentWidth").toDouble(),
	             status->property("contentHeight").toDouble(),
	             int(status->property("truncated").toBool()));
	CHECK(!status->property("truncated").toBool());
	CHECK(status->property("contentWidth").toDouble() <= status->width() + 0.5);
	CHECK(status->property("contentHeight").toDouble() <= status->height() + 0.5);
	CHECK(inside(*status, *pane) && inside(*status, *window.contentItem()));
	// A larger host font at the supported minimum width must add readable
	// lines and reserve their height, rather than clipping the recovery advice.
	window.resize(window.minimumWidth(), window.height());
	auto font = qvariant_cast<QFont>(status->property("font"));
	font.setPixelSize(28);
	CHECK(status->setProperty("font", font));
	CHECK(await([&] {
		return status->property("lineCount").toInt() >= 2 &&
		       status->property("contentHeight").toDouble() <= status->height() + 0.5 &&
		       inside(*status, *pane) && inside(*status, *window.contentItem());
	}));
	CHECK(!status->property("truncated").toBool());
	std::fprintf(stderr, "Inspector watch wrap: window=%dx%d lines=%d label=%.1fx%.1f pane=%.1f\n",
	             window.width(), window.height(), status->property("lineCount").toInt(),
	             double(status->width()), double(status->height()), double(pane->height()));
}

void connectionSettings(QQuickWindow& window, ui::InspectorController& controller)
{
	auto* toggle = control(window, QStringLiteral("connectionSettingsButton"));
	CHECK(!visual(window.contentItem(), QStringLiteral("tcpHost")));
	CHECK(!toggle->property("checked").toBool());
	click(window, *toggle);
	CHECK(control(window, QStringLiteral("tcpHost")));
	CHECK(control(window, QStringLiteral("descriptorPath")));
	// Unrelated state notifications must preserve the user's expanded choice.
	controller.setReadManyDiscovery(!controller.readManyDiscovery());
	CHECK(visual(window.contentItem(), QStringLiteral("tcpHost")));
	toggle->forceActiveFocus(Qt::TabFocusReason);
	CHECK(toggle->hasActiveFocus());
	QTest::keyClick(&window, Qt::Key_Space);
	CHECK(await([&] {
		return !visual(window.contentItem(), QStringLiteral("tcpHost"));
	}));
	controller.disconnectDevice();
	CHECK(control(window, QStringLiteral("tcpHost")));
	CHECK(toggle->property("checked").toBool());
	click(window, *control(window, QStringLiteral("writableDemoButton")));
	CHECK(controller.connected());
	CHECK(await([&] {
		return !visual(window.contentItem(), QStringLiteral("tcpHost"));
	}));
	CHECK(!toggle->property("checked").toBool());
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
	// This isolated path lets the old layout complete the same physical Apply
	// before measuring its default-size viewport; its default editor was clipped.
	const bool applyLayoutOnly = app.arguments().contains(QStringLiteral("--apply-layout-only"));
	if (applyLayoutOnly)
		window->resize(1280, 960);
	if (app.arguments().contains(QStringLiteral("--watch-layout-only"))) {
		watchLayout(*window, controller);
		CHECK(!warnings);
		std::printf("Inspector Watchlist layout checks=%u completeAdvice=1 Qt=%s\n", checks,
		            qVersion());
		return 0;
	}
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
	if (applyLayoutOnly)
		window->resize(1080, 720);
	defaultLayout(*window);
	const auto screenshot = app.arguments().indexOf(QStringLiteral("--screenshot"));
	if (screenshot >= 0) {
		CHECK(screenshot + 1 < app.arguments().size());
		CHECK(window->grabWindow().save(app.arguments()[screenshot + 1]));
	}
	if (applyLayoutOnly) {
		CHECK(!warnings);
		std::printf("Inspector Apply layout checks=%u normalized=10 Qt=%s\n", checks, qVersion());
		return 0;
	}

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
	CHECK(controller.connectWritableDemo());
	connectionSettings(*window, controller);
	watchLayout(*window, controller);
	CHECK(!warnings);
	controller.disconnectDevice();
	std::printf(
	    "Inspector QML physical input checks=%u writes=2 noEnter=2 gain=4.25 counter=18446744073709551614 Qt=%s\n",
	    checks, qVersion());
	return 0;
}
