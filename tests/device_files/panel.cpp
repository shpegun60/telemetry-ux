// DeviceFilesPanel with live models over a real session, off screen: the
// states, the tree, the download button following the marks, a transfer row
// with its progress and its result, and not one QML warning. MIT.
#include <Telemetry/UX/DeviceFileTransfers.hpp>
#include <Telemetry/UX/DeviceFilesModel.hpp>
#include <Telemetry/UX/FileContent.hpp>
#include <Telemetry/UX/HexView.hpp>
#include <Telemetry/UX/QmlTypes.hpp>
#include <integration/tcp/Server.hpp>
#include <resource/BytesFile.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <QClipboard>
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

void press(QObject* target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	QKeyEvent down(QEvent::KeyPress, key, modifiers);
	QKeyEvent up(QEvent::KeyRelease, key, modifiers);
	QCoreApplication::sendEvent(target, &down);
	QCoreApplication::sendEvent(target, &up);
}

// The hex grid on its own: its layout by width, the cursor and the selection
// by key, Copy as in every form, Go to and Select block with their refusals,
// the inspector in both byte orders, a codec's region, the copy limit, Tab
// between the panes and the menu from the keyboard.
void hexView()
{
	using telemetry::ux::HexView;
	QByteArray bytes;
	for (int i = 0; i < 300; ++i)
		bytes.append(static_cast<char>(i));
	HexView view;
	view.setSize(QSizeF(3000, 600));
	view.setBytes(bytes);
	CHECK(view.bytesPerRow() == 32 && view.textColumn() && view.rowCount() == 10 && view.size() == 300);
	view.setSize(QSizeF(160, 600));
	CHECK(view.bytesPerRow() == 8 && !view.textColumn());
	view.setSize(QSizeF(3000, 600));
	CHECK(view.bytesPerRow() == 32);

	// Find, in every kind: the match becomes the selection; next and previous
	// go round once, and say so; the count and the match's place in it.
	view.moveTo(0);
	CHECK(view.find(QStringLiteral("0A 0B"), QStringLiteral("hex")).isEmpty());
	CHECK(view.selectionStart() == 10 && view.selectionLength() == 2 && view.cursor() == 10);
	CHECK(view.matchCount() == 2 && view.matchIndex() == 1 && !view.counting());
	CHECK(view.find(QStringLiteral("0x0A,0x0B"), QStringLiteral("hex")).isEmpty() && view.selectionStart() == 266 &&
	      view.matchIndex() == 2);
	CHECK(view.find(QStringLiteral("0a0b"), QStringLiteral("hex")) == QStringLiteral("Wrapped to the start") &&
	      view.selectionStart() == 10);
	CHECK(view.find(QStringLiteral("0A 0B"), QStringLiteral("hex"), 4, false) == QStringLiteral("Wrapped to the end") &&
	      view.selectionStart() == 266);
	CHECK(view.find(QStringLiteral("ABC"), QStringLiteral("hex")).startsWith(QStringLiteral("Hex is pairs")));
	CHECK(view.find(QStringLiteral("FF FF"), QStringLiteral("hex")) == QStringLiteral("Not found"));
	view.moveTo(0);
	CHECK(view.find(QStringLiteral("ABC"), QStringLiteral("text")).isEmpty() && view.selectionStart() == 65 &&
	      view.selectionLength() == 3);
	CHECK(view.find(QStringLiteral("70000"), QStringLiteral("integer"), 2).contains(QStringLiteral("does not fit")));
	CHECK(view.find(QStringLiteral("x"), QStringLiteral("integer"), 2).startsWith(QStringLiteral("Not an integer")));
	view.clearFind();
	CHECK(view.matchCount() == 0 && view.matchIndex() == 0);
	{
		// Numbers in the inspector's byte order: 1000 is E8 03 little-endian,
		// 03 E8 big-endian; 1.5f is 00 00 C0 3F.
		QByteArray numbers(32, '\0');
		numbers[8] = static_cast<char>(0xe8);
		numbers[9] = 0x03;
		numbers[20] = static_cast<char>(0xc0);
		numbers[21] = 0x3f;
		HexView probe;
		probe.setSize(QSizeF(3000, 600));
		probe.setBytes(numbers);
		CHECK(probe.find(QStringLiteral("1000"), QStringLiteral("integer"), 2).isEmpty() && probe.selectionStart() == 8);
		// The only match again: next goes round to it, and says so.
		CHECK(probe.find(QStringLiteral("0x3E8"), QStringLiteral("integer"), 2) == QStringLiteral("Wrapped to the start") &&
		      probe.selectionStart() == 8);
		CHECK(probe.find(QStringLiteral("1.5"), QStringLiteral("float"), 4).isEmpty() && probe.selectionStart() == 18 &&
		      probe.selectionLength() == 4);
		probe.setBigEndian(true);
		CHECK(probe.find(QStringLiteral("1000"), QStringLiteral("integer"), 2) == QStringLiteral("Not found"));
		CHECK(probe.pattern(QStringLiteral("1000"), QStringLiteral("integer"), 4, nullptr) == QByteArray::fromHex("000003e8"));
		CHECK(probe.pattern(QStringLiteral("-1"), QStringLiteral("integer"), 1, nullptr) == QByteArray::fromHex("ff"));
	}

	// Keys move the cursor; Shift extends the selection.
	view.moveTo(0);
	press(&view, Qt::Key_Right);
	CHECK(view.cursor() == 1 && view.selectionLength() == 0);
	for (int i = 0; i < 3; ++i)
		press(&view, Qt::Key_Right, Qt::ShiftModifier);
	CHECK(view.selectionStart() == 1 && view.selectionLength() == 4 && view.cursor() == 4);
	CHECK(view.status() == QStringLiteral("0x0004 (4) · 4 bytes, 0x0001–0x0004"));
	CHECK(view.accessibleText().contains(QStringLiteral("4 bytes selected, 1 to 4.")));
	CHECK(view.textAs(QStringLiteral("hex")) == QStringLiteral("01 02 03 04"));
	CHECK(view.textAs(QStringLiteral("hexCompact")) == QStringLiteral("01020304"));
	CHECK(view.textAs(QStringLiteral("base64")) == QStringLiteral("AQIDBA=="));
	CHECK(view.textAs(QStringLiteral("offset")) == QStringLiteral("0x0001"));
	CHECK(view.textAs(QStringLiteral("offsetLength")) == QStringLiteral("0x0001, 4"));
	CHECK(view.textAs(QStringLiteral("c")) == QStringLiteral("const uint8_t data[4] = {\n    0x01, 0x02, 0x03, 0x04\n};"));
	press(&view, Qt::Key_Escape);
	CHECK(view.selectionLength() == 0 && view.cursor() == 4);
	press(&view, Qt::Key_Down);
	CHECK(view.cursor() == 36);
	press(&view, Qt::Key_Home);
	CHECK(view.cursor() == 32);
	press(&view, Qt::Key_End, Qt::ControlModifier);
	CHECK(view.cursor() == 299);
	press(&view, Qt::Key_A, Qt::ControlModifier);
	CHECK(view.selectionStart() == 0 && view.selectionLength() == 300);

	// Go to, in every form it takes, and its refusals.
	CHECK(view.goTo(QStringLiteral("0x20")).isEmpty() && view.cursor() == 32 && view.selectionLength() == 0);
	CHECK(view.goTo(QStringLiteral("40h")).isEmpty() && view.cursor() == 64);
	CHECK(view.goTo(QStringLiteral("100")).isEmpty() && view.cursor() == 100);
	CHECK(view.goTo(QStringLiteral("+0x10")).isEmpty() && view.cursor() == 116);
	CHECK(view.goTo(QStringLiteral("-16")).isEmpty() && view.cursor() == 100);
	CHECK(view.goTo(QStringLiteral("end-16")).isEmpty() && view.cursor() == 284);
	CHECK(view.goTo(QStringLiteral("end")).isEmpty() && view.cursor() == 299);
	CHECK(view.goTo(QStringLiteral("zz")).startsWith(QStringLiteral("Not an offset")) && view.cursor() == 299);
	CHECK(view.goTo(QStringLiteral("0x999")) == QStringLiteral("Not in this file: 0 to 0x12B."));
	CHECK(!view.goTo(QString()).isEmpty());

	// Select a block by length or by its end.
	CHECK(view.selectBlock(QStringLiteral("0x10"), QStringLiteral("8"), false).isEmpty() &&
	      view.selectionStart() == 16 && view.selectionLength() == 8);
	CHECK(view.selectBlock(QStringLiteral("0x10"), QStringLiteral("0x13"), true).isEmpty() &&
	      view.selectionStart() == 16 && view.selectionLength() == 4);
	CHECK(!view.selectBlock(QStringLiteral("0x10"), QStringLiteral("0x8"), true).isEmpty());
	CHECK(!view.selectBlock(QStringLiteral("0x120"), QStringLiteral("100"), false).isEmpty());

	// The inspector at E8 03 00 00: 1000 little-endian, its bytes the other way big-endian.
	QByteArray value(16, '\0');
	value[4] = static_cast<char>(0xe8);
	value[5] = 0x03;
	view.setBytes(value);
	view.moveTo(4);
	const auto read = [&](const QString& type) {
		for (const auto& row : view.inspector())
			if (row.toMap().value(QStringLiteral("type")).toString() == type)
				return row.toMap().value(QStringLiteral("value")).toString();
		return QStringLiteral("(none)");
	};
	CHECK(read(QStringLiteral("int16")) == QStringLiteral("1000") && read(QStringLiteral("uint32")) == QStringLiteral("1000"));
	CHECK(read(QStringLiteral("int8")) == QStringLiteral("-24") && read(QStringLiteral("uint8")) == QStringLiteral("232"));
	CHECK(read(QStringLiteral("bits")) == QStringLiteral("1110 1000"));
	CHECK(read(QStringLiteral("time_t32")) == QStringLiteral("1970-01-01 00:16:40 UTC"));
	view.setBigEndian(true);
	CHECK(read(QStringLiteral("uint16")) == QStringLiteral("59395") && read(QStringLiteral("int16")) == QStringLiteral("-6141"));
	view.moveTo(12);
	CHECK(read(QStringLiteral("uint64")) == QStringLiteral("—") && read(QStringLiteral("uint32")) == QStringLiteral("0"));
	view.setBigEndian(false);

	// A codec's region names the bytes at the cursor.
	view.setRegions({QVariantMap{{QStringLiteral("offset"), 0}, {QStringLiteral("length"), 8},
	                             {QStringLiteral("name"), QStringLiteral("magic")},
	                             {QStringLiteral("path"), QStringLiteral("header › magic")},
	                             {QStringLiteral("kind"), QStringLiteral("field")}}});
	view.moveTo(3);
	CHECK(view.status().contains(QStringLiteral("In: header › magic · 8 bytes at 0x0000")));
	CHECK(view.accessibleText().contains(QStringLiteral("In header › magic")));
	view.moveTo(9);
	CHECK(!view.status().contains(QStringLiteral("In:")));

	// Copying more than 1 MiB is refused with a reason; less is copied.
	view.setBytes(QByteArray(HexView::maxCopyBytes + 1, 'x'));
	view.selectAll();
	CHECK(!view.copy(QStringLiteral("hex")) && view.notice().startsWith(QStringLiteral("Copy is limited to 1 MiB")));
	view.selectRange(0, 3);
	CHECK(view.copy(QStringLiteral("text")) && view.notice().isEmpty());
	CHECK(QGuiApplication::clipboard()->text() == QStringLiteral("xxx"));

	// Tab: hex, then text, then out of the grid.
	view.moveTo(0);
	CHECK(!view.textPane());
	QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
	QCoreApplication::sendEvent(&view, &tab);
	CHECK(view.textPane() && tab.isAccepted());
	QKeyEvent again(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
	QCoreApplication::sendEvent(&view, &again);
	CHECK(view.textPane() && !again.isAccepted());
	press(&view, Qt::Key_Backtab);
	CHECK(!view.textPane());

	// The Menu key and Shift+F10 ask for the context menu.
	int menus = 0;
	QObject::connect(&view, &HexView::contextMenuRequested, &view, [&](qreal, qreal) {
		++menus;
	});
	press(&view, Qt::Key_Menu);
	press(&view, Qt::Key_F10, Qt::ShiftModifier);
	CHECK(menus == 2);

	// Offsets in decimal: wider, and the status keeps the hex.
	view.setBytes(bytes);
	view.setDecimalOffsets(true);
	CHECK(view.decimalOffsets() && view.textAs(QStringLiteral("offset")) == QStringLiteral("0x0000"));

	// An empty file: nothing to move to, the status says so.
	view.setBytes({});
	press(&view, Qt::Key_Right);
	CHECK(view.cursor() == 0 && view.rowCount() == 0 && view.status() == QStringLiteral("This file is empty."));
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
	telemetry::ux::registerQmlTypes();
	hexView();

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

	// Actions are in a context menu, as in a file explorer: no Open button.
	// The menu on a file: Open in bold first, Open with, Download, Copy path,
	// Properties; Copy path reaches the clipboard; Properties shows the path,
	// the exact size and the access; Open with hands the host the view.
	CHECK(!child(root, "deviceFilesOpen"));
	auto* pane = child(root, "panel");
	auto* treeItem = qobject_cast<QQuickItem*>(child(root, "deviceFilesTree"));
	const auto eventsIndex = files.indexOfPath(QStringLiteral("/logs/events.csv"));
	CHECK(QMetaObject::invokeMethod(pane, "openMenu", Q_ARG(QVariant, QVariant::fromValue(eventsIndex)),
	                                Q_ARG(QVariant, QVariant::fromValue<QObject*>(treeItem)), Q_ARG(QVariant, 10),
	                                Q_ARG(QVariant, 10)));
	auto* fileMenu = child(root, "deviceFileMenu");
	CHECK(await([&] {
		return fileMenu->property("visible").toBool();
	}));
	CHECK(fileMenu->property("kind").toString() == QStringLiteral("file"));
	auto* openItem = child(root, "deviceFileMenuOpen");
	CHECK(openItem->property("text").toString().startsWith(QStringLiteral("&Open\t")) &&
	      openItem->property("enabled").toBool());
	CHECK(child(root, "deviceFileMenuDownload")->property("enabled").toBool());
	CHECK(QMetaObject::invokeMethod(child(root, "deviceFileMenuCopyPath"), "triggered"));
	CHECK(QGuiApplication::clipboard()->text() == QStringLiteral("/logs/events.csv"));
	CHECK(QMetaObject::invokeMethod(child(root, "deviceFileMenuOpenHex"), "triggered"));
	CHECK(root->property("lastOpenWith").toMap().value(QStringLiteral("view")).toString() == QStringLiteral("hex") &&
	      root->property("lastOpenWith").toMap().value(QStringLiteral("path")).toString() == QStringLiteral("/logs/events.csv"));
	fileMenu->setProperty("visible", false);
	CHECK(QMetaObject::invokeMethod(pane, "showProperties", Q_ARG(QVariant, QVariant::fromValue(eventsIndex))));
	CHECK(await([&] {
		auto* properties = child(root, "deviceFileProperties");
		return properties && properties->property("visible").toBool();
	}));
	CHECK(child(root, "deviceFilePropertiesPath")->property("text").toString() == QStringLiteral("/logs/events.csv"));
	CHECK(child(root, "deviceFilePropertiesSize")->property("text").toString() ==
	      QStringLiteral("%1 bytes").arg(eventsBytes.size()));
	CHECK(child(root, "deviceFilePropertiesAccess")->property("text").toString() == QStringLiteral("read"));
	child(root, "deviceFileProperties")->setProperty("visible", false);
	// A folder's menu, and the menu for empty space.
	const auto logsFolder = files.index(1, 0);
	CHECK(QMetaObject::invokeMethod(pane, "openMenu", Q_ARG(QVariant, QVariant::fromValue(logsFolder)),
	                                Q_ARG(QVariant, QVariant::fromValue<QObject*>(treeItem)), Q_ARG(QVariant, 10),
	                                Q_ARG(QVariant, 10)));
	CHECK(fileMenu->property("kind").toString() == QStringLiteral("folder"));
	CHECK(child(root, "deviceFileMenuDownloadFolder")->property("enabled").toBool());
	fileMenu->setProperty("visible", false);
	CHECK(QMetaObject::invokeMethod(pane, "openMenu", Q_ARG(QVariant, QVariant()),
	                                Q_ARG(QVariant, QVariant::fromValue<QObject*>(treeItem)), Q_ARG(QVariant, 10),
	                                Q_ARG(QVariant, 10)));
	CHECK(fileMenu->property("kind").toString() == QStringLiteral("empty") &&
	      child(root, "deviceFileMenuRefresh")->property("enabled").toBool());
	fileMenu->setProperty("visible", false);
	// Keys on the tree: Ctrl+A marks everything readable, Ctrl+Shift+A clears.
	treeItem->forceActiveFocus();
	press(treeItem, Qt::Key_A, Qt::ControlModifier);
	CHECK(files.markedCount() == files.fileCount());
	press(treeItem, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
	CHECK(files.markedCount() == 0);

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
		return hex && hex->property("size").toLongLong() == static_cast<qint64>(largeBytes.size()) &&
		       hex->property("rowCount").toLongLong() ==
		           (static_cast<qint64>(largeBytes.size()) + hex->property("bytesPerRow").toInt() - 1) /
		               hex->property("bytesPerRow").toInt();
	}));
	CHECK(!child(root, "hexStatus")->property("text").toString().isEmpty());
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
	// The file's actions are in its ⋯ menu: Save as hands the host the local
	// copy, Copy path the device's path.
	CHECK(child(root, "fileViewerMore") && !child(root, "fileViewerMore")->property("text").toString().isEmpty());
	CHECK(QMetaObject::invokeMethod(viewer, "openMenu"));
	CHECK(await([&] {
		return child(root, "fileViewerMenu")->property("visible").toBool();
	}));
	CHECK(QMetaObject::invokeMethod(child(root, "fileViewerSaveAs"), "triggered"));
	CHECK(root->property("lastSaveAs").toString() == csvPath);
	CHECK(QMetaObject::invokeMethod(child(root, "fileViewerCopyPath"), "triggered"));
	CHECK(QGuiApplication::clipboard()->text() == QStringLiteral("/config/viewer.csv"));
	child(root, "fileViewerMenu")->setProperty("visible", false);

	// Disconnected: back to the words.
	session.disconnect();
	CHECK(await([&] {
		return child(root, "deviceFilesNotConnected")->property("visible").toBool();
	}));
	CHECK(warnings.isEmpty());
	std::printf("Device files panel: %u checks, warnings=0\n", checks);
	return 0;
}
