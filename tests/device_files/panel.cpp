// DeviceFilesPanel with live models over a real session, off screen: the
// states, the tree, the download button following the marks, a transfer row
// with its progress and its result, and not one QML warning. MIT.
#include <Telemetry/UX/DeviceFileTransfers.hpp>
#include <Telemetry/UX/DeviceFilesModel.hpp>
#include <Telemetry/UX/FileContent.hpp>
#include <integration/tcp/Server.hpp>
#include <resource/BytesFile.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QKeyEvent>
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

// The panel check measures no time, so it keeps the plain loop: waiting on
// events here makes Qt Quick's animation driver report "stopAnimationDriver:
// driver is not running" off screen. The model check waits on events instead.
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

void visualChildren(QQuickItem* from, const QString& name, QList<QQuickItem*>& into)
{
	if (!from)
		return;
	if (from->objectName() == name)
		into << from;
	for (auto* item : from->childItems())
		visualChildren(item, name, into);
}

// The delegate named name whose property equals value: one transfer row, one
// tree row, whatever the order the view made them in.
QQuickItem* delegateWhere(QObject* root, const char* name, const char* property, const QVariant& value)
{
	auto* window = qobject_cast<QQuickWindow*>(root);
	QList<QQuickItem*> found;
	visualChildren(window ? window->contentItem() : qobject_cast<QQuickItem*>(root), QString::fromLatin1(name),
	               found);
	for (auto* item : found)
		if (item->isVisible() && item->property(property) == value)
			return item;
	return nullptr;
}

void press(QObject* target, int key)
{
	QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier);
	QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier);
	QCoreApplication::sendEvent(target, &down);
	QCoreApplication::sendEvent(target, &up);
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
	telemetry::ux::FileContent viewerContent;
	QQmlApplicationEngine engine;
	engine.setInitialProperties({{QStringLiteral("filesModel"), QVariant::fromValue<QObject*>(&files)},
	                             {QStringLiteral("transfersModel"), QVariant::fromValue<QObject*>(&transfers)},
	                             {QStringLiteral("viewerContent"), QVariant::fromValue<QObject*>(&viewerContent)}});
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

	// The columns have titles, and the device has more than system files.
	CHECK(child(root, "deviceFilesHeading")->property("visible").toBool());
	CHECK(!child(root, "deviceFilesSystemOnly")->property("visible").toBool());

	// A saved row offers its folder: the host gets the local file.
	auto* savedRow = delegateWhere(root, "deviceFileTransfer", "index", row);
	CHECK(savedRow);
	auto* show = visualChild(savedRow, QStringLiteral("deviceFileTransferShow"));
	CHECK(show && show->isVisible());
	CHECK(QMetaObject::invokeMethod(show, "clicked"));
	CHECK(await([&] {
		return root->property("lastShown").toString() ==
		       QDir::toNativeSeparators(folder.filePath(QStringLiteral("large.bin")));
	}));

	// A download that cannot be written fails in the error colour with Retry;
	// once the obstacle is gone, Retry saves it in the same row.
	const auto blocker = folder.filePath(QStringLiteral("blocked"));
	{
		QFile file(blocker);
		CHECK(file.open(QIODevice::WriteOnly));
	}
	const auto events = files.indexOfPath(QStringLiteral("/logs/events.csv"));
	const auto failing = transfers.download(QStringLiteral("/logs/events.csv"),
	                                        files.data(events, DeviceFilesModel::FileIndexRole).toInt(),
	                                        eventsBytes.size(), blocker + QStringLiteral("/events.csv"));
	CHECK(failing == 1);
	QQuickItem* failedRow = nullptr;
	CHECK(await([&] {
		failedRow = delegateWhere(root, "deviceFileTransfer", "index", failing);
		auto* retry = failedRow ? visualChild(failedRow, QStringLiteral("deviceFileTransferRetry")) : nullptr;
		return retry && retry->isVisible();
	}));
	auto* failedText = visualChild(failedRow, QStringLiteral("deviceFileTransferFigures"));
	CHECK(failedText->property("text").toString().startsWith(QStringLiteral("! Could not write")));
	CHECK(QFile::remove(blocker));
	CHECK(QMetaObject::invokeMethod(visualChild(failedRow, QStringLiteral("deviceFileTransferRetry")), "clicked"));
	CHECK(await([&] {
		return transfers.data(transfers.index(failing), DeviceFileTransfers::StateRole).toInt() ==
		       static_cast<int>(DeviceFileTransfers::State::Saved);
	}));
	CHECK(QFile(blocker + QStringLiteral("/events.csv")).size() == static_cast<qint64>(eventsBytes.size()));

	// Clear finished removes both ended rows.
	auto* clear = delegate(root, "deviceFilesClearFinished");
	CHECK(clear && clear->isVisible());
	CHECK(QMetaObject::invokeMethod(clear, "clicked"));
	CHECK(await([&] {
		return transfers.rowCount() == 0 && !child(root, "deviceFilesTransfers")->property("visible").toBool();
	}));

	// The current row is highlighted and framed while the tree has the
	// keyboard; Enter on a folder folds and unfolds it.
	auto* tree = qobject_cast<QQuickItem*>(child(root, "deviceFilesTree"));
	auto* selection = qvariant_cast<QItemSelectionModel*>(tree->property("selectionModel"));
	CHECK(selection);
	const auto logs2 = files.index(1, 0);
	selection->setCurrentIndex(logs2, QItemSelectionModel::NoUpdate);
	tree->forceActiveFocus();
	QQuickItem* currentRow = nullptr;
	CHECK(await([&] {
		currentRow = delegateWhere(root, "deviceFileRow", "current", true);
		return currentRow && currentRow->property("highlighted").toBool();
	}));
	CHECK(currentRow->property("name").toString() == QStringLiteral("logs"));
	if (tree->hasActiveFocus())
		CHECK(visualChild(currentRow, QStringLiteral("deviceFileFocusFrame"))->isVisible());
	const int logsRow = tree->property("currentRow").toInt();
	bool expanded = false;
	CHECK(QMetaObject::invokeMethod(tree, "isExpanded", Q_RETURN_ARG(bool, expanded), Q_ARG(int, logsRow)));
	CHECK(expanded);
	press(tree, Qt::Key_Return);
	CHECK(await([&] {
		bool now = true;
		QMetaObject::invokeMethod(tree, "isExpanded", Q_RETURN_ARG(bool, now), Q_ARG(int, logsRow));
		return !now;
	}));
	press(tree, Qt::Key_Return);
	CHECK(await([&] {
		bool now = false;
		QMetaObject::invokeMethod(tree, "isExpanded", Q_RETURN_ARG(bool, now), Q_ARG(int, logsRow));
		return now;
	}));

	// The viewer: a binary download opens as hex, a CSV as a table with its
	// columns, and the switch moves to text; not one QML warning on the way.
	auto* viewer = child(root, "viewer");
	CHECK(viewer && viewer->property("shownMode").toString().isEmpty());
	viewerContent.setLocalFile(folder.filePath(QStringLiteral("large.bin")));
	CHECK(viewer->property("shownMode").toString() == QStringLiteral("hex"));
	CHECK(await([&] {
		auto* hex = child(root, "fileViewerHex");
		return hex && hex->property("count").toInt() == static_cast<int>((largeBytes.size() + 15) / 16);
	}));
	const auto csvPath = folder.filePath(QStringLiteral("viewer.csv"));
	{
		QFile file(csvPath);
		CHECK(file.open(QIODevice::WriteOnly) && file.write("name,value\nGain,2.5\nPeriod,5\n") > 0);
	}
	viewerContent.setDevicePath(QStringLiteral("/config/viewer.csv"));
	viewerContent.setLocalFile(csvPath);
	CHECK(viewer->property("shownMode").toString() == QStringLiteral("table"));
	CHECK(await([&] {
		auto* table = child(root, "fileViewerTable");
		return table && table->property("rows").toInt() == 2 && table->property("columns").toInt() == 2;
	}));
	CHECK(child(root, "fileViewerFooter")->property("text").toString().endsWith(QStringLiteral("/config/viewer.csv")));
	CHECK(QMetaObject::invokeMethod(delegate(root, "fileViewerMode_text"), "clicked"));
	CHECK(viewer->property("shownMode").toString() == QStringLiteral("text"));
	CHECK(child(root, "fileViewerText")->property("text").toString().startsWith(QStringLiteral("name,value")));

	// Disconnected: back to the words.
	session.disconnect();
	CHECK(await([&] {
		return child(root, "deviceFilesNotConnected")->property("visible").toBool();
	}));
	CHECK(warnings.isEmpty());
	std::printf("Device files panel: %u checks, warnings=0\n", checks);
	return 0;
}
