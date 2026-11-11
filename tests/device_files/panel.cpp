// DeviceFilesPanel with live models over a real session, off screen: the
// states, the tree, the download button following the marks, a transfer row
// with its progress and its result, and not one QML warning. MIT.
#include <Telemetry/UX/DeviceFileTransfers.hpp>
#include <Telemetry/UX/DeviceFilesModel.hpp>
#include <integration/tcp/Server.hpp>
#include <resource/BytesFile.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
namespace qt = telemetry::integration::qt;
namespace tcp = telemetry::integration::tcp;
using telemetry::ux::DeviceFilesModel;
using telemetry::ux::DeviceFileTransfers;

namespace {
unsigned checks = 0;
QStringList warnings;

void check(bool ok, int line)
{
	++checks;
	if (!ok) {
		// A GUI process may have no console here; leave the reason in a file too.
		if (auto* log = std::fopen("panel-failure.txt", "w")) {
			std::fprintf(log, "check %u failed at line %d\n", checks, line);
			for (const auto& warning : warnings)
				std::fprintf(log, "  %s\n", qPrintable(warning));
			std::fclose(log);
		}
		std::fprintf(stderr, "Device files panel check %u failed at line %d\n", checks, line);
		for (const auto& warning : warnings)
			std::fprintf(stderr, "  %s\n", qPrintable(warning));
		std::exit(1);
	}
}

#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)

bool await(const std::function<bool()>& done, int milliseconds = 15000)
{
	QElapsedTimer deadline;
	deadline.start();
	while (!done() && deadline.elapsed() < milliseconds) {
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	return done();
}

struct Device {
	std::uint32_t read() noexcept
	{
		return 42;
	}
} device;

constexpr telemetry::FieldTable localFields{telemetry::field<&Device::read>("Value", device)};
constexpr telemetry::FieldCatalogTable fields{telemetry::group("test", localFields)};
constexpr telemetry::CommandCatalogTable commands{};
constexpr telemetry::ServiceCatalogTable services{};
constexpr telemetry::Model model{fields, commands, services};
constexpr telemetry::descriptor::Descriptor descriptor{model};
constexpr auto packed = telemetry::descriptor::packDescriptor<descriptor>();

std::array<std::byte, 300> eventsBytes{};
std::array<std::byte, 200000> largeBytes{};
const resource::BytesFile descriptorFile{packed};
const resource::BytesFile eventsFile{eventsBytes};
const resource::BytesFile largeFile{largeBytes};
const auto table = resource::filesystem(resource::file("/telemetry/descriptor.bin", descriptorFile),
                                        resource::file("/logs/events.csv", eventsFile),
                                        resource::file("/logs/large.bin", largeFile));

class Peer {
public:
	QTcpServer listener;
	QPointer<QTcpSocket> socket;
	std::unique_ptr<tcp::Server> server;
	alignas(32) std::array<std::byte, model.maxScratch()> scratch{};
	telemetry::Workspace workspace{scratch};
	std::array<std::byte, 4096> rx{}, tx{};
	QByteArray pending;

	Peer()
	{
		CHECK(listener.listen(QHostAddress::LocalHost, 0));
		QObject::connect(&listener, &QTcpServer::newConnection, &listener, [this] {
			auto* candidate = listener.nextPendingConnection();
			socket = candidate;
			pending.clear();
			server = std::make_unique<tcp::Server>(model.view(), workspace, table.view(), rx, tx);
			QObject::connect(candidate, &QTcpSocket::readyRead, &listener, [this, candidate] {
				if (socket == candidate)
					read();
			});
		});
	}

	~Peer()
	{
		if (socket) {
			QObject::disconnect(socket, nullptr, &listener, nullptr);
			socket->abort();
		}
	}

	void read()
	{
		pending += socket->readAll();
		while (!pending.isEmpty()) {
			const auto result = server->receive({reinterpret_cast<const std::byte*>(pending.constData()),
			                                     static_cast<std::size_t>(pending.size())});
			pending.remove(0, static_cast<qsizetype>(result.consumed));
			if (result.status == tcp::Status::NeedMore)
				break;
			const auto bytes = server->reply();
			socket->write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
			server->releaseReply();
		}
	}
};

QObject* child(QObject* root, const char* name)
{
	return root->findChild<QObject*>(QString::fromLatin1(name));
}

// View delegates have a visual parent only, so findChild misses them.
QQuickItem* visualChild(QQuickItem* from, const QString& name)
{
	if (!from)
		return nullptr;
	if (from->objectName() == name)
		return from;
	for (auto* item : from->childItems())
		if (auto* found = visualChild(item, name))
			return found;
	return nullptr;
}

QQuickItem* delegate(QObject* root, const char* name)
{
	auto* window = qobject_cast<QQuickWindow*>(root);
	return visualChild(window ? window->contentItem() : qobject_cast<QQuickItem*>(root),
	                   QString::fromLatin1(name));
}
} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
		// The offscreen platform has no fonts of its own; that is the machine, not the panel.
		if (type != QtDebugMsg && type != QtInfoMsg && !message.startsWith(QStringLiteral("QFontDatabase:")))
			warnings << message;
	});
	for (std::size_t i = 0; i < largeBytes.size(); ++i)
		largeBytes[i] = std::byte(i * 3);
	QTemporaryDir folder;
	CHECK(folder.isValid());

	Peer peer;
	qt::ConnectionSession session;
	DeviceFilesModel files(&session);
	DeviceFileTransfers transfers(&session);
	QQmlApplicationEngine engine;
	engine.setInitialProperties({{QStringLiteral("filesModel"), QVariant::fromValue<QObject*>(&files)},
	                             {QStringLiteral("transfersModel"), QVariant::fromValue<QObject*>(&transfers)}});
	engine.load(QUrl(QStringLiteral("qrc:/Panel.qml")));
	CHECK(engine.rootObjects().size() == 1);
	auto* root = engine.rootObjects().first();

	// Not connected: the words, no tree, no actions.
	CHECK(child(root, "deviceFilesNotConnected")->property("visible").toBool());
	CHECK(!child(root, "deviceFilesTree")->property("visible").toBool());

	CHECK(session.connectTcpReference(QStringLiteral("127.0.0.1"), peer.listener.serverPort()));
	CHECK(await([&] {
		return files.available() && !files.loading() && files.fileCount() == 3;
	}));
	CHECK(await([&] {
		return child(root, "deviceFilesTree")->property("visible").toBool();
	}));
	CHECK(!child(root, "deviceFilesNotConnected")->property("visible").toBool());
	auto* download = child(root, "deviceFilesDownload");
	CHECK(download->property("text").toString() == QStringLiteral("Download…") &&
	      !download->property("enabled").toBool());

	// Marks drive the button and the summary line.
	const auto logs = files.index(1, 0);
	files.setMarked(logs, true);
	CHECK(await([&] {
		return download->property("text").toString() == QStringLiteral("Download 2 files…");
	}));
	CHECK(download->property("enabled").toBool());
	CHECK(child(root, "deviceFilesMarked")->property("text").toString().startsWith(QStringLiteral("2 marked")));
	CHECK(QMetaObject::invokeMethod(download, "clicked"));
	CHECK(await([&] {
		return root->property("lastSelection").toList().size() == 2;
	}));
	files.markAll(false);
	CHECK(await([&] {
		return !download->property("enabled").toBool();
	}));

	// A transfer row appears with Cancel while it runs, then shows its result.
	const auto large = files.indexOfPath(QStringLiteral("/logs/large.bin"));
	const auto row = transfers.download(QStringLiteral("/logs/large.bin"),
	                                    files.data(large, DeviceFilesModel::FileIndexRole).toInt(),
	                                    largeBytes.size(), folder.filePath(QStringLiteral("large.bin")));
	CHECK(row == 0);
	CHECK(await([&] {
		return child(root, "deviceFilesTransfers")->property("visible").toBool();
	}));
	CHECK(await([&] {
		return !transfers.data(transfers.index(row), DeviceFileTransfers::ActiveRole).toBool();
	}));
	QString shown;
	const bool saved = await([&] {
		auto* figures = delegate(root, "deviceFileTransferFigures");
		shown = figures ? figures->property("text").toString() : QStringLiteral("(no row)");
		return shown == QStringLiteral("Saved");
	});
	if (!saved)
		warnings << QStringLiteral("transfer row shows: ") + shown + QStringLiteral(" / model result: ") +
		                transfers.data(transfers.index(row), DeviceFileTransfers::ResultRole).toString();
	CHECK(saved);

	// Disconnected: back to the words.
	session.disconnect();
	CHECK(await([&] {
		return child(root, "deviceFilesNotConnected")->property("visible").toBool();
	}));
	CHECK(warnings.isEmpty());
	std::printf("Device files panel: %u checks, warnings=0\n", checks);
	return 0;
}
