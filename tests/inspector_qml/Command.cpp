/*
 * @file Command.cpp
 * @brief Explicit Command outcomes and real key/mouse Request editing checks.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "InspectorController.hpp"
#include <QtCore/QElapsedTimer>
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
		std::fprintf(stderr, "Inspector Command check %u failed at line %d\n", checks, line);
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
		    model.data(index, ui::EndpointModel::CategoryRole).toInt() == 2 &&
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
	CHECK(await([&] {
		return editor->hasActiveFocus();
	}));
	QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClick(&window, Qt::Key_Backspace);
	for (const char key : text)
		QTest::keyClick(&window, key);
	CHECK(editor->property("text").toString() == QString::fromLatin1(text));
	CHECK(editor->hasActiveFocus()); // No Enter, blur or direct signal invocation.
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	ui::InspectorController controller;
	CHECK(controller.connectWritableDemo());
	auto& endpoints = *qobject_cast<ui::EndpointModel*>(controller.endpoints());
	auto& values = *qobject_cast<ui::ValueModel*>(controller.values());
	QSignalSpy completed{&controller, &ui::InspectorController::commandFinished};
	QSignalSpy reads{&controller, &ui::InspectorController::readFinished};
	const auto select = [&](const QString& name) {
		const auto found = endpoint(endpoints, name);
		CHECK(found.isValid());
		CHECK(controller.selectEndpoint(found));
		CHECK(controller.commandSelected() && !controller.editing() && !controller.canRefresh());
	};
	select(QStringLiteral("Execute"));
	CHECK(controller.demoCommandCount() == 0 && controller.canExecute());
	CHECK(values.data(values.index(0, 0), ui::ValueModel::NameRole).toString() ==
	      QStringLiteral("Request"));
	CHECK(values.data(member(values, QStringLiteral("Gain")), ui::ValueModel::EditorTextRole)
	          .toString() == QStringLiteral("0"));
	CHECK(values.data(member(values, QStringLiteral("Counter")), ui::ValueModel::EditorTextRole)
	          .toString() == QStringLiteral("0"));
	CHECK(controller.executeCommand()); // Unchanged canonical arguments are valid.
	CHECK(controller.commandOutcome().startsWith(
	    QStringLiteral("Last Command control.Execute · Pending")));
	CHECK(!controller.executeCommand());
	CHECK(await([&] {
		return completed.count() == 1 && !controller.busy();
	}));
	CHECK(completed.last()[0].toBool() && !completed.last()[1].toBool());
	CHECK(controller.demoCommandCount() == 1 && controller.demoExecutedCount() == 1 &&
	      controller.demoAcceptedCount() == 0);
	CHECK(controller.demoCommandGain() == 0 && controller.demoCommandCounter() == 0);
	CHECK(controller.commandOutcome().startsWith(
	          QStringLiteral("Last Command control.Execute · Executed")) &&
	      reads.count() == 0);
	const auto executedOutcome = controller.commandOutcome();
	select(QStringLiteral("Accept"));
	CHECK(controller.commandOutcome() == executedOutcome);
	controller.setWritesEnabled(false);
	CHECK(!controller.executeCommand() && controller.commandOutcome() == executedOutcome);
	controller.setWritesEnabled(true);
	CHECK(controller.canExecute() && controller.executeCommand());
	CHECK(controller.commandOutcome().startsWith(
	    QStringLiteral("Last Command control.Accept · Pending")));
	CHECK(await([&] {
		return completed.count() == 2 && !controller.busy();
	}));
	CHECK(!completed.last()[0].toBool() && completed.last()[1].toBool());
	CHECK(controller.commandOutcome().startsWith(
	    QStringLiteral("Last Command control.Accept · Accepted")));
	CHECK(controller.demoCommandCount() == 2 && controller.demoExecutedCount() == 1 &&
	      controller.demoAcceptedCount() == 1);
	for (const auto& name : {QStringLiteral("Ping"), QStringLiteral("Empty")}) {
		const auto previous = controller.commandOutcome();
		select(name);
		CHECK(controller.commandOutcome() == previous);
		const auto count = completed.count();
		CHECK(controller.canExecute() && controller.executeCommand());
		CHECK(controller.commandOutcome().startsWith(
		    QStringLiteral("Last Command control.%1 · Pending").arg(name)));
		CHECK(await([&] {
			return completed.count() == count + 1 && !controller.busy();
		}));
		CHECK(completed.last()[0].toBool() && !completed.last()[1].toBool());
		CHECK(controller.commandOutcome().startsWith(
		    QStringLiteral("Last Command control.%1 · Executed").arg(name)));
	}
	CHECK(controller.demoCommandCount() == 4 && reads.count() == 0);

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
	const auto previousCommand = controller.commandOutcome();
	select(QStringLiteral("Execute"));
	type(*window, QStringLiteral("Gain"), QByteArrayLiteral("4.25"));
	type(*window, QStringLiteral("Counter"), QByteArrayLiteral("18446744073709551616"));
	CHECK(!controller.canExecute());
	click(*window, *control(*window, QStringLiteral("scalarEditor"), QStringLiteral("Gain")));
	CHECK(!controller.canExecute());
	CHECK(control(*window, QStringLiteral("scalarEditor"), QStringLiteral("Counter"))
	          ->property("text")
	          .toString() == QStringLiteral("18446744073709551616"));
	type(*window, QStringLiteral("Counter"), QByteArrayLiteral("18446744073709551614"));
	CHECK(controller.canExecute());
	CHECK(controller.commandOutcome() == previousCommand);
	const auto screenshot = app.arguments().indexOf(QStringLiteral("--screenshot"));
	if (screenshot >= 0) {
		CHECK(screenshot + 1 < app.arguments().size());
		window->update();
		QTest::qWait(30);
		CHECK(window->grabWindow().save(app.arguments()[screenshot + 1]));
	}
	click(*window, *control(*window, QStringLiteral("executeButton")));
	CHECK(await([&] {
		return completed.count() == 5 && !controller.busy();
	}));
	CHECK(controller.demoCommandCount() == 5 && controller.demoCommandGain() == 4.25f &&
	      controller.demoCommandCounter() == UINT64_C(18446744073709551614));
	CHECK(controller.commandOutcome().startsWith(
	    QStringLiteral("Last Command control.Execute · Executed")));
	CHECK(reads.count() == 0 && !warnings);

	controller.setDropReplies(true);
	CHECK(controller.executeCommand());
	controller.pump(); // Real native dispatch, deliberately no correlated reply.
	CHECK(controller.demoCommandCount() == 6 && completed.count() == 5);
	controller.disconnectDevice();
	CHECK(completed.count() == 6 && !completed.last()[0].toBool() && !completed.last()[1].toBool());
	CHECK(controller.commandOutcome().startsWith(
	          QStringLiteral("Last Command control.Execute · OutcomeUnknown")) &&
	      !controller.busy());
	CHECK(controller.connectWritableDemo());
	CHECK(controller.commandOutcome().startsWith(QStringLiteral("Previous session")) &&
	      controller.commandOutcome().contains(
	          QStringLiteral("Last Command control.Execute · OutcomeUnknown")));
	QTest::qWait(30);
	CHECK(controller.demoCommandCount() == 0 && completed.count() == 6); // No replay.
	const auto history = controller.commandOutcome();
	select(QStringLiteral("Ping"));
	CHECK(controller.commandOutcome() == history);
	controller.setWritesEnabled(false);
	CHECK(!controller.canExecute() && !controller.executeCommand());
	CHECK(controller.demoCommandCount() == 0 && completed.count() == 6 &&
	      controller.commandOutcome() == history);
	controller.setWritesEnabled(true);
	CHECK(controller.executeCommand());
	CHECK(controller.commandOutcome().startsWith(
	          QStringLiteral("Last Command control.Ping · Pending")) &&
	      !controller.commandOutcome().contains(QStringLiteral("Previous session")));
	controller.disconnectDevice(); // No process/handoff occurred.
	CHECK(completed.count() == 7 && !completed.last()[0].toBool() && !completed.last()[1].toBool());
	CHECK(controller.commandOutcome().contains(QStringLiteral("Cancelled")) &&
	      !controller.commandOutcome().contains(QStringLiteral("OutcomeUnknown")));
	CHECK(reads.count() == 0 && !warnings);

	// Terminal callbacks run outside the core and precede connection-change
	// signals. A slot may immediately replace or destroy the Qt owner.
	ui::InspectorController reentrant;
	CHECK(reentrant.connectWritableDemo());
	auto& reentrantEndpoints = *qobject_cast<ui::EndpointModel*>(reentrant.endpoints());
	CHECK(reentrant.selectEndpoint(endpoint(reentrantEndpoints, QStringLiteral("Accept"))));
	unsigned notifications = 0;
	QObject::connect(&reentrant, &ui::InspectorController::commandFinished, &app,
	                 [&](bool executed, bool accepted) {
		                 CHECK(!executed && accepted);
		                 ++notifications;
		                 CHECK(reentrant.connectWritableDemo());
	                 });
	CHECK(reentrant.executeCommand());
	reentrant.pump();
	CHECK(notifications == 1 && reentrant.demoCommandCount() == 0);
	CHECK(reentrant.commandOutcome().contains(QStringLiteral("Previous session")) &&
	      reentrant.commandOutcome().contains(
	          QStringLiteral("Last Command control.Accept · Accepted")));
	auto* deleted = new ui::InspectorController;
	CHECK(deleted->connectWritableDemo());
	auto& deletedEndpoints = *qobject_cast<ui::EndpointModel*>(deleted->endpoints());
	CHECK(deleted->selectEndpoint(endpoint(deletedEndpoints, QStringLiteral("Ping"))));
	QObject::connect(deleted, &ui::InspectorController::commandFinished, &app,
	                 [&](bool executed, bool accepted) {
		                 CHECK(executed && !accepted);
		                 delete deleted;
		                 deleted = nullptr;
	                 });
	CHECK(deleted->executeCommand());
	deleted->pump();
	CHECK(!deleted);
	std::printf("Inspector Command checks=%u executed=1 accepted=1 unknown=1 noEnter=1 Qt=%s\n",
	            checks, qVersion());
	return 0;
}
