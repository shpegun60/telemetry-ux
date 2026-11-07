/*
 * @file Watchlist.cpp
 * @brief Bounded watch rows, real controller lifecycle and shipped QML controls.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "InspectorController.hpp"
#include "DemoPeer.hpp"
#include <wire/EncodedWire.hpp>
#include <wire/ReadManyWire.hpp>
#include <QtCore/QElapsedTimer>
#include <QtCore/QPersistentModelIndex>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QAbstractItemModelTester>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace ui = inspector_qml;
namespace rt = telemetry::runtime;
namespace ew = telemetry::protocol::encoded;

namespace {
unsigned checks = 0;

void check(bool passed, int line)
{
	++checks;
	if (!passed) {
		std::fprintf(stderr, "Inspector Watchlist check %u failed at line %d\n", checks, line);
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

void modelChecks()
{
	ui::WatchlistModel model;
	QAbstractItemModelTester tester{&model, QAbstractItemModelTester::FailureReportingMode::Fatal};
	CHECK(model.count() == 0 && model.rowCount() == 0);
	CHECK(model.roleNames()[ui::WatchlistModel::EndpointIdRole] == "endpointId");
	CHECK(model.roleNames()[ui::WatchlistModel::NameRole] == "name");
	CHECK(model.roleNames()[ui::WatchlistModel::StatusRole] == "status");
	CHECK(model.roleNames()[ui::WatchlistModel::TextRole] == "text");
	bool guarded = false;
	const auto inserting =
	    QObject::connect(&model, &QAbstractItemModel::rowsAboutToBeInserted, &model, [&] {
		    guarded = model.changing() && !model.add(999, "reentrant");
		    model.clear();
	    });
	QString name = QStringLiteral("wide.Counter");
	CHECK(model.add(0, name)); // Field ID zero is a valid first-catalog ID.
	name.fill(QLatin1Char('x'));
	CHECK(guarded && !model.changing() && model.count() == 1);
	QObject::disconnect(inserting);
	CHECK(model.data(model.index(0, 0), ui::WatchlistModel::NameRole) == "wide.Counter");
	CHECK(!model.add(0, "duplicate") && !model.add(2, {}));
	QSignalSpy changed{&model, &QAbstractItemModel::dataChanged};
	QString value = QStringLiteral("18446744073709551615");
	CHECK(model.setResult(0, "Ok", value));
	value.fill(QLatin1Char('0'));
	CHECK(model.data(model.index(0, 0), ui::WatchlistModel::TextRole).toString() ==
	      "18446744073709551615");
	CHECK(changed.count() == 1 && !model.setResult(-1, {}, {}));
	bool resultGuard = false;
	const auto notification =
	    QObject::connect(&model, &QAbstractItemModel::dataChanged, &model, [&] {
		    resultGuard = model.changing() && !model.remove(0) && !model.setResult(0, {}, {});
	    });
	model.invalidateValues();
	CHECK(resultGuard && model.data(model.index(0, 0), ui::WatchlistModel::StatusRole) == "Stale");
	CHECK(model.data(model.index(0, 0), ui::WatchlistModel::TextRole).toString().isEmpty());
	QObject::disconnect(notification);
	for (int row = 1; row < 65; ++row)
		CHECK(model.add(quint32(row), QString::number(row)));
	CHECK(model.count() == 65 && !model.add(64, "duplicate"));
	const auto stale = model.index(0, 0);
	QPersistentModelIndex next{model.index(1, 0)};
	CHECK(model.remove(0) && model.count() == 64 && model.id(0) == 1);
	CHECK(!model.data(stale).isValid() && next.isValid() && next.row() == 0);
	CHECK(model.data(next, ui::WatchlistModel::EndpointIdRole).toUInt() == 1);
	CHECK(!model.index(0, 1).isValid() && model.rowCount(model.index(0, 0)) == 0);
	model.clear();
	CHECK(model.count() == 0 && !next.isValid());
	CHECK(model.add(1, "replacement") && !model.data(stale).isValid());
	CHECK(ui::WatchlistModel::formatValue({}).isEmpty());
}

void formattingChecks()
{
	ui::DemoPeer peer{false, true};
	CHECK(peer.readManyEnabled() && !peer.writable());
	std::array<rt::RuntimeType, 128> types{};
	std::array<rt::RuntimeMember, 256> members{};
	std::array<rt::RuntimeEnumEntry, 32> enums{};
	std::array<rt::RuntimeCatalog, 16> catalogs{};
	std::array<rt::RuntimeEndpoint, 64> endpoints{};
	rt::RuntimeSchemaWorkspace metadata{types, members, enums, catalogs, endpoints};
	const auto parsed = rt::RuntimeSchema::parse(peer.descriptor(), metadata);
	CHECK(parsed);
	const auto text = [&](std::string_view group, std::string_view name) {
		const auto* field = parsed.schema.findField(group, name);
		CHECK(field);
		const auto* type = parsed.schema.type(field->typeId);
		CHECK(type);
		std::array<std::byte, 32> request{};
		std::array<std::byte, 64> reply{};
		ew::Header header{};
		header.operation = ew::Operation::FieldRead;
		header.session = 7;
		header.requestId = 1;
		header.endpoint = field->id;
		header.expectedResponseBytes = type->wireBytes;
		CHECK(ew::writeHeader(request, header) == ew::Status::Ok);
		const auto written = peer.dispatch(request, reply, 7);
		CHECK(written != 0);
		const auto response = ew::parse(std::span<const std::byte>{reply}.first(written));
		CHECK(response);
		const auto value = parsed.schema.view(field->typeId, response.payload);
		CHECK(value);
		return ui::WatchlistModel::formatValue(value.value);
	};
	CHECK(text("wide", "U64_0") == "18446744073709551615");
	CHECK(text("signed", "S64_0") == "-9223372036854775808");
	CHECK(text("flags", "Bool_0") == "true" && text("flags", "Bool_1") == "false");
	CHECK(text("modes", "Enum_0") == "-2 (unknown)");
	CHECK(text("real", "F32_0") == "NaN (0x7fc12345)");
	CHECK(text("real", "F32_1") == "-0 (0x80000000)");
	CHECK(text("real", "F32_2") == "1.25" && text("real", "F32_3") == "-inf");
	CHECK(text("precise", "F64_0") == "NaN (0x7ff8123456789abc)");
	CHECK(text("precise", "F64_1") == "-0 (0x8000000000000000)");
	CHECK(text("precise", "F64_2") == "2.5" && text("precise", "F64_3") == "inf");
	ui::DemoPeer legacy;
	CHECK(!legacy.readManyEnabled() && legacy.descriptor().size() < peer.descriptor().size());
}

QModelIndex endpoint(ui::EndpointModel& model, const QString& name, int category = 1,
                     const QModelIndex& parent = {})
{
	for (int row = 0; row < model.rowCount(parent); ++row) {
		const auto index = model.index(row, 0, parent);
		if (model.data(index, ui::EndpointModel::IsEndpointRole).toBool() &&
		    model.data(index, ui::EndpointModel::CategoryRole).toInt() == category &&
		    model.data(index, ui::EndpointModel::NameRole).toString() == name)
			return index;
		if (const auto child = endpoint(model, name, category, index); child.isValid())
			return child;
	}
	return {};
}

void select(ui::InspectorController& controller, const QString& name, int category = 1)
{
	auto& model = *qobject_cast<ui::EndpointModel*>(controller.endpoints());
	const auto index = endpoint(model, name, category);
	CHECK(index.isValid() && controller.selectEndpoint(index));
}

QString watchText(ui::InspectorController& controller, int row)
{
	auto* model = controller.watchlist();
	return model->data(model->index(row, 0), ui::WatchlistModel::TextRole).toString();
}

void controllerChecks()
{
	ui::InspectorController controller;
	CHECK(controller.connectReadManyDemo() && controller.connected());
	for (const auto* name : {"U64_0", "S64_0", "Enum_0", "F32_0", "F64_1"}) {
		select(controller, QString::fromLatin1(name));
		CHECK(controller.canAddWatch() && controller.addWatch());
		CHECK(!controller.canAddWatch() && !controller.addWatch());
	}
	for (const auto& entry :
	     {std::pair{"Voltage", 1}, std::pair{"Calibrate", 2}, std::pair{"Identity", 3}}) {
		select(controller, QString::fromLatin1(entry.first), entry.second);
		CHECK(!controller.canAddWatch() && !controller.addWatch());
	}
	select(controller, "Counter");
	CHECK(controller.refresh() && await([&] {
		      return !controller.busy();
	      }));
	const auto snapshot = controller.inspector().snapshotInfo();
	const auto valueRows = controller.values()->rowCount();
	CHECK(controller.watchCount() == 5 && controller.refreshWatch());
	CHECK(await([&] {
		return !controller.watchPending();
	}));
	CHECK(watchText(controller, 0) == "18446744073709551615");
	CHECK(watchText(controller, 1) == "-9223372036854775808");
	CHECK(watchText(controller, 2) == "-2 (unknown)");
	CHECK(watchText(controller, 3) == "NaN (0x7fc12345)");
	CHECK(watchText(controller, 4) == "-0 (0x8000000000000000)");
	CHECK(controller.inspector().snapshotInfo().bytes == snapshot.bytes);
	CHECK(controller.values()->rowCount() == valueRows);
	controller.setWatchIntervalMs(100);
	controller.setReplyDelay(350);
	QSignalSpy valuesChanged{controller.watchlist(), &QAbstractItemModel::dataChanged};
	CHECK(controller.startWatch() && controller.watchPending());
	QElapsedTimer slow;
	slow.start();
	std::size_t maximum = 0;
	while (slow.elapsed() < 260) {
		QCoreApplication::processEvents();
		maximum = std::max(maximum, controller.inspector().pending());
		QTest::qWait(1);
	}
	CHECK(maximum == 1 && controller.watchPending() && valuesChanged.count() == 0);
	controller.stopWatch();
	CHECK(!controller.watchRunning() && controller.watchPending());
	CHECK(await([&] {
		return !controller.watchPending();
	}));
	CHECK(valuesChanged.count() == 0 && controller.inspector().pending() == 0);
	CHECK(!controller.inspector().recoveryRequired());
	controller.setReplyDelay(0);
	controller.clearWatch();
	CHECK(controller.watchCount() == 0);
	for (const auto& group : {std::pair{"U64", 8}, std::pair{"S64", 8}, std::pair{"Bool", 4},
	                          std::pair{"Enum", 4}, std::pair{"F32", 4}, std::pair{"F64", 4}})
		for (int row = 0; row < group.second; ++row) {
			select(controller, QStringLiteral("%1_%2").arg(group.first).arg(row));
			CHECK(controller.addWatch());
		}
	CHECK(controller.watchCount() == 32);
	select(controller, "Counter");
	CHECK(controller.canAddWatch() && controller.addWatch() && controller.watchCount() == 33);
	CHECK(controller.refreshWatch() && await([&] {
		      return !controller.watchActive();
	      }));
	CHECK(controller.removeWatch(32) && controller.watchCount() == 32);
	CHECK(!watchText(controller, 0).isEmpty());
	controller.inspector().invalidate();
	// No event-loop pump may be needed to hide cached values of a revoked
	// admission. Metadata labels remain owned until the controller resets them.
	CHECK(watchText(controller, 0).isEmpty());
	CHECK(controller.watchlist()->data(controller.watchlist()->index(0, 0),
	                                   ui::WatchlistModel::StatusRole) == "Stale");
	CHECK(!controller.watchlist()
	           ->data(controller.watchlist()->index(0, 0), ui::WatchlistModel::NameRole)
	           .toString()
	           .isEmpty());
	bool staleDuringReset = false;
	const auto resetting =
	    QObject::connect(controller.watchlist(), &QAbstractItemModel::modelAboutToBeReset,
	                     controller.watchlist(), [&] {
		                     staleDuringReset = watchText(controller, 0).isEmpty();
	                     });
	CHECK(controller.connectReadManyDemo() && controller.watchCount() == 0);
	CHECK(staleDuringReset);
	QObject::disconnect(resetting);
	CHECK(controller.connectDemo() && controller.watchCount() == 0);
	select(controller, "Counter");
	CHECK(controller.addWatch());
	CHECK(!controller.refreshWatch() && !controller.watchPending()); // no hidden fallback
	controller.setWatchIndividual(true);
	CHECK(controller.refreshWatch() && await([&] {
		      return !controller.watchPending();
	      }));
	CHECK(watchText(controller, 0) == "18446744073709551615");
	controller.setReplyDelay(150);
	CHECK(controller.refreshWatch());
	controller.disconnectDevice();
	QTest::qWait(180);
	CHECK(!controller.watchPending() && !controller.watchRunning() && controller.watchCount() == 0);
	CHECK(controller.connectWritableDemo());
	select(controller, "Config");
	CHECK(controller.refresh());
	CHECK(await([&] {
		return !controller.busy();
	}));
	CHECK(controller.beginEdit() && controller.editing());
	CHECK(controller.watchCount() == 0 && !controller.canAddWatch() && !controller.refreshWatch());
	CHECK(controller.editing() && controller.demoWriteCount() == 0);
	controller.cancelEdit();
	controller.disconnectDevice();
}

void largeControllerChecks()
{
	ui::InspectorController controller;
	CHECK(controller.connectReadManyDemo(true));
	controller.setWatchIntervalMs(1);
	CHECK(controller.watchIntervalMs() == 1);
	controller.setWatchIntervalMs(0);
	CHECK(controller.watchIntervalMs() == 1);
	controller.setWatchIntervalMs(60001);
	CHECK(controller.watchIntervalMs() == 1);
	controller.setWatchIntervalMs(60000);
	CHECK(controller.watchIntervalMs() == 60000);
	const auto fill = [&](int count) {
		controller.clearWatch();
		CHECK(controller.watchCount() == 0);
		for (int row = 0; row < count; ++row) {
			select(controller, QStringLiteral("Value_%1").arg(row));
			CHECK(controller.addWatch());
		}
		CHECK(controller.watchCount() == count);
	};
	const auto packet = [&](int cursor, int count, bool individual = false) {
		const auto offered = controller.inspector().nextPacket();
		const auto parsed = ew::parse(offered.bytes);
		CHECK(offered && parsed && controller.inspector().pending() == 1);
		if (individual) {
			CHECK(parsed.header.operation == ew::Operation::FieldRead);
			CHECK(parsed.header.endpoint == controller.watchlist()
			                                    ->data(controller.watchlist()->index(cursor, 0),
			                                           ui::WatchlistModel::EndpointIdRole)
			                                    .toUInt());
		} else {
			CHECK(parsed.header.operation == ew::Operation::ReadMany);
			const auto ids = ew::read_many::parseRequest(parsed.payload);
			CHECK(ids && ids.count == count);
			for (int row = 0; row < count; ++row)
				CHECK(ids.ids[row] == controller.watchlist()
				                          ->data(controller.watchlist()->index(cursor + row, 0),
				                                 ui::WatchlistModel::EndpointIdRole)
				                          .toUInt());
		}
		return offered;
	};
	for (const auto count : {1, 32, 33, 65}) {
		fill(count);
		CHECK(controller.refreshWatch());
		for (int cursor = 0; cursor < count; cursor += 32) {
			packet(cursor, std::min(32, count - cursor));
			CHECK(!controller.removeWatch(0));
			controller.pump();
		}
		CHECK(!controller.watchActive() && controller.inspector().pending() == 0);
		CHECK(controller.watchStatus().startsWith("ReadMany complete"));
		for (int row = 0; row < count; ++row)
			CHECK(watchText(controller, row) == QString::number(UINT64_MAX - row));
	}
	// The next packet is already accepted and prepared before Stop. Its real
	// Encoded reply must drain without publishing or issuing the final chunk.
	CHECK(controller.refreshWatch());
	controller.pump();
	const auto second = packet(32, 32);
	CHECK(controller.inspector().prepareHandoff(second.session, second.requestId));
	CHECK(controller.inspector().onSendAccepted(second.session, second.requestId));
	ui::DemoPeer peer{false, true, true};
	std::array<std::byte, 1024> reply{};
	const auto written = peer.dispatch(second.bytes, reply, second.session);
	CHECK(written != 0);
	QSignalSpy changed{controller.watchlist(), &QAbstractItemModel::dataChanged};
	controller.stopWatch();
	CHECK(controller.watchPending() && !controller.removeWatch(0));
	CHECK(controller.inspector().onReply(std::span<const std::byte>{reply}.first(written),
	                                     second.session, second.requestId));
	controller.pump();
	CHECK(!controller.watchActive() && controller.inspector().pending() == 0);
	CHECK(changed.count() == 0 && !controller.inspector().nextPacket());
	CHECK(!controller.inspector().recoveryRequired());
	// An explicit Individual cycle traverses the same complete host list.
	controller.setWatchIndividual(true);
	CHECK(controller.refreshWatch());
	for (int row = 0; row < 65; ++row) {
		packet(row, 1, true);
		controller.pump();
	}
	CHECK(!controller.watchActive() && controller.inspector().pending() == 0);
	CHECK(watchText(controller, 64) == QString::number(UINT64_MAX - 64));
	// A malformed middle chunk retires the earlier successful prefix and never
	// issues the final packet. No wall clock or event-loop race selects this phase.
	controller.setWatchIndividual(false);
	CHECK(controller.refreshWatch());
	controller.pump();
	const auto broken = packet(32, 32);
	CHECK(controller.inspector().prepareHandoff(broken.session, broken.requestId));
	CHECK(controller.inspector().onSendAccepted(broken.session, broken.requestId));
	const auto malformed = peer.dispatch(broken.bytes, reply, broken.session);
	CHECK(malformed > 34);
	reply[32] = std::byte{0};
	reply[33] = std::byte{0};
	CHECK(controller.inspector().onReply(std::span<const std::byte>{reply}.first(malformed),
	                                     broken.session, broken.requestId));
	controller.pump();
	CHECK(!controller.watchActive() && controller.inspector().pending() == 0);
	for (int row = 0; row < 65; ++row)
		CHECK(watchText(controller, row).isEmpty());
	CHECK(!controller.inspector().nextPacket() &&
	      !controller.watchStatus().contains("OutcomeUnknown"));
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

void click(QQuickWindow& window, QQuickItem& item)
{
	QCoreApplication::processEvents();
	const auto polish = [](auto&& self, QQuickItem* node) -> void {
		if (node->parentItem())
			self(self, node->parentItem());
		node->ensurePolished();
	};
	polish(polish, &item);
	const auto point = item.mapToScene(QPointF{item.width() / 2, item.height() / 2}).toPoint();
	CHECK(point.x() >= 0 && point.y() >= 0 && point.x() < window.width() &&
	      point.y() < window.height());
	QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point);
}

void qmlChecks(const QString& screenshot)
{
	ui::InspectorController controller;
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
	CHECK(engine.rootObjects().size() == 1);
	auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
	CHECK(window && QTest::qWaitForWindowExposed(window));
	window->resize(1280, 960);
	window->requestActivate();
	click(*window, *control(*window, "readManyDemoButton"));
	CHECK(controller.connected());
	click(*window, *control(*window, "watchlistExpanded"));
	auto* interval = control(*window, "watchFrequency");
	CHECK(interval->property("from") == 1 && interval->property("to") == 60000 &&
	      interval->property("stepSize") == 1);
	auto* editor = qobject_cast<QQuickItem*>(interval->property("contentItem").value<QObject*>());
	CHECK(editor);
	click(*window, *editor);
	QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClick(window, Qt::Key_1);
	QTest::keyClick(window, Qt::Key_Return);
	CHECK(controller.watchIntervalMs() == 1);
	controller.setWatchIntervalMs(1000);
	select(controller, "U64_0");
	click(*window, *control(*window, "addWatchButton"));
	CHECK(controller.watchCount() == 1);
	click(*window, *control(*window, "refreshWatchButton"));
	CHECK(await([&] {
		return !controller.watchPending() && !watchText(controller, 0).isEmpty();
	}));
	CHECK(watchText(controller, 0) == "18446744073709551615");
	CHECK(control(*window, "watchlistView")->height() > 0);
	click(*window, *control(*window, "refreshButton"));
	CHECK(await([&] {
		return !controller.busy() && controller.values()->rowCount() > 0;
	}));
	CHECK(watchText(controller, 0) == "18446744073709551615");
	click(*window, *control(*window, "watchMode"));
	QTest::keyClick(window, Qt::Key_Down);
	QTest::keyClick(window, Qt::Key_Return);
	CHECK(controller.watchIndividual());
	click(*window, *control(*window, "startWatchButton"));
	CHECK(controller.watchRunning());
	click(*window, *control(*window, "startWatchButton"));
	CHECK(!controller.watchRunning());
	CHECK(await([&] {
		return !controller.watchPending();
	}));
	if (!screenshot.isEmpty())
		CHECK(window->grabWindow().save(screenshot));
	click(*window, *control(*window, "removeWatchButton"));
	CHECK(controller.watchCount() == 0 && !warnings);
	controller.disconnectDevice();
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app{argc, argv};
	QQuickStyle::setStyle("Basic");
	modelChecks();
	formattingChecks();
	controllerChecks();
	largeControllerChecks();
	const auto arguments = app.arguments();
	const auto position = arguments.indexOf("--screenshot");
	qmlChecks(position >= 0 && position + 1 < arguments.size() ? arguments[position + 1]
	                                                           : QString{});
	std::printf(
	    "Inspector Watchlist checks=%u fields=65 packetLimit=32 exact=1 skippedTicks=1 fallback=1 qml=1 Qt=%s\n",
	    checks, qVersion());
}
