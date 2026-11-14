// DeviceFilesPanel with live models over a real session, off screen: the
// states, the tree, the download button following the marks, a transfer row
// with its progress and its result, and not one QML warning. MIT.
#include <Telemetry/UX/DeviceFileTransfers.hpp>
#include <Telemetry/UX/DeviceFilesModel.hpp>
#include <Telemetry/UX/FileContent.hpp>
#include <Telemetry/UX/HexView.hpp>
#include <Telemetry/UX/QmlTypes.hpp>
#include <Telemetry/UX/ToolIcon.hpp>
#include <integration/tcp/Server.hpp>
#include <resource/BytesFile.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QPainter>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
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
		for (const bool bigEndian : {false, true}) {
			probe.setBigEndian(bigEndian);
			for (const int width : {1, 2, 4}) {
				const auto limit = quint64{1} << (8 * width);
				QString error;
				CHECK(probe.pattern(QString::number(limit - 1), QStringLiteral("integer"), width, &error) ==
				          QByteArray(width, static_cast<char>(0xff)) && error.isEmpty());
				CHECK(probe.pattern(QString::number(-static_cast<qint64>(limit / 2)), QStringLiteral("integer"),
				                    width, &error).size() == width && error.isEmpty());
				for (const auto& overflow : {QStringLiteral("18446744073709551615"),
				                             QStringLiteral("0xFFFFFFFFFFFFFFFF"), QString::number(limit),
				                             QString::number(-static_cast<qint64>(limit / 2) - 1)}) {
					error.clear();
					CHECK(probe.pattern(overflow, QStringLiteral("integer"), width, &error).isEmpty() &&
					      error.contains(QStringLiteral("does not fit")));
					CHECK(probe.patternBytes(overflow, QStringLiteral("integer"), width) == -1);
					const auto selected = probe.selectionStart();
					const auto length = probe.selectionLength();
					CHECK(probe.find(overflow, QStringLiteral("integer"), width).contains(QStringLiteral("does not fit")) &&
					      probe.selectionStart() == selected && probe.selectionLength() == length);
				}
			}
			QString error;
			CHECK(probe.pattern(QStringLiteral("18446744073709551615"), QStringLiteral("integer"), 8, &error) ==
			          QByteArray(8, static_cast<char>(0xff)) && error.isEmpty());
			CHECK(probe.pattern(QStringLiteral("-9223372036854775808"), QStringLiteral("integer"), 8, &error) ==
			          QByteArray::fromHex(bigEndian ? "8000000000000000" : "0000000000000080") && error.isEmpty());
		}
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
	// Valid qint64 inputs can still overflow once made relative to the cursor
	// or added as a block length. Refusal keeps both cursor and selection.
	const auto beforeCursor = view.cursor();
	const auto beforeStart = view.selectionStart();
	const auto beforeLength = view.selectionLength();
	for (const auto& expression : {QStringLiteral("+9223372036854775807"),
	                              QStringLiteral("end+9223372036854775807")}) {
		QString error;
		CHECK(view.evaluate(expression, &error) == -1 && !error.isEmpty());
		CHECK(!view.goTo(expression).isEmpty());
		CHECK(view.cursor() == beforeCursor && view.selectionStart() == beforeStart &&
		      view.selectionLength() == beforeLength);
	}
	for (const auto& start : {QStringLiteral("16"), QStringLiteral("9223372036854775807")}) {
		CHECK(!view.selectBlock(start, QStringLiteral("9223372036854775807"), false).isEmpty());
		CHECK(view.cursor() == beforeCursor && view.selectionStart() == beforeStart &&
		      view.selectionLength() == beforeLength);
	}
	view.selectRange(17, std::numeric_limits<qint64>::max());
	CHECK(view.selectionStart() == 17 && view.cursor() == 299 && view.selectionLength() == 283);

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

void asyncMatchLifetime()
{
	using telemetry::ux::HexView;
	auto* view = new HexView;
	view->setBytes(QByteArray(HexView::countInlineBytes + 1, 'x'));
	QPointer<HexView> alive(view);
	unsigned completed = 0;
	// The queued worker publication is independent of find()'s stack: a
	// receiver may close the viewer when its count has finished.
	QObject::connect(view, &HexView::matchesChanged, QCoreApplication::instance(), [&] {
		if (view->counting())
			return;
		++completed;
		delete view;
		view = nullptr;
	});
	CHECK(view->find(QStringLiteral("not-present"), QStringLiteral("text")) == QStringLiteral("Not found"));
	CHECK(alive && alive->counting());
	CHECK(await([&] { return alive.isNull(); }));
	CHECK(completed == 1);
}

// Types text as keys with that text, as a keyboard does.
void type(QObject* target, const QString& text)
{
	for (const QChar c : text) {
		const auto upper = c.toUpper().unicode();
		const int key = upper >= 'A' && upper <= 'Z' ? Qt::Key_A + (upper - 'A')
		                : upper >= '0' && upper <= '9' ? Qt::Key_0 + (upper - '0')
		                                             : Qt::Key_unknown;
		QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier, QString(c));
		QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier, QString(c));
		QCoreApplication::sendEvent(target, &down);
		QCoreApplication::sendEvent(target, &up);
	}
}

// Editing in the grid: nothing until Edit; then two hex digits write a byte
// and move on, a letter in the text column writes one; one Ctrl+Z takes back
// what was typed in one go, Ctrl+Y brings it back; the status says a byte at
// the cursor is edited and not saved.
void hexEditing(const QString& folder)
{
	using telemetry::ux::FileContent;
	using telemetry::ux::HexView;
	const auto path = folder + QStringLiteral("/editing.bin");
	{
		QFile file(path);
		CHECK(file.open(QIODevice::WriteOnly) && file.write("ABCDEFGH") == 8);
	}
	FileContent content;
	content.setLocalFile(path);
	HexView view;
	view.setSize(QSizeF(3000, 600));
	view.setContent(&content);
	view.moveTo(0);
	type(&view, QStringLiteral("41"));
	CHECK(content.bytes() == QByteArray("ABCDEFGH") && view.cursor() == 0);
	content.setEditing(true);
	type(&view, QStringLiteral("5"));
	// The first digit is the byte's high one; the cursor stays for the second.
	CHECK(content.bytes().at(0) == 0x51 && view.cursor() == 0 && content.editedBytes() == 1);
	type(&view, QStringLiteral("a"));
	CHECK(content.bytes().at(0) == 0x5A && view.cursor() == 1);
	// The grid lets go of its copy before each edit and reads without
	// detaching: typing writes in place, never copying the file.
	const auto* buffer = content.bytes().constData();
	type(&view, QStringLiteral("42"));
	CHECK(content.bytes().constData() == buffer && content.bytes().at(1) == 0x42);
	press(&view, Qt::Key_Left);
	press(&view, Qt::Key_Tab);
	type(&view, QStringLiteral("z"));
	CHECK(content.bytes().startsWith("Zz") && view.cursor() == 2 && content.editedBytes() == 2);
	view.moveTo(1);
	CHECK(view.status().contains(QStringLiteral("Edited, not saved")));
	// One step for what was typed in one go.
	press(&view, Qt::Key_Z, Qt::ControlModifier);
	CHECK(content.bytes() == QByteArray("ABCDEFGH") && content.editedBytes() == 0 && !content.canUndo());
	press(&view, Qt::Key_Y, Qt::ControlModifier);
	CHECK(content.bytes().startsWith("Zz") && content.editedBytes() == 2);
	// A letter outside the printable range is not typed; a hex digit in the
	// text column is a letter there.
	view.moveTo(7);
	type(&view, QStringLiteral("7"));
	CHECK(content.bytes().at(7) == '7' && view.cursor() == 7);
	CHECK(content.save().isEmpty() && !content.modified());
	QFile saved(path);
	CHECK(saved.open(QIODevice::ReadOnly) && saved.readAll() == QByteArray("ZzCDEFG7"));
	content.setEditing(false);
	type(&view, QStringLiteral("q"));
	CHECK(content.bytes() == QByteArray("ZzCDEFG7"));

	// Paste over the bytes: hex in the hex column, written from the cursor,
	// never past the file's end or a selection; bad hex refused in words.
	auto* clipboard = QGuiApplication::clipboard();
	clipboard->setText(QStringLiteral("41 42"));
	CHECK(view.paste() == QStringLiteral("Turn on Edit to paste over the bytes.") && content.bytes() == QByteArray("ZzCDEFG7"));
	content.setEditing(true);
	press(&view, Qt::Key_Backtab);
	view.moveTo(1);
	press(&view, Qt::Key_V, Qt::ControlModifier);
	CHECK(content.bytes() == QByteArray("ZABDEFG7") && view.cursor() == 3 && view.notice() == QStringLiteral("Pasted 2 bytes") &&
	      view.outcome() == QStringLiteral("done"));
	view.moveTo(6);
	clipboard->setText(QStringLiteral("31 32 33"));
	CHECK(view.paste() == QStringLiteral("Pasted 2 of 3 bytes: the rest would pass the file's end") &&
	      content.bytes() == QByteArray("ZABDEF12") && view.outcome() == QStringLiteral("partial"));
	view.selectRange(0, 2);
	clipboard->setText(QStringLiteral("616263"));
	CHECK(view.paste() == QStringLiteral("Pasted 2 of 3 bytes: as many as are selected") && content.bytes().startsWith("ab"));
	view.selectRange(2, 3);
	clipboard->setText(QStringLiteral("2D"));
	CHECK(view.paste() == QStringLiteral("Pasted 1 of the 3 selected bytes") && content.bytes() == QByteArray("ab-DEF12"));
	clipboard->setText(QStringLiteral("4G"));
	CHECK(view.paste().startsWith(QStringLiteral("Not pasted:")) && content.bytes() == QByteArray("ab-DEF12") &&
	      view.outcome() == QStringLiteral("refused"));
	// In the text column the text is UTF-8, as Find reads text.
	press(&view, Qt::Key_Tab);
	view.moveTo(3);
	clipboard->setText(QString::fromUtf8("\xC3\xA9"));
	CHECK(view.paste() == QStringLiteral("Pasted 2 bytes") && content.bytes() == QByteArray("ab-\xC3\xA9" "F12"));
	// One step each: an undo takes back the last paste, the next the one before.
	content.undo();
	CHECK(content.bytes() == QByteArray("ab-DEF12"));
	content.undo();
	CHECK(content.bytes() == QByteArray("abBDEF12"));
	content.discard();
	content.setEditing(false);
}

// Replace: the first press finds, the next writes over and finds on; all at
// once is one step to undo; another length, or without Edit, is refused.
void hexReplacing(const QString& folder)
{
	using telemetry::ux::FileContent;
	using telemetry::ux::HexView;
	const auto path = folder + QStringLiteral("/replace.bin");
	{
		QFile file(path);
		CHECK(file.open(QIODevice::WriteOnly) && file.write("ab-ab-ab") == 8);
	}
	FileContent content;
	content.setLocalFile(path);
	HexView view;
	view.setSize(QSizeF(3000, 600));
	view.setContent(&content);
	const auto text = QStringLiteral("text");
	const auto ab = QStringLiteral("ab");
	const auto xy = QStringLiteral("XY");
	CHECK(view.replace(ab, text, 4, xy, false) == QStringLiteral("Turn on Edit to replace."));
	content.setEditing(true);
	// A value outside the selected width must never become truncated replacement bytes.
	for (const int width : {1, 2, 4}) {
		const auto original = content.bytes();
		const auto cursor = view.cursor();
		const auto selection = view.selectionLength();
		for (const bool all : {false, true}) {
			CHECK(view.replace(QStringLiteral("97"), QStringLiteral("integer"), width,
			                   QStringLiteral("18446744073709551615"), all).contains(QStringLiteral("does not fit")));
			CHECK(view.outcome() == QStringLiteral("refused") && content.bytes() == original &&
			      !content.modified() && !content.canUndo() && view.cursor() == cursor &&
			      view.selectionLength() == selection);
		}
	}
	CHECK(view.replace(ab, text, 4, QStringLiteral("XYZ"), false).startsWith(QStringLiteral("The replacement is 3 bytes")) &&
	      view.outcome() == QStringLiteral("refused"));
	CHECK(view.patternBytes(QStringLiteral("XYZ"), text, 4) == 3 && view.patternBytes(QStringLiteral("4G"), QStringLiteral("hex"), 4) == -1 &&
	      view.patternBytes(QStringLiteral("7"), QStringLiteral("integer"), 2) == 2);
	CHECK(view.replace(ab, text, 4, QString(), false) == QStringLiteral("Enter what to write instead."));
	CHECK(view.replace(ab, text, 4, xy, false).isEmpty() && view.selectionStart() == 0 &&
	      content.bytes() == QByteArray("ab-ab-ab"));
	CHECK(view.replace(ab, text, 4, xy, false) == QStringLiteral("Replaced; the next one selected") &&
	      content.bytes() == QByteArray("XY-ab-ab") && view.selectionStart() == 3);
	CHECK(view.replace(ab, text, 4, ab, true) == QStringLiteral("The replacement is what is found: nothing would change.") &&
	      view.outcome() == QStringLiteral("refused"));
	CHECK(view.replace(ab, text, 4, xy, true) == QStringLiteral("Replaced 2") && content.bytes() == QByteArray("XY-XY-XY") &&
	      view.outcome() == QStringLiteral("done"));
	content.undo();
	CHECK(content.bytes() == QByteArray("XY-ab-ab"));
	content.undo();
	CHECK(content.bytes() == QByteArray("ab-ab-ab") && !content.modified());
	// One byte: Replace writes a one-byte match too, in either column's kind.
	view.clearFind();
	view.moveTo(0);
	const auto dash = QStringLiteral("-");
	CHECK(view.replace(dash, text, 4, QStringLiteral("+"), false).isEmpty() && view.cursor() == 2);
	CHECK(view.replace(dash, text, 4, QStringLiteral("+"), false) == QStringLiteral("Replaced; the next one selected") &&
	      content.bytes() == QByteArray("ab+ab-ab") && view.cursor() == 5);
	CHECK(view.replace(QStringLiteral("2D"), QStringLiteral("hex"), 4, QStringLiteral("2A"), false) ==
	          QStringLiteral("Replaced; no more matches") &&
	      content.bytes() == QByteArray("ab+ab*ab"));
	content.undo();
	content.undo();
	CHECK(content.bytes() == QByteArray("ab-ab-ab") && !content.modified());
	// Hex as Find reads it; matches never overlap.
	CHECK(view.replace(QStringLiteral("2D"), QStringLiteral("hex"), 4, QStringLiteral("2B"), true) ==
	          QStringLiteral("Replaced 2") &&
	      content.bytes() == QByteArray("ab+ab+ab"));
	CHECK(view.replace(QStringLiteral("zz"), text, 4, QStringLiteral("QQ"), true) == QStringLiteral("Not found"));
}

// The pictograms: each name draws in the colour given, undo's arrow on the
// left and redo's on the right; an unknown name draws nothing.
void toolIcons()
{
	using telemetry::ux::ToolIcon;
	const auto drawn = [](const QString& name) {
		ToolIcon icon;
		icon.setSize(QSizeF(24, 24));
		icon.setName(name);
		icon.setColor(QColor(255, 0, 0));
		QImage image(24, 24, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		icon.paint(&painter);
		painter.end();
		return image;
	};
	const auto ink = [](const QImage& image, int x, int y) {
		const QRgb pixel = image.pixel(x, y);
		return qAlpha(pixel) > 128 && qRed(pixel) > 128 && qGreen(pixel) < 64;
	};
	const auto inked = [&](const QImage& image) {
		int count = 0;
		for (int y = 0; y < image.height(); ++y)
			for (int x = 0; x < image.width(); ++x)
				count += ink(image, x, y) ? 1 : 0;
		return count;
	};
	for (const char* name : {"edit", "save", "undo", "redo", "lock", "error", "info", "copy", "clear", "folder", "file", "fileCodec", "fileText", "fileTable",
	                         "fileJson", "fileDescriptor", "fileValues", "fileIdentity", "fileSession", "fileCapability"})
		CHECK(inked(drawn(QString::fromLatin1(name))) > 20);
	CHECK(inked(drawn(QStringLiteral("nothing"))) == 0);
	const auto undo = drawn(QStringLiteral("undo"));
	const auto redo = drawn(QStringLiteral("redo"));
	CHECK(ink(undo, 5, 9) && !ink(undo, 19, 9) && ink(redo, 19, 9) && !ink(redo, 5, 9));
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
	asyncMatchLifetime();
	hexEditing(folder.path());
	hexReplacing(folder.path());
	toolIcons();

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

	// The selection drives the button and the summary line.
	const auto logs = files.index(1, 0);
	files.select(logs, true);
	CHECK(await([&] {
		return download->property("text").toString() == QStringLiteral("Download 2 files…");
	}));
	CHECK(download->property("enabled").toBool());
	CHECK(child(root, "deviceFilesSummary")->property("text").toString().startsWith(QStringLiteral("2 files selected")));
	CHECK(QMetaObject::invokeMethod(download, "clicked"));
	CHECK(await([&] {
		return root->property("lastSelection").toList().size() == 2;
	}));
	files.clearSelection();
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
	// While it runs, under its title alone, the row is whole: its foot, where
	// the progress runs, inside the list.
	if (transfers.data(transfers.index(row), DeviceFileTransfers::ActiveRole).toBool()) {
		auto* list = qobject_cast<QQuickItem*>(child(root, "deviceFilesTransfers"));
		auto* running = delegateWhere(root, "deviceFileTransfer", "index", row);
		if (list && running) {
			const auto inList = running->mapRectToItem(list, QRectF{0, 0, running->width(), running->height()});
			CHECK(inList.top() >= -0.5 && inList.bottom() <= list->height() + 0.5);
		}
	}
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
	// The items for other kinds are hidden and off: the arrow keys reach a
	// hidden item, and Enter would run it.
	for (const char* hidden : {"deviceFileMenuDownloadAll", "deviceFileMenuSelectAll", "deviceFileMenuSelectNone",
	                           "deviceFileMenuRefresh", "deviceFileMenuExpand", "deviceFileMenuSelectFolder",
	                           "deviceFileMenuDownloadSelected"})
		CHECK(!child(root, hidden)->property("enabled").toBool());
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
	{
		// Beside its row, never over it, once it has laid out the file.
		auto* properties = child(root, "deviceFileProperties");
		auto* popupParent = properties->property("parent").value<QQuickItem*>();
		QQuickItem* described = nullptr;
		CHECK(popupParent && await([&] {
			described = delegateWhere(root, "deviceFileRow", "name", QStringLiteral("events.csv"));
			return described && properties->property("height").toReal() > 0;
		}));
		const auto rowRect = described->mapRectToItem(popupParent, QRectF(0, 0, described->width(), described->height()));
		const QRectF popupRect(properties->property("x").toReal(), properties->property("y").toReal(),
		                       properties->property("width").toReal(), properties->property("height").toReal());
		CHECK(!popupRect.intersects(rowRect.adjusted(0, 1, 0, -1)));
	}
	CHECK(child(root, "deviceFilePropertiesSize")->property("text").toString() ==
	      QStringLiteral("%1 bytes").arg(eventsBytes.size()));
	CHECK(child(root, "deviceFilePropertiesAccessMark")->property("text").toString() == QStringLiteral("r--") &&
	      child(root, "deviceFilePropertiesAccess")->property("text").toString() == QStringLiteral("read only"));
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
	// Keys on the tree: Ctrl+A selects every readable file, Ctrl+Shift+A none.
	treeItem->forceActiveFocus();
	press(treeItem, Qt::Key_A, Qt::ControlModifier);
	CHECK(files.selectedCount() == files.fileCount());
	press(treeItem, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
	CHECK(files.selectedCount() == 0);
	// Ctrl+S on the tree is Download even beside a window's own Ctrl+S (a
	// host's Save): typed through the window, the list claims it first.
	files.select(files.index(0, 0), true);
	root->setProperty("lastSelection", QVariantList{});
	treeItem->forceActiveFocus();
	QTest::keyClick(qobject_cast<QWindow*>(root), Qt::Key_S, Qt::ControlModifier);
	CHECK(await([&] { return !root->property("lastSelection").toList().isEmpty(); }));
	CHECK(root->property("windowSaves").toInt() == 0);
	files.clearSelection();

	// The current row is framed while the tree has the keyboard; a row is
	// highlighted because it is selected, not because it is current. Enter on
	// a folder folds and unfolds it.
	auto* tree = qobject_cast<QQuickItem*>(child(root, "deviceFilesTree"));
	auto* selection = qvariant_cast<QItemSelectionModel*>(tree->property("selectionModel"));
	CHECK(selection);
	const auto logs2 = files.index(1, 0);
	selection->setCurrentIndex(logs2, QItemSelectionModel::NoUpdate);
	tree->forceActiveFocus();
	QQuickItem* currentRow = nullptr;
	CHECK(await([&] {
		currentRow = delegateWhere(root, "deviceFileRow", "current", true);
		return currentRow != nullptr;
	}));
	CHECK(currentRow->property("name").toString() == QStringLiteral("logs"));
	CHECK(!currentRow->property("highlighted").toBool());
	// Keyboard Copy path follows the current folder, even after a menu on a
	// different file; it must not reuse that menu's retained index.
	QGuiApplication::clipboard()->setText(QStringLiteral("unchanged"));
	QTest::keyClick(qobject_cast<QWindow*>(root), Qt::Key_C, Qt::AltModifier | Qt::ShiftModifier);
	CHECK(await([&] { return QGuiApplication::clipboard()->text() == QStringLiteral("/logs"); }));
	if (tree->hasActiveFocus())
		CHECK(visualChild(currentRow, QStringLiteral("deviceFileFocusFrame"))->isVisible());
	// A folder is never highlighted: it says how many of its files are
	// selected. A selected file is, current or not.
	files.select(logs2, true);
	auto* eventsRow = delegateWhere(root, "deviceFileRow", "name", QStringLiteral("events.csv"));
	CHECK(eventsRow && await([&] { return eventsRow->property("highlighted").toBool(); }));
	CHECK(!currentRow->property("highlighted").toBool());
	auto* pill = visualChild(currentRow, QStringLiteral("deviceFileSelectedInside"));
	CHECK(pill && pill->isVisible() && pill->property("text").toString() == QStringLiteral("2"));
	CHECK(currentRow->property("insideText").toString() == QStringLiteral("2 of 2 files selected"));
	// What each row is: a folder's icon and no access letters; a CSV's table
	// and its three letters; the descriptor's own icon, in the program's hue.
	CHECK(visualChild(currentRow, QStringLiteral("deviceFileIcon"))->property("name").toString() == QStringLiteral("folder"));
	CHECK(visualChild(eventsRow, QStringLiteral("deviceFileIcon"))->property("name").toString() == QStringLiteral("fileTable") &&
	      eventsRow->property("accessMark").toString() == QStringLiteral("r--") &&
	      eventsRow->property("accessTip").toString().startsWith(QStringLiteral("Read only.")));
	if (auto* descriptorRow = delegateWhere(root, "deviceFileRow", "name", QStringLiteral("descriptor.bin")))
		CHECK(visualChild(descriptorRow, QStringLiteral("deviceFileIcon"))->property("name").toString() ==
		          QStringLiteral("fileDescriptor") &&
		      descriptorRow->property("programFile").toBool());
	files.clearSelection();
	CHECK(await([&] { return !pill->isVisible(); }));

	// Selection as in a file explorer: a click selects that row alone, Ctrl+
	// click adds and removes, Shift+click takes the rows from the anchor, the
	// arrows move it, Shift+arrows extend it, Esc selects nothing. A folder
	// row stands for its files.
	{
		auto* window = qobject_cast<QQuickWindow*>(root);
		// The row a path is shown at now, -1 inside a folded folder.
		const auto rowOf = [&](const QString& path) {
			int row = -1;
			CHECK(QMetaObject::invokeMethod(tree, "rowAtIndex", Q_RETURN_ARG(int, row),
			                                Q_ARG(QModelIndex, files.indexOfPath(path))));
			return row;
		};
		// Clicked where a person would: the row scrolled into view first.
		const auto clickRow = [&](const QString& path, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
			const int at = rowOf(path);
			if (at < 0)
				warnings << QStringLiteral("not shown: %1 (rows %2)").arg(path).arg(tree->property("rows").toInt());
			CHECK(at >= 0);
			QQmlExpression bringIn(qmlContext(tree), tree,
			                       QStringLiteral("positionViewAtRow(%1, TableView.Contain); forceLayout()").arg(at));
			bringIn.evaluate();
			CHECK(!bringIn.hasError());
			const auto name = path.section(QLatin1Char('/'), -1);
			QQuickItem* row = nullptr;
			CHECK(await([&] { return (row = delegateWhere(root, "deviceFileRow", "name", name)) != nullptr; }));
			const auto point = row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint();
			QTest::mouseClick(window, Qt::LeftButton, modifiers, point);
			QCoreApplication::processEvents();
		};
		const auto selected = [&](const QString& path) {
			return files.isSelected(files.indexOfPath(path));
		};
		const auto paths = [&] { return files.selectedPaths(); };
		// Every folder unfolded: the ranges below are counted in rows shown -
		// telemetry, descriptor.bin, logs, events.csv, large.bin.
		QQmlExpression unfold(qmlContext(tree), tree, QStringLiteral("expandRecursively()"));
		unfold.evaluate();
		CHECK(!unfold.hasError() && await([&] { return tree->property("rows").toInt() == 5; }));
		const QString events = QStringLiteral("/logs/events.csv"), large = QStringLiteral("/logs/large.bin"),
		              descriptor = QStringLiteral("/telemetry/descriptor.bin"), logsPath = QStringLiteral("/logs"),
		              telemetryPath = QStringLiteral("/telemetry");
		clickRow(events);
		CHECK(files.selectedCount() == 1 && selected(events));
		clickRow(large);
		CHECK(files.selectedCount() == 1 && selected(large));
		clickRow(events, Qt::ControlModifier);
		CHECK(files.selectedCount() == 2 && selected(events));
		clickRow(events, Qt::ControlModifier);
		CHECK(files.selectedCount() == 1 && !selected(events));
		press(tree, Qt::Key_Escape);
		CHECK(files.selectedCount() == 0);
		// The folder row: all its readable files.
		clickRow(logsPath);
		CHECK(files.selectedCount() == 2 && selected(events) && selected(large));
		// Down moves the selection to the next row alone; Shift+Down extends it.
		const int logsAt = tree->property("currentRow").toInt();
		press(tree, Qt::Key_Down);
		CHECK(tree->property("currentRow").toInt() == logsAt + 1 && files.selectedCount() == 1);
		press(tree, Qt::Key_Down, Qt::ShiftModifier);
		CHECK(tree->property("currentRow").toInt() == logsAt + 2 && files.selectedCount() == 2);
		press(tree, Qt::Key_Escape);
		CHECK(files.selectedCount() == 0);

		// Shift+click: the rows shown from the anchor. An unfolded folder in
		// the range adds nothing - its rows below speak for it, so a range that
		// ends on logs leaves large.bin out.
		clickRow(events);
		clickRow(logsPath, Qt::ShiftModifier);
		CHECK(paths() == QStringList{events});
		clickRow(descriptor, Qt::ShiftModifier);
		CHECK(paths() == (QStringList{descriptor, events}));
		// The anchor stays where the range started.
		clickRow(large, Qt::ShiftModifier);
		CHECK(paths() == (QStringList{events, large}));
		// Ctrl+Shift+click adds a range to what is selected.
		clickRow(descriptor);
		clickRow(large, Qt::ControlModifier);
		clickRow(events, Qt::ControlModifier | Qt::ShiftModifier);
		CHECK(paths() == (QStringList{descriptor, events, large}));
		clickRow(large);
		clickRow(events, Qt::ShiftModifier);
		CHECK(paths() == (QStringList{events, large}));
		// The anchor is the file, not its row: folding a folder above it moves
		// the rows, and a Shift+click still ranges from that file (a row
		// number would have reached large.bin too).
		clickRow(events);
		const int telemetryRow = rowOf(telemetryPath);
		CHECK(telemetryRow >= 0 && QMetaObject::invokeMethod(tree, "collapse", Q_ARG(int, telemetryRow)));
		CHECK(await([&] { return rowOf(descriptor) < 0; }));
		clickRow(logsPath, Qt::ShiftModifier);
		CHECK(paths() == QStringList{events});
		CHECK(QMetaObject::invokeMethod(tree, "expand", Q_ARG(int, telemetryRow)));
		CHECK(await([&] { return rowOf(descriptor) >= 0; }));
		// An anchor folded away: Shift+arrows grow the range from where they
		// started, never slide it along with the current row.
		clickRow(descriptor);
		CHECK(QMetaObject::invokeMethod(tree, "collapse", Q_ARG(int, telemetryRow)));
		CHECK(await([&] { return rowOf(descriptor) < 0; }));
		selection->setCurrentIndex(files.indexOfPath(telemetryPath), QItemSelectionModel::NoUpdate);
		press(tree, Qt::Key_Down, Qt::ShiftModifier);
		CHECK(paths() == QStringList{descriptor});
		press(tree, Qt::Key_Down, Qt::ShiftModifier);
		CHECK(paths() == (QStringList{descriptor, events}));
		press(tree, Qt::Key_Down, Qt::ShiftModifier);
		CHECK(paths() == (QStringList{descriptor, events, large}));
		CHECK(QMetaObject::invokeMethod(tree, "expand", Q_ARG(int, telemetryRow)));
		CHECK(await([&] { return rowOf(descriptor) >= 0; }));
		// Ctrl+arrows move the current row alone; Space and Ctrl+Space add it
		// or take it away.
		clickRow(events);
		const int eventsAt = tree->property("currentRow").toInt();
		press(tree, Qt::Key_Down, Qt::ControlModifier);
		CHECK(tree->property("currentRow").toInt() == eventsAt + 1 && paths() == QStringList{events});
		press(tree, Qt::Key_Space);
		CHECK(paths() == (QStringList{events, large}));
		press(tree, Qt::Key_Space, Qt::ControlModifier);
		CHECK(paths() == QStringList{events});
		// Home and End move it, as the arrows do.
		press(tree, Qt::Key_End);
		CHECK(tree->property("currentRow").toInt() == tree->property("rows").toInt() - 1 && paths() == QStringList{large});
		press(tree, Qt::Key_Home, Qt::ShiftModifier);
		CHECK(tree->property("currentRow").toInt() == 0 && files.selectedCount() == files.fileCount());
		press(tree, Qt::Key_Escape);
		// A right click outside the selection selects that file; inside a
		// selection of two or more the menu acts on all of them.
		clickRow(events);
		const auto openMenuOn = [&](const QString& path) {
			CHECK(QMetaObject::invokeMethod(pane, "openMenu", Q_ARG(QVariant, QVariant::fromValue(files.indexOfPath(path))),
			                                Q_ARG(QVariant, QVariant::fromValue<QObject*>(treeItem)), Q_ARG(QVariant, 10),
			                                Q_ARG(QVariant, 10)));
			const auto kind = fileMenu->property("kind").toString();
			fileMenu->setProperty("visible", false);
			return kind;
		};
		CHECK(openMenuOn(large) == QStringLiteral("file") && paths() == QStringList{large});
		clickRow(events, Qt::ControlModifier);
		CHECK(openMenuOn(events) == QStringLiteral("selection") && paths() == (QStringList{events, large}));
		// A folder has its own menu and leaves the selection as it is.
		CHECK(openMenuOn(telemetryPath) == QStringLiteral("folder") && paths() == (QStringList{events, large}));
		press(tree, Qt::Key_Escape);
		CHECK(files.selectedCount() == 0);
		selection->setCurrentIndex(logs2, QItemSelectionModel::NoUpdate);
	}
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
	// Sorted by a heading, filtered, the count of rows shown.
	auto* rows = qobject_cast<telemetry::ux::TableRowsModel*>(viewerContent.rows());
	CHECK(rows);
	rows->sortBy(1);
	CHECK(rows->cellText(0, 1) == QStringLiteral("2.5"));
	rows->sortBy(1);
	CHECK(rows->cellText(0, 1) == QStringLiteral("5") && rows->descending());
	rows->sortBy(1);
	child(root, "tableFilter")->setProperty("text", QStringLiteral("period"));
	CHECK(await([&] { return child(root, "tableShown")->property("text").toString() == QStringLiteral("1 of 2 rows"); }));
	child(root, "tableFilter")->setProperty("text", QString());
	CHECK(await([&] { return child(root, "tableShown")->property("text").toString() == QStringLiteral("2 rows"); }));
	CHECK(child(root, "fileViewerFooter")->property("text").toString().endsWith(QStringLiteral("/config/viewer.csv")));
	CHECK(QMetaObject::invokeMethod(delegate(root, "fileViewerMode_text"), "clicked"));
	CHECK(viewer->property("shownMode").toString() == QStringLiteral("text"));
	// The text: numbered lines, Find with its count, Go to line.
	CHECK(await([&] {
		auto* list = child(root, "textLines");
		return list && list->property("count").toInt() == 3;
	}));
	auto* textPane = child(root, "textPane");
	CHECK(QMetaObject::invokeMethod(child(root, "textFind"), "open"));
	child(root, "textFindField")->setProperty("text", QStringLiteral("gain"));
	CHECK(await([&] { return child(root, "textFindCount")->property("text").toString() == QStringLiteral("1 of 1"); }));
	CHECK(child(root, "textLines")->property("currentIndex").toInt() == 1);
	CHECK(QMetaObject::invokeMethod(child(root, "textFind"), "close"));
	CHECK(QMetaObject::invokeMethod(textPane, "openGoto"));
	child(root, "textGotoField")->setProperty("text", QStringLiteral("3"));
	CHECK(QMetaObject::invokeMethod(child(root, "textGotoField"), "accepted"));
	CHECK(child(root, "textLines")->property("currentIndex").toInt() == 2);
	child(root, "textGotoField")->setProperty("text", QStringLiteral("9"));
	CHECK(QMetaObject::invokeMethod(textPane, "openGoto"));
	child(root, "textGotoField")->setProperty("text", QStringLiteral("9"));
	CHECK(QMetaObject::invokeMethod(child(root, "textGotoField"), "accepted"));
	CHECK(child(root, "textGotoProblem")->property("text").toString() == QStringLiteral("No line 9: the text has 3 lines"));
	CHECK(QMetaObject::invokeMethod(textPane, "closeGoto"));
	// Editing's pictograms on the viewer's row, each named with the file:
	// Edit shows the bytes and starts editing, pressed again it stops; Save
	// waits for an edit.
	auto* editTool = child(root, "hexEdit");
	CHECK(editTool && editTool->property("visible").toBool() &&
	      editTool->property("text").toString().contains(QStringLiteral("viewer.csv")));
	CHECK(!child(root, "hexSave")->property("enabled").toBool());
	CHECK(QMetaObject::invokeMethod(editTool, "clicked"));
	CHECK(viewer->property("shownMode").toString() == QStringLiteral("hex") && viewerContent.editing() &&
	      editTool->property("switchedOn").toBool());
	CHECK(QMetaObject::invokeMethod(editTool, "clicked"));
	CHECK(!viewerContent.editing() && !viewerContent.modified());
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
