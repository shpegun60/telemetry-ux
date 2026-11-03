/*
 * @file WatchlistFailures.cpp
 * @brief Real QML watch caches and accepted-read lifetime after foreground failures.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "InspectorController.hpp"
#include <integration/tcp/Server.hpp>
#include <resource/BytesFile.hpp>
#include <resource/FileSystem.hpp>
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace ui = inspector_qml;
namespace tcp = telemetry::integration::tcp;
namespace ew = telemetry::protocol::encoded;

namespace {
unsigned checks = 0, failures = 0;

void check(bool passed, int line)
{
	++checks;
	if (!passed) {
		std::fprintf(stderr, "Inspector Watchlist Failures check %u failed at line %d\n", checks,
		             line);
		std::exit(1);
	}
}

#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)

void expect(bool passed, const char* path, const char* condition)
{
	++checks;
	if (!passed) {
		++failures;
		std::fprintf(stderr, "Watchlist cache regression path=%s condition=%s\n", path, condition);
	}
}

template<class F>
bool await(F ready, int timeout = 5000)
{
	QElapsedTimer elapsed;
	elapsed.start();
	while (!ready() && elapsed.elapsed() < timeout) {
		QCoreApplication::processEvents();
		QTest::qWait(1);
	}
	return ready();
}

struct Device {
	std::uint64_t value = UINT64_C(18446744073709551615);
	unsigned reads = 0, writes = 0, commands = 0, services = 0;

	std::uint64_t read() noexcept
	{
		++reads;
		return value;
	}

	telemetry::WriteResult write(const std::uint64_t& next) noexcept
	{
		++writes;
		value = next;
		return telemetry::WriteResult::Applied;
	}

	std::int32_t other() noexcept
	{
		++reads;
		return -17;
	}

	telemetry::CommandResult execute() noexcept
	{
		++commands;
		return telemetry::CommandResult::Executed;
	}

	telemetry::ServiceResult<void> transform() noexcept
	{
		++services;
		return telemetry::ServiceResult<void>::success();
	}
};

Device prototype;
constexpr telemetry::FieldTable fields{
    telemetry::field<&Device::read, &Device::write>("Value", prototype),
    telemetry::field<&Device::other>("Other", prototype)};
constexpr telemetry::FieldCatalogTable catalogs{telemetry::group("device", fields)};
constexpr telemetry::CommandTable commands{
    telemetry::command<&Device::execute>("Execute", prototype)};
constexpr telemetry::CommandCatalogTable commandCatalogs{telemetry::group("control", commands)};
constexpr telemetry::ServiceTable services{
    telemetry::service<&Device::transform>("Transform", prototype)};
constexpr telemetry::ServiceCatalogTable serviceCatalogs{telemetry::group("control", services)};
constexpr telemetry::Model model{catalogs, commandCatalogs, serviceCatalogs};
constexpr telemetry::descriptor::Descriptor descriptor{model};
constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();

// The fixture supplies sockets only. Resource discovery, stream assembly and every
// native operation still run through the production portable TCP server.
class Peer final : public QObject {
public:
	Device device;
	bool dropNext = false, holdNext = false;
	QByteArray held;
	unsigned requests = 0;
	decltype(fields) boundFields{telemetry::field<&Device::read, &Device::write>("Value", device),
	                             telemetry::field<&Device::other>("Other", device)};
	decltype(catalogs) boundCatalogs{telemetry::group("device", boundFields)};
	decltype(commands) boundCommands{telemetry::command<&Device::execute>("Execute", device)};
	decltype(commandCatalogs) boundCommandCatalogs{telemetry::group("control", boundCommands)};
	decltype(services) boundServices{telemetry::service<&Device::transform>("Transform", device)};
	decltype(serviceCatalogs) boundServiceCatalogs{telemetry::group("control", boundServices)};
	decltype(model) boundModel{boundCatalogs, boundCommandCatalogs, boundServiceCatalogs};
	alignas(std::max_align_t) std::array<std::byte, model.maxScratch()> scratch{};
	telemetry::Workspace workspace{scratch};
	resource::BytesFile descriptorFile{descriptorBytes};
	resource::FileSystem<1> files{
	    resource::filesystem(resource::file("/telemetry/descriptor.bin", descriptorFile))};
	std::array<std::byte, 4096> rx{}, tx{};
	std::unique_ptr<tcp::Server> server;
	QTcpServer listener;
	QPointer<QTcpSocket> socket;
	QByteArray pending;

	Peer()
	{
		QObject::connect(&listener, &QTcpServer::newConnection, this, [&] {
			CHECK(!socket);
			socket = listener.nextPendingConnection();
			server =
			    std::make_unique<tcp::Server>(boundModel.view(), workspace, files.view(), rx, tx);
			QObject::connect(socket, &QTcpSocket::readyRead, this, [&] {
				receive();
			});
		});
		CHECK(listener.listen(QHostAddress::LocalHost, 0));
	}

	~Peer() override
	{
		if (socket) {
			QObject::disconnect(socket, nullptr, this, nullptr);
			socket->abort();
		}
		if (server)
			server->close();
	}

	void receive()
	{
		pending += socket->readAll();
		while (!pending.isEmpty()) {
			const auto input =
			    std::span<const std::byte>{reinterpret_cast<const std::byte*>(pending.constData()),
			                               std::size_t(pending.size())};
			const auto result = server->receive(input);
			CHECK(result.consumed <= input.size());
			pending.remove(0, qsizetype(result.consumed));
			if (result.status == tcp::Status::Ready) {
				const auto reply = server->reply();
				QByteArray owned{reinterpret_cast<const char*>(reply.data()),
				                 qsizetype(reply.size())};
				const bool encoded = owned.startsWith("TEQ1");
				server->releaseReply();
				if (encoded) {
					++requests;
					if (dropNext) {
						dropNext = false;
						continue;
					}
					if (holdNext) {
						holdNext = false;
						held = std::move(owned);
						continue;
					}
				}
				CHECK(socket->write(owned) == owned.size());
			} else {
				CHECK(result.status == tcp::Status::NeedMore && result.consumed != 0);
			}
		}
	}

	void release()
	{
		CHECK(socket && !held.isEmpty());
		CHECK(socket->write(held) == held.size());
		held.clear();
	}
};

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

QQuickItem* visual(QQuickItem* parent, const QString& name)
{
	if (parent->objectName() == name && parent->isVisible())
		return parent;
	for (auto* child : parent->childItems())
		if (auto* found = visual(child, name))
			return found;
	return nullptr;
}

struct Fixture {
	Peer peer;
	ui::InspectorController controller;
	QQmlApplicationEngine engine;
	QQuickWindow* window = nullptr;
	QPointer<QQuickItem> cached;
	bool warnings = false;
	const char* context;

	explicit Fixture(const char* phase) : context(phase)
	{
		CHECK(controller.connectTcp(QStringLiteral("127.0.0.1"), peer.listener.serverPort()));
		CHECK(await([&] {
			return controller.connected();
		}));
		controller.setWritesEnabled(true);
		controller.setWatchIndividual(true);
		select("Value");
		CHECK(controller.addWatch() && controller.refreshWatch());
		CHECK(await([&] {
			return !controller.watchPending();
		}));
		CHECK(peer.device.reads == 1 && text() == "18446744073709551615");
		QObject::connect(&engine, &QQmlApplicationEngine::warnings, &controller,
		                 [&](const QList<QQmlError>& errors) {
			                 warnings = true;
			                 std::fprintf(
			                     stderr, "Watchlist QML warning phase=%s connected=%d pending=%d\n",
			                     context, controller.connected(), controller.watchPending());
			                 for (const auto& error : errors)
				                 std::fprintf(stderr, "%s\n", qPrintable(error.toString()));
		                 });
		engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
		engine.load(QUrl::fromLocalFile(QStringLiteral(INSPECTOR_QML_SOURCE_FILE)));
		CHECK(engine.rootObjects().size() == 1);
		window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
		CHECK(window && QTest::qWaitForWindowExposed(window));
		window->resize(1280, 960);
		CHECK(await([&] {
			return visual(window->contentItem(), "watchlistExpanded");
		}));
		CHECK(visual(window->contentItem(), "watchlistExpanded")->setProperty("checked", true));
		CHECK(await([&] {
			cached = visual(window->contentItem(), "watchlistRow");
			return cached && cached->property("text").toString() == text();
		}));
		CHECK(cached->property("status") == "Ok" && !warnings);
	}

	void select(const QString& name, int category = 1)
	{
		auto& model = *qobject_cast<ui::EndpointModel*>(controller.endpoints());
		const auto found = endpoint(model, name, category);
		CHECK(found.isValid() && controller.selectEndpoint(found));
	}

	QString text()
	{
		auto* model = controller.watchlist();
		return model->data(model->index(0, 0), ui::WatchlistModel::TextRole).toString();
	}
};

enum class Action {
	Read,
	Command,
	Service,
	Apply,
	Readback
};

void foregroundFailure(Action action, const char* name)
{
	Fixture fixture{name};
	auto& controller = fixture.controller;
	if (action == Action::Command)
		fixture.select("Execute", 2);
	else if (action == Action::Service)
		fixture.select("Transform", 3);
	else
		fixture.select("Value");
	if (action == Action::Apply || action == Action::Readback) {
		CHECK(controller.refresh() && await([&] {
			      return !controller.busy();
		      }));
		CHECK(controller.beginEdit());
		CHECK(controller.setScalarText(controller.values()->index(0, 0), QStringLiteral("123")));
	}
	QSignalSpy changed{controller.watchlist(), &QAbstractItemModel::dataChanged};
	QSignalSpy resets{controller.watchlist(), &QAbstractItemModel::modelReset};
	QSignalSpy reads{&controller, &ui::InspectorController::readFinished};
	QSignalSpy commandsDone{&controller, &ui::InspectorController::commandFinished};
	QSignalSpy servicesDone{&controller, &ui::InspectorController::serviceFinished};
	QSignalSpy applied{&controller, &ui::InspectorController::applyFinished};
	controller.setReadTimeout(1000);
	if (action == Action::Readback)
		fixture.peer.holdNext = true;
	else
		fixture.peer.dropNext = true;
	const auto before = fixture.peer.requests;
	const bool accepted = action == Action::Read      ? controller.refresh()
	                      : action == Action::Command ? controller.executeCommand()
	                      : action == Action::Service ? controller.callService()
	                                                  : controller.apply();
	CHECK(accepted && controller.busy());
	CHECK(await(
	    [&] {
		    return fixture.peer.requests == before + 1;
	    },
	    750));
	if (action == Action::Readback) {
		CHECK(fixture.peer.device.writes == 1 && !fixture.peer.held.isEmpty());
		fixture.peer.dropNext = true;
		fixture.peer.release();
		CHECK(await(
		    [&] {
			    return fixture.peer.requests == before + 2;
		    },
		    750));
	}
	CHECK(await([&] {
		return !controller.connected() && !controller.busy();
	}));
	CHECK(!controller.inspector().isCurrent() && controller.watchCount() == 1);
	CHECK(fixture.text().isEmpty()); // A direct re-read is safe even in the broken version.
	CHECK(controller.watchlist()->data(controller.watchlist()->index(0, 0),
	                                   ui::WatchlistModel::StatusRole) == "Stale");
	QTest::qWait(20); // Let model role notifications, rather than an explicit refresh, update QML.
	auto* row = visual(fixture.window->contentItem(), "watchlistRow");
	const auto cachedText = row ? row->property("text").toString() : QString{};
	const auto cachedStatus = row ? row->property("status").toString() : QString{};
	std::printf(
	    "Watchlist foreground path=%s modelTextEmpty=%d cachedText=%s cachedStatus=%s dataChanged=%lld resets=%lld\n",
	    name, fixture.text().isEmpty(), qPrintable(cachedText), qPrintable(cachedStatus),
	    static_cast<long long>(changed.count()), static_cast<long long>(resets.count()));
	expect(changed.count() + resets.count() > 0, name, "model notification");
	expect(row && cachedText.isEmpty() && cachedStatus == "Stale", name,
	       "cached QML role retirement");
	const auto completions =
	    reads.count() + commandsDone.count() + servicesDone.count() + applied.count();
	CHECK(completions == 1);
	CHECK(
	    (action != Action::Read || !reads.first()[0].toBool()) &&
	    (action != Action::Command || !commandsDone.first()[0].toBool()) &&
	    (action != Action::Service || !servicesDone.first()[0].toBool()) &&
	    ((action != Action::Apply && action != Action::Readback) || !applied.first()[0].toBool()));
	const auto requests = fixture.peer.requests;
	for (unsigned i = 0; i < 4; ++i)
		controller.pump();
	QTest::qWait(30);
	CHECK(fixture.peer.requests == requests &&
	      reads.count() + commandsDone.count() + servicesDone.count() + applied.count() == 1);
	CHECK(!controller.watchRunning() && !controller.watchPending() && !fixture.warnings);
}

void acceptedWatchDrain(bool handedOff)
{
	Fixture fixture{handedOff ? "handed-watch" : "queued-watch"};
	auto& controller = fixture.controller;
	QSignalSpy changed{controller.watchlist(), &QAbstractItemModel::dataChanged};
	QSignalSpy resets{controller.watchlist(), &QAbstractItemModel::modelReset};
	const auto requests = fixture.peer.requests;
	fixture.peer.holdNext = true;
	CHECK(controller.refreshWatch() && controller.watchPending() &&
	      controller.inspector().pending() == 1);
	if (handedOff)
		CHECK(await([&] {
			return !fixture.peer.held.isEmpty();
		}));
	controller.stopWatch();
	CHECK(controller.watchPending() && controller.inspector().pending() == 1);
	CHECK(changed.count() == 0 && resets.count() == 0);
	auto* endpointTree = visual(fixture.window->contentItem(), "endpointTree");
	auto* endpointRow = visual(fixture.window->contentItem(), "endpointRow");
	auto* endpoints = controller.endpoints();
	const auto first = endpoints->index(0, 0);
	const auto last = endpoints->index(endpoints->rowCount() - 1, 0);
	CHECK(endpointTree && endpointRow && !endpointRow->property("text").toString().isEmpty());
	CHECK(first.isValid() && last.isValid() && endpoints->data(first, Qt::DisplayRole).isValid());
	controller.inspector().invalidate();
	// Invalidating admission does not itself deliver the already accepted callback.
	CHECK(controller.watchPending() && controller.inspector().pending() == 1);
	CHECK(first.isValid() && !endpoints->data(first, Qt::DisplayRole).isValid());
	std::printf("Watchlist stale endpoint phase=%s rows=%d pending=%zu requestDelta=%u\n",
	            fixture.context, endpoints->rowCount(), controller.inspector().pending(),
	            fixture.peer.requests - requests);
	std::fflush(stdout);
	// Force a live delegate to reread the guarded role before the owner pump
	// clears its retained tree. Qt Basic's default contentItem reads model.display.
	Q_EMIT endpoints->dataChanged(first, last, {Qt::DisplayRole});
	CHECK(QMetaObject::invokeMethod(endpointTree, "forceLayout", Qt::DirectConnection));
	CHECK(controller.watchPending() && controller.inspector().pending() == 1);
	CHECK(!fixture.warnings);
	CHECK(await([&] {
		return !controller.watchPending() && controller.inspector().pending() == 0;
	}));
	CHECK(!controller.connected() && controller.watchCount() == 1);
	CHECK(fixture.text().isEmpty());
	auto* row = visual(fixture.window->contentItem(), "watchlistRow");
	expect(row && row->property("text").toString().isEmpty() && row->property("status") == "Stale",
	       handedOff ? "handed-watch" : "queued-watch", "cached QML role retirement");
	const auto notifications = changed.count() + resets.count();
	CHECK(notifications > 0);
	for (unsigned i = 0; i < 4; ++i)
		controller.pump();
	QTest::qWait(30);
	CHECK(!controller.watchPending() && controller.inspector().pending() == 0);
	CHECK(changed.count() + resets.count() == notifications);
	CHECK(fixture.peer.requests == requests + unsigned(handedOff));
	const auto terminal = controller.watchStatus();
	CHECK(terminal.contains(QStringLiteral("drained")) ||
	      (terminal.startsWith(QStringLiteral("Watch read: ")) &&
	       !terminal.endsWith(QStringLiteral("Ok"))));
	CHECK(!terminal.contains(QStringLiteral("pending"), Qt::CaseInsensitive) &&
	      !terminal.contains(QStringLiteral("reading"), Qt::CaseInsensitive) &&
	      !terminal.contains(QStringLiteral("complete"), Qt::CaseInsensitive));
	CHECK(!fixture.warnings);
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app{argc, argv};
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	for (const auto& [action, name] :
	     {std::pair{Action::Read, "FieldRead"}, std::pair{Action::Command, "Command"},
	      std::pair{Action::Service, "Service"}, std::pair{Action::Apply, "ApplyWrite"},
	      std::pair{Action::Readback, "ApplyReadback"}})
		foregroundFailure(action, name);
	acceptedWatchDrain(false);
	acceptedWatchDrain(true);
	std::printf("Inspector Watchlist Failures checks=%u paths=5 qml=1 drain=1 failures=%u Qt=%s\n",
	            checks, failures, qVersion());
	return failures ? 1 : 0;
}
