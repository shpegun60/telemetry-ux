// Installed owner and reusable UI: real TCP, single FIFO, no replay. MIT.
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <Telemetry/UX/TrendHistory.hpp>
#include <Telemetry/UX/WatchlistModel.hpp>
#include <Telemetry/UX/WatchTraces.hpp>
#include <integration/qt/ConnectionSession.hpp>
#include "TcpDemoServer.hpp"
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>
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
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <cstring>
#include <bit>
#include <cstdint>
#include <memory>
#include <vector>

namespace ux = telemetry::ux;
namespace host = telemetry::integration::qt;
namespace cl = telemetry::client;

namespace {
unsigned checks = 0, warnings = 0;
#define CHECK(value)                                                                               \
	do {                                                                                           \
		++checks;                                                                                  \
		if (!(value)) {                                                                            \
			std::fprintf(stderr, "Qt Session check %u line %d: %s\n", checks, __LINE__, #value);   \
			std::abort();                                                                          \
		}                                                                                          \
	} while (false)

bool await(const std::function<bool()>& ready)
{
	QElapsedTimer time;
	time.start();
	do {
		QCoreApplication::processEvents();
		if (ready())
			return true;
		QTest::qWait(1);
	} while (time.elapsed() < 10000);
	return ready();
}

QModelIndex endpoint(QAbstractItemModel* model, const QString& name, const QModelIndex& parent = {},
                    int category = 0)
{
	for (int row = 0; row < model->rowCount(parent); ++row) {
		const auto index = model->index(row, 0, parent);
		if (model->data(index, ux::EndpointModel::IsEndpointRole).toBool() &&
		    model->data(index, ux::EndpointModel::NameRole).toString() == name &&
		    (!category || model->data(index, ux::EndpointModel::CategoryRole).toInt() == category))
			return index;
		const auto child = endpoint(model, name, index, category);
		if (child.isValid())
			return child;
	}
	return {};
}

QModelIndex member(QAbstractItemModel* model, const QString& name)
{
	const auto root = model->index(0, 0);
	if (model->canFetchMore(root))
		model->fetchMore(root);
	for (int row = 0; row < model->rowCount(root); ++row) {
		const auto index = model->index(row, 0, root);
		if (model->data(index, ux::ValueModel::NameRole).toString() == name)
			return index;
	}
	return {};
}

void connect(host::ConnectionSession& connection, inspector_qml::TcpDemoServer& peer)
{
	CHECK(await([&] {
		return !peer.hasClient();
	}));
	CHECK(connection.connectTcpReference(QStringLiteral("127.0.0.1"), peer.port()));
	const bool admitted = await([&] {
		return connection.ready();
	});
	if (!admitted)
		std::fprintf(stderr, "Session admission: %s\n", qPrintable(connection.lastError()));
	CHECK(admitted);
}

void readConfig(ux::DynamicConfiguratorProvider& provider)
{
	CHECK(provider.selectEndpoint(endpoint(provider.endpoints(), QStringLiteral("Config"))));
	CHECK(provider.refresh());
	CHECK(await([&] {
		return !provider.busy();
	}));
	CHECK(provider.values()->rowCount() == 1);
}

QQuickItem* visual(QQuickItem* parent, const QString& name, const QString& text = {})
{
	if (parent->isVisible() && parent->objectName() == name &&
	    (text.isEmpty() || parent->property("text").toString() == text))
		return parent;
	for (auto* child : parent->childItems())
		if (auto* item = visual(child, name, text))
			return item;
	return nullptr;
}

QQuickItem* control(QQuickItem* parent, const QString& name, const QString& text = {})
{
	QQuickItem* result = nullptr;
	const bool available = await([&] {
		result = visual(parent, name, text);
		return result && result->width() > 0 && result->height() > 0;
	});
	if (!available)
		std::fprintf(stderr, "Missing control %s text=%s under %s\n", qPrintable(name),
		             qPrintable(text), qPrintable(parent->objectName()));
	CHECK(available);
	return result;
}

void click(QQuickWindow& window, QQuickItem& item)
{
	// Layouts that just became visible (a header shown once something is
	// selected) place the item only when they are polished: from the top down.
	QList<QQuickItem*> chain;
	for (auto* level = &item; level; level = level->parentItem())
		chain.prepend(level);
	for (auto* level : chain)
		level->ensurePolished();
	// A table row keeps its logical width while only its Name column may be
	// visible. Its centre can therefore lie beyond the clipped TreeView and
	// hit another panel. Click the user's visible Name text, not hidden cells.
	auto* target = &item;
	if (item.objectName() == QStringLiteral("endpointRow") && item.property("tableMode").toBool()) {
		target = visual(&item, QStringLiteral("endpointRowName"));
		CHECK(target);
	}
	QRectF visible = target->mapRectToScene(target->boundingRect());
	for (auto* level = target->parentItem(); level; level = level->parentItem())
		if (level->clip()) visible = visible.intersected(level->mapRectToScene(level->boundingRect()));
	visible = visible.intersected(QRectF(QPointF{}, window.size()));
	const auto point = visible.center().toPoint();
	if (!target->isEnabled() || visible.isEmpty())
		std::fprintf(stderr, "Unreachable native click: %s target=%s visible=%.1f,%.1f %.1fx%.1f\n",
		    qPrintable(item.objectName()), qPrintable(target->objectName()), visible.x(), visible.y(),
		    visible.width(), visible.height());
	CHECK(target->isEnabled() && !visible.isEmpty() && point.x() >= 0 && point.y() >= 0 &&
	      point.x() < window.width() && point.y() < window.height());
	QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point);
}

void twoPanels()
{
	QByteArray controlPayload;
	{
		inspector_qml::TcpDemoServer control;
		control.setWritableDemo(true);
		CHECK(control.listen());
		host::ConnectionSession session;
		ux::QtConnectionBinding binding{&session};
		binding.setMutationsAllowed(true);
		auto* provider = binding.createProvider();
		connect(session, control);
		readConfig(*provider);
		provider->setAllowMutations(true);
		CHECK(provider->beginEdit());
		CHECK(provider->setScalarText(member(provider->values(), QStringLiteral("Gain")),
		                              QStringLiteral("4.25")));
		QSignalSpy applied(provider, &ux::DynamicConfiguratorProvider::applyFinished);
		CHECK(provider->apply() && await([&] { return applied.count() == 1; }));
		CHECK(applied.last().at(0).toBool());
		controlPayload = control.lastWritePayload();
		CHECK(!controlPayload.isEmpty());
		session.disconnect();
	}
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection}, sibling{&connection};
	presentation.setMutationsAllowed(true);
	auto* first = presentation.createProvider();
	auto* second = sibling.createProvider();
	ux::TrendHistory trendHistory;
	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("connection"), &connection);
	engine.rootContext()->setContextProperty(QStringLiteral("configuration"), first);
	engine.rootContext()->setContextProperty(QStringLiteral("measurements"), second);
	engine.rootContext()->setContextProperty(QStringLiteral("trendHistory"), &trendHistory);
	engine.load(QUrl{QStringLiteral("qrc:/Main.qml")});
	CHECK(engine.rootObjects().size() == 1);
	auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
	CHECK(window && QTest::qWaitForWindowExposed(window));
	// This journey counts only explicit foreground requests. Exercise the
	// user-visible preview opt-out, while the separate endpoint-preview UI
	// journey verifies background demand and its shared FIFO end to end.
	const auto trees = window->findChildren<QObject*>(QStringLiteral("endpointTreePane"));
	CHECK(trees.size() == 2);
	for (auto* tree : trees) CHECK(tree->setProperty("previewValues", false));
	click(*window, *control(window->contentItem(), QStringLiteral("openTrend")));
	auto* trendWindow = window->findChild<QQuickWindow*>(QStringLiteral("trendWindow"));
	CHECK(trendWindow && QTest::qWaitForWindowExposed(trendWindow));
	CHECK(control(trendWindow->contentItem(), QStringLiteral("trendPanel")));
	trendWindow->hide();
	CHECK(first->selection().isEmpty() && second->selection().isEmpty());
	connect(connection, peer);
	CHECK(first->connected() && second->connected());
	CHECK(first->selection().isEmpty() && second->selection().isEmpty() && peer.fieldReads() == 0);
	// Use the shipped window exactly as a user does: the previously blank
	// second pane must provide its own visible endpoint navigation and Refresh.
	auto* right = control(window->contentItem(), QStringLiteral("measurementPanel"));
	click(*window, *control(right, QStringLiteral("endpointRow"), QStringLiteral("Config")));
	CHECK(!second->selection().isEmpty() && first->selection().isEmpty());
	CHECK(peer.fieldReads() == 0 && connection.workbench()->pending() == 0);
	auto* left = control(window->contentItem(), QStringLiteral("configurationPanel"));
	click(*window, *control(left, QStringLiteral("endpointRow"), QStringLiteral("Config")));
	CHECK(first->selection() == second->selection() && peer.fieldReads() == 0);
	click(*window, *control(left, QStringLiteral("refreshButton")));
	CHECK(await([&] {
		return !first->busy() && first->values()->rowCount() == 1;
	}));
	CHECK(peer.fieldReads() == 1 && second->values()->rowCount() == 0);
	click(*window, *control(right, QStringLiteral("refreshButton")));
	CHECK(await([&] {
		return !second->busy() && second->values()->rowCount() == 1;
	}));
	CHECK(peer.fieldReads() == 2);
	// Concurrent explicit reads still use the one existing FIFO after those
	// independent real-QML selections and Refresh actions.
	CHECK(first->refresh());
	CHECK(second->refresh());
	CHECK(connection.workbench()->pending() == 2);
	CHECK(await([&] {
		return !first->busy() && !second->busy();
	}));
	CHECK(peer.fieldReads() == 4);
	CHECK(first->selection() == second->selection());
	const auto maximum = member(second->values(), QStringLiteral("Counter"));
	CHECK(second->values()->data(maximum, ux::ValueModel::ValueRole).toString() ==
	      QStringLiteral("18446744073709551615"));
	const auto arguments = QCoreApplication::arguments();
	const auto screenshot = arguments.indexOf(QStringLiteral("--screenshot"));
	if (screenshot >= 0 && screenshot + 1 < arguments.size())
		CHECK(window->grabWindow().save(arguments[screenshot + 1]));
	first->setAllowMutations(true);
	CHECK(first->beginEdit());
	CHECK(first->setScalarText(member(first->values(), QStringLiteral("Gain")),
	                           QStringLiteral("4.25")));
	const auto oldSelection = first->selectedEndpointIndex();
	const auto gain = member(first->values(), QStringLiteral("Gain"));
	const auto draftText = first->values()->data(gain, ux::ValueModel::EditorTextRole);
	QVariantList fields;
	for (int i = 0; i < 65; ++i) {
		const auto index = endpoint(first->endpoints(), QStringLiteral("Value_%1").arg(i));
		CHECK(index.isValid() && first->watchCandidate(index));
		fields.push_back(QVariant::fromValue(index));
	}
	const auto requestsBeforeBulk = peer.requests();
	QSignalSpy inserted(first->watchlist(), &QAbstractItemModel::rowsInserted);
	const auto added = first->addWatchMany(fields);
	CHECK(added.value("status") == "ok" && added.value("added").toInt() == 65);
	QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
	QCoreApplication::processEvents();
	CHECK(first->watchCount() == 65 && second->watchCount() == 0 && inserted.count() == 1);
	CHECK(peer.requests() == requestsBeforeBulk && connection.workbench()->pending() == 0);
	CHECK(first->editing() && first->selectedEndpointIndex() == oldSelection);
	CHECK(first->values()->data(gain, ux::ValueModel::EditorTextRole) == draftText);
	// The open draft does not hold the Watchlist. This peer serves no
	// ReadMany, so the refusal is the capability's, said in the status, and
	// the draft stays as it was.
	first->refreshWatch();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
	QCoreApplication::processEvents();
	CHECK(peer.requests() == requestsBeforeBulk && !first->watchPending());
	CHECK(first->watchStatus().startsWith(QStringLiteral("This device can't read several Fields at once:")));
	CHECK(first->editing() && first->values()->data(gain, ux::ValueModel::EditorTextRole) == draftText);
	QSignalSpy applied(first, &ux::DynamicConfiguratorProvider::applyFinished);
	CHECK(first->apply());
	CHECK(await([&] {
		return applied.count() == 1;
	}));
	CHECK(applied.last().at(0).toBool() && peer.nativeWrites() == 1);
	CHECK(peer.lastWritePayload() == controlPayload); // Entire canonical write payload, byte for byte.
	CHECK(first->values()->data(member(first->values(), QStringLiteral("Gain")),
	                            ux::ValueModel::ValueRole).toString() == "4.25");
	CHECK(second->values()->rowCount() ==
	      0); // Shared registry invalidates sibling, even other adapter.
	connection.disconnect();
	CHECK(!first->connected() && !second->connected());
	CHECK(!first->writeOutcome().isEmpty());
	connect(connection, peer);
	CHECK(first->connected() && second->connected() && peer.nativeWrites() == 1);
	CHECK(first->values()->rowCount() == 0 && first->selection().isEmpty());
}


QByteArray frameBits(const ux::WatchFrameRow& row)
{
	return QByteArray(reinterpret_cast<const char*>(row.value.bytes.data()), row.value.size);
}

void nativeWatchFrames()
{
	// This fixture's ordinary Descriptor does not advertise ReadMany: exercise
	// the real Individual callback, not an invented TCP capability.
	inspector_qml::TcpDemoServer peer;
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	provider->setWatchIndividual(true);
	for (const auto& name : {QStringLiteral("Counter"), QStringLiteral("Offset"), QStringLiteral("Mode")})
		CHECK(provider->addWatchMany({QVariant::fromValue(endpoint(provider->endpoints(), name))}).value("added").toInt() == 1);
	std::vector<ux::WatchFrame> frames;
	QObject observer;
	QObject::connect(provider, &ux::DynamicConfiguratorProvider::watchCompleted, &observer,
	                 [&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	provider->refreshWatch();
	CHECK(await([&] { return frames.size() == 3 && !provider->watchActive() && !provider->watchPending(); }));
	const auto first = frames;
	for (std::size_t i = 0; i != first.size(); ++i) {
		const auto& frame = first[i];
		CHECK(frame.count == 1 && frame.exact && frame.correlated && frame.operation == ux::WatchOperation::FieldRead);
		CHECK(frame.phase == ux::WatchCompletionPhase::Current && frame.sequence == qint64(i + 1) && frame.portion == qint64(i));
		CHECK(frame.native.client == 0 && frame.native.dispatch == 0 && frame.native.endpoint == 0 &&
		      frame.native.codec == 0 && frame.native.value == 0 && frame.rows[0].itemStatus == 0 && frame.rows[0].valueStatus == 0);
		CHECK(frame.source.nativeSession != 0 && frame.source.fingerprint == connection.storage()->schema.fingerprint() &&
		      frame.source.schemaGeneration == connection.storage()->schema.generation());
		CHECK(frame.completed.monotonicNs >= frame.startedNs && frame.completed.utcMs > 0);
	}
	CHECK(first[0].rows[0].value.type.scalar == ux::WatchScalar::U64 && frameBits(first[0].rows[0]) == QByteArray::fromHex("ffffffffffffffff"));
	CHECK(first[1].rows[0].value.type.scalar == ux::WatchScalar::S64 && frameBits(first[1].rows[0]) == QByteArray::fromHex("0000000000000080"));
	CHECK(first[2].rows[0].value.type.scalar == ux::WatchScalar::S16 && first[2].rows[0].value.type.enumeration &&
	      frameBits(first[2].rows[0]) == QByteArray::fromHex("feff"));
	provider->refreshWatch();
	CHECK(await([&] { return frames.size() == 6 && !provider->watchActive(); }));
	for (std::size_t i = 0; i != first.size(); ++i)
		CHECK(frameBits(frames[i].rows[0]) == frameBits(first[i].rows[0])); // prior RX borrow has been reused
	peer.setNextFieldReadUnavailable();
	provider->refreshWatch();
	CHECK(await([&] { return frames.size() == 9 && !provider->watchActive(); }));
	CHECK(frames[6].count == 1 && frames[6].rows[0].id == first[0].rows[0].id &&
	      frames[6].phase == ux::WatchCompletionPhase::Current && frames[6].rows[0].value.size == 0);
	CHECK(frames[6].native.client == 0 && frames[6].native.dispatch == 9 &&
	      frames[6].rows[0].valueStatus != 0 && frames[6].rows[0].value.type.valid());
	// The live Plot's rings (LP-1): every read kept by its value's path,
	// plotted or not; a number no double holds exactly (Counter's u64 max),
	// or a failed read, keeps its place with no number; an enum is drawn as
	// states.
	auto* traces = qobject_cast<ux::WatchTraces*>(provider->watchTraces());
	auto* watched = provider->watchlist();
	QString counterPath, modePath;
	for (int row = 0; row < watched->rowCount(); ++row) {
		const auto path = watched->index(row, 0).data(ux::WatchlistModel::NameRole).toString();
		if (path.endsWith(QStringLiteral(".Counter")))
			counterPath = path;
		if (path.endsWith(QStringLiteral(".Mode")))
			modePath = path;
	}
	CHECK(traces && !counterPath.isEmpty() && !modePath.isEmpty());
	CHECK(traces->count(modePath) == 3 && traces->latest(modePath).toDouble() == -2.0 && traces->categorical(modePath));
	CHECK(traces->count(counterPath) == 3 && !traces->latest(counterPath).isValid() && !traces->categorical(counterPath));
	CHECK(traces->plot(modePath).isEmpty() && traces->plotted() == QStringList{modePath} && traces->newestNs() > 0);
	peer.setReplyDelay(20);
	provider->refreshWatch();
	CHECK(provider->watchPending());
	provider->stopWatch();
	CHECK(await([&] { return frames.size() == 10 && !provider->watchActive() && !provider->watchPending(); }));
	CHECK(frames.back().phase == ux::WatchCompletionPhase::Drained && frames.back().count == 1 && connection.ready());
	connection.disconnect();

	inspector_qml::TcpDemoServer writable;
	writable.setWritableDemo(true);
	CHECK(writable.listen());
	host::ConnectionSession zeros;
	ux::QtConnectionBinding zeroPresentation{&zeros};
	auto* zeroProvider = zeroPresentation.createProvider();
	connect(zeros, writable);
	zeroProvider->setWatchIndividual(true);
	for (const auto& name : {QStringLiteral("SignedZero32"), QStringLiteral("SignedZero64")})
		CHECK(zeroProvider->addWatchMany({QVariant::fromValue(endpoint(zeroProvider->endpoints(), name))}).value("added").toInt() == 1);
	std::vector<ux::WatchFrame> zeroFrames;
	QObject::connect(zeroProvider, &ux::DynamicConfiguratorProvider::watchCompleted, &observer,
	                 [&](const ux::WatchFrame& frame) { zeroFrames.push_back(frame); });
	zeroProvider->refreshWatch();
	CHECK(await([&] { return zeroFrames.size() == 2 && !zeroProvider->watchActive(); }));
	CHECK(zeroFrames[0].exact && zeroFrames[0].rows[0].value.type.scalar == ux::WatchScalar::F32 &&
	      frameBits(zeroFrames[0].rows[0]) == QByteArray::fromHex("00000080"));
	CHECK(zeroFrames[1].exact && zeroFrames[1].rows[0].value.type.scalar == ux::WatchScalar::F64 &&
	      frameBits(zeroFrames[1].rows[0]) == QByteArray::fromHex("0000000000000080"));
	zeros.disconnect();
}

void bulkSessionWatch()
{
	inspector_qml::TcpDemoServer peer;
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	auto* first = presentation.createProvider();
	auto* second = presentation.createProvider();
	connect(connection, peer);
	const auto voltage = endpoint(first->endpoints(), QStringLiteral("Offset"));
	const auto counter = endpoint(first->endpoints(), QStringLiteral("Counter"));
	const auto samples = endpoint(first->endpoints(), QStringLiteral("Samples"));
	const auto foreign = endpoint(second->endpoints(), QStringLiteral("Counter"));
	CHECK(voltage.isValid() && counter.isValid() && samples.isValid() && foreign.isValid());
	const auto requests = peer.requests();
	QSignalSpy inserted(first->watchlist(), &QAbstractItemModel::rowsInserted);
	const auto invalid = first->addWatchMany({QVariant::fromValue(voltage), QVariant::fromValue(foreign)});
	CHECK(invalid.value("status") == "invalid" && first->watchCount() == 0);
	const auto added = first->addWatchMany({QVariant::fromValue(voltage), QVariant::fromValue(counter),
	                                        QVariant::fromValue(counter), QVariant::fromValue(samples)});
	CHECK(added.value("status") == "ok" && added.value("added").toInt() == 2);
	CHECK(added.value("duplicate").toInt() == 1 && added.value("unsupported").toInt() == 1);
	CHECK(first->watchCount() == 2 && inserted.count() == 1 && peer.requests() == requests);
	CHECK(first->selection().isEmpty() && connection.workbench()->pending() == 0);
	QPointer<ux::DynamicConfiguratorProvider> doomed = presentation.createProvider();
	QObject observer;
	const auto doomedIndex = endpoint(doomed->endpoints(), QStringLiteral("Counter"));
	QObject::connect(doomed->watchlist(), &QAbstractItemModel::rowsAboutToBeInserted, &observer,
	                 [&] { delete doomed.data(); });
	const auto destroyed = doomed->addWatchMany({QVariant::fromValue(doomedIndex)});
	CHECK(!doomed && destroyed.value("added").toInt() == 1 && peer.requests() == requests);
	connection.disconnect();
}

// The Device tree's watch boxes (T-1). A group's box covers every Field under
// it the Watchlist can hold, and shows all, some or none. It watches or stops
// watching them in one change with no request sent. Undo puts the rows back
// where they were, and is gone once anything else changes the Watchlist.
void groupWatchBoxes()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	auto* provider = presentation.createProvider();
	auto* other = presentation.createProvider();
	connect(connection, peer);
	auto* model = provider->endpoints();
	const auto first = endpoint(model, QStringLiteral("Value_0"));
	const auto second = endpoint(model, QStringLiteral("Value_1"));
	const auto third = endpoint(model, QStringLiteral("Value_2"));
	const auto command = endpoint(model, QStringLiteral("Execute"));
	CHECK(first.isValid() && second.isValid() && third.isValid() && command.isValid());
	const auto group = first.parent();
	int holdable = 0;
	for (int row = 0; row < model->rowCount(group); ++row)
		holdable += provider->watchState(model->index(row, 0, group)) >= 0 ? 1 : 0;
	CHECK(holdable >= 3 && holdable <= ux::watchlistRowLimit);
	const auto requests = peer.requests();
	const auto watched = [&](const QModelIndex& index) { return QVariantList{QVariant::fromValue(index)}; };
	// A Command has no box; the Fields start empty.
	CHECK(provider->watchState(command) == -1 && provider->watchState(first) == Qt::Unchecked &&
	      provider->watchState(group) == Qt::Unchecked && !provider->canUndoWatch());
	CHECK(provider->watchLimit() == ux::watchlistRowLimit && ux::watchlistRowLimit == 8192);
	auto result = provider->setWatchMany(watched(first), true);
	CHECK(result.value("status") == "ok" && result.value("changed").toInt() == 1);
	CHECK(provider->watchState(first) == Qt::Checked && provider->watchState(group) == Qt::PartiallyChecked);
	// The group: every Field it holds, the one already watched unchanged.
	QSignalSpy inserted(provider->watchlist(), &QAbstractItemModel::rowsInserted);
	result = provider->setWatchMany(watched(group), true);
	CHECK(result.value("status") == "ok" && result.value("changed").toInt() == holdable - 1 &&
	      result.value("unchanged").toInt() == 1 && result.value("fields").toInt() == holdable);
	CHECK(inserted.count() == 1 && provider->watchCount() == holdable &&
	      provider->watchState(group) == Qt::Checked && provider->canUndoWatch());
	// Undo takes back what the group added, and only that.
	result = provider->undoWatch();
	CHECK(result.value("status") == "ok" && result.value("changed").toInt() == holdable - 1);
	CHECK(provider->watchCount() == 1 && provider->watchState(group) == Qt::PartiallyChecked &&
	      !provider->canUndoWatch() && provider->undoWatch().value("status") == "refused");
	// Unwatching the group, then Undo: the rows come back in their order.
	CHECK(provider->setWatchMany(watched(third), true).value("changed").toInt() == 1);
	CHECK(provider->setWatchMany(watched(second), true).value("changed").toInt() == 1);
	const auto names = [&] {
		QStringList list;
		for (int row = 0; row < provider->watchlist()->rowCount(); ++row)
			list << provider->watchlist()->index(row, 0).data(Qt::DisplayRole).toString();
		return list;
	};
	const auto before = names();
	CHECK(before.size() == 3);
	QSignalSpy removed(provider->watchlist(), &QAbstractItemModel::rowsRemoved);
	result = provider->setWatchMany(watched(group), false);
	CHECK(result.value("status") == "ok" && result.value("changed").toInt() == 3 &&
	      provider->watchCount() == 0 && provider->watchState(group) == Qt::Unchecked &&
	      removed.count() == 1);
	CHECK(provider->undoWatch().value("changed").toInt() == 3 && names() == before);
	// Rows apart from one another: taken out run by run, put back in place.
	const auto fourth = endpoint(model, QStringLiteral("Value_3"));
	CHECK(provider->setWatchMany(watched(fourth), true).value("changed").toInt() == 1);
	const auto four = names();
	result = provider->setWatchMany({QVariant::fromValue(first), QVariant::fromValue(second)}, false);
	CHECK(result.value("status") == "ok" && result.value("changed").toInt() == 2 && provider->watchCount() == 2);
	CHECK(provider->undoWatch().value("changed").toInt() == 2 && names() == four);
	CHECK(provider->setWatchMany(watched(fourth), false).value("changed").toInt() == 1);
	// Another change of the Watchlist takes Undo away.
	CHECK(provider->setWatchMany(watched(group), true).value("status") == "ok" && provider->canUndoWatch());
	provider->removeWatch(0);
	CHECK(!provider->canUndoWatch());
	// Another provider's index is refused; nothing was sent to the device.
	CHECK(provider->setWatchMany(watched(endpoint(other->endpoints(), QStringLiteral("Value_0"))), true)
	          .value("status") == "invalid");
	CHECK(peer.requests() == requests && connection.workbench()->pending() == 0);
	connection.disconnect();
	CHECK(provider->watchState(group) == -1 &&
	      provider->setWatchMany(watched(group), true).value("status") == "refused");
}

// Values inside struct and array Fields have boxes (S3-A). A struct's box is
// three-state over all its values, opened or not; a member's box is its own.
// Opening a struct changes no box and keeps Undo. One read answers every value
// ticked in one Field. The struct demo's Config holds 21 values: Gain, Counter,
// Offset, Enabled, OperatingMode, Matrix[2][2], FloatBits[6], DoubleBits[6].
void memberWatchBoxes()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	auto* model = provider->endpoints();
	// A row under parent by name, its rows fetched first; any provider's model.
	const auto child = [](const QModelIndex& parent, const QString& name) {
		auto* rows = const_cast<QAbstractItemModel*>(parent.model());
		while (rows->canFetchMore(parent))
			rows->fetchMore(parent);
		for (int row = 0; row < rows->rowCount(parent); ++row) {
			const auto index = rows->index(row, 0, parent);
			if (index.data(ux::EndpointModel::NameRole).toString() == name)
				return index;
		}
		return QModelIndex{};
	};
	const auto one = [](const QModelIndex& index) { return QVariantList{QVariant::fromValue(index)}; };
	const auto config = endpoint(model, QStringLiteral("Config"), {}, 1);
	CHECK(config.isValid() && provider->watchState(config) == Qt::Unchecked && provider->watchRefusal(config).isEmpty());
	auto tally = provider->watchTally(config);
	CHECK(tally.value("total").toInt() == 21 && tally.value("watched").toInt() == 0 && tally.value("kind") == "values");
	const auto gain = child(config, QStringLiteral("Gain"));
	CHECK(gain.isValid() && provider->watchState(gain) == Qt::Unchecked);
	auto result = provider->setWatchMany(one(gain), true);
	CHECK(result.value("status") == "ok" && result.value("changed").toInt() == 1 && result.value("values").toBool());
	CHECK(provider->watchState(gain) == Qt::Checked && provider->watchState(config) == Qt::PartiallyChecked &&
	      provider->canUndoWatch());
	// Opening more of the struct changes no box and keeps Undo.
	const auto matrix = child(config, QStringLiteral("Matrix"));
	const auto second = child(matrix, QStringLiteral("[1]"));
	const auto cell = child(second, QStringLiteral("[0]"));
	CHECK(cell.isValid() && provider->canUndoWatch() && provider->watchState(matrix) == Qt::Unchecked);
	CHECK(provider->watchTally(matrix).value("kind") == "values" && provider->watchTally(matrix).value("total").toInt() == 4);
	CHECK(provider->watchTally(second).value("kind") == "elements");
	CHECK(provider->setWatchMany(one(cell), true).value("changed").toInt() == 1);
	CHECK(provider->watchState(cell) == Qt::Checked && provider->watchState(second) == Qt::PartiallyChecked &&
	      provider->watchState(matrix) == Qt::PartiallyChecked && provider->watchState(gain) == Qt::Checked);
	tally = provider->watchTally(config);
	CHECK(tally.value("watched").toInt() == 2 && tally.value("total").toInt() == 21);
	// Rows are named by path and keep their member text; written from the row
	// by writing Config, a writable struct within one write.
	auto* list = provider->watchlist();
	CHECK(list->rowCount() == 2 && list->index(0, 0).data(Qt::DisplayRole).toString() == "device.Config.Gain" &&
	      list->index(1, 0).data(Qt::DisplayRole).toString() == "device.Config.Matrix[1][0]" &&
	      list->index(1, 0).data(ux::WatchlistModel::MemberRole).toString() == "Matrix[1][0]" &&
	      list->index(0, 0).data(ux::WatchlistModel::WritableRole).toBool());
	// One read of Config answers both, as one frame.
	std::vector<ux::WatchFrame> frames;
	QObject observer;
	QObject::connect(provider, &ux::DynamicConfiguratorProvider::watchCompleted, &observer,
	                 [&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	provider->refreshWatch();
	CHECK(await([&] { return frames.size() == 1 && !provider->watchActive() && !provider->watchPending(); }));
	CHECK(frames[0].operation == ux::WatchOperation::FieldRead && frames[0].count == 2 && frames[0].exact &&
	      frames[0].correlated && frames[0].phase == ux::WatchCompletionPhase::Current);
	CHECK(frames[0].rows[0].id == frames[0].rows[1].id && frames[0].rows[0].leaf == 1 && frames[0].rows[1].leaf == 8);
	CHECK(frames[0].rows[0].value.type.scalar == ux::WatchScalar::F32 &&
	      frames[0].rows[1].value.type.scalar == ux::WatchScalar::S32);
	CHECK(list->index(0, 0).data(ux::WatchlistModel::TextRole).toString() == "1" &&
	      list->index(1, 0).data(ux::WatchlistModel::TextRole).toString() == "3" &&
	      list->index(1, 0).data(ux::WatchlistModel::TypeTextRole).toString() == "i32");
	// Ticking the struct takes the rest; unticking takes all; Undo puts them back.
	result = provider->setWatchMany(one(config), true);
	CHECK(result.value("changed").toInt() == 19 && result.value("unchanged").toInt() == 2 &&
	      result.value("fields").toInt() == 21 && provider->watchState(config) == Qt::Checked);
	result = provider->setWatchMany(one(config), false);
	CHECK(result.value("changed").toInt() == 21 && provider->watchCount() == 0 &&
	      provider->watchState(config) == Qt::Unchecked && provider->watchState(gain) == Qt::Unchecked);
	CHECK(provider->undoWatch().value("changed").toInt() == 21 && provider->watchCount() == 21 &&
	      provider->watchState(cell) == Qt::Checked);
	connection.disconnect();
	CHECK(provider->watchState(config) == -1 && provider->watchState(gain) == -1);

	// The data demo: an array larger than the room left, a struct with no values,
	// and a range row ticking its 256 elements.
	inspector_qml::TcpDemoServer data;
	CHECK(data.listen());
	host::ConnectionSession dataConnection;
	ux::QtConnectionBinding dataPresentation{&dataConnection};
	auto* dataProvider = dataPresentation.createProvider();
	connect(dataConnection, data);
	auto* dataModel = dataProvider->endpoints();
	const auto samples = endpoint(dataModel, QStringLiteral("Samples"), {}, 1);
	const auto empty = endpoint(dataModel, QStringLiteral("Empty"), {}, 1);
	CHECK(dataProvider->watchState(samples) == Qt::Unchecked);
	CHECK(dataProvider->watchTally(samples).value("kind") == "elements" &&
	      dataProvider->watchTally(samples).value("total").toInt() == 16384);
	result = dataProvider->setWatchMany(one(samples), true);
	CHECK(result.value("status") == "capacity" && result.value("fields").toInt() == 16384 &&
	      result.value("adding").toInt() == 16384 &&
	      result.value("room").toInt() == ux::watchlistRowLimit && result.value("values").toBool() && dataProvider->watchCount() == 0);
	CHECK(dataProvider->watchState(empty) == -1 && dataProvider->watchRefusal(empty) == "data.Empty has no values to watch.");
	// Typed, the same room is said before Enter, and a paste adds all or nothing.
	CHECK(dataProvider->checkWatchPath(QStringLiteral("data.Samples")).value("status") == "error" &&
	      dataProvider->checkWatchPath(QStringLiteral("data.Samples")).value("message").toString().contains(QStringLiteral("room for %1").arg(QLocale().toString(ux::watchlistRowLimit))));
	CHECK(dataProvider->addWatchPaths({QStringLiteral("data.Samples")}).value("status") == "capacity" &&
	      dataProvider->watchCount() == 0);
	CHECK(dataProvider->checkWatchPath(QStringLiteral("data.Samples[300]")).value("message") == "Adds data.Samples[300]");
	const auto range = child(samples, QStringLiteral("[256 … 511]"));
	CHECK(range.isValid() && dataProvider->watchState(range) == Qt::Unchecked);
	result = dataProvider->setWatchMany(one(range), true);
	CHECK(result.value("status") == "ok" && result.value("changed").toInt() == 256);
	CHECK(dataProvider->watchState(range) == Qt::Checked && dataProvider->watchState(samples) == Qt::PartiallyChecked);
	auto* dataList = dataProvider->watchlist();
	CHECK(dataList->index(3, 0).data(Qt::DisplayRole).toString() == "data.Samples[259]" &&
	      dataList->index(3, 0).data(ux::WatchlistModel::MemberRole).toString() == "[259]");
	dataConnection.disconnect();
}

// The "+" row's API over a real session (WL-4b): a typed path in words, what
// may come next, and pasted lines added as one change Undo takes back.
void watchPaths()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	const auto check = [&](const char* text) { return provider->checkWatchPath(QString::fromUtf8(text)); };
	auto said = check("device.Config.Gain");
	CHECK(said.value("status") == "adds" && said.value("message") == "Adds device.Config.Gain" &&
	      said.value("adds").toInt() == 1);
	said = check("device.Config");
	CHECK(said.value("status") == "adds" && said.value("adds").toInt() == 21);
	CHECK(check("control.Execute").value("message") == "control.Execute is a Command. Only Fields can be watched.");
	CHECK(check("control.Transform").value("message") == "control.Transform is a Service. Only Fields can be watched.");
	CHECK(check("device.Nope").value("status") == "missing");
	CHECK(check("device.Config.Gainn").value("status") == "error" &&
	      check("device.Config.Gainn").value("message").toString().contains("Did you mean Gain?"));
	CHECK(check("Gain").value("status") == "error" && check("  ").value("status") == "empty");
	// Candidates come from the descriptor, in its order.
	const auto candidates = provider->watchPathCandidates(QStringLiteral("device.Config."));
	CHECK(!candidates.isEmpty() && candidates.first().toMap().value("path") == "device.Config.Gain");
	CHECK(provider->watchPathCandidates(QStringLiteral("gain")).size() >= 1);
	// Pasted lines: a path, a copied "path = value", a struct for all its
	// values, one not on this device, a Command; one change, Undo takes it back.
	auto added = provider->addWatchPaths({QStringLiteral("device.Config.Gain = 1.5"), QStringLiteral("device.SignedZero32\t-0"),
	                                      QStringLiteral("device.Nope"), QStringLiteral("control.Execute"),
	                                      QStringLiteral(""), QStringLiteral("device.Config.Gain")});
	// The one not on this device is kept as a row that says so (WL-4c).
	CHECK(added.value("status") == "ok" && added.value("added").toInt() == 2 && provider->watchCount() == 3);
	CHECK(added.value("missing").toList() == QVariantList({QStringLiteral("device.Nope")}));
	CHECK(added.value("refused").toList().size() == 1 && provider->canUndoWatch());
	said = check("device.Config.Gain");
	CHECK(said.value("status") == "watched" && said.value("row").toInt() >= 0);
	said = check("device.Nope");
	CHECK(said.value("status") == "watched" && said.value("row").toInt() == 2);
	CHECK(provider->addWatchPaths({QStringLiteral("device.Nope")}).value("already").toInt() == 1 && provider->watchCount() == 3);
	CHECK(provider->undoWatch().value("status") == "ok" && provider->watchCount() == 0);
	CHECK(provider->addWatchPaths({QStringLiteral("device.Config")}).value("added").toInt() == 21 &&
	      provider->watchCount() == 21);
	// While the Watchlist polls, a typed path goes in between two reads (fix 8).
	provider->startWatch();
	CHECK(await([&] { return provider->watchActive(); }));
	CHECK(provider->addWatchPaths({QStringLiteral("device.SignedZero32")}).value("added").toInt() == 1 &&
	      provider->watchCount() == 22);
	provider->stopWatch();
	CHECK(await([&] { return !provider->watchActive(); }));
	connection.disconnect();
}

// A project saves the Watchlist as paths: {group, name} for a whole Field,
// {group, name, member} for a value inside one. Saved rows come back in their
// order; a path this device does not have is reported by its position.
void watchBindings()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	auto* provider = presentation.createProvider();
	CHECK(provider->watchBindings().isEmpty() && provider->addWatchBindings({}).value("status") == "refused");
	connect(connection, peer);
	auto* model = provider->endpoints();
	const auto child = [model](const QModelIndex& parent, const QString& name) {
		while (model->canFetchMore(parent))
			model->fetchMore(parent);
		for (int row = 0; row < model->rowCount(parent); ++row) {
			const auto index = model->index(row, 0, parent);
			if (index.data(ux::EndpointModel::NameRole).toString() == name)
				return index;
		}
		return QModelIndex{};
	};
	const auto config = endpoint(model, QStringLiteral("Config"), {}, 1);
	const auto setpoint = endpoint(model, QStringLiteral("Setpoint"), {}, 1);
	const auto gain = child(config, QStringLiteral("Gain"));
	const auto cell = child(child(child(config, QStringLiteral("Matrix")), QStringLiteral("[1]")), QStringLiteral("[0]"));
	CHECK(provider->setWatchMany({QVariant::fromValue(setpoint)}, true).value("changed").toInt() == 1);
	CHECK(provider->setWatchMany({QVariant::fromValue(gain), QVariant::fromValue(cell)}, true).value("changed").toInt() == 2);
	auto* list = provider->watchlist();
	const auto names = [list] {
		QStringList all;
		for (int row = 0; row < list->rowCount(); ++row)
			all << list->index(row, 0).data(Qt::DisplayRole).toString();
		return all;
	};
	const auto before = names();
	const auto saved = provider->watchBindings();
	CHECK(saved.size() == 3);
	CHECK(saved[0].toMap() == QVariantMap({{"group", "device"}, {"name", "Setpoint"}}));
	CHECK(saved[1].toMap() == QVariantMap({{"group", "device"}, {"name", "Config"}, {"member", "Gain"}}));
	CHECK(saved[2].toMap() == QVariantMap({{"group", "device"}, {"name", "Config"}, {"member", "Matrix[1][0]"}}));
	provider->clearWatch();
	CHECK(provider->watchCount() == 0);
	auto entries = saved;
	entries << QVariantMap({{"group", "device"}, {"name", "Config"}, {"member", "Matrix[2][0]"}})
	        << QVariantMap({{"group", "device"}, {"name", "Absent"}})
	        << QVariantMap({{"group", "device"}, {"name", "Config"}});
	const auto result = provider->addWatchBindings(entries);
	CHECK(result.value("status") == "ok" && result.value("added").toInt() == 3);
	CHECK(result.value("missing").toList() == QVariantList({3, 4, 5}));
	// Each one this device has no value for is kept as a row that says why
	// (WL-4c): an element past the end can't be watched, an absent Field is
	// not on the device, and a struct kept whole becomes its other values.
	const auto now = names();
	CHECK(now.mid(0, 5) == before + QStringList({QStringLiteral("device.Config.Matrix[2][0]"), QStringLiteral("device.Absent")}));
	CHECK(list->index(3, 0).data(ux::WatchlistModel::StatusRole) == "Unwatchable" &&
	      !list->index(3, 0).data(ux::WatchlistModel::WriteRefusalRole).toString().isEmpty());
	CHECK(list->index(4, 0).data(ux::WatchlistModel::StatusRole) == "Missing");
	CHECK(now.size() > 6 && !now.contains(QStringLiteral("device.Config")) && now.count(QStringLiteral("device.Config.Gain")) == 1);
	CHECK(provider->watchState(gain) == Qt::Checked);
	// Added again, nothing more: values once, kept paths once.
	const auto again = provider->addWatchBindings(entries);
	CHECK(again.value("status") == "ok" && names() == now);
	const auto kept = provider->watchBindings();
	connection.disconnect();
	// The rows outlive the device, saved by the same paths (WL-4c).
	CHECK(await([&] { return provider->watchBindings() == kept; }));
}

// Finding what to add to the Batch by name or path (BA-1). This server has
// no Batch, so the lines are read and refused in words; adding them, and
// executing what was added, is checked against the Client's demo device,
// which has one.
void batchPaths()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	auto* provider = presentation.createProvider();
	CHECK(provider->checkBatchPath(QStringLiteral("  ")).value("status") == "empty");
	connect(connection, peer);
	const auto check = [provider](const char* text) { return provider->checkBatchPath(QString::fromUtf8(text)); };
	// A name two endpoints share asks for a choice; a kind word makes it.
	CHECK(check("Ping").value("status") == "choose" && check("ping").value("message").toString().contains("2 endpoints"));
	const auto command = check("command control.Ping");
	CHECK(command.value("status") == "unsupported" && command.value("operation") == "command" &&
	      command.value("path") == "control.Ping" && command.value("message") == "This device has no Batch.");
	CHECK(check("service Ping").value("operation") == "service");
	// A value inside a Field names the Field, read whole.
	const auto member = check("device.Config.Gain");
	CHECK(member.value("path") == "device.Config" && member.value("operation") == "read");
	CHECK(check("Setpoint").value("path") == "device.Setpoint");
	// One whose input has a struct or an array goes to Details (BA-2).
	const auto input = check("control.Execute");
	CHECK(input.value("status") == "input" && input.value("message").toString().contains("which is filled in Details"));
	// Input to one that takes none is refused in words; a Field's "= value" still is.
	const auto none = check("command control.Ping now");
	CHECK(none.value("status") == "error" && none.value("message") == "control.Ping takes no input; remove \"now\".");
	CHECK(provider->selectBatchPath(QStringLiteral("control.Execute")) &&
	      provider->selectedEndpointIndex().data(ux::EndpointModel::PathRole) == "control.Execute");
	CHECK(!provider->selectBatchPath(QStringLiteral("Ping")) && !provider->selectBatchPath(QStringLiteral("Nope")));
	// Selected and untouched, a Command is not an edit to lose: the row
	// selects another (BA-1 refused the second).
	CHECK(provider->selectBatchPath(QStringLiteral("control.Accept")) &&
	      provider->selectedEndpointIndex().data(ux::EndpointModel::PathRole) == "control.Accept");
	CHECK(check("Nope").value("status") == "missing" && check("Nope").value("message") == "Nothing on this device is named Nope.");
	CHECK(check("device.Nope").value("message") == "device.Nope is not on this device.");
	CHECK(check("device.Setpoint = 3").value("status") == "error");
	// Suggestions: groups to go into first, then names word by word.
	const auto groups = provider->batchPathCandidates(QStringLiteral("dev"));
	CHECK(!groups.isEmpty() && groups.first().toMap().value("path") == "device" &&
	      groups.first().toMap().value("container").toBool());
	QStringList pings;
	for (const auto& candidate : provider->batchPathCandidates(QStringLiteral("Pi")))
		pings << candidate.toMap().value("completion").toString() + QLatin1Char('/') + candidate.toMap().value("kind").toString();
	CHECK(pings.contains("command control.Ping/Command") && pings.contains("service control.Ping/Service"));
	const auto words = provider->batchPathCandidates(QStringLiteral("dev set"));
	CHECK(words.size() == 1 && words.first().toMap().value("path") == "device.Setpoint" &&
	      words.first().toMap().value("typeText").toString().endsWith("rw"));
	const auto under = provider->batchPathCandidates(QStringLiteral("control."));
	CHECK(under.size() == 8);
	for (const auto& candidate : under)
		if (candidate.toMap().value("path") == "control.Execute")
			CHECK(candidate.toMap().value("input").toBool());
	// A half-typed name: Enter takes the first suggestion, said quietly.
	const auto partial = check("Setp");
	CHECK(partial.value("status") == "partial" && partial.value("message") == "Enter takes device.Setpoint.");
	// A value inside the Field must be one.
	CHECK(check("device.Config.Gian").value("status") == "error" &&
	      check("device.Config.Gian").value("message").toString().contains("Did you mean Gain?"));
	// Every line or none, each refusal in words.
	const auto added = provider->addBatchPaths({QStringLiteral("device.Setpoint"), QStringLiteral("Nope")});
	CHECK(added.value("status") == "refused" && added.value("added").toInt() == 0 &&
	      added.value("refused").toList().size() == 2 && provider->batchCount() == 0);
	CHECK(added.value("message") ==
	      "Nothing added: line 1, device.Setpoint: This device has no Batch. (and one more line)");
	CHECK(provider->addBatchPaths({QStringLiteral("device.Setpoint")}).value("message") == "This device has no Batch.");
	connection.disconnect();

	// A Command's values typed after its path (BA-2), checked as Details would,
	// on the demo whose control.Calibrate takes reference (f32).
	inspector_qml::TcpDemoServer plain;
	CHECK(plain.listen());
	host::ConnectionSession second;
	ux::QtConnectionBinding secondPresentation{&second};
	auto* calibrate = secondPresentation.createProvider();
	connect(second, plain);
	const auto line = [calibrate](const char* text) { return calibrate->checkBatchPath(QString::fromUtf8(text)); };
	auto typed = line("control.Calibrate");
	CHECK(typed.value("status") == "needs" && typed.value("next") == "reference" &&
	      typed.value("line") == "control.Calibrate reference=" &&
	      typed.value("message") == "control.Calibrate needs reference (f32). Enter writes reference=; Ctrl+Enter fills it in Details.");
	// A name half typed is a name: quiet, and Enter completes it.
	typed = line("control.Calibrate ref");
	CHECK(typed.value("status") == "needs" && typed.value("line") == "control.Calibrate reference=");
	// "reference=" waits for its value, quietly.
	typed = line("control.Calibrate reference=");
	CHECK(typed.value("status") == "needs" && typed.value("message") == "reference (f32) takes a number." &&
	      typed.contains("line") && typed.value("line").toString().isEmpty());
	typed = line("control.Calibrate reference=abc");
	CHECK(typed.value("status") == "error" && typed.value("message") == "reference (f32) takes a number, not \"abc\".");
	typed = line("control.Calibrate referenc=1");
	CHECK(typed.value("status") == "error" && typed.value("message") == "control.Calibrate has no argument referenc. Did you mean reference?");
	// Valid, it would be added - here the device has no Batch - with its value as sent.
	for (const auto* text : {"control.Calibrate reference=1.5", "control.Calibrate reference = 1.5", "control.Calibrate 1.5",
	                         "Calibrate reference=0x3fc00000"}) {
		typed = line(text);
		const auto arguments = typed.value("arguments").toList();
		CHECK(typed.value("status") == "unsupported" && arguments.size() == 1 &&
		      arguments.first().toMap().value("status") == "ok" && arguments.first().toMap().value("canonical") == "1.5");
	}
	// Suggestions: the named signature, then the arguments to give.
	const auto found = calibrate->batchPathCandidates(QStringLiteral("Calib"));
	bool signature = false;
	for (const auto& candidate : found)
		signature = signature || (candidate.toMap().value("path") == "control.Calibrate" &&
		                          candidate.toMap().value("typeText") == "(reference: f32)" &&
		                          candidate.toMap().value("follow") == " " && candidate.toMap().value("continues").toBool());
	CHECK(signature);
	// The argument shows as "reference=" with its type; choosing it writes
	// the whole line.
	const auto arguments = calibrate->batchPathCandidates(QStringLiteral("control.Calibrate "));
	CHECK(arguments.size() == 1 && arguments.first().toMap().value("completion") == "control.Calibrate reference=" &&
	      arguments.first().toMap().value("path") == "reference=" && arguments.first().toMap().value("kind") == "" &&
	      arguments.first().toMap().value("kindCode") == "argument" && arguments.first().toMap().value("typeText") == "f32");
	const auto starting = calibrate->batchPathCandidates(QStringLiteral("control.Calibrate re"));
	CHECK(starting.size() == 1 && starting.first().toMap().value("completion") == "control.Calibrate reference=");
	CHECK(calibrate->batchPathCandidates(QStringLiteral("control.Calibrate 1.")).isEmpty());
	CHECK(calibrate->batchPathCandidates(QStringLiteral("control.Calibrate reference=1 ")).isEmpty());
	// Ctrl+Enter takes the typed value into the draft, as Details would; an
	// edit made that way is then one to finish or cancel before the next.
	secondPresentation.setMutationsAllowed(true);
	CHECK(calibrate->selectBatchPath(QStringLiteral("control.Calibrate")) && !calibrate->selectionRequiresDiscard());
	calibrate->setAllowMutations(true);
	CHECK(calibrate->allowMutations() && calibrate->selectBatchPath(QStringLiteral("Calibrate reference=0x3fc00000")));
	CHECK(::member(calibrate->values(), QStringLiteral("reference")).data(ux::ValueModel::EditorTextRole) == "1.5" &&
	      calibrate->selectionRequiresDiscard());
	CHECK(!calibrate->selectBatchPath(QStringLiteral("control.Calibrate reference=2")));
	calibrate->cancelEdit();
	CHECK(calibrate->selectBatchPath(QStringLiteral("control.Calibrate reference=nope")) &&
	      !calibrate->selectionRequiresDiscard());
	second.disconnect();
}

// The Watchlist outlives its device (WL-4c): a disconnect keeps every row by
// its path with its last value, stale; the next device that has a path reads
// it again in place, and a path it lacks stays a row that says so.
void watchKeptAcrossReconnect()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	const auto added = provider->addWatchPaths({QStringLiteral("device.Config.Gain"), QStringLiteral("device.Nope"),
	                                            QStringLiteral("device.Config.Counter")});
	CHECK(added.value("added").toInt() == 2 && added.value("missing").toList().size() == 1 && provider->watchCount() == 3);
	auto* list = provider->watchlist();
	const auto role = [list](int row, int which) { return list->index(row, 0).data(which).toString(); };
	const auto bound = [list](int row) {
		return list->index(row, 0).data(ux::WatchlistModel::WatchKeyRole).toULongLong() < (quint64{1} << 52);
	};
	provider->refreshWatch();
	CHECK(await([&] { return !provider->watchActive() && role(0, ux::WatchlistModel::TextRole) == "1"; }));
	const auto read = role(2, ux::WatchlistModel::TextRole);
	CHECK(read == "18446744073709551615" && role(1, ux::WatchlistModel::StatusRole) == "Missing" && !bound(1));
	const auto saved = provider->watchBindings();
	CHECK(saved.size() == 3 && saved[1].toMap() == QVariantMap({{"group", "device"}, {"name", "Nope"}}));
	// The device goes: the rows stay, with their last values, stale.
	connection.disconnect();
	CHECK(await([&] { return !bound(0) && !bound(2); }));
	CHECK(provider->watchCount() == 3 && role(0, ux::WatchlistModel::NameRole) == "device.Config.Gain");
	CHECK(role(0, ux::WatchlistModel::StatusRole) == "Stale" && role(0, ux::WatchlistModel::TextRole) == "1");
	CHECK(role(2, ux::WatchlistModel::StatusRole) == "Stale" && role(2, ux::WatchlistModel::TextRole) == read);
	CHECK(!list->index(0, 0).data(ux::WatchlistModel::WritableRole).toBool());
	CHECK(provider->watchBindings() == saved);
	// It comes back: found again in place, and read.
	connect(connection, peer);
	CHECK(await([&] { return bound(0) && bound(2); }));
	CHECK(!bound(1) && role(1, ux::WatchlistModel::StatusRole) == "Missing" && provider->watchCount() == 3);
	provider->refreshWatch();
	CHECK(await([&] { return !provider->watchActive() && role(0, ux::WatchlistModel::StatusRole) == "Ok"; }));
	CHECK(role(0, ux::WatchlistModel::TextRole) == "1" && role(2, ux::WatchlistModel::StatusRole) == "Ok");
	// A saved Watchlist comes back in its own order, the absent path in its place.
	const auto restored = provider->restoreWatchBindings(QVariantList{saved[2], saved[1], saved[0]});
	CHECK(restored.value("status") == "ok" && restored.value("added").toInt() == 2 &&
	      restored.value("missing").toList() == QVariantList{1});
	CHECK(role(0, ux::WatchlistModel::NameRole) == "device.Config.Counter" && role(1, ux::WatchlistModel::NameRole) == "device.Nope" &&
	      role(2, ux::WatchlistModel::NameRole) == "device.Config.Gain" && !provider->canUndoWatch());
	// A saved path the device has but that names nothing to watch says why.
	const auto command = provider->restoreWatchBindings({QVariantMap{{"group", "control"}, {"name", "Execute"}}});
	CHECK(command.value("status") == "ok" && provider->watchCount() == 1 && !provider->watchReadable());
	CHECK(role(0, ux::WatchlistModel::StatusRole) == "Unwatchable" &&
	      role(0, ux::WatchlistModel::WriteRefusalRole).contains("Command"));
	provider->refreshWatch();
	CHECK(!provider->watchActive() && provider->watchStatus() == "Nothing to read: none of these paths is on this device.");
	// A refused restore leaves the list as it was; a binding without a group
	// is reported and kept nowhere, since it could not be saved back.
	QVariantList tooMany;
	for (int i = 0; i <= provider->watchLimit(); ++i)
		tooMany.append(QVariantMap{{"group", "device"}, {"name", QStringLiteral("F%1").arg(i)}});
	CHECK(provider->restoreWatchBindings(tooMany).value("status") == "refused" && provider->watchCount() == 1);
	const auto odd = provider->restoreWatchBindings({QVariantMap{{"group", ""}, {"name", "Lonely"}}, saved[0]});
	CHECK(odd.value("status") == "ok" && odd.value("missing").toList() == QVariantList{0} && provider->watchCount() == 1);
	CHECK(provider->watchBindings() == QVariantList({saved[0]}));
	connection.disconnect();
}

// A value inside a struct is written from its row by writing its whole Field:
// the Field is read, the value changed in it, the whole Field written and read
// back. The other values keep what was read. The row says what Write all covers.
void memberRowWrite()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	presentation.setMutationsAllowed(true);
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	provider->setAllowMutations(true);
	auto* model = provider->endpoints();
	const auto child = [model](const QModelIndex& parent, const QString& name) {
		while (model->canFetchMore(parent))
			model->fetchMore(parent);
		for (int row = 0; row < model->rowCount(parent); ++row) {
			const auto index = model->index(row, 0, parent);
			if (index.data(ux::EndpointModel::NameRole).toString() == name)
				return index;
		}
		return QModelIndex{};
	};
	const auto config = endpoint(model, QStringLiteral("Config"), {}, 1);
	const auto gain = child(config, QStringLiteral("Gain"));
	const auto counter = child(config, QStringLiteral("Counter"));
	const auto cell = child(child(child(config, QStringLiteral("Matrix")), QStringLiteral("[1]")), QStringLiteral("[0]"));
	CHECK(provider->setWatchMany({QVariant::fromValue(gain), QVariant::fromValue(counter), QVariant::fromValue(cell)}, true)
	          .value("changed").toInt() == 3);
	auto* list = provider->watchlist();
	const auto scope = list->index(0, 0).data(ux::WatchlistModel::WriteScopeRole).toMap();
	CHECK(scope.value("field") == "Config" && scope.value("others").toInt() == 20 && scope.value("kind") == "values");
	CHECK(list->index(0, 0).data(ux::WatchlistModel::WritableRole).toBool() &&
	      list->index(0, 0).data(ux::WatchlistModel::WriteRefusalRole).toString().isEmpty());
	const auto text = [list](int row) { return list->index(row, 0).data(ux::WatchlistModel::TextRole).toString(); };
	provider->refreshWatch();
	CHECK(await([&] { return !provider->watchActive() && text(0) == "1"; }));
	CHECK(text(1) == "18446744073709551615" && text(2) == "3");
	QSignalSpy finished(provider, &ux::DynamicConfiguratorProvider::watchWriteFinished);
	const auto writes = peer.nativeWrites();
	CHECK(provider->writeWatch(0, QStringLiteral("2.0")));
	CHECK(await([&] { return finished.count() == 1; }));
	CHECK(finished.last().at(0).toBool() && peer.nativeWrites() == writes + 1);
	CHECK(provider->watchWriteOutcome() == QStringLiteral("Wrote device.Config.Gain = 2 · read back 2.") &&
	      provider->watchWriteKind() == QStringLiteral("confirmed"));
	CHECK(await([&] { return !provider->watchActive() && text(0) == "2"; }));
	CHECK(text(1) == "18446744073709551615" && text(2) == "3");
	// The element of a nested array, the same way.
	CHECK(provider->writeWatch(2, QStringLiteral("-7")));
	CHECK(await([&] { return finished.count() == 2; }));
	CHECK(provider->watchWriteOutcome() == QStringLiteral("Wrote device.Config.Matrix[1][0] = -7 · read back -7."));
	CHECK(await([&] { return !provider->watchActive() && text(2) == "-7"; }));
	CHECK(text(0) == "2" && text(1) == "18446744073709551615");
	// An invalid text is refused locally and nothing is written.
	const auto before = peer.nativeWrites();
	CHECK(provider->writeWatch(0, QStringLiteral("gain")));
	CHECK(await([&] { return finished.count() == 3; }));
	// Said in words: what the text is not, for the row's type (WL-3b).
	CHECK(!finished.last().at(0).toBool() && peer.nativeWrites() == before &&
	      provider->watchWriteOutcome() == QStringLiteral("device.Config.Gain not written: “gain” is not a number for f32.") &&
	      provider->watchWriteKind() == QStringLiteral("refused"));
	connection.disconnect();
}

// Fix 8: the Watchlist takes changes while it polls a real session. Boxes,
// a struct's member, Undo, remove and Clear change the rows between two reads;
// polling never stops and nothing reports a failure.
void watchChangesWhilePolling()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	CHECK(provider->watchChangesWhilePolling());
	auto* model = provider->endpoints();
	const auto child = [](const QModelIndex& parent, const QString& name) {
		auto* rows = const_cast<QAbstractItemModel*>(parent.model());
		while (rows->canFetchMore(parent))
			rows->fetchMore(parent);
		for (int row = 0; row < rows->rowCount(parent); ++row) {
			const auto index = rows->index(row, 0, parent);
			if (index.data(ux::EndpointModel::NameRole).toString() == name)
				return index;
		}
		return QModelIndex{};
	};
	const auto one = [](const QModelIndex& index) { return QVariantList{QVariant::fromValue(index)}; };
	const auto first = endpoint(model, QStringLiteral("Value_0"));
	const auto second = endpoint(model, QStringLiteral("Value_1"));
	const auto gain = child(endpoint(model, QStringLiteral("Config"), {}, 1), QStringLiteral("Gain"));
	CHECK(first.isValid() && second.isValid() && gain.isValid());
	auto* list = provider->watchlist();
	const auto text = [list](int row) { return list->index(row, 0).data(ux::WatchlistModel::TextRole).toString(); };
	CHECK(provider->setWatchMany(one(first), true).value("changed").toInt() == 1);
	provider->setWatchIndividual(true);
	provider->setWatchIntervalMs(20);
	provider->startWatch();
	CHECK(await([&] { return provider->watchActive() && !text(0).isEmpty(); }));
	// Ticked while polling: a whole Field and a struct's member join the reads.
	auto result = provider->setWatchMany({QVariant::fromValue(second), QVariant::fromValue(gain)}, true);
	CHECK(result.value("status") == "ok" && result.value("changed").toInt() == 2 && provider->watchActive());
	CHECK(await([&] { return provider->watchCount() == 3 && !text(1).isEmpty() && text(2) == "1"; }));
	CHECK(provider->watchState(gain) == Qt::Checked && provider->canUndoWatch());
	CHECK(provider->undoWatch().value("changed").toInt() == 2 && provider->watchCount() == 1 && provider->watchActive());
	provider->removeWatch(0);
	CHECK(provider->watchCount() == 0 && provider->watchActive());
	CHECK(provider->setWatchMany(one(second), true).value("changed").toInt() == 1);
	CHECK(await([&] { return !text(0).isEmpty(); }));
	provider->clearWatch();
	CHECK(provider->watchCount() == 0 && provider->watchActive());
	CHECK(await([&] { return provider->watchStatus() == "Nothing to read. Add a value, or Stop."; }));
	CHECK(!provider->watchStatus().startsWith("Watch read failed"));
	provider->stopWatch();
	CHECK(await([&] { return !provider->watchActive() && !provider->watchPending(); }));
	connection.disconnect();
}

// The Watchlist and the rest of the panel work together. Selecting a Command or
// Service (its request editor is open, so the provider reports editing) leaves
// Read once and polling going. While it polls, the tree selects, Details
// refreshes, a Field draft stays open with polling going on, Apply writes and a
// Command executes, all through the one ordered FIFO. A start refused because an
// operation is in flight says so instead of leaving the last status in place.
void requestSelectionKeepsWatchlist()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	presentation.setMutationsAllowed(true);
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	provider->setWatchIndividual(true);
	const auto value = endpoint(provider->endpoints(), QStringLiteral("Value_0"));
	CHECK(value.isValid() && provider->addWatchMany({QVariant::fromValue(value)}).value("added").toInt() == 1);
	const auto readOnce = [&] {
		const auto before = peer.requests();
		provider->refreshWatch();
		const bool started = provider->watchPending();
		CHECK(await([&] {
			return !provider->watchPending() && !provider->watchActive();
		}));
		return started && peer.requests() > before;
	};
	CHECK(readOnce());
	for (const auto* name : {"Execute", "Transform"}) {
		const auto request = endpoint(provider->endpoints(), QString::fromLatin1(name));
		CHECK(request.isValid() && provider->selectEndpoint(request));
		CHECK(provider->editing()); // the request editor is open
		CHECK(readOnce());
		CHECK(!provider->watchStatus().startsWith(QStringLiteral("Not started")));
	}
	// Polling started while the Service is selected keeps reading on its timer.
	provider->setWatchIntervalMs(20);
	provider->startWatch();
	CHECK(provider->watchActive());
	auto polling = peer.requests();
	CHECK(await([&] {
		return peer.requests() >= polling + 3;
	}));
	// The view's busy leaves the Watchlist's reads out: Cancel and Batch editing
	// stay enabled while it polls.
	CHECK(await([&] {
		return provider->watchPending();
	}));
	CHECK(!provider->busy());
	// While it polls, the tree still selects: a Command, then a Field whose
	// Details refresh reads it once beside the Watchlist's own reads.
	CHECK(provider->selectEndpoint(endpoint(provider->endpoints(), QStringLiteral("Execute"))));
	CHECK(provider->commandSelected() && provider->watchActive());
	const auto field = endpoint(provider->endpoints(), QStringLiteral("Value_1"));
	CHECK(await([&] {
		return provider->selectEndpoint(field);
	}));
	CHECK(await([&] {
		return provider->refresh();
	}));
	CHECK(await([&] {
		return !provider->busy() && provider->values()->rowCount() == 1;
	}));
	CHECK(provider->watchActive());
	polling = peer.requests();
	CHECK(await([&] {
		return peer.requests() >= polling + 3;
	}));
	// Writing while it polls: the draft leaves polling going and keeps its text,
	// Apply writes and reads back, then a Command executes.
	const auto config = endpoint(provider->endpoints(), QStringLiteral("Config"));
	CHECK(await([&] {
		return provider->selectEndpoint(config);
	}));
	CHECK(await([&] {
		return provider->refresh();
	}));
	CHECK(await([&] {
		return !provider->busy() && provider->values()->rowCount() == 1;
	}));
	provider->setAllowMutations(true);
	CHECK(await([&] {
		return provider->beginEdit();
	}));
	CHECK(provider->editing() && provider->watchActive());
	const auto gain = member(provider->values(), QStringLiteral("Gain"));
	CHECK(provider->setScalarText(gain, QStringLiteral("4.25")));
	const auto draft = provider->values()->data(gain, ux::ValueModel::EditorTextRole);
	polling = peer.requests();
	CHECK(await([&] {
		return peer.requests() >= polling + 3;
	}));
	CHECK(provider->editing() && provider->values()->data(gain, ux::ValueModel::EditorTextRole) == draft);
	QSignalSpy applied(provider, &ux::DynamicConfiguratorProvider::applyFinished);
	const auto writes = peer.nativeWrites();
	CHECK(await([&] {
		return provider->apply();
	}));
	CHECK(await([&] {
		return applied.count() == 1;
	}));
	CHECK(applied.last().at(0).toBool() && peer.nativeWrites() == writes + 1);
	CHECK(provider->values()->data(member(provider->values(), QStringLiteral("Gain")),
	                               ux::ValueModel::ValueRole).toString() == QStringLiteral("4.25"));
	CHECK(provider->watchActive());
	const auto commands = peer.nativeCommands();
	const auto execute = endpoint(provider->endpoints(), QStringLiteral("Execute"));
	CHECK(await([&] {
		return provider->selectEndpoint(execute);
	}));
	CHECK(await([&] {
		return provider->executeCommand();
	}));
	CHECK(await([&] {
		return !provider->busy() && peer.nativeCommands() == commands + 1;
	}));
	polling = peer.requests();
	CHECK(await([&] {
		return peer.requests() >= polling + 3;
	}));
	provider->stopWatch();
	CHECK(await([&] {
		return !provider->watchPending() && !provider->watchActive();
	}));
	// An operation in flight holds a new start, and the status says so.
	const auto value1 = endpoint(provider->endpoints(), QStringLiteral("Value_1"));
	CHECK(await([&] {
		return provider->selectEndpoint(value1);
	}));
	CHECK(provider->refresh() && provider->busy());
	provider->refreshWatch();
	CHECK(!provider->watchPending());
	CHECK(provider->watchStatus() == QStringLiteral("Not started: another operation is in progress"));
	CHECK(await([&] {
		return !provider->busy();
	}));
	CHECK(readOnce());
	// Polling once a minute: a Command makes the Watchlist read again as soon
	// as it completes, not when the minute is up.
	provider->setWatchIntervalMs(60000);
	provider->startWatch();
	CHECK(await([&] {
		return provider->watchActive() && !provider->watchPending() &&
		       provider->watchStatus().contains(QStringLiteral("complete"));
	}));
	const auto quiet = peer.requests();
	QTest::qWait(200);
	CHECK(peer.requests() == quiet); // nothing until the timer
	const auto executed = peer.nativeCommands();
	CHECK(await([&] {
		return provider->selectEndpoint(execute);
	}));
	CHECK(await([&] {
		return provider->executeCommand();
	}));
	CHECK(await([&] {
		return peer.nativeCommands() == executed + 1 && peer.requests() >= quiet + 2;
	}));
	provider->stopWatch();
	CHECK(await([&] {
		return !provider->watchPending() && !provider->watchActive();
	}));
	connection.disconnect();
}

// A write from another panel on the same session does not stop this panel's
// polling: it goes on and reads the list again.
void siblingWriteKeepsPolling()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	presentation.setMutationsAllowed(true);
	auto* watcher = presentation.createProvider();
	auto* writer = presentation.createProvider();
	connect(connection, peer);
	watcher->setWatchIndividual(true);
	const auto value = endpoint(watcher->endpoints(), QStringLiteral("Value_0"));
	CHECK(watcher->addWatchMany({QVariant::fromValue(value)}).value("added").toInt() == 1);
	// A minute between cycles: only the wake can explain a read soon after.
	watcher->setWatchIntervalMs(60000);
	watcher->startWatch();
	CHECK(await([&] {
		return !watcher->watchPending() &&
		       watcher->watchStatus().contains(QStringLiteral("complete"));
	}));
	readConfig(*writer);
	writer->setAllowMutations(true);
	CHECK(writer->beginEdit());
	CHECK(writer->setScalarText(member(writer->values(), QStringLiteral("Gain")), QStringLiteral("2.5")));
	QSignalSpy applied(writer, &ux::DynamicConfiguratorProvider::applyFinished);
	const auto writes = peer.nativeWrites();
	const auto reads = peer.fieldReads();
	CHECK(await([&] {
		return writer->apply();
	}));
	CHECK(await([&] {
		return applied.count() == 1;
	}));
	CHECK(peer.nativeWrites() == writes + 1);
	CHECK(watcher->watchActive());
	CHECK(!watcher->watchStatus().startsWith(QStringLiteral("Stopped")));
	// The writer's confirming read plus the watcher reading its list again.
	CHECK(await([&] {
		return peer.fieldReads() >= reads + 2;
	}));
	watcher->stopWatch();
	CHECK(await([&] {
		return !watcher->watchPending() && !watcher->watchActive();
	}));
	connection.disconnect();
}

// A writable watched Field is written from its own row, as Details would:
// read, exact draft, write, confirming read. The Details selection stays; a
// read-only row, a missing permission, an invalid text and a device refusal
// are each refused in words; a write while polling keeps polling going.
void watchRowWrite()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	presentation.setMutationsAllowed(true);
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	provider->setWatchIndividual(true);
	const auto setpoint = endpoint(provider->endpoints(), QStringLiteral("Setpoint"));
	const auto value0 = endpoint(provider->endpoints(), QStringLiteral("Value_0"));
	CHECK(setpoint.isValid() && value0.isValid());
	CHECK(provider->addWatchMany({QVariant::fromValue(setpoint), QVariant::fromValue(value0)})
	          .value("added")
	          .toInt() == 2);
	auto* rows = provider->watchlist();
	const auto writable = [&](int row) {
		return rows->data(rows->index(row, 0), ux::WatchlistModel::WritableRole).toBool();
	};
	const auto text = [&](int row) {
		return rows->data(rows->index(row, 0), ux::WatchlistModel::TextRole).toString();
	};
	CHECK(writable(0) && !writable(1));
	// Details shows another Field; a row write must leave that selection alone.
	const auto value1 = endpoint(provider->endpoints(), QStringLiteral("Value_1"));
	CHECK(provider->selectEndpoint(value1));
	const auto selected = provider->selectedEndpointIndex();
	CHECK(!provider->canWriteWatch() && !provider->writeWatch(0, QStringLiteral("5")));
	provider->setAllowMutations(true);
	CHECK(provider->canWriteWatch());
	CHECK(!provider->writeWatch(1, QStringLiteral("5"))); // read-only Field
	QSignalSpy finished(provider, &ux::DynamicConfiguratorProvider::watchWriteFinished);
	auto writes = peer.nativeWrites();
	CHECK(provider->writeWatch(0, QStringLiteral("1234")));
	CHECK(provider->watchWriting() && !provider->canWriteWatch());
	CHECK(await([&] {
		return finished.count() == 1;
	}));
	CHECK(finished.last().at(0).toBool() && peer.nativeWrites() == writes + 1);
	CHECK(provider->watchWriteOutcome() == QStringLiteral("Wrote device.Setpoint = 1234 · read back 1234."));
	CHECK(await([&] {
		return text(0) == QStringLiteral("1234");
	}));
	CHECK(await([&] {
		return !provider->watchActive(); // the Read once after the write finishes
	}));
	CHECK(provider->selectedEndpointIndex() == selected && provider->selection().endsWith(QStringLiteral("Value_1")));
	// An invalid text is read, refused locally and never written.
	writes = peer.nativeWrites();
	CHECK(provider->writeWatch(0, QStringLiteral("twelve")));
	CHECK(await([&] {
		return finished.count() == 2;
	}));
	CHECK(!finished.last().at(0).toBool() && peer.nativeWrites() == writes);
	CHECK(provider->watchWriteOutcome().contains(QStringLiteral("not written: “twelve” is not a number for ")));
	// The device refuses a value above its limit; the row keeps the old one.
	CHECK(provider->writeWatch(0, QStringLiteral("200000")));
	CHECK(await([&] {
		return finished.count() == 3;
	}));
	CHECK(!finished.last().at(0).toBool());
	CHECK(provider->watchWriteOutcome().contains(QStringLiteral("not written")));
	// While polling: the write goes through and polling goes on.
	provider->setWatchIntervalMs(20);
	provider->startWatch();
	CHECK(provider->watchActive());
	CHECK(await([&] {
		return provider->canWriteWatch();
	}));
	CHECK(provider->writeWatch(0, QStringLiteral("77")));
	CHECK(await([&] {
		return finished.count() == 4;
	}));
	CHECK(finished.last().at(0).toBool());
	CHECK(await([&] {
		return text(0) == QStringLiteral("77");
	}));
	CHECK(provider->watchActive());
	const auto polling = peer.requests();
	const bool kept = await([&] {
		return peer.requests() >= polling + 3;
	});
	CHECK(kept);
	provider->stopWatch();
	CHECK(await([&] {
		return !provider->watchPending() && !provider->watchActive();
	}));
	// Without permission nothing is offered.
	provider->setAllowMutations(false);
	CHECK(!provider->canWriteWatch() && !provider->writeWatch(0, QStringLiteral("1")));
	connection.disconnect();
}

// A row write may execute before disconnect loses its ACK. Its presentation
// must preserve that uncertainty, without claiming the Field was not written.
void watchRowWriteUnknown()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	presentation.setMutationsAllowed(true);
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	provider->setAllowMutations(true);
	const auto field = endpoint(provider->endpoints(), QStringLiteral("Setpoint"));
	CHECK(field.isValid());
	CHECK(provider->addWatchMany({QVariant::fromValue(field)}).value("added").toInt() == 1);
	QSignalSpy finished(provider, &ux::DynamicConfiguratorProvider::watchWriteFinished);
	bool armed = false;
	QObject::connect(&presentation, &ux::QtConnectionBinding::operationFinished, &presentation,
	                 [&](const QVariantMap& facts) {
		                 // Completion is published before the row editor submits its
		                 // Write: drop that Write's reply, never the preliminary Read.
		                 if (!armed && facts.value(QStringLiteral("operation")).toUInt() ==
		                                   unsigned(cl::Operation::FieldRead) &&
		                     facts.value(QStringLiteral("succeeded")).toBool()) {
			                 armed = true;
			                 peer.setDropNextEncodedReply();
		                 }
	                 });
	CHECK(provider->writeWatch(0, QStringLiteral("1234")));
	CHECK(await([&] { return peer.nativeWrites() == 1; }));
	CHECK(armed && finished.count() == 0);
	connection.disconnect();
	CHECK(await([&] { return finished.count() == 1 && !provider->watchWriting(); }));
	CHECK(!finished.last().at(0).toBool());
	CHECK(provider->watchWriteOutcome().contains(QStringLiteral("OutcomeUnknown")));
	CHECK(!provider->watchWriteOutcome().contains(QStringLiteral("not written")) &&
	      provider->watchWriteKind() == QStringLiteral("unconfirmed"));
	CHECK(!provider->watchWriting() && connection.recoveryRequired());
	const auto writes = peer.nativeWrites();
	connect(connection, peer);
	CHECK(!connection.recoveryRequired() && peer.nativeWrites() == writes && finished.count() == 1);
	CHECK(provider->watchWriteOutcome().contains(QStringLiteral("OutcomeUnknown")) &&
	      provider->watchWriteOutcome().contains(QStringLiteral("previous session")));
	connection.disconnect();
}

// Opening and submitting an unchanged floating-point row must preserve -0.
// Drive the real QML editor with F2/Enter. Unchanged input must not turn into
// a write; explicitly editing +0 back to -0 checks the actual TCP write bytes
// and the separate confirming read, rather than calling writeWatch directly.
void watchRowSignedZero()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	presentation.setMutationsAllowed(true);
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	provider->setWatchIndividual(true);
	provider->setAllowMutations(true);
	for (const auto& name : {QStringLiteral("SignedZero32"), QStringLiteral("SignedZero64")}) {
		const auto field = endpoint(provider->endpoints(), name);
		CHECK(field.isValid());
		CHECK(provider->addWatchMany({QVariant::fromValue(field)}).value("added").toInt() == 1);
	}
	provider->refreshWatch();
	CHECK(await([&] { return !provider->watchActive(); }));

	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("testProvider"), provider);
	QQmlComponent component(&engine);
	component.setData(R"qml(
import QtQuick
import QtQuick.Controls
import Telemetry.UX
ApplicationWindow {
    width: 840; height: 480; visible: true
    WatchlistPanel { anchors.fill: parent; provider: testProvider; collapsible: false }
}
)qml", QUrl(QStringLiteral("signed-zero-row.qml")));
	CHECK(component.isReady());
	std::unique_ptr<QObject> object(component.create());
	auto* window = qobject_cast<QQuickWindow*>(object.get());
	CHECK(window && QTest::qWaitForWindowExposed(window));
	window->requestActivate();
	CHECK(QTest::qWaitForWindowActive(window));
	auto* list = control(window->contentItem(), QStringLiteral("watchlistView"));
	auto* rows = provider->watchlist();
	QSignalSpy finished(provider, &ux::DynamicConfiguratorProvider::watchWriteFinished);
	for (int row = 0; row < 2; ++row) {
		const auto index = rows->index(row, 0);
		CHECK(rows->data(index, ux::WatchlistModel::SampleExactRole).toBool());
		const auto sampleBits = [&] {
			return std::bit_cast<std::uint64_t>(
			    rows->data(index, ux::WatchlistModel::SampleValueRole).toDouble());
		};
		const auto openEditor = [&] {
			CHECK(list->setProperty("currentIndex", row));
			list->forceActiveFocus(Qt::TabFocusReason);
			CHECK(await([&] { return list->hasActiveFocus(); }));
			QTest::keyClick(window, Qt::Key_F2);
			auto* editor = control(window->contentItem(), QStringLiteral("watchValueEditor"));
			CHECK(editor->hasActiveFocus());
			return editor;
		};
		CHECK(sampleBits() == UINT64_C(0x8000000000000000));
		CHECK(openEditor()->property("text").toString() == QStringLiteral("-0"));
		const auto writes = peer.nativeWrites();
		// Workbench only applies dirty drafts. Opening -0 and immediately
		// accepting it must therefore leave the device unchanged, with no write.
		QTest::keyClick(window, Qt::Key_Return);
		CHECK(await([&] { return finished.count() == row * 3 + 1 && !provider->watchActive(); }));
		CHECK(!finished.last().at(0).toBool() && peer.nativeWrites() == writes);
		CHECK(sampleBits() == UINT64_C(0x8000000000000000));
		// Set +0 through this same real editor, then deliberately enter -0.
		CHECK(openEditor()->property("text").toString() == QStringLiteral("-0"));
		QTest::keyClick(window, Qt::Key_0);
		QTest::keyClick(window, Qt::Key_Return);
		CHECK(await([&] { return finished.count() == row * 3 + 2 && !provider->watchActive(); }));
		CHECK(finished.last().at(0).toBool() && peer.nativeWrites() == writes + 1);
		CHECK(peer.lastWritePayload() == QByteArray(row == 0 ? 4 : 8, '\0'));
		CHECK(sampleBits() == 0);
		CHECK(openEditor()->property("text").toString() == QStringLiteral("0"));
		QTest::keyClick(window, Qt::Key_Minus);
		QTest::keyClick(window, Qt::Key_0);
		QTest::keyClick(window, Qt::Key_Return);
		CHECK(await([&] { return finished.count() == row * 3 + 3 && !provider->watchActive(); }));
		CHECK(finished.last().at(0).toBool() && peer.nativeWrites() == writes + 2);
		const auto expected = QByteArray::fromHex(row == 0 ? "00000080" : "0000000000000080");
		std::fprintf(stderr, "Signed zero F%d TCP payload=%s expected=%s\n", row == 0 ? 32 : 64,
		             peer.lastWritePayload().toHex().constData(), expected.toHex().constData());
		CHECK(peer.lastWritePayload() == expected);
		CHECK(sampleBits() == UINT64_C(0x8000000000000000));
	}
	// Hex is taken whatever the row shows (WL-3): a float's bits are written
	// exactly, and a row in hex shows them back.
	CHECK(provider->property("watchFormat").toString() == QStringLiteral("original"));
	CHECK(provider->writeWatch(0, QStringLiteral("0x4000_0000")));
	CHECK(await([&] { return finished.count() == 7 && !provider->watchActive(); }));
	CHECK(finished.last().at(0).toBool() && peer.lastWritePayload() == QByteArray::fromHex("00000040"));
	CHECK(provider->setWatchRowFormat({0}, QStringLiteral("hex")));
	CHECK(rows->data(rows->index(0, 0), ux::WatchlistModel::ShownTextRole).toString() == QStringLiteral("0x40000000"));
	CHECK(rows->data(rows->index(0, 0), ux::WatchlistModel::TextRole).toString() == QStringLiteral("2"));
	CHECK(provider->watchRowFormats() == QVariantMap({{rows->data(rows->index(0, 0), ux::WatchlistModel::NameRole).toString(),
	                                                   QStringLiteral("hex")}}));
	CHECK(!provider->setWatchRowFormat({0}, QStringLiteral("binary")) && !provider->setWatchRowFormat({9}, QString{}));
	provider->setWatchFormat(QStringLiteral("hex"));
	CHECK(rows->data(rows->index(1, 0), ux::WatchlistModel::ShownTextRole).toString().startsWith(QStringLiteral("0x")));
	provider->setWatchFormat(QStringLiteral("original"));
	// Every value back to Original (WL-3b): the Watchlist's format and every
	// row's own, one for a path not watched now included, as one change Undo
	// takes back while no format has changed since; the revision moves.
	{
		const auto gain = rows->data(rows->index(0, 0), ux::WatchlistModel::NameRole).toString();
		const QVariantMap own{{gain, QStringLiteral("hex")}, {QStringLiteral("device.Gone"), QStringLiteral("dec")}};
		CHECK(provider->setWatchRowFormats(own));
		// The mark's count (WL-3c): watched rows in a format other than the
		// Watchlist's; a path not watched now is not one of them.
		CHECK(provider->watchOwnFormatCount() == 1);
		provider->setWatchFormat(QStringLiteral("hex"));
		CHECK(provider->watchOwnFormatCount() == 0);
		provider->setWatchFormat(QStringLiteral("dec"));
		CHECK(provider->watchOwnFormatCount() == 1);
		CHECK(provider->watchFormatsDiffer());
		QSignalSpy changed(provider, &ux::DynamicConfiguratorProvider::stateChanged);
		const auto revision = provider->watchRevision();
		const auto reset = provider->resetWatchFormats();
		CHECK(reset.value("status") == "ok" && reset.value("rows").toInt() == 1 && changed.count() == 1);
		CHECK(provider->watchFormat() == QStringLiteral("original") && provider->watchRowFormats().isEmpty() &&
		      !provider->watchFormatsDiffer());
		CHECK(rows->data(rows->index(0, 0), ux::WatchlistModel::FormatRole).toString().isEmpty());
		CHECK(provider->watchRevision() != revision && provider->canUndoWatch());
		CHECK(provider->resetWatchFormats().value("status") == "unchanged");
		CHECK(provider->undoWatch().value("status") == "ok");
		CHECK(provider->watchFormat() == QStringLiteral("dec") && provider->watchRowFormats() == own);
		CHECK(rows->data(rows->index(0, 0), ux::WatchlistModel::FormatRole).toString() == QStringLiteral("hex"));
		CHECK(!provider->canUndoWatch() && provider->watchOwnFormatCount() == 1);
		// A format chosen after it - here, in another panel, by a project
		// loaded - ends the reset's Undo.
		CHECK(provider->resetWatchFormats().value("status") == "ok" && provider->canUndoWatch());
		CHECK(provider->chooseWatchFormat(QStringLiteral("hex")) && !provider->chooseWatchFormat(QStringLiteral("binary")));
		// A format chosen is a change of its own (WL-2c): Undo takes it back,
		// and the reset under it is gone.
		CHECK(provider->canUndoWatch() && provider->undoWatch().value("status") == "ok");
		CHECK(provider->watchFormat() == QStringLiteral("original") && provider->watchRowFormats().isEmpty());
		CHECK(!provider->canUndoWatch() && provider->undoWatch().value("status") == "refused");
		// A format given to many rows: one change.
		CHECK(provider->setWatchRowFormat({0, 1}, QStringLiteral("dec")) && provider->canUndoWatch());
		CHECK(provider->watchRowFormats().size() == 2 && provider->undoWatch().value("status") == "ok");
		CHECK(provider->watchRowFormats().isEmpty());
		// The property is a project's or a host's: no Undo of its own, and
		// the format Undo before it is gone.
		CHECK(provider->chooseWatchFormat(QStringLiteral("dec")) && provider->canUndoWatch());
		provider->setWatchFormat(QStringLiteral("hex"));
		CHECK(!provider->canUndoWatch());
		CHECK(provider->resetWatchFormats().value("status") == "ok" && provider->canUndoWatch());
		CHECK(provider->setWatchRowFormats(own) && !provider->canUndoWatch());
		// A pin to Original shows nothing different, yet a reset clears it.
		CHECK(provider->setWatchRowFormats({}) && provider->setWatchRowFormat({0}, QStringLiteral("original")));
		CHECK(!provider->watchFormatsDiffer() && provider->resetWatchFormats().value("status") == "ok" &&
		      provider->watchRowFormats().isEmpty());
		// Past the limit: refused, nothing changed.
		QVariantMap many;
		for (int at = 0; at <= ux::WatchlistModel::formatLimit; ++at)
			many.insert(QStringLiteral("device.X%1").arg(at), QStringLiteral("hex"));
		CHECK(!provider->setWatchRowFormats(many) && provider->watchRowFormats().isEmpty());
		CHECK(provider->setWatchRowFormat({0}, QStringLiteral("hex")));
	}
	// Too wide for the type: refused in words, nothing written.
	const auto writesBefore = peer.nativeWrites();
	// While typing: the other form of what would be written, or why not (WL-3b).
	CHECK(provider->watchEditPreview(0, QStringLiteral("2")).value("preview") == "= 0x40000000 (f32 bits)");
	CHECK(provider->watchEditPreview(0, QStringLiteral("0x3FC00000")).value("preview") == "= 1.5");
	CHECK(provider->watchEditPreview(0, QStringLiteral("abc")).value("error") == "“abc” is not a number for f32.");
	CHECK(provider->watchEditPreview(0, QString{}).value("preview").toString().isEmpty());
	// A start not yet a number is judged on Enter, not on the key.
	for (const auto* start : {"-", "0x", "1e", "2."})
		CHECK(provider->watchEditPreview(0, QString::fromLatin1(start)).value("error").toString().isEmpty());
	CHECK(!provider->writeWatch(0, QStringLiteral("0x1FFFFFFFF")));
	CHECK(provider->watchWriteOutcome().endsWith(QStringLiteral(" not written: “0x1FFFFFFFF”: f32 takes at most 8 hex digits.")) &&
	      peer.nativeWrites() == writesBefore);
	connection.disconnect();
}

// The live Plot from the Watchlist (LP-1g): Ctrl+P on a row puts its value on
// the Plot and takes it off again; on a selection it takes every value in it,
// and the row menu says how many. Counter reads a u64 no double holds: a
// number type, so it is plotted all the same.
void watchRowsToPlot()
{
	inspector_qml::TcpDemoServer peer;
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	provider->setWatchIndividual(true);
	for (const auto& name : {QStringLiteral("Counter"), QStringLiteral("Offset"), QStringLiteral("Mode")}) {
		const auto field = endpoint(provider->endpoints(), name);
		CHECK(field.isValid() && provider->addWatchMany({QVariant::fromValue(field)}).value("added").toInt() == 1);
	}
	provider->refreshWatch();
	CHECK(await([&] { return !provider->watchActive() && !provider->watchPending(); }));
	auto* traces = qobject_cast<ux::WatchTraces*>(provider->watchTraces());
	CHECK(traces && traces->plotted().isEmpty());

	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("testProvider"), provider);
	QQmlComponent component(&engine);
	component.setData(R"qml(
import QtQuick
import QtQuick.Controls
import Telemetry.UX
ApplicationWindow {
    width: 840; height: 480; visible: true
    WatchlistPanel { anchors.fill: parent; provider: testProvider; collapsible: false }
}
)qml", QUrl(QStringLiteral("rows-to-plot.qml")));
	CHECK(component.isReady());
	std::unique_ptr<QObject> object(component.create());
	auto* window = qobject_cast<QQuickWindow*>(object.get());
	CHECK(window && QTest::qWaitForWindowExposed(window));
	window->requestActivate();
	CHECK(QTest::qWaitForWindowActive(window));
	auto* list = control(window->contentItem(), QStringLiteral("watchlistView"));
	CHECK(list->setProperty("currentIndex", 0));
	list->forceActiveFocus(Qt::TabFocusReason);
	CHECK(await([&] { return list->hasActiveFocus(); }));
	const auto first = provider->watchlist()->index(0, 0).data(ux::WatchlistModel::NameRole).toString();
	// One row: on, then off; its readings stay.
	QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier);
	CHECK(await([&] { return traces->plotted() == QStringList{first}; }));
	QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier);
	CHECK(await([&] { return traces->plotted().isEmpty() && traces->count(first) > 0; }));
	// The whole list selected: every value, in the order shown.
	QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier);
	CHECK(await([&] { return traces->plotted().size() == 3 && traces->plotted().first() == first; }));
	// The row menu speaks for the selection.
	QTest::keyClick(window, Qt::Key_Menu);
	auto* item = control(window->contentItem(), QStringLiteral("watchRowPlot"));
	CHECK(item->property("text").toString() == QStringLiteral("Plot 3 values\tCtrl+P") && item->property("enabled").toBool() &&
	      !item->property("checkable").toBool());
	QTest::keyClick(window, Qt::Key_Escape);
	// Every one on it already: Ctrl+P takes them all off.
	CHECK(await([&] { return list->hasActiveFocus(); }));
	QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier);
	CHECK(await([&] { return traces->plotted().isEmpty(); }));
	object.reset();
	connection.disconnect();
}

void presentationPanels()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	auto* first = presentation.createProvider();
	auto* second = presentation.createProvider();
	// Before any device the formats wait for the rows, and a reset and its
	// Undo work on them all the same (WL-3b).
	CHECK(second->setWatchRowFormats({{QStringLiteral("device.Config.Gain"), QStringLiteral("hex")}}));
	CHECK(second->resetWatchFormats().value("status") == "ok" && second->watchRowFormats().isEmpty());
	CHECK(second->undoWatch().value("status") == "ok" && second->watchRowFormats().size() == 1);
	CHECK(second->setWatchRowFormats({}) && !second->canUndoWatch());
	connect(connection, peer);
	const auto fingerprint = QStringLiteral("0x%1").arg(connection.storage()->schema.fingerprint(),
	                                                    16, 16, QLatin1Char('0'));
	const QJsonArray entries{QJsonObject{{"category", 1},
	                                     {"id", 0},
	                                     {"role", "value"},
	                                     {"path", QJsonArray{}},
	                                     {"label", "Settings"},
	                                     {"help", "Plain device settings"}},
	                         QJsonObject{{"category", 1},
	                                     {"id", 0},
	                                     {"role", "value"},
	                                     {"path", QJsonArray{"Counter"}},
	                                     {"label", "Exact counter"},
	                                     {"unit", "ticks"}}};
	const auto document = [&](const QString& identity) {
		return QJsonDocument(QJsonObject{{"format", "telemetry-presentation"},
		                                 {"version", 1},
		                                 {"fingerprint", identity},
		                                 {"entries", entries}})
		    .toJson(QJsonDocument::Compact);
	};
	const auto metadata = document(fingerprint);
	auto input = metadata;
	const auto requests = peer.requests();
	CHECK(presentation.setPresentation(input));
	input.fill('x'); // The adapter owns its validated copy.
	CHECK(presentation.presentationError().isEmpty());
	CHECK(peer.requests() == requests && peer.fieldReads() == 0);
	CHECK(connection.workbench()->pending() == 0);
	CHECK(first->selection().isEmpty() && second->selection().isEmpty());
	CHECK(endpoint(first->endpoints(), QStringLiteral("Settings")).isValid());
	CHECK(endpoint(second->endpoints(), QStringLiteral("Settings")).isValid());
	auto* future = presentation.createProvider();
	CHECK(endpoint(future->endpoints(), QStringLiteral("Settings")).isValid());
	CHECK(peer.requests() == requests && connection.workbench()->pending() == 0);
	for (auto* provider : {first, second, future}) {
		CHECK(
		    provider->selectEndpoint(endpoint(provider->endpoints(), QStringLiteral("Settings"))));
		CHECK(provider->selection().contains(QStringLiteral("Config")));
		CHECK(provider->refresh());
	}
	CHECK(connection.workbench()->pending() == 3);
	CHECK(await([&] {
		return !first->busy() && !second->busy() && !future->busy();
	}));
	CHECK(peer.fieldReads() == 3);
	for (auto* provider : {first, second, future}) {
		const auto counter = member(provider->values(), QStringLiteral("Exact counter"));
		CHECK(counter.isValid());
		CHECK(provider->values()->data(counter, ux::ValueModel::ValueRole) ==
		      QStringLiteral("18446744073709551615 ticks"));
	}
	const auto completedRequests = peer.requests();
	CHECK(!presentation.setPresentation("{"));
	CHECK(!presentation.presentationError().isEmpty());
	CHECK(endpoint(first->endpoints(), QStringLiteral("Settings")).isValid());
	CHECK(first->values()->data(member(first->values(), QStringLiteral("Exact counter")),
	                            ux::ValueModel::ValueRole) ==
	      QStringLiteral("18446744073709551615 ticks"));
	delete second;
	second = presentation.createProvider();
	CHECK(endpoint(second->endpoints(), QStringLiteral("Settings")).isValid());
	CHECK(second->selection().isEmpty() && second->values()->rowCount() == 0);
	const auto wrongFingerprint = fingerprint == QStringLiteral("0x0000000000000000")
	                                  ? QStringLiteral("0x0000000000000001")
	                                  : QStringLiteral("0x0000000000000000");
	CHECK(presentation.setPresentation(document(wrongFingerprint)));
	for (auto* provider : {first, second, future}) {
		CHECK(endpoint(provider->endpoints(), QStringLiteral("Config")).isValid());
		CHECK(!endpoint(provider->endpoints(), QStringLiteral("Settings")).isValid());
	}
	CHECK(first->values()->data(member(first->values(), QStringLiteral("Counter")),
	                            ux::ValueModel::ValueRole) ==
	      QStringLiteral("18446744073709551615"));
	CHECK(presentation.setPresentation(metadata));
	CHECK(presentation.presentationError().isEmpty());
	CHECK(peer.requests() == completedRequests && peer.fieldReads() == 3);
	CHECK(connection.workbench()->pending() == 0);
	connection.disconnect();
	CHECK(!first->connected() && first->values()->rowCount() == 0);
	// The replacement schema reuses Field ID 0 for Voltage. Retained hints for
	// Config must stay inactive before and after that fresh admission.
	peer.setWritableDemo(false);
	connect(connection, peer);
	CHECK(connection.storage()->schema.fingerprint() !=
	      fingerprint.mid(2).toULongLong(nullptr, 16));
	for (auto* provider : {first, second, future}) {
		CHECK(provider->connected() && provider->selection().isEmpty());
		CHECK(endpoint(provider->endpoints(), QStringLiteral("Voltage")).isValid());
		CHECK(!endpoint(provider->endpoints(), QStringLiteral("Settings")).isValid());
		CHECK(provider->values()->rowCount() == 0);
	}
	CHECK(peer.fieldReads() == 3 && connection.workbench()->pending() == 0);
	CHECK(peer.nativeWrites() == 0 && peer.nativeCommands() == 0 && peer.nativeServices() == 0);
	connection.disconnect();
	peer.setWritableDemo(true);
	connect(connection, peer);
	for (auto* provider : {first, second, future})
		CHECK(endpoint(provider->endpoints(), QStringLiteral("Settings")).isValid());
	CHECK(peer.fieldReads() == 3 && connection.workbench()->pending() == 0);
	const auto finalRequests = peer.requests();
	CHECK(presentation.setPresentation({}));
	for (auto* provider : {first, second, future})
		CHECK(endpoint(provider->endpoints(), QStringLiteral("Config")).isValid());
	CHECK(peer.requests() == finalRequests && peer.fieldReads() == 3);
	CHECK(peer.nativeWrites() == 0 && peer.nativeCommands() == 0 && peer.nativeServices() == 0);
}

void disconnectedPanels()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	presentation.setMutationsAllowed(true);
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	provider->setAllowMutations(true);
	readConfig(*provider);
	CHECK(provider->beginEdit());
	CHECK(provider->setScalarText(member(provider->values(), QStringLiteral("Gain")), QStringLiteral("2")));
	CHECK(provider->apply() && await([&] { return !provider->busy(); }));
	CHECK(!provider->writeOutcome().isEmpty());
	CHECK(!provider->writeOutcomeNeedsAttention());
	CHECK(provider->selectEndpoint(endpoint(provider->endpoints(), QStringLiteral("Execute"))));
	CHECK(provider->executeCommand() && await([&] { return !provider->busy(); }));
	CHECK(!provider->commandOutcome().isEmpty());
	CHECK(!provider->commandOutcomeNeedsAttention());
	CHECK(provider->selectEndpoint(endpoint(provider->endpoints(), QStringLiteral("Transform"))));
	CHECK(provider->callService() && await([&] { return !provider->busy(); }));
	CHECK(!provider->serviceOutcome().isEmpty());
	CHECK(!provider->serviceOutcomeNeedsAttention());
	CHECK(provider->selectEndpoint(endpoint(provider->endpoints(), QStringLiteral("Config"))));
	QQmlEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("testProvider"), provider);
	QQmlComponent component(&engine);
	component.setData(R"qml(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX
ApplicationWindow {
    width: 960; height: 600; visible: true
    RowLayout {
        anchors.fill: parent
        FieldEditor { objectName: "fieldPanel"; Layout.fillWidth: true; Layout.fillHeight: true; provider: testProvider }
        CommandPanel { objectName: "commandPanel"; Layout.fillWidth: true; Layout.fillHeight: true; provider: testProvider }
        ServicePanel { objectName: "servicePanel"; Layout.fillWidth: true; Layout.fillHeight: true; provider: testProvider }
    }
}
)qml", QUrl(QStringLiteral("retained-outcomes.qml")));
	CHECK(component.isReady());
	std::unique_ptr<QObject> object(component.create());
	auto* window = qobject_cast<QQuickWindow*>(object.get());
	CHECK(window && QTest::qWaitForWindowExposed(window));
	CHECK(visual(window->contentItem(), QStringLiteral("writeOutcomeLabel")));
	CHECK(!visual(window->contentItem(), QStringLiteral("commandOutcomeLabel")));
	CHECK(!visual(window->contentItem(), QStringLiteral("serviceOutcomeLabel")));
	const auto writes = peer.nativeWrites(), commands = peer.nativeCommands(), services = peer.nativeServices();
	connection.disconnect();
	CHECK(!provider->writeOutcomeNeedsAttention() && !provider->commandOutcomeNeedsAttention() &&
	      !provider->serviceOutcomeNeedsAttention());
	CHECK(await([&] {
		return visual(window->contentItem(), QStringLiteral("writeOutcomeLabel")) &&
		       visual(window->contentItem(), QStringLiteral("commandOutcomeLabel")) &&
		       visual(window->contentItem(), QStringLiteral("serviceOutcomeLabel"));
	}));
	CHECK(!visual(window->contentItem(), QStringLiteral("applyButton")) &&
	      !visual(window->contentItem(), QStringLiteral("executeButton")) &&
	      !visual(window->contentItem(), QStringLiteral("callServiceButton")));
	connect(connection, peer);
	CHECK(!visual(window->contentItem(), QStringLiteral("commandOutcomeLabel")) &&
	      !visual(window->contentItem(), QStringLiteral("serviceOutcomeLabel")));
	CHECK(peer.nativeWrites() == writes && peer.nativeCommands() == commands && peer.nativeServices() == services);
	connection.disconnect();
}

void outcomeAttention()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	presentation.setMutationsAllowed(true);
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	provider->setAllowMutations(true);
	CHECK(!provider->writeOutcomeNeedsAttention() && !provider->commandOutcomeNeedsAttention() &&
	      !provider->serviceOutcomeNeedsAttention());
	CHECK(provider->selectEndpoint(endpoint(provider->endpoints(), QStringLiteral("Accept"))));
	CHECK(provider->executeCommand() && !provider->commandOutcomeNeedsAttention());
	CHECK(await([&] { return !provider->busy(); }));
	CHECK(peer.acceptedCommands() == 1 && provider->commandOutcomeNeedsAttention());
	CHECK(provider->selectEndpoint(endpoint(provider->endpoints(), QStringLiteral("Execute"))));
	CHECK(provider->executeCommand() && !provider->commandOutcomeNeedsAttention());
	CHECK(await([&] { return !provider->busy(); }));
	CHECK(peer.executedCommands() == 1 && !provider->commandOutcomeNeedsAttention());
	CHECK(provider->selectEndpoint(endpoint(provider->endpoints(), QStringLiteral("Reject"))));
	CHECK(provider->callService() && !provider->serviceOutcomeNeedsAttention());
	CHECK(await([&] { return !provider->busy(); }));
	CHECK(provider->serviceOutcomeNeedsAttention());
	CHECK(provider->selectEndpoint(endpoint(provider->endpoints(), QStringLiteral("Ping"), {}, 3)));
	QSignalSpy serviceDone(provider, &ux::DynamicConfiguratorProvider::serviceFinished);
	CHECK(provider->callService() && !provider->serviceOutcomeNeedsAttention());
	CHECK(await([&] { return serviceDone.size() == 1 && !provider->busy(); }));
	CHECK(serviceDone.last().at(0).toBool() && !serviceDone.last().at(1).toBool() &&
	      !provider->serviceOutcomeNeedsAttention()); // Successful Void has no response to publish.
	readConfig(*provider);
	CHECK(provider->beginEdit());
	CHECK(provider->setScalarText(member(provider->values(), QStringLiteral("Gain")), QStringLiteral("3.5")));
	CHECK(provider->canApply());
	peer.setNextFieldReadUnavailable();
	QSignalSpy applied(provider, &ux::DynamicConfiguratorProvider::applyFinished);
	CHECK(provider->apply());
	CHECK(!provider->writeOutcomeNeedsAttention());
	CHECK(await([&] { return applied.size() == 1 && !provider->busy(); }));
	CHECK(peer.nativeWrites() == 1 && !applied.last().at(0).toBool() && provider->writeOutcomeNeedsAttention());
	connection.disconnect();
	CHECK(provider->writeOutcomeNeedsAttention() && !provider->commandOutcomeNeedsAttention() &&
	      !provider->serviceOutcomeNeedsAttention());
	connect(connection, peer);
	CHECK(provider->writeOutcomeNeedsAttention() && !provider->commandOutcomeNeedsAttention() &&
	      !provider->serviceOutcomeNeedsAttention());
}

enum class OutcomePhase : std::uint8_t {
	Prepared,
	Executed,
};

void unknownOutcomeAttention(cl::Operation operation, OutcomePhase phase)
{
	const bool service = operation == cl::Operation::Service;
	const bool executed = phase == OutcomePhase::Executed;
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	presentation.setMutationsAllowed(true);
	auto* provider = presentation.createProvider();
	connect(connection, peer);
	provider->setAllowMutations(true);
	CHECK(provider->selectEndpoint(endpoint(provider->endpoints(), service ? QStringLiteral("Transform") : QStringLiteral("Execute"))));
	if (executed)
		peer.setDropNextEncodedReply();
	CHECK(service ? provider->callService() : provider->executeCommand());
	CHECK(service ? !provider->serviceOutcomeNeedsAttention() : !provider->commandOutcomeNeedsAttention());
	if (executed) {
		CHECK(await([&] { return service ? peer.nativeServices() == 1 : peer.nativeCommands() == 1; }));
	} else {
		auto* owner = connection.workbench();
		owner->process(connection.storage()->now());
		const auto packet = owner->nextPacket();
		CHECK(bool(packet) && owner->prepareHandoff(packet.session, packet.requestId));
	}
	connection.disconnect();
	CHECK(service ? provider->serviceOutcomeNeedsAttention() : provider->commandOutcomeNeedsAttention());
	const auto mutations = service ? peer.nativeServices() : peer.nativeCommands();
	connect(connection, peer);
	CHECK(service ? provider->serviceOutcomeNeedsAttention() : provider->commandOutcomeNeedsAttention());
	CHECK((service ? peer.nativeServices() : peer.nativeCommands()) == mutations);
}

void phase(const QString& name)
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	presentation.setMutationsAllowed(true);
	auto* provider = presentation.createProvider();
	QSignalSpy facts(&presentation, &ux::QtConnectionBinding::operationFinished);
	connect(connection, peer);
	readConfig(*provider);
	facts.clear();
	provider->setAllowMutations(true);
	CHECK(provider->beginEdit());
	CHECK(provider->setScalarText(member(provider->values(), QStringLiteral("Gain")),
	                              QStringLiteral("3.5")));
	if (name == QStringLiteral("executed"))
		peer.setDropNextEncodedReply();
	CHECK(provider->apply());
	CHECK(!provider->writeOutcomeNeedsAttention());
	if (name == QStringLiteral("offered") || name == QStringLiteral("prepared")) {
		auto* owner = connection.workbench();
		owner->process(connection.storage()->now());
		const auto packet = owner->nextPacket();
		CHECK(bool(packet));
		if (name == QStringLiteral("prepared"))
			CHECK(owner->prepareHandoff(packet.session, packet.requestId));
	} else if (name == QStringLiteral("executed"))
		CHECK(await([&] {
			return peer.nativeWrites() == 1;
		}));
	connection.disconnect();
	CHECK(provider->writeOutcomeNeedsAttention());
	CHECK(facts.count() == 1);
	const auto fact = facts.last().at(0).toMap();
	const bool possible = name == QStringLiteral("prepared") || name == QStringLiteral("executed");
	CHECK(fact.value(QStringLiteral("status")).toUInt() ==
	      unsigned(possible ? cl::Status::OutcomeUnknown : cl::Status::Cancelled));
	CHECK(connection.recoveryRequired() == possible);
	const auto writes = peer.nativeWrites();
	connect(connection, peer);
	CHECK(!connection.recoveryRequired());
	CHECK(provider->writeOutcomeNeedsAttention());
	CHECK(peer.nativeWrites() == writes && facts.count() == 1);
	std::printf("Qt Session phase=%s status=%u writes=%llu\n", qPrintable(name),
	            fact.value(QStringLiteral("status")).toUInt(),
	            static_cast<unsigned long long>(writes));
}

void removedPanel()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	auto adapter = std::make_unique<ux::QtConnectionBinding>(&connection);
	adapter->setMutationsAllowed(true);
	auto* provider = adapter->createProvider();
	ux::QtConnectionBinding observer{&connection};
	QSignalSpy facts(&observer, &ux::QtConnectionBinding::operationFinished);
	connect(connection, peer);
	readConfig(*provider);
	facts.clear();
	provider->setAllowMutations(true);
	CHECK(provider->beginEdit());
	CHECK(provider->setScalarText(member(provider->values(), QStringLiteral("Gain")),
	                              QStringLiteral("2")));
	CHECK(provider->apply());
	adapter.reset();
	CHECK(await([&] {
		return facts.count() == 1;
	}));
	CHECK(peer.nativeWrites() == 1 && connection.ready());
}

void removeSessionDuringReset()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	auto connection = std::make_unique<host::ConnectionSession>();
	ux::QtConnectionBinding presentation{connection.get()};
	presentation.setMutationsAllowed(true);
	auto* provider = presentation.createProvider();
	QSignalSpy facts(&presentation, &ux::QtConnectionBinding::operationFinished);
	connect(*connection, peer);
	readConfig(*provider);
	facts.clear();
	provider->setAllowMutations(true);
	CHECK(provider->beginEdit());
	CHECK(provider->setScalarText(member(provider->values(), QStringLiteral("Gain")),
	                              QStringLiteral("2")));
	CHECK(provider->apply());
	QObject::connect(provider->endpoints(), &QAbstractItemModel::modelReset, &presentation, [&] {
		connection.reset();
	});
	connection->disconnect();
	CHECK(!connection && !provider->connected());
	CHECK(facts.count() == 1);
	CHECK(facts.last().at(0).toMap().value(QStringLiteral("status")).toUInt() ==
	      unsigned(cl::Status::Cancelled));
	CHECK(peer.nativeWrites() == 0);
}

void reentrantAdmission()
{
	inspector_qml::TcpDemoServer peer;
	CHECK(peer.listen());
	std::unique_ptr<host::ConnectionSession> deleted;
	deleted = std::make_unique<host::ConnectionSession>([&](QByteArray bytes) {
		deleted.reset();
		return std::make_shared<host::ConnectionStorage>(std::move(bytes));
	});
	CHECK(deleted->connectTcpReference(QStringLiteral("127.0.0.1"), peer.port()));
	CHECK(await([&] {
		return !deleted;
	}));
	CHECK(await([&] {
		return !peer.hasClient();
	}));
	host::ConnectionSession connection;
	connect(connection, peer);
	bool cancel = true;
	QObject::connect(&connection, &host::ConnectionSession::aboutToRetire, &connection, [&] {
		if (cancel) {
			cancel = false;
			connection.disconnect();
		}
	});
	const auto before = peer.connections();
	CHECK(!connection.connectTcpReference(QStringLiteral("127.0.0.1"), peer.port()));
	CHECK(!connection.ready() && !connection.connecting());
	QCoreApplication::processEvents();
	CHECK(peer.connections() == before);
	connect(connection, peer);
	CHECK(connection.ready());
}

} // namespace
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void injectedApplicationLeak()
{
	void* memory = std::malloc(123);
	CHECK(memory);
	std::memset(memory, 0x42, 123);
#if defined(__GNUC__)
	asm volatile("" : : "r"(memory) : "memory");
#endif
}

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& text) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
			++warnings;
		std::fprintf(stderr, "%s\n", qPrintable(text));
	});
	twoPanels();
	nativeWatchFrames();
	bulkSessionWatch();
	groupWatchBoxes();
	memberWatchBoxes();
	watchPaths();
	memberRowWrite();
	watchBindings();
	watchKeptAcrossReconnect();
	batchPaths();
	watchChangesWhilePolling();
	requestSelectionKeepsWatchlist();
	siblingWriteKeepsPolling();
	watchRowWrite();
	watchRowWriteUnknown();
	watchRowSignedZero();
	watchRowsToPlot();
	presentationPanels();
	disconnectedPanels();
	outcomeAttention();
	unknownOutcomeAttention(cl::Operation::Command, OutcomePhase::Prepared);
	unknownOutcomeAttention(cl::Operation::Command, OutcomePhase::Executed);
	unknownOutcomeAttention(cl::Operation::Service, OutcomePhase::Prepared);
	unknownOutcomeAttention(cl::Operation::Service, OutcomePhase::Executed);
	phase(QStringLiteral("queued"));
	phase(QStringLiteral("offered"));
	phase(QStringLiteral("prepared"));
	phase(QStringLiteral("executed"));
	removedPanel();
	removeSessionDuringReset();
	reentrantAdmission();
	CHECK(!warnings);
	if (app.arguments().contains(QStringLiteral("--application-leak-control")))
		injectedApplicationLeak();
	std::printf("Qt owned connection checks=%u warnings=%u panels=2 tcp=1\n", checks, warnings);
}
