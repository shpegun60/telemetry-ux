/*
 * @file WatchlistScheduling.cpp
 * @brief Individual-cycle scheduling guards and polling QML affordances.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "InspectorController.hpp"
#include <wire/EncodedWire.hpp>
#include <QtCore/QElapsedTimer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <array>
#include <cstdio>

namespace ui = inspector_qml;
namespace ew = telemetry::protocol::encoded;

namespace {
unsigned checks = 0, failures = 0;
const char* scenario = "setup";

bool check(bool passed, int line)
{
	++checks;
	if (!passed) {
		++failures;
		std::fprintf(stderr, "Inspector Watchlist scheduling scenario=%s check=%u line=%d failed\n",
		             scenario, checks, line);
	}
	return passed;
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
		    model.data(index, ui::EndpointModel::CategoryRole).toInt() == 1 &&
		    model.data(index, ui::EndpointModel::NameRole).toString() == name)
			return index;
		if (const auto child = endpoint(model, name, index); child.isValid())
			return child;
	}
	return {};
}

bool select(ui::InspectorController& controller, const QString& name)
{
	auto& model = *qobject_cast<ui::EndpointModel*>(controller.endpoints());
	const auto index = endpoint(model, name);
	return CHECK(index.isValid() && controller.selectEndpoint(index));
}

QString watchData(ui::InspectorController& controller, int row, int role)
{
	auto* model = controller.watchlist();
	return model->data(model->index(row, 0), role).toString();
}

QString watchText(ui::InspectorController& controller, int row)
{
	return watchData(controller, row, ui::WatchlistModel::TextRole);
}

std::array<quint32, 3> ids(ui::InspectorController& controller)
{
	std::array<quint32, 3> result{};
	for (int row = 0; row < 3; ++row) {
		auto* model = controller.watchlist();
		result[row] =
		    model->data(model->index(row, 0), ui::WatchlistModel::EndpointIdRole).toUInt();
	}
	return result;
}

bool prepareWindow(ui::InspectorController& controller)
{
	if (!CHECK(controller.connectReadManyDemo()))
		return false;
	controller.setReplyDelay(0);
	controller.setWatchIndividual(true);
	for (const auto* name : {"Counter", "Offset", "Mode"})
		if (!select(controller, QString::fromLatin1(name)) || !CHECK(controller.addWatch()))
			return false;
	if (!select(controller, QStringLiteral("U64_0")) || !CHECK(controller.canAddWatch()))
		return false;
	if (!CHECK(controller.refreshWatch() && controller.watchPending() && !controller.busy()))
		return false;
	// Enqueue foreground work behind the first watch read without pumping events.
	if (!CHECK(controller.refresh() && controller.busy() && controller.watchPending()))
		return false;
	controller.pump();
	// One manual pump completes Counter and offers the foreground read. The
	// individual cycle still owes Offset and Mode, but owns no pending request.
	if (!CHECK(!controller.watchPending() && !controller.watchRunning() && controller.busy() &&
	           controller.inspector().pending() == 1))
		return false;
	CHECK(watchText(controller, 0) == "18446744073709551615");
	CHECK(watchText(controller, 1).isEmpty() && watchText(controller, 2).isEmpty());
	const auto offered = controller.inspector().nextPacket();
	const auto parsed = ew::parse(offered.bytes);
	return CHECK(offered && parsed && parsed.header.operation == ew::Operation::FieldRead &&
	             parsed.header.endpoint == controller.inspector().snapshotInfo().fieldId);
}

enum class Attempt {
	Remove,
	Mode,
	Refresh,
	Start,
	Clear,
	Add,
	Frequency
};

void cycleGuard(Attempt attempt, const char* name)
{
	scenario = name;
	ui::InspectorController controller;
	if (!prepareWindow(controller))
		return;
	const auto originalIds = ids(controller);
	const auto originalStatus = controller.watchStatus();
	const auto originalInterval = controller.watchIntervalMs();
	const auto selected = controller.inspector().snapshotInfo().fieldId;
	QSignalSpy changed{&controller, &ui::InspectorController::stateChanged};
	QList<quint32> completed;
	QObject::connect(controller.watchlist(), &QAbstractItemModel::dataChanged, &controller,
	                 [&](const QModelIndex& first, const QModelIndex&, const QList<int>&) {
		                 auto* model = controller.watchlist();
		                 if (model->data(first, ui::WatchlistModel::StatusRole) == "Ok")
			                 completed.push_back(
			                     model->data(first, ui::WatchlistModel::EndpointIdRole).toUInt());
	                 });
	switch (attempt) {
		case Attempt::Remove:
			CHECK(!controller.removeWatch(0));
			break;
		case Attempt::Mode:
			controller.setWatchIndividual(false);
			break;
		case Attempt::Refresh:
			CHECK(!controller.refreshWatch());
			break;
		case Attempt::Start:
			CHECK(!controller.startWatch());
			break;
		case Attempt::Clear:
			controller.clearWatch();
			break;
		case Attempt::Frequency:
			controller.setWatchIntervalMs(100);
			break;
		case Attempt::Add:
			CHECK(!controller.canAddWatch());
			CHECK(!controller.addWatch());
			break;
	}
	CHECK(controller.watchCount() == 3 && ids(controller) == originalIds);
	CHECK(controller.watchIndividual() && !controller.watchRunning() && !controller.watchPending());
	CHECK(controller.watchIntervalMs() == originalInterval);
	CHECK(controller.watchStatus() == originalStatus && changed.count() == 0);
	CHECK(controller.busy() && controller.inspector().pending() == 1);
	CHECK(controller.inspector().snapshotInfo().fieldId == selected);
	// No event loop or wall-clock wait: foreground completion must resume the
	// original cursor at Offset, then Mode, with no skipped or repeated row.
	for (int step = 0; step < 5; ++step)
		controller.pump();
	CHECK(!controller.busy() && !controller.watchPending() && !controller.watchRunning());
	CHECK(controller.inspector().pending() == 0 && !controller.inspector().recoveryRequired());
	CHECK((completed == QList<quint32>{originalIds[1], originalIds[2]}));
	CHECK(watchText(controller, 0) == "18446744073709551615");
	CHECK(watchText(controller, 1) == "-9223372036854775808");
	CHECK(watchText(controller, 2) == "-2 (unknown)");
	CHECK(controller.watchStatus().startsWith("Individual FieldRead cycle complete"));
	CHECK(controller.values()->rowCount() > 0);
	CHECK(controller.canAddWatch());
	controller.disconnectDevice();
}

void stopWindow()
{
	scenario = "stop-paused-cycle";
	ui::InspectorController controller;
	if (!prepareWindow(controller))
		return;
	const auto originalIds = ids(controller);
	controller.stopWatch();
	CHECK(controller.watchCount() == 3 && ids(controller) == originalIds);
	CHECK(!controller.watchRunning() && !controller.watchPending() && controller.busy());
	for (int step = 0; step < 5; ++step)
		controller.pump();
	CHECK(!controller.busy() && !controller.watchPending() &&
	      controller.inspector().pending() == 0);
	CHECK(controller.watchStatus() == "Stopped.");
	CHECK(watchText(controller, 0) == "18446744073709551615");
	CHECK(watchText(controller, 1).isEmpty() && watchText(controller, 2).isEmpty());
	CHECK(!controller.inspector().recoveryRequired());
	CHECK(controller.removeWatch(2) && controller.watchCount() == 2);
	controller.disconnectDevice();

	scenario = "stop-pending-cycle";
	CHECK(controller.connectReadManyDemo());
	controller.setWatchIndividual(true);
	if (!select(controller, QStringLiteral("Counter")))
		return;
	CHECK(controller.addWatch() && controller.startWatch() && controller.watchPending());
	controller.stopWatch();
	CHECK(!controller.watchRunning() && controller.watchPending());
	for (int step = 0; step < 3; ++step)
		controller.pump();
	CHECK(!controller.watchPending() && controller.inspector().pending() == 0);
	CHECK(watchText(controller, 0).isEmpty() && !controller.inspector().recoveryRequired());
	controller.disconnectDevice();
}

QQuickItem* visual(QQuickItem* parent, const QString& name)
{
	if (parent->isVisible() && parent->objectName() == name)
		return parent;
	for (auto* child : parent->childItems())
		if (auto* found = visual(child, name))
			return found;
	return nullptr;
}

QQuickItem* control(QQuickWindow& window, const QString& name)
{
	QQuickItem* item = nullptr;
	CHECK(await([&] {
		item = visual(window.contentItem(), name);
		return item && item->width() > 0 && item->height() > 0;
	}));
	return item;
}

void qmlPollingGap()
{
	scenario = "qml-between-polls";
	ui::InspectorController controller;
	CHECK(controller.connectReadManyDemo());
	if (!select(controller, QStringLiteral("Counter")) || !CHECK(controller.addWatch()))
		return;
	controller.setWatchIntervalMs(10000);
	QQmlApplicationEngine engine;
	bool warnings = false;
	QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine,
	                 [&](const QList<QQmlError>& errors) {
		                 warnings = true;
		                 for (const auto& error : errors)
			                 std::fprintf(stderr, "%s\n", qPrintable(error.toString()));
	                 });
	engine.rootContext()->setContextProperty("controller", &controller);
	engine.load(QUrl::fromLocalFile(QStringLiteral(INSPECTOR_QML_SOURCE_FILE)));
	if (!CHECK(engine.rootObjects().size() == 1))
		return;
	auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
	if (!CHECK(window && QTest::qWaitForWindowExposed(window)))
		return;
	window->resize(1280, 960);
	auto* expanded = control(*window, "watchlistExpanded");
	if (!expanded)
		return;
	CHECK(expanded->setProperty("checked", true));
	CHECK(controller.startWatch());
	controller.pump();
	CHECK(controller.watchRunning() && !controller.watchPending());
	CHECK(watchText(controller, 0) == "18446744073709551615");
	for (const auto* name : {"refreshWatchButton", "clearWatchButton", "removeWatchButton"}) {
		auto* item = control(*window, QString::fromLatin1(name));
		if (!item)
			continue;
		CHECK(controller.watchRunning() && !controller.watchPending());
		CHECK(!item->isEnabled());
		const auto point = item->mapToScene({item->width() / 2, item->height() / 2}).toPoint();
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
		CHECK(controller.watchCount() == 1 && controller.watchRunning() &&
		      !controller.watchPending());
		CHECK(watchText(controller, 0) == "18446744073709551615");
	}
	auto* stop = control(*window, "startWatchButton");
	if (stop) {
		CHECK(stop->isEnabled() && stop->property("text") == "Stop");
		const auto point = stop->mapToScene({stop->width() / 2, stop->height() / 2}).toPoint();
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
	}
	CHECK(!controller.watchRunning() && !controller.watchPending() && !warnings);
	controller.disconnectDevice();
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app{argc, argv};
	QQuickStyle::setStyle("Basic");
	cycleGuard(Attempt::Remove, "remove");
	cycleGuard(Attempt::Mode, "mode");
	cycleGuard(Attempt::Refresh, "refresh");
	cycleGuard(Attempt::Start, "start");
	cycleGuard(Attempt::Clear, "clear");
	cycleGuard(Attempt::Add, "add");
	cycleGuard(Attempt::Frequency, "frequency");
	stopWindow();
	qmlPollingGap();
	std::printf(
	    "Inspector Watchlist scheduling checks=%u failures=%u cycleWindow=1 continuation=1 stop=1 qmlGap=1 Qt=%s\n",
	    checks, failures, qVersion());
	return failures ? 1 : 0;
}
