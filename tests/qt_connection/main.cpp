// Installed owner and reusable UI: real TCP, single FIFO, no replay. MIT.
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <integration/qt/ConnectionSession.hpp>
#include "TcpDemoServer.hpp"
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
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection}, sibling{&connection};
	presentation.setMutationsAllowed(true);
	auto* first = presentation.createProvider();
	auto* second = sibling.createProvider();
	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("connection"), &connection);
	engine.rootContext()->setContextProperty(QStringLiteral("configuration"), first);
	engine.rootContext()->setContextProperty(QStringLiteral("measurements"), second);
	engine.load(QUrl{QStringLiteral("qrc:/Main.qml")});
	CHECK(engine.rootObjects().size() == 1);
	auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
	CHECK(window && QTest::qWaitForWindowExposed(window));
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
	QSignalSpy applied(first, &ux::DynamicConfiguratorProvider::applyFinished);
	CHECK(first->apply());
	CHECK(await([&] {
		return applied.count() == 1;
	}));
	CHECK(applied.last().at(0).toBool() && peer.nativeWrites() == 1);
	CHECK(second->values()->rowCount() ==
	      0); // Shared registry invalidates sibling, even other adapter.
	connection.disconnect();
	CHECK(!first->connected() && !second->connected());
	CHECK(!first->writeOutcome().isEmpty());
	connect(connection, peer);
	CHECK(first->connected() && second->connected() && peer.nativeWrites() == 1);
	CHECK(first->values()->rowCount() == 0 && first->selection().isEmpty());
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
