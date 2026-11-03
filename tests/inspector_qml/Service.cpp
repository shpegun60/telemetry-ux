/*
 * @file Service.cpp
 * @brief Actual Service Request input and owned, lazy Response qualification.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "InspectorController.hpp"
#include <QtCore/QElapsedTimer>
#include <QtCore/QPersistentModelIndex>
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
		std::fprintf(stderr, "Inspector Service check %u failed at line %d\n", checks, line);
		std::exit(1);
	}
}

#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)

template<class F>
bool await(F ready, int timeout = 5000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < timeout) {
		QCoreApplication::processEvents();
		QTest::qWait(1);
	}
	return ready();
}

QModelIndex endpoint(ui::EndpointModel& model, const QString& name, const QModelIndex& parent = {})
{
	for (int row = 0; row < model.rowCount(parent); ++row) {
		const auto index = model.index(row, 0, parent);
		if (model.data(index, ui::EndpointModel::IsEndpointRole).toBool() &&
		    model.data(index, ui::EndpointModel::CategoryRole).toInt() == 3 &&
		    model.data(index, ui::EndpointModel::NameRole).toString() == name)
			return index;
		const auto child = endpoint(model, name, index);
		if (child.isValid())
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

QString value(ui::ValueModel& model, const QString& name)
{
	const auto index = member(model, name);
	CHECK(index.isValid());
	return model.data(index, ui::ValueModel::ValueRole).toString();
}

QQuickItem* visual(QQuickItem* parent, const QString& name, const QString& scalar = {})
{
	if (parent->isVisible() && parent->objectName() == name &&
	    (scalar.isEmpty() || parent->property("scalarName").toString() == scalar))
		return parent;
	for (auto* child : parent->childItems())
		if (auto* found = visual(child, name, scalar))
			return found;
	return nullptr;
}

QQuickItem* control(QQuickWindow& window, const QString& name, const QString& scalar = {})
{
	QQuickItem* result = nullptr;
	CHECK(await([&] {
		result = visual(window.contentItem(), name, scalar);
		return result && result->width() > 0 && result->height() > 0;
	}));
	return result;
}

void click(QQuickWindow& window, QQuickItem& item)
{
	const auto point = item.mapToScene(QPointF{item.width() / 2, item.height() / 2}).toPoint();
	CHECK(point.x() >= 0 && point.x() < window.width() && point.y() >= 0 &&
	      point.y() < window.height());
	QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point);
}

void type(QQuickWindow& window, const QString& name, const QByteArray& text)
{
	auto* editor = control(window, QStringLiteral("scalarEditor"), name);
	click(window, *editor);
	const bool focused = await([&] {
		return editor->hasActiveFocus();
	});
	if (!focused) {
		const auto bounds = editor->mapRectToScene(QRectF{0, 0, editor->width(), editor->height()});
		std::fprintf(stderr, "Service editor %s scene=%g,%g %gx%g focus=%d\n", qPrintable(name),
		             bounds.x(), bounds.y(), bounds.width(), bounds.height(),
		             editor->hasActiveFocus());
		const auto arguments = QCoreApplication::arguments();
		const auto screenshot = arguments.indexOf(QStringLiteral("--screenshot"));
		if (screenshot >= 0 && screenshot + 1 < arguments.size())
			(void)window.grabWindow().save(arguments[screenshot + 1]);
	}
	CHECK(focused);
	QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClick(&window, Qt::Key_Backspace);
	for (const char key : text)
		QTest::keyClick(&window, key);
	CHECK(editor->property("text").toString() == QString::fromLatin1(text));
	CHECK(editor->hasActiveFocus());
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	ui::InspectorController controller;
	CHECK(controller.connectWritableDemo());
	auto& endpoints = *qobject_cast<ui::EndpointModel*>(controller.endpoints());
	auto& args = *qobject_cast<ui::ValueModel*>(controller.values());
	auto& response = *qobject_cast<ui::ValueModel*>(controller.responses());
	QSignalSpy finished{&controller, &ui::InspectorController::serviceFinished};
	QSignalSpy reads{&controller, &ui::InspectorController::readFinished};
	QSignalSpy commands{&controller, &ui::InspectorController::commandFinished};
	const auto select = [&](const QString& name) {
		const auto found = endpoint(endpoints, name);
		CHECK(found.isValid() && controller.selectEndpoint(found));
		CHECK(controller.serviceSelected() && !controller.commandSelected() &&
		      !controller.editing() && !controller.canRefresh());
	};
	const auto call = [&] {
		const auto count = finished.count();
		CHECK(controller.canCallService() && controller.callService());
		CHECK(!controller.callService());
		CHECK(response.rowCount() == 0);
		CHECK(await([&] {
			return finished.count() == count + 1 && !controller.busy();
		}));
	};
	select(QStringLiteral("Transform"));
	CHECK(controller.demoServiceCount() == 0 && controller.canCallService());
	CHECK(args.data(args.index(0, 0), ui::ValueModel::NameRole).toString() ==
	      QStringLiteral("Request"));
	call();
	CHECK(finished.last()[0].toBool() && finished.last()[1].toBool() &&
	      controller.demoServiceCount() == 1);
	CHECK(response.data(response.index(0, 0), ui::ValueModel::NameRole).toString() ==
	      QStringLiteral("Response"));
	CHECK(value(response, QStringLiteral("Gain")) == QStringLiteral("0") &&
	      value(response, QStringLiteral("Counter")) == QStringLiteral("0"));
	CHECK(controller.serviceResponseEndpoint() == QStringLiteral("control.Transform"));
	const QPersistentModelIndex oldCounter = member(response, QStringLiteral("Counter"));
	CHECK(!response.data(oldCounter, ui::ValueModel::EditableRole).toBool());
	CHECK(!controller.setScalarText(oldCounter, QStringLiteral("9")));
	controller.setWritesEnabled(false);
	CHECK(!controller.callService() && oldCounter.isValid() && finished.count() == 1 &&
	      controller.demoServiceCount() == 1);
	controller.setWritesEnabled(true);
	CHECK(controller.setScalarText(member(args, QStringLiteral("Offset")),
	                               QStringLiteral("-9223372036854775808")));
	CHECK(controller.setScalarText(member(args, QStringLiteral("OperatingMode")),
	                               QStringLiteral("-2")));
	CHECK(
	    controller.setScalarText(member(args, QStringLiteral("Enabled")), QStringLiteral("true")));
	const auto requestMatrix = member(args, QStringLiteral("Matrix"));
	args.fetchMore(requestMatrix);
	const auto requestRow = args.index(1, 0, requestMatrix);
	args.fetchMore(requestRow);
	CHECK(controller.setScalarText(args.index(1, 0, requestRow), QStringLiteral("-17")));
	CHECK(response.data(oldCounter, ui::ValueModel::ValueRole).toString() == QStringLiteral("0"));

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
	type(*window, QStringLiteral("Gain"), QByteArrayLiteral("25"));
	type(*window, QStringLiteral("Counter"), QByteArrayLiteral("18446744073709551616"));
	CHECK(!controller.canCallService());
	click(*window, *control(*window, QStringLiteral("scalarEditor"), QStringLiteral("Gain")));
	CHECK(!controller.canCallService() &&
	      control(*window, QStringLiteral("scalarEditor"), QStringLiteral("Counter"))
	              ->property("text")
	              .toString() == QStringLiteral("18446744073709551616"));
	type(*window, QStringLiteral("Counter"), QByteArrayLiteral("18446744073709551614"));
	CHECK(controller.canCallService() && oldCounter.isValid());
	bool retiredOnAcceptance = false;
	const auto observed =
	    QObject::connect(&controller, &ui::InspectorController::stateChanged, &app, [&] {
		    if (controller.busy())
			    retiredOnAcceptance = response.rowCount() == 0;
	    });
	click(*window, *control(*window, QStringLiteral("callServiceButton")));
	CHECK(await([&] {
		return finished.count() == 2 && !controller.busy();
	}));
	QObject::disconnect(observed);
	CHECK(retiredOnAcceptance && !oldCounter.isValid());
	CHECK(finished.last()[0].toBool() && finished.last()[1].toBool() &&
	      controller.demoServiceCount() == 2);
	CHECK(value(response, QStringLiteral("Gain")) == QStringLiteral("10"));
	CHECK(value(response, QStringLiteral("Counter")) == QStringLiteral("18446744073709551614"));
	CHECK(value(response, QStringLiteral("Offset")) == QStringLiteral("-9223372036854775808"));
	CHECK(value(response, QStringLiteral("OperatingMode")) == QStringLiteral("-2 (unknown)"));
	CHECK(value(response, QStringLiteral("Enabled")) == QStringLiteral("true"));
	const auto matrix = member(response, QStringLiteral("Matrix"));
	response.fetchMore(matrix);
	CHECK(response.rowCount(matrix) == 2);
	const auto matrixRow = response.index(1, 0, matrix);
	response.fetchMore(matrixRow);
	CHECK(response.rowCount(matrixRow) == 2 &&
	      response.data(response.index(1, 0, matrixRow), ui::ValueModel::ValueRole).toString() ==
	          QStringLiteral("-17"));
	CHECK(!response.data(response.index(1, 0, matrixRow), ui::ValueModel::EditableRole).toBool());
	CHECK(args.data(member(args, QStringLiteral("Gain")), ui::ValueModel::EditorTextRole)
	          .toString() == QStringLiteral("25"));
	const auto samples = member(response, QStringLiteral("Samples"));
	CHECK(samples.isValid() &&
	      response.data(samples, ui::ValueModel::ChildCountRole).toInt() == 300 &&
	      response.rowCount(samples) == 0);
	response.fetchMore(samples);
	CHECK(response.rowCount(samples) == 128);
	response.fetchMore(samples);
	CHECK(response.rowCount(samples) == 256);
	response.fetchMore(samples);
	CHECK(response.rowCount(samples) == 300 && !response.canFetchMore(samples));
	CHECK(response.data(response.index(299, 0, samples), ui::ValueModel::ValueRole).toString() ==
	      QStringLiteral("553"));
	const QPersistentModelIndex copied = member(response, QStringLiteral("Counter"));
	const auto screenshot = app.arguments().indexOf(QStringLiteral("--screenshot"));
	if (screenshot >= 0) {
		CHECK(screenshot + 1 < app.arguments().size());
		window->update();
		QTest::qWait(30);
		CHECK(window->grabWindow().save(app.arguments()[screenshot + 1]));
	}
	CHECK(controller.setScalarText(member(args, QStringLiteral("Counter")), QStringLiteral("7")));
	controller.pump();
	CHECK(response.data(copied, ui::ValueModel::ValueRole).toString() ==
	      QStringLiteral("18446744073709551614"));
	CHECK(reads.isEmpty() && commands.isEmpty() && !warnings);

	CHECK(controller.callService());
	controller.disconnectDevice();
	CHECK(finished.count() == 3 && !finished.last()[0].toBool() && !finished.last()[1].toBool());
	CHECK(controller.serviceOutcome().contains(QStringLiteral("Cancelled")) &&
	      response.rowCount() == 0);
	CHECK(controller.connectWritableDemo());
	CHECK(controller.serviceOutcome().startsWith(QStringLiteral("Previous session")) &&
	      controller.serviceOutcome().contains(QStringLiteral("Last Service control.Transform")));
	select(QStringLiteral("Reject"));
	call();
	CHECK(!finished.last()[0].toBool() && !finished.last()[1].toBool() && response.rowCount() == 0);
	CHECK(controller.serviceOutcome().contains(QStringLiteral("InvalidArgument")) &&
	      controller.serviceOutcome().contains(QStringLiteral("endpoint 1")) &&
	      !controller.serviceOutcome().contains(QStringLiteral("Accepted")));
	select(QStringLiteral("Ping"));
	call();
	CHECK(finished.last()[0].toBool() && !finished.last()[1].toBool() && response.rowCount() == 0 &&
	      controller.serviceOutcome().contains(QStringLiteral("Void")));
	select(QStringLiteral("Empty"));
	call();
	CHECK(finished.last()[0].toBool() && finished.last()[1].toBool() && response.rowCount() == 1 &&
	      response.rowCount(response.index(0, 0)) == 0);
	select(QStringLiteral("Transform"));
	controller.setDropReplies(true);
	CHECK(controller.callService());
	controller.pump();
	CHECK(controller.demoServiceCount() == 4 && finished.count() == 6);
	controller.disconnectDevice();
	CHECK(finished.count() == 7 && !finished.last()[0].toBool() && !finished.last()[1].toBool());
	CHECK(controller.serviceOutcome().startsWith(
	          QStringLiteral("Last Service control.Transform · OutcomeUnknown")) &&
	      response.rowCount() == 0);
	CHECK(controller.connectWritableDemo());
	QTest::qWait(30);
	CHECK(controller.demoServiceCount() == 0 && finished.count() == 7 &&
	      controller.serviceOutcome().startsWith(QStringLiteral("Previous session")) &&
	      controller.serviceOutcome().contains(
	          QStringLiteral("Last Service control.Transform · OutcomeUnknown")));
	const auto history = controller.serviceOutcome();
	select(QStringLiteral("Reject"));
	controller.setWritesEnabled(false);
	CHECK(!controller.callService() && controller.serviceOutcome() == history &&
	      controller.demoServiceCount() == 0);
	controller.disconnectDevice();
	CHECK(finished.count() == 7);

	ui::InspectorController reentrant;
	CHECK(reentrant.connectWritableDemo());
	auto& reentrantEndpoints = *qobject_cast<ui::EndpointModel*>(reentrant.endpoints());
	CHECK(reentrant.selectEndpoint(endpoint(reentrantEndpoints, QStringLiteral("Transform"))));
	unsigned notifications = 0;
	QObject::connect(&reentrant, &ui::InspectorController::serviceFinished, &app,
	                 [&](bool ok, bool published) {
		                 CHECK(ok && published);
		                 ++notifications;
		                 CHECK(reentrant.connectWritableDemo());
	                 });
	CHECK(reentrant.callService());
	reentrant.pump();
	CHECK(notifications == 1 && reentrant.demoServiceCount() == 0 &&
	      reentrant.responses()->rowCount() == 0 &&
	      reentrant.serviceOutcome().contains(QStringLiteral("Previous session")));
	auto* deleted = new ui::InspectorController;
	CHECK(deleted->connectWritableDemo());
	auto& deletedEndpoints = *qobject_cast<ui::EndpointModel*>(deleted->endpoints());
	CHECK(deleted->selectEndpoint(endpoint(deletedEndpoints, QStringLiteral("Transform"))));
	QObject::connect(deleted, &ui::InspectorController::serviceFinished, &app,
	                 [&](bool ok, bool published) {
		                 CHECK(ok && published);
		                 delete deleted;
		                 deleted = nullptr;
	                 });
	CHECK(deleted->callService());
	deleted->pump();
	CHECK(!deleted);
	CHECK(!warnings);
	std::printf("Inspector Service checks=%u completed=1 rejected=1 unknown=1 noEnter=1 Qt=%s\n",
	            checks, qVersion());
	return 0;
}
