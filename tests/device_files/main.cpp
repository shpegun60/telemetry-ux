// The Device files models over a real session and the native TCP server:
// the tree of the device's paths, marks and selection, downloads written
// exactly, cancel, a model destroyed mid-download, and a files-only device. MIT.
#include <Telemetry/UX/DeviceFileTransfers.hpp>
#include <Telemetry/UX/DeviceFilesModel.hpp>
#include <Telemetry/UX/FileContent.hpp>
#include <integration/tcp/Server.hpp>
#include <resource/BytesFile.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLocale>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QEventLoop>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>
namespace qt = telemetry::integration::qt;
namespace tcp = telemetry::integration::tcp;
using telemetry::ux::DeviceFilesModel;
using telemetry::ux::DeviceFileTransfers;

namespace {
unsigned checks = 0;

void check(bool ok, int line)
{
	++checks;
	if (!ok) {
		std::fprintf(stderr, "Device files UX check %u failed at line %d\n", checks, line);
		std::exit(1);
	}
}

#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)

// Runs the event loop as a program does: it waits for the next event rather
// than sleeping. A sleep between passes costs a whole Windows timer quantum
// (about 15.6 ms) per pass and made the 4100-file list measure the loop, not
// the session. A precise 5 ms timer keeps the wait bounded.
bool await(const std::function<bool()>& done, int milliseconds = 15000)
{
	QElapsedTimer deadline;
	deadline.start();
	QTimer wake;
	wake.setTimerType(Qt::PreciseTimer);
	wake.start(5);
	while (!done() && deadline.elapsed() < milliseconds)
		QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
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
std::array<std::byte, 300000> largeBytes{};
std::array<std::byte, 40> settingsBytes{};
const resource::BytesFile descriptorFile{packed};
const resource::BytesFile eventsFile{eventsBytes};
const resource::BytesFile largeFile{largeBytes};
const resource::BytesFile settingsFile{settingsBytes};
const auto withDescriptor = resource::filesystem(
    resource::file("/telemetry/descriptor.bin", descriptorFile), resource::file("/logs/events.csv", eventsFile),
    resource::file("/logs/large.bin", largeFile), resource::file("/cfg/settings.csv", settingsFile));
const auto withoutDescriptor =
    resource::filesystem(resource::file("/logs/events.csv", eventsFile), resource::file("/cfg/settings.csv", settingsFile));

class Peer {
public:
	QTcpServer listener;
	QPointer<QTcpSocket> socket;
	std::unique_ptr<tcp::Server> server;
	alignas(32) std::array<std::byte, model.maxScratch()> scratch{};
	telemetry::Workspace workspace{scratch};
	std::array<std::byte, 4096> rx{}, tx{};
	QByteArray pending;

	explicit Peer(resource::FileSystemView table)
	{
		CHECK(listener.listen(QHostAddress::LocalHost, 0));
		QObject::connect(&listener, &QTcpServer::newConnection, &listener, [this, table] {
			auto* candidate = listener.nextPendingConnection();
			socket = candidate;
			pending.clear();
			server = std::make_unique<tcp::Server>(model.view(), workspace, table, rx, tx);
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

bool sameBytes(const QString& path, const auto& expected)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return false;
	const auto bytes = file.readAll();
	return bytes.size() == static_cast<qsizetype>(expected.size()) &&
	       std::equal(expected.begin(), expected.end(), reinterpret_cast<const std::byte*>(bytes.constData()));
}

QModelIndex child(const DeviceFilesModel& files, const QModelIndex& parent, const QString& name)
{
	for (int row = 0; row < files.rowCount(parent); ++row) {
		const auto at = files.index(row, 0, parent);
		if (files.data(at, DeviceFilesModel::NameRole).toString() == name)
			return at;
	}
	return {};
}

// Waits for the row's download to end and returns its state.
DeviceFileTransfers::State ended(DeviceFileTransfers& transfers, int row)
{
	await([&] {
		return !transfers.data(transfers.index(row), DeviceFileTransfers::ActiveRole).toBool();
	});
	const auto state = static_cast<DeviceFileTransfers::State>(
	    transfers.data(transfers.index(row), DeviceFileTransfers::StateRole).toInt());
	std::fprintf(stderr, "download %d ended: state %d, %s\n", row, static_cast<int>(state),
	             qPrintable(transfers.data(transfers.index(row), DeviceFileTransfers::ResultRole).toString()));
	return state;
}

void tree(qt::ConnectionSession& session, DeviceFilesModel& files)
{
	CHECK(files.available() && files.fileCount() == 4 && files.error().isEmpty());
	CHECK(files.rowCount() == 3); // telemetry, logs, cfg: the device's order
	const auto telemetryFolder = files.index(0, 0);
	const auto logs = files.index(1, 0);
	CHECK(files.data(telemetryFolder, DeviceFilesModel::NameRole).toString() == QStringLiteral("telemetry"));
	CHECK(files.data(logs, DeviceFilesModel::IsFolderRole).toBool() &&
	      files.data(logs, DeviceFilesModel::FileCountRole).toInt() == 2);
	const auto events = child(files, logs, QStringLiteral("events.csv"));
	CHECK(events.isValid() && files.parent(events) == logs);
	CHECK(files.data(events, DeviceFilesModel::PathRole).toString() == QStringLiteral("/logs/events.csv"));
	CHECK(files.data(events, DeviceFilesModel::SizeTextRole).toString() == QStringLiteral("300 B"));
	CHECK(files.data(events, DeviceFilesModel::AccessTextRole).toString() == QStringLiteral("read"));
	// The device's names are kept exactly, the descriptor included.
	CHECK(child(files, telemetryFolder, QStringLiteral("descriptor.bin")).isValid());

	files.setMarked(logs, true);
	CHECK(files.markedCount() == 2 && files.markedBytes() == eventsBytes.size() + largeBytes.size());
	CHECK(files.data(logs, DeviceFilesModel::MarkedRole).toInt() == Qt::Checked);
	files.setMarked(child(files, logs, QStringLiteral("large.bin")), false);
	CHECK(files.data(logs, DeviceFilesModel::MarkedRole).toInt() == Qt::PartiallyChecked);
	const auto marked = files.selection();
	CHECK(marked.size() == 1 &&
	      marked[0].toMap().value(QStringLiteral("path")).toString() == QStringLiteral("/logs/events.csv"));
	files.markAll(false);
	CHECK(files.markedCount() == 0);
	const auto current = files.selection(events);
	CHECK(current.size() == 1 && current[0].toMap().value(QStringLiteral("fileIndex")).toInt() == 1);
	CHECK(files.indexOfPath(QStringLiteral("/cfg/settings.csv")).isValid());
	(void)session;
}

void downloads(qt::ConnectionSession& session, DeviceFilesModel& files, const QString& folder)
{
	DeviceFileTransfers transfers(&session);
	const auto events = files.indexOfPath(QStringLiteral("/logs/events.csv"));
	const auto large = files.indexOfPath(QStringLiteral("/logs/large.bin"));
	const auto eventsLocal = DeviceFileTransfers::localPathFor(folder, QStringLiteral("/logs/events.csv"));
	CHECK(eventsLocal.endsWith(QStringLiteral("logs/events.csv")));
	const auto row = transfers.download(QStringLiteral("/logs/events.csv"),
	                                    files.data(events, DeviceFilesModel::FileIndexRole).toInt(),
	                                    eventsBytes.size(), eventsLocal);
	CHECK(row == 0 && transfers.activeCount() >= 1);
	CHECK(ended(transfers, row) == DeviceFileTransfers::State::Saved);
	CHECK(sameBytes(eventsLocal, eventsBytes));
	CHECK(transfers.data(transfers.index(row), DeviceFileTransfers::ResultRole).toString() == QStringLiteral("Saved"));

	// A large file, its progress seen while it is read, then whole on disk.
	const auto largeLocal = DeviceFileTransfers::localPathFor(folder, QStringLiteral("/logs/large.bin"));
	const auto largeIndex = files.data(large, DeviceFilesModel::FileIndexRole).toInt();
	const auto whole = transfers.download(QStringLiteral("/logs/large.bin"), largeIndex, largeBytes.size(), largeLocal);
	bool sawProgress = false, sawSummary = false;
	CHECK(await([&] {
		const auto progress = transfers.data(transfers.index(whole), DeviceFileTransfers::ProgressRole).toDouble();
		sawProgress = sawProgress || (progress > 0 && progress < 1);
		sawSummary = sawSummary || transfers.summary().startsWith(QStringLiteral("Downloading large.bin"));
		return !transfers.data(transfers.index(whole), DeviceFileTransfers::ActiveRole).toBool();
	}));
	CHECK(sawProgress && sawSummary);
	CHECK(ended(transfers, whole) == DeviceFileTransfers::State::Saved && sameBytes(largeLocal, largeBytes));
	CHECK(transfers.activeCount() == 0 && transfers.summary().isEmpty());

	// Cancelled midway: no file under the real name.
	const auto cancelledLocal = folder + QStringLiteral("/cancelled.bin");
	const auto stopped = transfers.download(QStringLiteral("/logs/large.bin"), largeIndex, largeBytes.size(), cancelledLocal);
	CHECK(await([&] {
		return transfers.data(transfers.index(stopped), DeviceFileTransfers::ReceivedRole).toULongLong() > 20000;
	}));
	transfers.cancel(stopped);
	CHECK(ended(transfers, stopped) == DeviceFileTransfers::State::Cancelled);
	CHECK(!QFile::exists(cancelledLocal));
	transfers.clearFinished();
	CHECK(transfers.rowCount() == 0);
}

// Open: the viewer's tab is there before the first byte, reading; it then
// shows the file, or says why it could not be read.
void openInViewer(qt::ConnectionSession& session, DeviceFilesModel& files, const QString& folder)
{
	using telemetry::ux::FileContent;
	using telemetry::ux::OpenFiles;
	DeviceFileTransfers transfers(&session);
	OpenFiles open;
	const auto large = files.indexOfPath(QStringLiteral("/logs/large.bin"));
	const auto local = folder + QStringLiteral("/open/large.bin");
	const auto row = transfers.openIn(&open, QStringLiteral("/logs/large.bin"),
	                                  files.data(large, DeviceFilesModel::FileIndexRole).toInt(),
	                                  largeBytes.size(), local);
	CHECK(row >= 0 && open.count() == 1 && open.rowOf(local) == 0);
	auto* content = open.contentFor(local);
	CHECK(content && content->loading() && !content->loaded());
	CHECK(open.data(open.index(0), OpenFiles::LoadingRole).toBool());
	CHECK(transfers.data(transfers.index(row), DeviceFileTransfers::OpeningRole).toBool());
	bool sawProgress = false;
	QElapsedTimer reading;
	reading.start();
	CHECK(await([&] {
		sawProgress = sawProgress || (content->loading() && content->progress() > 0 && content->progress() < 1);
		return !content->loading();
	}));
	// Progress is published at most every 500 ms; a quicker read shows none between.
	CHECK((sawProgress || reading.elapsed() < 500) && content->loaded() && content->size() == largeBytes.size() &&
	      content->error().isEmpty());
	CHECK(transfers.data(transfers.index(row), DeviceFileTransfers::ResultRole).toString() ==
	      QStringLiteral("Opened in the Viewer"));
	CHECK(!open.data(open.index(0), OpenFiles::LoadingRole).toBool());

	// A copy that cannot be written: the tab says why, and no reading is left.
	const auto blocker = folder + QStringLiteral("/open-blocked");
	{
		QFile file(blocker);
		CHECK(file.open(QIODevice::WriteOnly));
	}
	const auto events = files.indexOfPath(QStringLiteral("/logs/events.csv"));
	const auto blockedLocal = blocker + QStringLiteral("/events.csv");
	CHECK(transfers.openIn(&open, QStringLiteral("/logs/events.csv"),
	                       files.data(events, DeviceFilesModel::FileIndexRole).toInt(), eventsBytes.size(),
	                       blockedLocal) >= 0);
	auto* failed = open.contentFor(blockedLocal);
	CHECK(failed && await([&] {
		return !failed->loading();
	}));
	CHECK(!failed->loaded() && failed->error().startsWith(QStringLiteral("Could not read events.csv: Could not write")));

	// Cancel while reading: the read stops; the host closes the tab.
	const auto cancelLocal = folder + QStringLiteral("/open/again.bin");
	CHECK(transfers.openIn(&open, QStringLiteral("/logs/large.bin"),
	                       files.data(large, DeviceFilesModel::FileIndexRole).toInt(), largeBytes.size(),
	                       cancelLocal) >= 0);
	transfers.cancelFor(cancelLocal);
	auto* cancelled = open.contentFor(cancelLocal);
	CHECK(cancelled && await([&] {
		return !cancelled->loading();
	}));
	CHECK(!cancelled->loaded() && transfers.activeCount() == 0);
	open.close(open.rowOf(cancelLocal));
	CHECK(open.count() == 2 && open.rowOf(cancelLocal) < 0);
}

void destroyedMidway(qt::ConnectionSession& session, DeviceFilesModel& files, const QString& folder)
{
	auto transfers = std::make_unique<DeviceFileTransfers>(&session);
	const auto large = files.indexOfPath(QStringLiteral("/logs/large.bin"));
	const auto row = transfers->download(QStringLiteral("/logs/large.bin"),
	                                     files.data(large, DeviceFilesModel::FileIndexRole).toInt(),
	                                     largeBytes.size(), folder + QStringLiteral("/orphan.bin"));
	CHECK(await([&] {
		return transfers->data(transfers->index(row), DeviceFileTransfers::ReceivedRole).toULongLong() > 20000;
	}));
	transfers.reset(); // Its reader still has a request in flight; the session keeps it.
	QElapsedTimer settle;
	settle.start();
	while (settle.elapsed() < 500) {
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	CHECK(session.filesSupported() && !QFile::exists(folder + QStringLiteral("/orphan.bin")));
	// The session still works: the list reads again.
	files.refresh();
	CHECK(await([&] {
		return !files.loading();
	}));
	CHECK(files.fileCount() == 4 && files.error().isEmpty());
}

// The viewers' reading of a local file: CSV as a table, text, anything as hex.
void fileContent(const QString& folder)
{
	using telemetry::ux::FileContent;
	using telemetry::ux::FileHexModel;
	using telemetry::ux::FileTableModel;
	const auto write = [&](const char* name, const QByteArray& bytes) {
		const auto path = folder + QLatin1Char('/') + QLatin1String(name);
		QFile file(path);
		CHECK(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size());
		return path;
	};
	const QByteArray csvBytes =
	    "time_ms,event,note\r\n0,start,\"a, quoted \"\"note\"\"\"\n250,sample,\n500,threshold";
	FileContent csv;
	csv.setLocalFile(write("table.csv", csvBytes));
	CHECK(csv.loaded() && csv.textual() && csv.tabular() && csv.format() == QStringLiteral("Table"));
	CHECK(csv.table()->columnCount() == 3 && csv.table()->rowCount() == 3);
	CHECK(csv.table()->headerData(2, Qt::Horizontal).toString() == QStringLiteral("note"));
	CHECK(csv.table()->index(0, 2).data().toString() == QStringLiteral("a, quoted \"note\""));
	CHECK(csv.table()->index(2, 2).data().toString().isEmpty()); // a short row is padded
	auto* table = qobject_cast<FileTableModel*>(csv.table());
	CHECK(table && table->numeric(0) && !table->numeric(1) && !table->numeric(2));
	CHECK(csv.hex()->rowCount() == (csvBytes.size() + 15) / 16 && csv.size() == qulonglong(csvBytes.size()));

	FileContent text;
	text.setLocalFile(write("note.txt", "line one\nline two\n"));
	CHECK(text.textual() && !text.tabular() && text.format() == QStringLiteral("Text") &&
	      text.text() == QStringLiteral("line one\nline two\n"));

	QByteArray bytes;
	for (int i = 0; i < 40; ++i)
		bytes += char(i);
	FileContent binary;
	binary.setLocalFile(write("data.bin", bytes));
	CHECK(binary.loaded() && !binary.textual() && binary.format() == QStringLiteral("Binary"));
	CHECK(binary.hex()->rowCount() == 3);
	CHECK(binary.hex()->index(0, 0).data(FileHexModel::AsciiRole).toString() == QString(16, QLatin1Char('.')));
	CHECK(binary.hex()->index(1, 0).data(FileHexModel::OffsetRole).toString() == QStringLiteral("00000010"));
	CHECK(binary.hex()->index(1, 0).data(FileHexModel::BytesRole).toString().startsWith(QStringLiteral("10 11 12 ")));
	CHECK(binary.hex()->index(2, 0).data(FileHexModel::AsciiRole).toString() == QStringLiteral(" !\"#$%&'"));

	// An open quote is not a table, but it is still text.
	FileContent broken;
	broken.setLocalFile(write("broken.csv", "a,b\n\"open,1\n"));
	CHECK(broken.textual() && !broken.tabular() && broken.format() == QStringLiteral("Text"));
	// The device's name can say CSV where the local copy's does not.
	FileContent named;
	named.setLocalFile(write("plain.txt", "a,b\n1,2\n"));
	CHECK(!named.tabular());
	named.setDevicePath(QStringLiteral("/logs/plain.csv"));
	CHECK(named.tabular() && named.table()->rowCount() == 1);
	FileContent missing;
	missing.setLocalFile(folder + QStringLiteral("/none.bin"));
	CHECK(!missing.loaded() && !missing.error().isEmpty());

	// The open files: the same copy again is the same row, read anew; Close
	// removes it.
	telemetry::ux::OpenFiles open;
	const auto csvPath = folder + QStringLiteral("/table.csv");
	CHECK(open.open(csvPath, QStringLiteral("/logs/table.csv")) == 0);
	CHECK(open.open(folder + QStringLiteral("/data.bin"), QStringLiteral("/data/data.bin")) == 1);
	CHECK(open.open(csvPath, QStringLiteral("/logs/table.csv")) == 0 && open.count() == 2);
	CHECK(open.data(open.index(0), telemetry::ux::OpenFiles::NameRole).toString() == QStringLiteral("table.csv"));
	auto* first = qobject_cast<FileContent*>(
	    open.data(open.index(0), telemetry::ux::OpenFiles::ContentRole).value<QObject*>());
	CHECK(first && first->tabular() && first->devicePath() == QStringLiteral("/logs/table.csv"));
	open.close(0);
	CHECK(open.count() == 1 &&
	      open.data(open.index(0), telemetry::ux::OpenFiles::NameRole).toString() == QStringLiteral("data.bin"));
}

// A codec: a file whose head says "DEMO" is read into a model and offered
// first; one that claims it and cannot read it says why, and the file stays
// text or hex. Re-reading makes a new model, and a file of another format is
// not offered to it.
void codecs(const QString& folder)
{
	using telemetry::ux::FileCodec;
	using telemetry::ux::FileContent;
	using telemetry::ux::FileIdentity;
	const auto write = [&](const char* name, const QByteArray& bytes) {
		const auto path = folder + QLatin1Char('/') + QLatin1String(name);
		QFile file(path);
		CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size());
		return path;
	};
	int parses = 0;
	QString seenPath;
	telemetry::ux::OpenFiles open;
	open.addCodec(FileCodec{
	    QStringLiteral("Demo"),
	    [&](const FileIdentity& identity) {
		    seenPath = identity.devicePath;
		    return identity.head.startsWith("DEMO");
	    },
	    [&](const QByteArray& bytes, QString& error) -> std::unique_ptr<QObject> {
		    ++parses;
		    if (bytes.size() < 8) {
			    error = QStringLiteral("shorter than its header");
			    return nullptr;
		    }
		    auto model = std::make_unique<QObject>();
		    model->setObjectName(QString::fromLatin1(bytes.mid(4, 4)));
		    return model;
	    },
	    QUrl(QStringLiteral("qrc:/Demo.qml"))});

	const auto good = write("good.demo", "DEMOabcd\x01\x02");
	const int row = open.open(good, QStringLiteral("/data/good.demo"));
	auto* content = open.contentFor(good);
	CHECK(content && content->loaded() && parses == 1 && seenPath == QStringLiteral("/data/good.demo"));
	CHECK(content->decoded() && content->decoded()->objectName() == QStringLiteral("abcd"));
	CHECK(content->decoded()->parent() == content && content->codecName() == QStringLiteral("Demo"));
	CHECK(content->format() == QStringLiteral("Demo") && content->codecError().isEmpty());
	CHECK(content->codecView() == QUrl(QStringLiteral("qrc:/Demo.qml")));
	// Hex stays on offer under the codec's view.
	CHECK(content->hex()->rowCount() == 1);

	// Read again: a new model; the old one goes once the views had their turn.
	QPointer<QObject> before = content->decoded();
	write("good.demo", "DEMOwxyz");
	CHECK(open.open(good, QStringLiteral("/data/good.demo")) == row && parses == 2);
	CHECK(content->decoded() && content->decoded() != before.data() &&
	      content->decoded()->objectName() == QStringLiteral("wxyz"));
	CHECK(!before.isNull()); // not under a view that may still hold it
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	CHECK(before.isNull());

	// Claimed and not readable: the reason, and the file as text.
	const auto bad = write("bad.demo", "DEMO");
	open.open(bad, QStringLiteral("/data/bad.demo"));
	auto* broken = open.contentFor(bad);
	CHECK(broken && broken->loaded() && !broken->decoded() && parses == 3);
	CHECK(broken->codecError() == QStringLiteral("Not a valid Demo file: shorter than its header"));
	CHECK(broken->format() == QStringLiteral("Text") && broken->codecName().isEmpty());

	// Another format is never parsed by it.
	const auto other = write("other.bin", QByteArray("\x00\x01\x02", 3));
	open.open(other);
	CHECK(open.contentFor(other) && !open.contentFor(other)->decoded() && parses == 3 &&
	      open.contentFor(other)->format() == QStringLiteral("Binary"));

	// A content outside OpenFiles knows no codecs.
	FileContent alone;
	alone.setLocalFile(good);
	CHECK(alone.loaded() && !alone.decoded() && alone.format() == QStringLiteral("Text") && parses == 3);
}

// The device names its files, so no name of its choosing may place a download
// outside the chosen folder or under a name Windows treats specially.
void localPaths(const QString& folder)
{
	const auto lands = [&](const QString& devicePath, const char* expected) {
		return DeviceFileTransfers::localPathFor(folder, devicePath) ==
		       folder + QLatin1Char('/') + QString::fromUtf8(expected);
	};
	CHECK(lands(QStringLiteral("/logs/events.csv"), "logs/events.csv"));
	CHECK(lands(QStringLiteral("/../../outside.txt"), "outside.txt"));
	CHECK(lands(QStringLiteral("/a\\..\\..\\b"), "a/b"));
	CHECK(lands(QStringLiteral("C:/Windows/win.ini"), "C_/Windows/win.ini"));
	CHECK(lands(QStringLiteral("//server/share/x"), "server/share/x"));
	CHECK(lands(QStringLiteral("/CON"), "_CON"));
	CHECK(lands(QStringLiteral("/logs/aux.txt"), "logs/_aux.txt"));
	CHECK(lands(QStringLiteral("/name. . "), "name"));
	CHECK(lands(QStringLiteral("/a<b>c:d\"e|f?g*h"), "a_b_c_d_e_f_g_h"));
	CHECK(lands(QStringLiteral("/x") + QChar(1) + QStringLiteral("y"), "x_y"));
	CHECK(lands(QStringLiteral("/..foo"), "..foo"));
	CHECK(lands(QStringLiteral("/..."), "_"));
	CHECK(lands(QString{}, "file"));
	CHECK(lands(QStringLiteral("/"), "file"));
}

// A device listing more files than the tree holds: the first maxFiles are
// shown, in the device's order, and the model says the list was cut.
constexpr std::size_t manyFiles = DeviceFilesModel::maxFiles + 4;
std::array<std::array<char, 16>, manyFiles> manyNames{};
const resource::BytesFile tinyFile{settingsBytes};

template<std::size_t... I>
auto manyEntries(std::index_sequence<I...>)
{
	return resource::FileSystem<manyFiles>{std::array<resource::FileEntry, manyFiles>{
	    resource::file(std::string_view(manyNames[I].data()), tinyFile)...}};
}

void truncatedList()
{
	for (std::size_t i = 0; i < manyFiles; ++i)
		std::snprintf(manyNames[i].data(), manyNames[i].size(), "/many/f%04u", static_cast<unsigned>(i));
	static const auto many = manyEntries(std::make_index_sequence<manyFiles>{});
	Peer peer{many.view()};
	qt::ConnectionSession session;
	DeviceFilesModel files(&session);
	QElapsedTimer took;
	took.start();
	CHECK(session.connectTcpReference(QStringLiteral("127.0.0.1"), peer.listener.serverPort()));
	const bool listed = await(
	    [&] {
		    return files.available() && !files.loading() && files.fileCount() > 0;
	    },
	    300000);
	if (!listed)
		std::printf("The 4100-file list did not finish in %lld ms\n", static_cast<long long>(took.elapsed()));
	CHECK(listed);
	CHECK(files.truncated() && files.fileCount() == DeviceFilesModel::maxFiles && files.error().isEmpty());
	const auto folder = files.index(0, 0);
	CHECK(files.data(folder, DeviceFilesModel::FileCountRole).toInt() == DeviceFilesModel::maxFiles);
	const auto last = files.index(DeviceFilesModel::maxFiles - 1, 0, folder);
	CHECK(files.data(last, DeviceFilesModel::PathRole).toString() == QStringLiteral("/many/f4095"));
	CHECK(files.data(last, DeviceFilesModel::SizeKnownRole).toBool());
	std::printf("A list of %zu files cut at %d and examined in %lld ms\n", manyFiles,
	            DeviceFilesModel::maxFiles, static_cast<long long>(took.elapsed()));
	session.disconnect();
	CHECK(await([&] {
		return !files.available() && !files.truncated();
	}));
}

void filesOnly(const QString& folder)
{
	Peer peer{withoutDescriptor.view()};
	qt::ConnectionSession session;
	DeviceFilesModel files(&session);
	CHECK(!files.available());
	CHECK(session.connectTcpReference(QStringLiteral("127.0.0.1"), peer.listener.serverPort()));
	CHECK(await([&] {
		return files.available() && !files.loading() && files.fileCount() == 2;
	}));
	CHECK(session.filesOnly() && !session.ready());
	DeviceFileTransfers transfers(&session);
	const auto settings = files.indexOfPath(QStringLiteral("/cfg/settings.csv"));
	const auto local = DeviceFileTransfers::localPathFor(folder + QStringLiteral("/files-only"),
	                                                     QStringLiteral("/cfg/settings.csv"));
	const auto row = transfers.download(QStringLiteral("/cfg/settings.csv"),
	                                    files.data(settings, DeviceFilesModel::FileIndexRole).toInt(),
	                                    settingsBytes.size(), local);
	CHECK(ended(transfers, row) == DeviceFileTransfers::State::Saved && sameBytes(local, settingsBytes));
	session.disconnect();
	CHECK(await([&] {
		return !files.available() && files.fileCount() == 0;
	}));
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QLocale::setDefault(QLocale::c()); // Figures compared as text.
	for (std::size_t i = 0; i < eventsBytes.size(); ++i)
		eventsBytes[i] = std::byte('a' + i % 26);
	for (std::size_t i = 0; i < largeBytes.size(); ++i)
		largeBytes[i] = std::byte((i * 7) ^ (i >> 9));
	for (std::size_t i = 0; i < settingsBytes.size(); ++i)
		settingsBytes[i] = std::byte('0' + i % 10);
	QTemporaryDir folder;
	CHECK(folder.isValid());
	CHECK(DeviceFileTransfers::figures(98304, 131072, 43008, 1.2, true) ==
	      QStringLiteral("96 / 128 KiB · 42 KiB/s · about 5 s left"));
	CHECK(DeviceFileTransfers::figures(143360, 131072, 0, -1, false).contains(QStringLiteral("growing")));

	Peer peer{withDescriptor.view()};
	qt::ConnectionSession session;
	DeviceFilesModel files(&session);
	CHECK(!files.available());
	CHECK(session.connectTcpReference(QStringLiteral("127.0.0.1"), peer.listener.serverPort()));
	CHECK(await([&] {
		return files.available() && !files.loading() && files.fileCount() == 4;
	}));
	CHECK(session.ready());
	tree(session, files);
	downloads(session, files, folder.path());
	openInViewer(session, files, folder.path());
	destroyedMidway(session, files, folder.path());
	session.disconnect();
	CHECK(await([&] {
		return !files.available();
	}));
	filesOnly(folder.path());
	localPaths(folder.path());
	fileContent(folder.path());
	codecs(folder.path());
	truncatedList();
	std::printf("Device files UX: %u checks, 0 failures\n", checks);
	return 0;
}
