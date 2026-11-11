// Installed owner and reusable UI: real TCP, single FIFO, no replay. MIT.
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <Telemetry/UX/TrendHistory.hpp>
#include <integration/qt/ConnectionSession.hpp>
#include "TcpDemoServer.hpp"
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
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
#include <functional>
#include <cstring>

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

QModelIndex endpoint(QAbstractItemModel* model, const QString& name, const QModelIndex& parent = {})
{
	for (int row = 0; row < model->rowCount(parent); ++row) {
		const auto index = model->index(row, 0, parent);
		if (model->data(index, ux::EndpointModel::IsEndpointRole).toBool() &&
		    model->data(index, ux::EndpointModel::NameRole).toString() == name)
			return index;
		const auto child = endpoint(model, name, index);
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
	CHECK(await([&] {
		result = visual(parent, name, text);
		return result && result->width() > 0 && result->height() > 0;
	}));
	return result;
}

void click(QQuickWindow& window, QQuickItem& item)
{
	item.ensurePolished();
	const auto point = item.mapToScene({item.width() / 2, item.height() / 2}).toPoint();
	CHECK(item.isEnabled() && point.x() >= 0 && point.y() >= 0 && point.x() < window.width() &&
	      point.y() < window.height());
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
	first->refreshWatch();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
	QCoreApplication::processEvents();
	CHECK(peer.requests() == requestsBeforeBulk && !first->watchPending());
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

void presentationPanels()
{
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	auto* first = presentation.createProvider();
	auto* second = presentation.createProvider();
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
	CHECK(facts.count() == 1);
	const auto fact = facts.last().at(0).toMap();
	const bool possible = name == QStringLiteral("prepared") || name == QStringLiteral("executed");
	CHECK(fact.value(QStringLiteral("status")).toUInt() ==
	      unsigned(possible ? cl::Status::OutcomeUnknown : cl::Status::Cancelled));
	CHECK(connection.recoveryRequired() == possible);
	const auto writes = peer.nativeWrites();
	connect(connection, peer);
	CHECK(!connection.recoveryRequired());
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
	bulkSessionWatch();
	presentationPanels();
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
