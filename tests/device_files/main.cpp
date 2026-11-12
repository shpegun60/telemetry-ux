// The Device files models over a real session and the native TCP server:
// the tree of the device's paths, marks and selection, downloads written
// exactly, cancel, a model destroyed mid-download, and a files-only device. MIT.
#include <Telemetry/UX/DeviceFileTransfers.hpp>
#include <Telemetry/UX/DeviceFilesModel.hpp>
#include <Telemetry/UX/DescriptorContent.hpp>
#include <Telemetry/UX/DeviceFileLive.hpp>
#include <Telemetry/UX/ValuesContent.hpp>
#include <Telemetry/UX/FileContent.hpp>
#include <Telemetry/UX/FileTextModels.hpp>
#include <Telemetry/UX/TelemetryCodecs.hpp>
#include <integration/tcp/Server.hpp>
#include <integration/telemetry/v3/IdentityFile.hpp>
#include <integration/telemetry/v3/ValuesFile.hpp>
#include <resource/BytesFile.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <wire/IdentityWire.hpp>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QLocale>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QEventLoop>
#include <algorithm>
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
	std::uint32_t value = 42;

	std::uint32_t read() noexcept
	{
		return value;
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
// A model with the shapes a value can take: 64 bits, a struct with an enum, an array.
enum class Mode : std::uint8_t {
	Idle,
	Run
};

struct Pair {
	float voltage;
	Mode mode;
};

struct Rich {
	std::uint64_t big = 18446744073709551615ull;
	Pair pair{230.5f, Mode::Run};
	std::array<std::int16_t, 3> samples{-1, 0, 2};

	std::uint64_t readBig() noexcept
	{
		return big;
	}

	Pair readPair() noexcept
	{
		return pair;
	}

	std::array<std::int16_t, 3> readSamples() noexcept
	{
		return samples;
	}
} rich;

constexpr telemetry::FieldTable richTable{telemetry::field<&Rich::readBig>("Big", rich),
                                          telemetry::field<&Rich::readPair>("Pair", rich),
                                          telemetry::field<&Rich::readSamples>("Samples", rich)};
constexpr telemetry::FieldCatalogTable richFields{telemetry::group("rich", richTable)};
constexpr telemetry::Model richModel{richFields, commands, services};
constexpr telemetry::descriptor::Descriptor richDescriptor{richModel};
constexpr auto richPacked = telemetry::descriptor::packDescriptor<richDescriptor>();
alignas(32) std::array<std::byte, richModel.maxScratch() + 1> richScratch{};
telemetry::Workspace richWorkspace{richScratch};
const resource::telemetry::v3::ValuesFile richValues{richDescriptor, richWorkspace};

// values.bin as the device serves it: every Field's value at each read.
alignas(32) std::array<std::byte, model.maxScratch() + 1> valuesScratch{};
telemetry::Workspace valuesWorkspace{valuesScratch};
const resource::telemetry::v3::ValuesFile valuesFile{descriptor, valuesWorkspace};
// Two units of the same firmware, told apart by their identities, and one with none.
resource::telemetry::v3::IdentityFile unitA, unitB;
const auto withValues = resource::filesystem(resource::file("/telemetry/descriptor.bin", descriptorFile),
                                             resource::file(telemetry::protocol::identity::path, unitA),
                                             resource::file("/telemetry/values.bin", valuesFile));
const auto otherUnit = resource::filesystem(resource::file("/telemetry/descriptor.bin", descriptorFile),
                                            resource::file(telemetry::protocol::identity::path, unitB),
                                            resource::file("/telemetry/values.bin", valuesFile));
const auto anonymousUnit = resource::filesystem(resource::file("/telemetry/descriptor.bin", descriptorFile),
                                                resource::file("/telemetry/values.bin", valuesFile));

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
	using telemetry::ux::FileCodecContext;
	using telemetry::ux::FileContent;
	using telemetry::ux::FileIdentity;
	using telemetry::ux::FileMatch;
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
		    return identity.head.startsWith("DEMO") ? FileMatch::Signature : FileMatch::No;
	    },
	    [&](const QByteArray& bytes, const FileCodecContext&, QString& error) -> std::unique_ptr<QObject> {
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

// The device's folder from its identity file - a files-only device has one
// too: "<name> [<id>]" with the name made a plain local name; the same folder
// renamed when the device is, unless a file in it is open; the id alone for a
// device without a name; the connection's label without an identity.
void identityFolders(const QString& folder)
{
	namespace id = telemetry::protocol::identity;
	std::array<std::byte, id::writtenBytes> bytes{};
	std::size_t size = 0;
	const auto serve = [&](std::string_view name) {
		size = id::write(id::Identity{"UNIT-7", name, "Bench", "2.0"}, bytes);
		CHECK(size > 0);
	};
	serve("Stand: A/B");
	const resource::BytesFile events{eventsBytes};
	// The device's side, written with the wire's own encoder: it borrows the
	// array and its size, and each serve() rewrites them in place.
	class IdentityProvider {
	public:
		IdentityProvider(const std::array<std::byte, id::writtenBytes>& bytes, const std::size_t& size) noexcept
		    : bytes_(bytes), size_(size)
		{}
		[[nodiscard]] resource::FileSize size() const noexcept { return static_cast<resource::FileSize>(size_); }
		[[nodiscard]] resource::ReadResult read(resource::Cursor cursor, resource::Output output) const noexcept
		{
			if (cursor > size_)
				return {resource::Status::InvalidCursor, cursor};
			const auto count = std::min<std::size_t>(output.size(), size_ - static_cast<std::size_t>(cursor));
			std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(cursor), count, output.begin());
			return {resource::Status::Ok, cursor + count, static_cast<std::uint32_t>(count), cursor + count == size_};
		}

	private:
		const std::array<std::byte, id::writtenBytes>& bytes_;
		const std::size_t& size_;
	};
	const IdentityProvider provider(bytes, size);
	const auto table = resource::filesystem(resource::file(id::path, provider), resource::file("/logs/events.csv", events));
	Peer peer{table.view()};
	qt::ConnectionSession session;
	DeviceFileTransfers transfers(&session);
	const auto root = folder + QStringLiteral("/identity");
	const auto connect = [&] {
		CHECK(session.connectTcpReference(QStringLiteral("127.0.0.1"), peer.listener.serverPort()));
		CHECK(await([&] {
			return session.identityState() == qt::ConnectionSession::IdentityState::Known;
		}));
		CHECK(session.filesOnly() && session.deviceId() == QStringLiteral("UNIT-7"));
	};
	const auto exists = [&](const char* name) {
		return QFileInfo(root + QLatin1Char('/') + QString::fromUtf8(name)).isDir();
	};

	connect();
	CHECK(transfers.deviceFolder(root, QStringLiteral("/Fallback")) == root + QStringLiteral("/Stand_ A_B [UNIT-7]"));
	CHECK(exists("Stand_ A_B [UNIT-7]"));
	session.disconnect();

	// Renamed on the device: the same folder under the new name.
	serve("Bench");
	connect();
	CHECK(transfers.deviceFolder(root, QStringLiteral("/Fallback")) == root + QStringLiteral("/Bench [UNIT-7]"));
	CHECK(exists("Bench [UNIT-7]") && !exists("Stand_ A_B [UNIT-7]"));
	session.disconnect();

	// A file of it open in a viewer keeps the folder where it is.
	const auto copy = root + QStringLiteral("/Bench [UNIT-7]/logs/events.csv");
	QDir().mkpath(QFileInfo(copy).path());
	{
		QFile file(copy);
		CHECK(file.open(QIODevice::WriteOnly) && file.write("a,b\n1,2\n") > 0);
	}
	telemetry::ux::OpenFiles viewer;
	viewer.open(copy, QStringLiteral("/logs/events.csv"));
	serve("Third");
	connect();
	CHECK(transfers.deviceFolder(root, QStringLiteral("/Fallback"), &viewer) == root + QStringLiteral("/Bench [UNIT-7]"));
	CHECK(!exists("Third [UNIT-7]"));
	viewer.close(0);
	CHECK(transfers.deviceFolder(root, QStringLiteral("/Fallback"), &viewer) == root + QStringLiteral("/Third [UNIT-7]"));
	CHECK(QFileInfo::exists(root + QStringLiteral("/Third [UNIT-7]/logs/events.csv")));
	session.disconnect();

	// No name: the id alone, still the same folder.
	serve("");
	connect();
	CHECK(transfers.deviceFolder(root, QStringLiteral("/Fallback")) == root + QStringLiteral("/UNIT-7"));
	CHECK(exists("UNIT-7") && !exists("Third [UNIT-7]"));
	session.disconnect();
	CHECK(await([&] {
		return session.identityState() == qt::ConnectionSession::IdentityState::Absent;
	}));

	// No identity: the connection's label.
	Peer plain{withoutDescriptor.view()};
	CHECK(session.connectTcpReference(QStringLiteral("127.0.0.1"), plain.listener.serverPort()));
	CHECK(await([&] {
		return session.filesOnly();
	}));
	CHECK(session.identityState() == qt::ConnectionSession::IdentityState::Absent);
	CHECK(transfers.deviceFolder(root, QStringLiteral("/TCP · 10.0.0.5:5577")) ==
	      root + QStringLiteral("/TCP · 10.0.0.5_5577"));
	session.disconnect();
}
} // namespace

// The Text and Table views' models: lines at every kind of line break, Find
// from the reader's line with its wrap words, Go to line; a table sorted by
// heading - words in natural order, numbers as numbers, empty cells last -
// filtered, searched and copied; and a text of 10 MB, timed.
void textAndTable()
{
	using telemetry::ux::FileTableModel;
	using telemetry::ux::TableRowsModel;
	using telemetry::ux::TextLinesModel;
	TextLinesModel lines;
	lines.setText(QStringLiteral("alpha\r\nbeta\rgamma\n"));
	CHECK(lines.lineCount() == 3 && lines.lineText(0) == QStringLiteral("alpha") &&
	      lines.lineText(1) == QStringLiteral("beta") && lines.lineText(2) == QStringLiteral("gamma") &&
	      lines.longestLine() == 5);
	CHECK(lines.data(lines.index(1), TextLinesModel::NumberRole).toInt() == 2);
	CHECK(lines.linesText(0, 1) == QStringLiteral("alpha\nbeta") && lines.linesText(-5, 99).count(QLatin1Char('\n')) == 2);
	CHECK(lines.lineFor(QStringLiteral("2")) == 1 && lines.lineFor(QStringLiteral(" end ")) == 2 &&
	      lines.lineFor(QStringLiteral("0")) == -1 && lines.lineFor(QStringLiteral("4")) == -1 &&
	      lines.lineFor(QStringLiteral("two")) == -1);
	// "a" ignoring case: alpha 2, beta 1, gamma 2.
	CHECK(lines.find(QStringLiteral("A"), false, true, 0).isEmpty() && lines.matchCount() == 5 &&
	      lines.matchIndex() == 1 && lines.matchLine() == 0);
	CHECK(lines.data(lines.index(2), TextLinesModel::MatchesRole).toList().size() == 2 &&
	      lines.data(lines.index(0), TextLinesModel::CurrentRole).toInt() == 0);
	for (int i = 0; i < 4; ++i)
		CHECK(lines.find(QStringLiteral("A"), false, true, 0).isEmpty());
	CHECK(lines.matchIndex() == 5 && lines.matchLine() == 2);
	CHECK(lines.find(QStringLiteral("A"), false, true, 0) == QStringLiteral("Wrapped to the start") && lines.matchIndex() == 1);
	CHECK(lines.find(QStringLiteral("A"), false, false, 0) == QStringLiteral("Wrapped to the end") && lines.matchIndex() == 5);
	// A new text starts from the reader's line: beta, from line 3, wraps.
	CHECK(lines.find(QStringLiteral("beta"), false, true, 2) == QStringLiteral("Wrapped to the start") &&
	      lines.matchLine() == 1);
	CHECK(lines.find(QStringLiteral("A"), true, true, 0) == QStringLiteral("Not found") && lines.matchCount() == 0);
	lines.clearFind();
	CHECK(lines.matchCount() == 0 && lines.data(lines.index(0), TextLinesModel::MatchesRole).toList().isEmpty());
	lines.setText(QString());
	CHECK(lines.lineCount() == 1 && lines.lineText(0).isEmpty());
	lines.setText(QStringLiteral("one\n"));
	CHECK(lines.lineCount() == 1);

	// A log of about 10 MB: split and searched at once.
	QString big;
	big.reserve(10 * 1024 * 1024);
	for (int i = 0; big.size() < 10 * 1024 * 1024; ++i)
		big += QStringLiteral("2026-11-12 12:00:%1 INFO sample %2 value=%3\n").arg(i % 60, 2, 10, QLatin1Char('0')).arg(i).arg(i * 7);
	QElapsedTimer timer;
	timer.start();
	lines.setText(big);
	const auto splitMs = timer.restart();
	CHECK(lines.find(QStringLiteral("sample 123456 "), false, true, 0).isEmpty() && lines.matchCount() == 1);
	const auto findMs = timer.elapsed();
	std::printf("A text of %lld bytes, %d lines: split in %lld ms, searched in %lld ms\n",
	            static_cast<long long>(big.size()), lines.lineCount(), static_cast<long long>(splitMs),
	            static_cast<long long>(findMs));
	CHECK(lines.lineCount() > 200000 && lines.matchLine() == 123456);

	FileTableModel table;
	CHECK(table.setText(QStringLiteral("name,value\nitem 10,5\nitem 2,10\nitem 1,\n\"a, b\",7\n")));
	TableRowsModel rows(&table);
	const auto column = [&](int c) {
		QStringList cells;
		for (int r = 0; r < rows.rowCount(); ++r)
			cells << rows.cellText(r, c);
		return cells.join(QLatin1Char('|'));
	};
	CHECK(rows.totalRows() == 4 && rows.shownRows() == 4 && rows.numeric(1) && !rows.numeric(0) &&
	      rows.heading(1) == QStringLiteral("value"));
	rows.sortBy(0);
	CHECK(rows.sortedColumn() == 0 && !rows.descending() && column(0) == QStringLiteral("a, b|item 1|item 2|item 10"));
	rows.sortBy(0);
	CHECK(rows.descending() && column(0) == QStringLiteral("item 10|item 2|item 1|a, b"));
	rows.sortBy(0);
	CHECK(rows.sortedColumn() == -1 && column(0) == QStringLiteral("item 10|item 2|item 1|a, b"));
	rows.sortBy(1);
	CHECK(column(1) == QStringLiteral("5|7|10|"));
	rows.sortBy(1);
	CHECK(column(1) == QStringLiteral("10|7|5|"));
	rows.sortBy(1);
	CHECK(rows.setFilter(QStringLiteral("ITEM 1")) == 2 && rows.shownRows() == 2 && rows.totalRows() == 4);
	CHECK(rows.setFilter(QString()) == 4);
	// Find: cells containing "1" - item 10, 10, item 1 - in shown order, wrapping.
	auto found = rows.find(QStringLiteral("1"), true, 0, 0);
	CHECK(found.value(QStringLiteral("row")).toInt() == 0 && found.value(QStringLiteral("column")).toInt() == 0 &&
	      rows.matchCount() == 3 && rows.matchIndex() == 1);
	found = rows.find(QStringLiteral("1"), true, 0, 0);
	CHECK(found.value(QStringLiteral("row")).toInt() == 1 && found.value(QStringLiteral("column")).toInt() == 1);
	found = rows.find(QStringLiteral("1"), true, 0, 0);
	CHECK(found.value(QStringLiteral("row")).toInt() == 2 && rows.matchIndex() == 3);
	CHECK(rows.find(QStringLiteral("1"), true, 0, 0).value(QStringLiteral("message")).toString() ==
	      QStringLiteral("Wrapped to the start"));
	CHECK(rows.find(QStringLiteral("zzz"), true, 0, 0).value(QStringLiteral("row")).toInt() == -1);
	// Copy: a row as TSV, everything shown as CSV with its quoting.
	CHECK(rows.rowText(0, QStringLiteral("tsv")) == QStringLiteral("item 10\t5"));
	CHECK(rows.shownText(QStringLiteral("csv")) == QStringLiteral("name,value\nitem 10,5\nitem 2,10\nitem 1,\n\"a, b\",7\n"));
	// Equal values keep the file's order, ascending and descending; by name, as the menu sorts.
	{
		FileTableModel ties;
		CHECK(ties.setText(QStringLiteral("k,v\na,1\nb,2\nc,1\n")));
		TableRowsModel tied(&ties);
		const auto keys = [&] {
			QStringList cells;
			for (int r = 0; r < tied.rowCount(); ++r)
				cells << tied.cellText(r, 0);
			return cells.join(QLatin1Char('|'));
		};
		tied.sortColumn(1, true);
		CHECK(keys() == QStringLiteral("b|a|c") && tied.descending() && tied.sortedColumn() == 1);
		tied.sortColumn(1, false);
		CHECK(keys() == QStringLiteral("a|c|b") && !tied.descending());
		tied.clearSort();
		CHECK(keys() == QStringLiteral("a|b|c") && tied.sortedColumn() == -1);
	}
	// A new file resets the order and the filter.
	rows.sortBy(0);
	rows.setFilter(QStringLiteral("item"));
	CHECK(table.setText(QStringLiteral("x\n1\n")) && rows.sortedColumn() == -1 && rows.shownRows() == 1);
}

// The descriptor codec, on the bytes of this test's own model: offered first
// for /telemetry/descriptor.bin and for a renamed copy (by its "TDS3"), its
// fingerprint, the header's fields and sections as regions, the endpoint tree,
// Find, and Copy as text, JSON and TSV. Something else under the descriptor's
// name says why and stays hex; a surer codec is tried before an earlier one.
void descriptorCodec(const QString& folder)
{
	using telemetry::ux::DescriptorContent;
	using telemetry::ux::FileCodec;
	using telemetry::ux::FileCodecContext;
	using telemetry::ux::FileIdentity;
	using telemetry::ux::FileMatch;
	const auto write = [&](const char* name, const QByteArray& bytes) {
		const auto path = folder + QLatin1Char('/') + QLatin1String(name);
		QFile file(path);
		CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size());
		return path;
	};
	const QByteArray bytes(reinterpret_cast<const char*>(packed.data()), static_cast<qsizetype>(packed.size()));
	telemetry::ux::OpenFiles open;
	telemetry::ux::installTelemetryCodecs(open);
	const auto local = write("descriptor.bin", bytes);
	open.open(local, QStringLiteral("/telemetry/descriptor.bin"));
	auto* content = open.contentFor(local);
	CHECK(content && content->codecName() == QStringLiteral("Descriptor") && content->format() == QStringLiteral("Descriptor"));
	auto* decoded = qobject_cast<DescriptorContent*>(content->decoded());
	CHECK(decoded && decoded->valid() && decoded->fieldCount() == 1);
	CHECK(decoded->fingerprint() ==
	      QStringLiteral("0x") + QStringLiteral("%1").arg(descriptor.fingerprint(), 16, 16, QLatin1Char('0')).toUpper());
	CHECK(decoded->fingerprintGroups().split(QLatin1Char(' ')).size() == 4);
	CHECK(content->codecView().toString().endsWith(QStringLiteral("DescriptorView.qml")));

	// The header's 17 fields under "header", and its sections.
	const auto regions = content->regions();
	int fields = 0;
	QStringList names;
	for (const auto& entry : regions) {
		const auto region = entry.toMap();
		names << region.value(QStringLiteral("name")).toString();
		if (region.value(QStringLiteral("depth")).toInt() == 1)
			++fields;
	}
	CHECK(fields == 17 && names.contains(QStringLiteral("fingerprint")) && names.contains(QStringLiteral("endpoints")));
	CHECK(regions.front().toMap().value(QStringLiteral("length")).toInt() == 64);

	// The tree: Fields › test › Value, with its type and access.
	auto* model = decoded->endpoints();
	CHECK(model->rowCount() == 1);
	const auto category = model->index(0, 0);
	CHECK(model->data(category).toString() == QStringLiteral("Fields"));
	const auto group = model->index(0, 0, category);
	const auto value = model->index(0, 0, group);
	CHECK(model->data(group).toString() == QStringLiteral("test") && model->data(value).toString() == QStringLiteral("Value"));
	CHECK(decoded->pathOf(value) == QStringLiteral("test.Value"));
	CHECK(decoded->rowOf(value) == QStringLiteral("Value\tu32\tread"));
	CHECK(decoded->textOfAll(QStringLiteral("tsv")).contains(QStringLiteral("test.Value\tendpoint\tu32\tread")));
	// An endpoint's JSON: its type and access by name, members (none here) apart.
	CHECK(decoded->textOf(value, QStringLiteral("json")).contains(QStringLiteral("\"access\": \"read\"")));
	CHECK(decoded->textOf(value, QStringLiteral("json")).contains(QStringLiteral("\"Value\"")) &&
	      decoded->textOf(value, QStringLiteral("json")).contains(QStringLiteral("\"u32\"")));
	CHECK(decoded->textOf(group, QStringLiteral("text")) == QStringLiteral("test\n  Value: u32 (read)\n"));

	// Find keeps the matches and their parents; nothing matching leaves nothing.
	// The tree under it is never rebuilt: a kept index follows its row through
	// a find that keeps it, and is gone - naming nothing - once it is hidden.
	const QPersistentModelIndex kept(value);
	CHECK(decoded->setFilter(QStringLiteral("val")) == 1 && model->rowCount() == 1);
	CHECK(kept.isValid() && decoded->pathOf(kept) == QStringLiteral("test.Value") &&
	      decoded->matches(kept) && !decoded->matches(model->index(0, 0)));
	CHECK(decoded->setFilter(QStringLiteral("nothing here")) == 0 && model->rowCount() == 0);
	CHECK(!kept.isValid() && decoded->textOf(kept, QStringLiteral("text")).isEmpty() &&
	      decoded->pathOf(kept).isEmpty() && decoded->textOfAll(QStringLiteral("text")).isEmpty());
	CHECK(decoded->setFilter(QString()) == 0 && model->rowCount() == 1);
	// A match inside a match counts once: the group, not also its endpoint.
	CHECK(decoded->setFilter(QStringLiteral("test")) == 1);
	CHECK(decoded->setFilter(QString()) == 0);
	// An index of another model names nothing here.
	CHECK(decoded->textOf(open.index(0, 0), QStringLiteral("text")).isEmpty() &&
	      decoded->rowOf(open.index(0, 0)).isEmpty());

	// A renamed copy is still a descriptor, by its signature.
	const auto renamed = write("copy.dat", bytes);
	open.open(renamed);
	CHECK(open.contentFor(renamed)->codecName() == QStringLiteral("Descriptor"));

	// Something else under its name: why not, and the file stays hex.
	const auto other = write("not-a-descriptor.bin", QByteArray(80, '\x07'));
	open.open(other, QStringLiteral("/telemetry/descriptor.bin"));
	auto* broken = open.contentFor(other);
	CHECK(broken && !broken->decoded() && broken->codecError().startsWith(QStringLiteral("Not a valid Descriptor file:")) &&
	      broken->format() == QStringLiteral("Binary"));

	// The surer match first: a later codec that knows the signature beats an
	// earlier one that only knows the name.
	telemetry::ux::OpenFiles ranked;
	int byName = 0, bySignature = 0;
	ranked.addCodec(FileCodec{QStringLiteral("ByName"),
	                          [](const FileIdentity& identity) {
		                          return identity.devicePath.endsWith(QStringLiteral(".x")) ? FileMatch::Path : FileMatch::No;
	                          },
	                          [&](const QByteArray&, const FileCodecContext&, QString&) -> std::unique_ptr<QObject> {
		                          ++byName;
		                          return std::make_unique<QObject>();
	                          },
	                          {}});
	ranked.addCodec(FileCodec{QStringLiteral("BySignature"),
	                          [](const FileIdentity& identity) {
		                          return identity.head.startsWith("SIG") ? FileMatch::Signature : FileMatch::No;
	                          },
	                          [&](const QByteArray&, const FileCodecContext&, QString&) -> std::unique_ptr<QObject> {
		                          ++bySignature;
		                          return std::make_unique<QObject>();
	                          },
	                          {}});
	const auto both = write("both.x", "SIG and a name");
	ranked.open(both, QStringLiteral("/data/both.x"));
	CHECK(ranked.contentFor(both)->codecName() == QStringLiteral("BySignature") && bySignature == 1 && byName == 0);
}

namespace {
// The values file as a host reads it: whole, in pieces.
QByteArray valuesBytes()
{
	QByteArray bytes;
	std::array<std::byte, 64> piece{};
	resource::Cursor cursor = 0;
	for (int guard = 0; guard < 100; ++guard) {
		const auto result = valuesFile.read(cursor, piece);
		CHECK(result.status == resource::Status::Ok);
		bytes.append(reinterpret_cast<const char*>(piece.data()), static_cast<qsizetype>(result.written));
		cursor = result.next;
		if (result.eof)
			return bytes;
	}
	CHECK(false);
	return bytes;
}

QByteArray descriptorBytes()
{
	return QByteArray(reinterpret_cast<const char*>(packed.data()), static_cast<qsizetype>(packed.size()));
}

// The first Field's shown row: group "test", Field "Value".
QModelIndex valueRow(QAbstractItemModel& fields)
{
	return fields.index(0, 0, fields.index(0, 0));
}
} // namespace

// The values codec: values.bin read against the descriptor it names - the one
// beside it, the context's, or one chosen by hand, only with its fingerprint -
// its tree with values, the regions of every token, a newer snapshot marking
// what changed, Hold, Find and Copy; a broken file says why and stays hex.
void valuesCodec(const QString& folder)
{
	using telemetry::ux::FileCodecContext;
	using telemetry::ux::ValuesContent;
	using telemetry::ux::ValuesTreeModel;
	const auto write = [&](const QString& name, const QByteArray& bytes) {
		const auto path = folder + QLatin1Char('/') + name;
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile file(path);
		CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size());
		return path;
	};
	device.value = 42;
	const auto bytes = valuesBytes();
	CHECK(bytes.startsWith("TVL3") && bytes.size() == 24 + 1 + 4);

	// Beside its descriptor, as a device's folder keeps them.
	write(QStringLiteral("device-a/telemetry/descriptor.bin"), descriptorBytes());
	const auto local = write(QStringLiteral("device-a/telemetry/values.bin"), bytes);
	telemetry::ux::OpenFiles open;
	telemetry::ux::installTelemetryCodecs(open);
	open.open(local, QStringLiteral("/telemetry/values.bin"));
	auto* content = open.contentFor(local);
	CHECK(content && content->codecName() == QStringLiteral("Values") && content->format() == QStringLiteral("Values"));
	CHECK(content->codecView().toString().endsWith(QStringLiteral("ValuesView.qml")));
	auto* values = qobject_cast<ValuesContent*>(content->decoded());
	CHECK(values && values->valid() && !values->needsDescriptor() && values->error().isEmpty());
	CHECK(values->fingerprint() == descriptor.fingerprint() && values->fingerprintGroups().split(QLatin1Char(' ')).size() == 4);
	CHECK(values->summary() == QStringLiteral("1 Field in 1 group") && values->snapshots() == 1);
	// The header's six fields, and the Field's token with its status and value.
	QStringList paths;
	for (const auto& entry : values->regions())
		paths << entry.toMap().value(QStringLiteral("path")).toString();
	CHECK(paths.contains(QStringLiteral("header › fingerprint")) && paths.contains(QStringLiteral("test.Value")) &&
	      paths.contains(QStringLiteral("test.Value › status")) && paths.contains(QStringLiteral("test.Value › value")));

	auto* fields = values->fields();
	CHECK(fields->rowCount() == 1 && fields->data(fields->index(0, 0)).toString() == QStringLiteral("test"));
	const QPersistentModelIndex row(valueRow(*fields));
	CHECK(fields->data(row, ValuesTreeModel::ValueRole).toString() == QStringLiteral("42") &&
	      fields->data(row, ValuesTreeModel::TypeRole).toString() == QStringLiteral("u32") &&
	      !fields->data(row, ValuesTreeModel::ChangedRole).toBool());
	CHECK(values->pathOf(row) == QStringLiteral("test.Value") && values->valueOf(row) == QStringLiteral("42") &&
	      values->rowOf(row) == QStringLiteral("Value\t42\tu32"));
	CHECK(values->textOf(row, QStringLiteral("text")) == QStringLiteral("Value = 42\n"));
	CHECK(values->textOfAll(QStringLiteral("tsv")) == QStringLiteral("path\tvalue\ttype\ntest.Value\t42\tu32\n"));
	CHECK(values->textOfAll(QStringLiteral("json")).contains(QStringLiteral("\"Value\": 42")));
	CHECK(values->valueOf(fields->index(0, 0)).isEmpty() && values->textOf(QModelIndex(), QStringLiteral("text")).isEmpty());

	// A newer snapshot: the value in place, marked as changed; the same again unmarks it.
	device.value = 43;
	CHECK(values->update(valuesBytes()).isEmpty());
	CHECK(row.isValid() && fields->data(row, ValuesTreeModel::ValueRole).toString() == QStringLiteral("43") &&
	      fields->data(row, ValuesTreeModel::ChangedRole).toBool() && values->changedCount() == 1 &&
	      values->snapshots() == 2);
	CHECK(values->update(valuesBytes()).isEmpty());
	CHECK(!fields->data(row, ValuesTreeModel::ChangedRole).toBool() && values->changedCount() == 0);
	// Held: newer snapshots wait and the shown values stay; released, the newest shows.
	values->setHeld(true);
	device.value = 44;
	CHECK(values->update(valuesBytes()).isEmpty());
	device.value = 45;
	CHECK(values->update(valuesBytes()).isEmpty());
	CHECK(values->waiting() == 2 && fields->data(row, ValuesTreeModel::ValueRole).toString() == QStringLiteral("43"));
	values->setHeld(false);
	CHECK(values->waiting() == 0 && fields->data(row, ValuesTreeModel::ValueRole).toString() == QStringLiteral("45") &&
	      fields->data(row, ValuesTreeModel::ChangedRole).toBool());
	// Something that is not a snapshot of it: refused, the values stay.
	auto broken = valuesBytes();
	broken[24] = 7;
	CHECK(!values->update(broken).isEmpty() && fields->data(row, ValuesTreeModel::ValueRole).toString() == QStringLiteral("45"));
	CHECK(!values->update(descriptorBytes()).isEmpty());
	// Find: the Field by name; nothing matching shows nothing; the row comes back.
	CHECK(values->setFilter(QStringLiteral("val")) == 1 && fields->rowCount() == 1 && row.isValid() &&
	      values->matches(row));
	CHECK(values->setFilter(QStringLiteral("nothing here")) == 0 && fields->rowCount() == 0 && !row.isValid() &&
	      values->textOfAll(QStringLiteral("text")).isEmpty());
	CHECK(values->setFilter(QString()) == 0 && fields->rowCount() == 1);
	CHECK(values->textOf(open.index(0, 0), QStringLiteral("text")).isEmpty());

	// Alone, with no descriptor at hand: it waits for one and says which.
	device.value = 42;
	const auto alone = write(QStringLiteral("loose/values.bin"), bytes);
	open.open(alone);
	auto* waiting = qobject_cast<ValuesContent*>(open.contentFor(alone)->decoded());
	CHECK(waiting && !waiting->valid() && waiting->needsDescriptor() && waiting->fingerprint() == descriptor.fingerprint());
	CHECK(waiting->fields()->rowCount() == 0 && waiting->textOfAll(QStringLiteral("text")).isEmpty());
	// Only a descriptor with its fingerprint is taken.
	static constexpr telemetry::FieldTable otherFields{telemetry::field<&Device::read>("Other", device)};
	static constexpr telemetry::FieldCatalogTable otherCatalogs{telemetry::group("test", otherFields)};
	static constexpr telemetry::Model otherModel{otherCatalogs, commands, services};
	static constexpr telemetry::descriptor::Descriptor otherDescriptor{otherModel};
	static constexpr auto otherPacked = telemetry::descriptor::packDescriptor<otherDescriptor>();
	const auto other = write(QStringLiteral("loose/other.bin"),
	                         QByteArray(reinterpret_cast<const char*>(otherPacked.data()), static_cast<qsizetype>(otherPacked.size())));
	CHECK(waiting->useDescriptor(QUrl::fromLocalFile(other)).contains(QStringLiteral("fingerprint")) &&
	      !waiting->valid() && waiting->needsDescriptor());
	CHECK(!waiting->useDescriptor(QUrl::fromLocalFile(folder + QStringLiteral("/loose/missing.bin"))).isEmpty());
	const auto chosen = write(QStringLiteral("loose/descriptor-copy.bin"), descriptorBytes());
	CHECK(waiting->useDescriptor(QUrl::fromLocalFile(chosen)).isEmpty() && waiting->valid() &&
	      !waiting->needsDescriptor() && waiting->fields()->rowCount() == 1 &&
	      waiting->valueOf(valueRow(*waiting->fields())) == QStringLiteral("42"));

	// The context's descriptor: none at first, then a device is admitted.
	bool known = false;
	telemetry::ux::OpenFiles later;
	telemetry::ux::installTelemetryCodecs(later);
	FileCodecContext context;
	context.descriptor = [&known](quint64 fingerprint) {
		return known && fingerprint == descriptor.fingerprint() ? descriptorBytes() : QByteArray{};
	};
	later.setContext(context);
	later.open(alone);
	CHECK(qobject_cast<ValuesContent*>(later.contentFor(alone)->decoded())->needsDescriptor());
	known = true;
	later.contextChanged();
	auto* found = qobject_cast<ValuesContent*>(later.contentFor(alone)->decoded());
	CHECK(found && found->valid() && found->valueOf(valueRow(*found->fields())) == QStringLiteral("42"));

	// Not a values file under its name: why, and the file stays hex.
	auto wrongVersion = bytes;
	wrongVersion[4] = 9;
	const auto wrong = write(QStringLiteral("loose/values-wrong.bin"), wrongVersion);
	open.open(wrong, QStringLiteral("/telemetry/values.bin"));
	auto* refused = open.contentFor(wrong);
	CHECK(refused && !refused->decoded() && refused->codecError().startsWith(QStringLiteral("Not a valid Values file:")) &&
	      refused->format() == QStringLiteral("Binary"));
}

// How values read, as the designer set them: a 64-bit number whole (and as
// text in JSON, where a number would lose digits), an enum by name with its
// code (the code in JSON), a struct summarised on its row, an array on its
// row and element by element, numbers marked for right alignment.
void valuesFormatting()
{
	using telemetry::ux::ValuesContent;
	using telemetry::ux::ValuesTreeModel;
	QByteArray bytes;
	std::array<std::byte, 64> piece{};
	resource::Cursor cursor = 0;
	for (int guard = 0; guard < 100; ++guard) {
		const auto result = richValues.read(cursor, piece);
		CHECK(result.status == resource::Status::Ok);
		bytes.append(reinterpret_cast<const char*>(piece.data()), static_cast<qsizetype>(result.written));
		cursor = result.next;
		if (result.eof)
			break;
	}
	ValuesContent values(bytes, QByteArray(reinterpret_cast<const char*>(richPacked.data()),
	                                       static_cast<qsizetype>(richPacked.size())));
	CHECK(values.valid() && values.summary() == QStringLiteral("3 Fields in 1 group"));
	auto* fields = values.fields();
	const auto group = fields->index(0, 0);
	const auto big = fields->index(0, 0, group);
	const auto pair = fields->index(1, 0, group);
	const auto samples = fields->index(2, 0, group);
	const auto value = [&](const QModelIndex& index) { return fields->data(index, ValuesTreeModel::ValueRole).toString(); };
	const auto numeric = [&](const QModelIndex& index) { return fields->data(index, ValuesTreeModel::NumericRole).toBool(); };
	CHECK(value(big) == QStringLiteral("18446744073709551615") && numeric(big));
	const auto voltage = fields->index(0, 0, pair);
	const auto mode = fields->index(1, 0, pair);
	CHECK(value(voltage) == QStringLiteral("230.5") && numeric(voltage));
	CHECK(value(mode) == QStringLiteral("Run (1)") && !numeric(mode));
	CHECK(value(pair) == QStringLiteral("{voltage 230.5, mode Run (1)}") && !numeric(pair));
	CHECK(value(samples) == QStringLiteral("[-1, 0, 2]") && fields->rowCount(samples) == 3 &&
	      value(fields->index(0, 0, samples)) == QStringLiteral("-1"));
	CHECK(values.textOf(samples, QStringLiteral("text")) == QStringLiteral("Samples = [-1, 0, 2]\n"));
	const auto json = values.textOfAll(QStringLiteral("json"));
	CHECK(json.contains(QStringLiteral("\"Big\": \"18446744073709551615\"")) &&
	      json.contains(QStringLiteral("\"mode\": 1")) && json.contains(QStringLiteral("\"voltage\": 230.5")));
	CHECK(values.textOfAll(QStringLiteral("tsv")).contains(QStringLiteral("rich.Pair.mode\tRun (1)\t")));
}

// Live against a real session: the device's values.bin read again and again
// into memory - each read whole, never two at once, starts at least the
// period apart and never below 200 ms - the device's changes arriving; a
// lost connection waits and says so, and the reads go on when it is back. The
// viewer reaches it through connectTelemetryDevice, which also reads values
// against the connected device's descriptor.
void liveValues(const QString& folder)
{
	using telemetry::ux::DeviceFileLive;
	using telemetry::ux::ValuesContent;
	device.value = 7;
	Peer peer{withValues.view()};
	qt::ConnectionSession session;
	DeviceFilesModel files(&session);
	CHECK(unitA.set("UNIT-A", "Bench A") && unitB.set("UNIT-B", "Bench B"));
	const auto connectTo = [&](Peer& to) {
		CHECK(session.connectTcpReference(QStringLiteral("127.0.0.1"), to.listener.serverPort()));
		CHECK(await([&] {
			return session.ready() && files.available() && !files.loading() &&
			       session.identityState() != qt::ConnectionSession::IdentityState::Reading;
		}));
	};
	connectTo(peer);
	CHECK(files.fileCount() == 3 && session.deviceId() == QStringLiteral("UNIT-A"));

	DeviceFileLive live(&session, &files, QStringLiteral("/telemetry/values.bin"));
	live.setPeriodMs(50);
	CHECK(live.periodMs() == DeviceFileLive::minimumPeriodMs && !live.running() && live.reads() == 0);
	std::vector<qint64> arrived;
	std::vector<QByteArray> snapshots;
	QElapsedTimer clock;
	clock.start();
	int overlapping = 0;
	QObject::connect(&live, &DeviceFileLive::snapshot, &live, [&](const QByteArray& bytes) {
		arrived.push_back(clock.elapsed());
		snapshots.push_back(bytes);
		overlapping += live.reading() ? 1 : 0;
		++device.value;
	});
	live.setRunning(true);
	const bool fiveReads = await([&] { return snapshots.size() >= 5; }, 10000);
	if (!fiveReads)
		std::fprintf(stderr, "live: %d reads, reading %d, problem '%s'\n", live.reads(), int(live.reading()),
		             qPrintable(live.problem()));
	CHECK(fiveReads);
	live.setRunning(false);
	CHECK(await([&] { return !live.reading(); }));
	const auto count = snapshots.size();
	// Every read whole, in turn, the device's newest value each time.
	ValuesContent values(snapshots.front(), descriptorBytes());
	CHECK(values.valid() && values.valueOf(valueRow(*values.fields())) == QStringLiteral("7"));
	for (std::size_t i = 1; i < count; ++i) {
		CHECK(values.update(snapshots[i]).isEmpty());
		CHECK(values.valueOf(valueRow(*values.fields())) == QString::number(7 + i));
	}
	// A read ends before the next begins, and reads start the period apart; a
	// read's own length varies by a few milliseconds, hence the allowance.
	CHECK(overlapping == 0);
	for (std::size_t i = 1; i < arrived.size(); ++i)
		CHECK(arrived[i] - arrived[i - 1] >= DeviceFileLive::minimumPeriodMs - 5);
	CHECK(live.reads() == static_cast<int>(count) && live.problem().isEmpty());
	// Stopped: no more reads.
	QElapsedTimer quiet;
	quiet.start();
	await([&] { return quiet.elapsed() > 600; }, 700);
	CHECK(snapshots.size() == count);

	// Off and on again at once, right after a read: the next read still starts
	// the period after the last one did.
	live.setRunning(true);
	CHECK(await([&] { return snapshots.size() > count; }));
	const auto restartFrom = snapshots.size();
	live.setRunning(false);
	live.setRunning(true);
	CHECK(await([&] { return snapshots.size() > restartFrom; }));
	CHECK(arrived[restartFrom] - arrived[restartFrom - 1] >= DeviceFileLive::minimumPeriodMs - 5);

	// The connection drops while running: it waits and says so; the same unit
	// back, it reads again.
	session.disconnect();
	CHECK(await([&] { return !live.reading() && live.waitingForDevice() && live.problem() == QStringLiteral("not connected"); }));
	const auto beforeReconnect = snapshots.size();
	connectTo(peer);
	CHECK(await([&] { return snapshots.size() > beforeReconnect + 1; }, 10000) && live.problem().isEmpty() &&
	      !live.waitingForDevice() && live.running());

	// Another unit of the same firmware connected in its place: Live stops and
	// names it, rather than show its values as this file's.
	Peer other{otherUnit.view()};
	session.disconnect();
	CHECK(await([&] { return !live.reading() && live.waitingForDevice(); }));
	const auto beforeOther = snapshots.size();
	connectTo(other);
	CHECK(await([&] { return !live.running(); }) && live.problem().contains(QStringLiteral("UNIT-B")) &&
	      live.problem().contains(QStringLiteral("UNIT-A")) && snapshots.size() == beforeOther);
	session.disconnect();
	CHECK(await([&] { return !live.reading(); }));

	// A unit without an identity cannot be told apart after the connection was
	// lost: Live stops instead of resuming.
	{
		Peer anonymous{anonymousUnit.view()};
		connectTo(anonymous);
		DeviceFileLive unnamed(&session, &files, QStringLiteral("/telemetry/values.bin"));
		int unnamedReads = 0;
		QObject::connect(&unnamed, &DeviceFileLive::snapshot, &unnamed, [&](const QByteArray&) { ++unnamedReads; });
		unnamed.setRunning(true);
		CHECK(await([&] { return unnamedReads >= 1; }));
		session.disconnect();
		CHECK(await([&] { return !unnamed.reading() && unnamed.waitingForDevice(); }));
		connectTo(anonymous);
		CHECK(await([&] { return !unnamed.running(); }) && unnamed.problem().contains(QStringLiteral("no identity")));
		session.disconnect();
		CHECK(await([&] { return !files.available(); }));
	}

	// A reader that goes while its read is on the way: the session finishes the
	// read, and the connection goes on working.
	connectTo(peer);
	{
		auto doomed = std::make_unique<DeviceFileLive>(&session, &files, QStringLiteral("/telemetry/values.bin"));
		doomed->setRunning(true);
		CHECK(doomed->reading());
		doomed.reset();
	}
	CHECK(snapshots.size() >= beforeOther);
	live.setRunning(true);
	const auto beforeAfterOrphan = snapshots.size();
	CHECK(await([&] { return snapshots.size() > beforeAfterOrphan; }) && live.problem().isEmpty());
	live.setRunning(false);
	CHECK(await([&] { return !live.reading(); }));

	// Through the viewer: the descriptor is the connected device's (none on
	// disk beside the file), and the content's live reader reads its path.
	const auto local = folder + QStringLiteral("/live/values.bin");
	QDir().mkpath(QFileInfo(local).absolutePath());
	{
		QFile file(local);
		CHECK(file.open(QIODevice::WriteOnly) && file.write(snapshots.back()) == snapshots.back().size());
	}
	telemetry::ux::OpenFiles open;
	telemetry::ux::installTelemetryCodecs(open);
	telemetry::ux::connectTelemetryDevice(open, &session, &files, [] { return QStringList{}; });
	open.open(local, QStringLiteral("/telemetry/values.bin"));
	auto* content = open.contentFor(local);
	auto* shown = qobject_cast<ValuesContent*>(content->decoded());
	CHECK(shown && shown->valid());
	auto* reader = qobject_cast<DeviceFileLive*>(content->live());
	CHECK(reader && reader->devicePath() == QStringLiteral("/telemetry/values.bin") && content->live() == reader);
	int fromViewer = 0;
	QObject::connect(reader, &DeviceFileLive::snapshot, reader, [&](const QByteArray& bytes) {
		fromViewer += shown->update(bytes).isEmpty() ? 1 : 0;
	});
	reader->setRunning(true);
	CHECK(await([&] { return fromViewer >= 2; }));
	reader->setRunning(false);
	CHECK(await([&] { return !reader->reading(); }));
	// A file from disk has nothing to read again.
	open.open(folder + QStringLiteral("/loose/values.bin"));
	CHECK(open.contentFor(folder + QStringLiteral("/loose/values.bin"))->live() == nullptr);
	session.disconnect();
	device.value = 42;
}

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
	identityFolders(folder.path());
	localPaths(folder.path());
	fileContent(folder.path());
	codecs(folder.path());
	textAndTable();
	descriptorCodec(folder.path());
	valuesCodec(folder.path());
	valuesFormatting();
	liveValues(folder.path());
	truncatedList();
	std::printf("Device files UX: %u checks, 0 failures\n", checks);
	return 0;
}
