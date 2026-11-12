// The Device files models over a real session and the native TCP server:
// the tree of the device's paths, marks and selection, downloads written
// exactly, cancel, a model destroyed mid-download, and a files-only device. MIT.
#include <Telemetry/UX/DeviceFileTransfers.hpp>
#include <Telemetry/UX/DeviceFilesModel.hpp>
#include <Telemetry/UX/DescriptorContent.hpp>
#include <Telemetry/UX/DeviceFileLive.hpp>
#include <Telemetry/UX/ValuesContent.hpp>
#include <Telemetry/UX/CapabilityContent.hpp>
#include <Telemetry/UX/JsonContent.hpp>
#include <Telemetry/UX/FileContent.hpp>
#include <Telemetry/UX/FileTextModels.hpp>
#include <Telemetry/UX/TelemetryCodecs.hpp>
#include <integration/tcp/Server.hpp>
#include <integration/telemetry/v3/IdentityFile.hpp>
#include <integration/telemetry/v3/ValuesFile.hpp>
#include <resource/BytesFile.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <wire/BatchWire.hpp>
#include <wire/IdentityWire.hpp>
#include <wire/ReadManyWire.hpp>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <vector>
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

// Names "w000", "w001", ... for a table made from a pack: each one a whole array.
template<std::size_t Count>
struct Names {
	char text[Count][5]{};

	constexpr explicit Names(char prefix)
	{
		for (std::size_t i = 0; i < Count; ++i) {
			text[i][0] = prefix;
			text[i][1] = static_cast<char>('0' + i / 100 % 10);
			text[i][2] = static_cast<char>('0' + i / 10 % 10);
			text[i][3] = static_cast<char>('0' + i % 10);
		}
	}
};

// Arrays longer than a step of the tree, of numbers and of structs.
struct Wide {
	std::array<std::uint16_t, 600> counts{};
	std::array<Pair, 300> pairs{};

	std::array<std::uint16_t, 600> readCounts() noexcept
	{
		return counts;
	}

	std::array<Pair, 300> readPairs() noexcept
	{
		return pairs;
	}
} wide;

constexpr telemetry::FieldTable wideTable{telemetry::field<&Wide::readCounts>("Counts", wide),
                                          telemetry::field<&Wide::readPairs>("Pairs", wide)};
constexpr telemetry::FieldCatalogTable wideFields{telemetry::group("wide", wideTable)};
constexpr telemetry::Model wideModel{wideFields, commands, services};
constexpr telemetry::descriptor::Descriptor wideDescriptor{wideModel};
constexpr auto widePacked = telemetry::descriptor::packDescriptor<wideDescriptor>();
alignas(32) std::array<std::byte, wideModel.maxScratch() + 1> wideScratch{};
telemetry::Workspace wideWorkspace{wideScratch};
const resource::telemetry::v3::ValuesFile wideValues{wideDescriptor, wideWorkspace};

// The most elements an array may have (Limits::maxArrayElements): 256 ranges.
struct Long {
	std::array<std::uint8_t, 65536> bytes{};

	std::array<std::uint8_t, 65536> readBytes() noexcept
	{
		return bytes;
	}
} longest;

constexpr telemetry::FieldTable longTable{telemetry::field<&Long::readBytes>("Bytes", longest)};
constexpr telemetry::FieldCatalogTable longFields{telemetry::group("long", longTable)};
constexpr telemetry::Model longModel{longFields, commands, services};
constexpr telemetry::descriptor::Descriptor longDescriptor{longModel};
constexpr auto longPacked = telemetry::descriptor::packDescriptor<longDescriptor>();
alignas(32) std::array<std::byte, longModel.maxScratch() + 1> longScratch{};
telemetry::Workspace longWorkspace{longScratch};
const resource::telemetry::v3::ValuesFile longValues{longDescriptor, longWorkspace};

// Types that nest seven deep, four members at each level: 54 612 rows below
// each of twenty endpoints, had the tree made them all at once.
struct DeepLeaf {
	float x, y;
};
struct Deep6 {
	DeepLeaf a, b, c, d;
};
struct Deep5 {
	Deep6 a, b, c, d;
};
struct Deep4 {
	Deep5 a, b, c, d;
};
struct Deep3 {
	Deep4 a, b, c, d;
};
struct Deep2 {
	Deep3 a, b, c, d;
};
struct Deep1 {
	Deep2 a, b, c, d;
};
struct Deep0 {
	Deep1 a, b, c, d;
};

// Never read: only its type goes into the descriptor.
Deep0 readDeep() noexcept
{
	return {};
}

constexpr Names<20> deepNames{'d'};
template<std::size_t... I>
constexpr auto deepTable(std::index_sequence<I...>)
{
	return telemetry::FieldTable{telemetry::field<&readDeep>(deepNames.text[I])...};
}
constexpr auto deepFields = deepTable(std::make_index_sequence<20>{});
constexpr telemetry::FieldCatalogTable deepCatalogs{telemetry::group("deep", deepFields)};
constexpr telemetry::Model deepModel{deepCatalogs, commands, services};
constexpr telemetry::descriptor::Descriptor deepDescriptor{deepModel};
constexpr auto deepPacked = telemetry::descriptor::packDescriptor<deepDescriptor>();

// Ten megabytes of values: 160 Fields of 16 000 floats. Each token stays
// under a READ's 65 535 bytes, as a device that serves the file must keep it.
constexpr std::size_t waveCount = 160;
constexpr std::size_t waveLength = 16000;
std::array<std::array<float, waveLength>, waveCount> waves{};

template<std::size_t I>
std::array<float, waveLength> readWave() noexcept
{
	return waves[I];
}

constexpr Names<waveCount> waveNames{'w'};
template<std::size_t... I>
constexpr auto waveTable(std::index_sequence<I...>)
{
	return telemetry::FieldTable{telemetry::field<&readWave<I>>(waveNames.text[I])...};
}
constexpr auto waveFields = waveTable(std::make_index_sequence<waveCount>{});
constexpr telemetry::FieldCatalogTable waveCatalogs{telemetry::group("waves", waveFields)};
constexpr telemetry::Model waveModel{waveCatalogs, commands, services};
constexpr telemetry::descriptor::Descriptor waveDescriptor{waveModel};
constexpr auto wavePacked = telemetry::descriptor::packDescriptor<waveDescriptor>();
alignas(32) std::array<std::byte, waveModel.maxScratch() + 1> waveScratch{};
telemetry::Workspace waveWorkspace{waveScratch};
const resource::telemetry::v3::ValuesFile waveValues{waveDescriptor, waveWorkspace};

// A whole values file, as a host reads it in pieces of a READ's size (or
// larger, for a token no READ could carry).
template<class File>
QByteArray readWhole(const File& file, std::size_t pieceBytes = 65535)
{
	QByteArray bytes;
	std::vector<std::byte> piece(pieceBytes);
	resource::Cursor cursor = 0;
	for (int guard = 0; guard < 100000; ++guard) {
		const auto result = file.read(cursor, piece);
		CHECK(result.status == resource::Status::Ok);
		bytes.append(reinterpret_cast<const char*>(piece.data()), static_cast<qsizetype>(result.written));
		cursor = result.next;
		if (result.eof)
			return bytes;
	}
	CHECK(false);
	return bytes;
}

template<std::size_t N>
QByteArray bytesOf(const std::array<std::byte, N>& packedDescriptor)
{
	return QByteArray(reinterpret_cast<const char*>(packedDescriptor.data()), static_cast<qsizetype>(N));
}

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
		CHECK(lines.find(QStringLiteral("A"), false, true, lines.matchLine()).isEmpty());
	CHECK(lines.matchIndex() == 5 && lines.matchLine() == 2);
	CHECK(lines.find(QStringLiteral("A"), false, true, lines.matchLine()) == QStringLiteral("Wrapped to the start") &&
	      lines.matchIndex() == 1);
	CHECK(lines.find(QStringLiteral("A"), false, false, lines.matchLine()) == QStringLiteral("Wrapped to the end") &&
	      lines.matchIndex() == 5);
	// The reader moved to another line: Next goes on from there, not from the match.
	CHECK(lines.find(QStringLiteral("A"), false, true, 1).isEmpty() && lines.matchLine() == 1);
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

	// Past the cap: every line holds a match, more than are counted; Next from
	// a line far past the cap finds it there, without wrapping, and says
	// nothing of its number among the matches.
	{
		QString many;
		for (int i = 0; i < 150000; ++i)
			many += QStringLiteral("x\n");
		TextLinesModel crowded;
		crowded.setText(many);
		CHECK(crowded.find(QStringLiteral("x"), false, true, 0).isEmpty() && crowded.matchesCapped() &&
		      crowded.matchCount() == TextLinesModel::maxMatches);
		CHECK(crowded.find(QStringLiteral("x"), false, true, 149000).isEmpty() && crowded.matchLine() == 149000 &&
		      crowded.matchIndex() == 0);
		CHECK(crowded.find(QStringLiteral("x"), false, true, crowded.matchLine()).isEmpty() &&
		      crowded.matchLine() == 149001);
		CHECK(crowded.data(crowded.index(149500), TextLinesModel::MatchesRole).toList().size() == 1);
	}
	// A line of 30000 characters shows its first 10000 and says how many more;
	// Copy takes it whole.
	{
		TextLinesModel wide;
		wide.setText(QString(30000, QLatin1Char('a')) + QStringLiteral("\nshort\tline\n"));
		CHECK(wide.data(wide.index(0), TextLinesModel::TextRole).toString().size() == TextLinesModel::maxShownChars &&
		      wide.data(wide.index(0), TextLinesModel::HiddenRole).toInt() == 20000 &&
		      wide.lineText(0).size() == 30000 && wide.longestLine() == TextLinesModel::maxShownChars);
		// A tab reaches the next multiple of eight columns.
		TextLinesModel tabbed;
		tabbed.setText(QStringLiteral("a\tb\n"));
		CHECK(tabbed.longestLine() == 9);
		// No more marks drawn on a line than the cap.
		CHECK(wide.find(QStringLiteral("a"), true, true, 0).isEmpty() &&
		      wide.data(wide.index(0), TextLinesModel::MatchesRole).toList().size() == TextLinesModel::maxDrawnPerLine);
	}

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
	const auto next = [&] {
		return rows.find(QStringLiteral("1"), true, found.value(QStringLiteral("row")).toInt(),
		                 found.value(QStringLiteral("column")).toInt());
	};
	found = next();
	CHECK(found.value(QStringLiteral("row")).toInt() == 1 && found.value(QStringLiteral("column")).toInt() == 1);
	found = next();
	CHECK(found.value(QStringLiteral("row")).toInt() == 2 && rows.matchIndex() == 3);
	CHECK(next().value(QStringLiteral("message")).toString() == QStringLiteral("Wrapped to the start"));
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
	// The file read again keeps its sort and its filter - the filter field
	// shows what is applied; a sort past the new file's columns goes.
	rows.sortBy(1);
	rows.setFilter(QStringLiteral("item"));
	CHECK(table.setText(QStringLiteral("name,value\nitem 3,3\nother,9\nitem 1,1\n")) && rows.sortedColumn() == 1 &&
	      rows.filterText() == QStringLiteral("item") && rows.shownRows() == 2 && rows.cellText(0, 1) == QStringLiteral("1"));
	CHECK(table.setText(QStringLiteral("x\nitem\n")) && rows.sortedColumn() == -1 && rows.shownRows() == 1);
	rows.setFilter(QString());
	// NaN is no number: last, whichever way; digits of any script by value.
	{
		FileTableModel nan;
		CHECK(nan.setText(QStringLiteral("v\n3\nnan\n1\n2\n")));
		TableRowsModel byValue(&nan);
		byValue.sortColumn(0, false);
		CHECK(byValue.cellText(0, 0) == QStringLiteral("1") && byValue.cellText(2, 0) == QStringLiteral("3") &&
		      byValue.cellText(3, 0) == QStringLiteral("nan"));
		byValue.sortColumn(0, true);
		CHECK(byValue.cellText(0, 0) == QStringLiteral("3") && byValue.cellText(3, 0) == QStringLiteral("nan"));
		FileTableModel arabic;
		CHECK(arabic.setText(QString::fromUtf8("k\nx \xD9\xA5\nx 9\nx 10\n")));
		TableRowsModel byName(&arabic);
		byName.sortColumn(0, false);
		CHECK(byName.cellText(0, 0) == QString::fromUtf8("x \xD9\xA5") && byName.cellText(2, 0) == QStringLiteral("x 10"));
	}
	// A CSV of about 10 MB: read, sorted, filtered and searched, timed.
	{
		QString csv = QStringLiteral("time_ms,level,event,value\n");
		csv.reserve(10 * 1024 * 1024);
		int warns = 0;
		for (int i = 0; csv.size() < 10 * 1024 * 1024; ++i, warns += i % 5 == 4 ? 1 : 0)
			csv += QStringLiteral("%1,%2,%3,%4\n").arg(i * 250).arg(i % 5 == 3 ? QStringLiteral("WARN") : QStringLiteral("INFO"))
			           .arg(i % 5 == 3 ? QStringLiteral("threshold") : QStringLiteral("sample")).arg(220 + (i * 37) % 23);
		FileTableModel big;
		TableRowsModel bigRows(&big);
		QElapsedTimer clock;
		clock.start();
		CHECK(big.setText(csv));
		const auto readMs = clock.restart();
		bigRows.sortColumn(3, true);
		const auto sortMs = clock.restart();
		const auto shown = bigRows.setFilter(QStringLiteral("WARN"));
		const auto filterMs = clock.restart();
		const auto first = bigRows.find(QStringLiteral("threshold"), true, 0, 0);
		const auto findMs = clock.elapsed();
		std::printf("A CSV of %lld bytes, %d rows: read in %lld ms, sorted in %lld ms, filtered in %lld ms, "
		            "searched in %lld ms\n",
		            static_cast<long long>(csv.size()), big.rowCount(), static_cast<long long>(readMs),
		            static_cast<long long>(sortMs), static_cast<long long>(filterMs), static_cast<long long>(findMs));
		CHECK(shown == warns && first.value(QStringLiteral("row")).toInt() == 0 &&
		      bigRows.matchCount() == shown);
	}
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
	// A struct and an array come folded: summarised from the data, no rows yet.
	CHECK(value(pair) == QStringLiteral("{voltage 230.5, mode Run (1)}") && !numeric(pair));
	CHECK(value(samples) == QStringLiteral("[-1, 0, 2]"));
	CHECK(fields->rowCount(pair) == 0 && fields->hasChildren(pair) && fields->canFetchMore(pair));
	CHECK(fields->rowCount(samples) == 0 && fields->hasChildren(samples) && !fields->hasChildren(big));
	// Copy reads the data, so what is not opened is not lost.
	const auto json = values.textOfAll(QStringLiteral("json"));
	CHECK(json.contains(QStringLiteral("\"Big\": \"18446744073709551615\"")) &&
	      json.contains(QStringLiteral("\"mode\": 1")) && json.contains(QStringLiteral("\"voltage\": 230.5")) &&
	      json.contains(QStringLiteral("\"Samples\": [")));
	CHECK(values.textOfAll(QStringLiteral("tsv")).contains(QStringLiteral("rich.Pair.mode\tRun (1)\t")));
	CHECK(values.textOf(pair, QStringLiteral("text")) == QStringLiteral("Pair\n  voltage = 230.5\n  mode = Run (1)\n"));
	// Opened, the rows come, filled from the shown snapshot.
	fields->fetchMore(pair);
	fields->fetchMore(samples);
	CHECK(fields->rowCount(pair) == 2 && !fields->canFetchMore(pair) && fields->rowCount(samples) == 3);
	const auto voltage = fields->index(0, 0, pair);
	const auto mode = fields->index(1, 0, pair);
	CHECK(value(voltage) == QStringLiteral("230.5") && numeric(voltage));
	CHECK(value(mode) == QStringLiteral("Run (1)") && !numeric(mode));
	CHECK(values.pathOf(mode) == QStringLiteral("rich.Pair.mode"));
	CHECK(value(fields->index(0, 0, samples)) == QStringLiteral("-1"));
	CHECK(values.textOf(samples, QStringLiteral("text")) == QStringLiteral("Samples = [-1, 0, 2]\n"));
	CHECK(values.textOfAll(QStringLiteral("json")) == json);
	// A newer snapshot reaches the rows made since.
	rich.pair.voltage = 231.0f;
	CHECK(values.update(readWhole(richValues)).isEmpty());
	CHECK(value(voltage) == QStringLiteral("231") && fields->data(voltage, ValuesTreeModel::ChangedRole).toBool() &&
	      value(pair) == QStringLiteral("{voltage 231, mode Run (1)}"));
	rich.pair.voltage = 230.5f;
}

// A large array opens as ranges of 256, as Eclipse shows one; Find makes
// only the rows that lead to a match, wherever it is; Copy writes
// the elements not made yet from the data, and under Find only what is shown.
void lazyValues()
{
	using telemetry::ux::ValuesContent;
	using telemetry::ux::ValuesTreeModel;
	for (std::size_t i = 0; i < wide.counts.size(); ++i)
		wide.counts[i] = static_cast<std::uint16_t>(i);
	for (std::size_t i = 0; i < wide.pairs.size(); ++i)
		wide.pairs[i] = Pair{static_cast<float>(i), i % 2 ? Mode::Run : Mode::Idle};
	const auto bytes = readWhole(wideValues);
	const auto descriptorBytes = bytesOf(widePacked);
	const auto name = [](QAbstractItemModel* model, const QModelIndex& index) { return model->data(index).toString(); };
	const auto value = [](QAbstractItemModel* model, const QModelIndex& index) {
		return model->data(index, ValuesTreeModel::ValueRole).toString();
	};
	{
		ValuesContent values(bytes, descriptorBytes);
		CHECK(values.valid());
		auto* fields = values.fields();
		// Indexes of the shown model are taken again after anything that changes its rows.
		const auto field = [&](int row) { return fields->index(row, 0, fields->index(0, 0)); };
		const auto range = [&](int row) { return fields->index(row, 0, field(0)); };
		const auto element = [&](int inRange, int row) { return fields->index(row, 0, range(inRange)); };
		const auto type = [&](const QModelIndex& index) { return fields->data(index, ValuesTreeModel::TypeRole).toString(); };
		CHECK(value(fields, field(0)) == QStringLiteral("[0, 1, 2, 3, 4, 5, 6, 7, …]") && fields->rowCount(field(0)) == 0);
		fields->fetchMore(field(0));
		// As Eclipse shows a long array: ranges of 256, each opening like any row.
		CHECK(fields->rowCount(field(0)) == 3 && name(fields, range(0)) == QStringLiteral("[0 … 255]") &&
		      name(fields, range(2)) == QStringLiteral("[512 … 599]") && type(range(2)) == QStringLiteral("88 items") &&
		      fields->rowCount(range(1)) == 0 && fields->hasChildren(range(1)) && fields->canFetchMore(range(1)));
		// A range has no path or value of its own, and Find never takes it for a match.
		CHECK(values.pathOf(range(1)).isEmpty() && values.valueOf(range(1)).isEmpty());
		CHECK(values.setFilter(QStringLiteral("256 …")) == 0 && values.setFilter(QString()) == 0);
		// Copy of a range: its elements, from the data, opened or not.
		const auto rangeTsv = values.textOf(range(1), QStringLiteral("tsv"));
		CHECK(rangeTsv.contains(QStringLiteral("wide.Counts[300]\t300\tu16")) &&
		      !rangeTsv.contains(QStringLiteral("wide.Counts[255]")) && !rangeTsv.contains(QStringLiteral("wide.Counts[512]")));
		CHECK(values.textOf(field(0), QStringLiteral("tsv")).contains(QStringLiteral("wide.Counts[599]\t599\tu16")));
		fields->fetchMore(range(2));
		CHECK(fields->rowCount(range(2)) == 88 && name(fields, element(2, 87)) == QStringLiteral("[599]") &&
		      value(fields, element(2, 87)) == QStringLiteral("599") && values.pathOf(element(2, 87)) == QStringLiteral("wide.Counts[599]"));
		// A newer snapshot reaches every made element, through the ranges.
		wide.counts[599] = 9999;
		CHECK(values.update(readWhole(wideValues)).isEmpty());
		CHECK(value(fields, element(2, 87)) == QStringLiteral("9999") &&
		      fields->data(element(2, 87), ValuesTreeModel::ChangedRole).toBool() && values.changedCount() == 1);
		// The Field is marked by its bytes, and its group counts it.
		CHECK(fields->data(field(0), ValuesTreeModel::ChangedRole).toBool() &&
		      fields->data(fields->index(0, 0), ValuesTreeModel::ChangedFieldsRole).toInt() == 1);
		wide.counts[599] = 599;
		// A folded Field whose change lies past its summary: marked all the same,
		// one change; with Counts changed back, two Fields of the group changed.
		wide.pairs[299].voltage = 1000.0f;
		CHECK(values.update(readWhole(wideValues)).isEmpty());
		CHECK(fields->rowCount(field(1)) == 0 && fields->data(field(1), ValuesTreeModel::ChangedRole).toBool() &&
		      fields->data(fields->index(0, 0), ValuesTreeModel::ChangedFieldsRole).toInt() == 2 &&
		      values.changedCount() == 2);
		wide.pairs[299].voltage = 299.0f;
		CHECK(values.update(readWhole(wideValues)).isEmpty());
		CHECK(!fields->data(field(0), ValuesTreeModel::ChangedRole).toBool() &&
		      fields->data(fields->index(0, 0), ValuesTreeModel::ChangedFieldsRole).toInt() == 1);

		// An array of structs, not opened: Copy has all of it.
		const auto pairs = field(1);
		CHECK(fields->rowCount(pairs) == 0);
		const auto pairsText = values.textOf(pairs, QStringLiteral("text"));
		CHECK(pairsText.contains(QStringLiteral("  [299]\n    voltage = 299\n    mode = Run (1)\n")));
		CHECK(values.textOf(pairs, QStringLiteral("json")).contains(QStringLiteral("\"voltage\": 299")));
		// Find into it: every element's mode, the rows that lead there made.
		CHECK(values.setFilter(QStringLiteral("mode")) == 300 && !values.findLimited());
		const auto shownPairs = fields->index(0, 0, fields->index(0, 0));
		CHECK(name(fields, shownPairs) == QStringLiteral("Pairs") && fields->rowCount(shownPairs) == 2 &&
		      fields->rowCount(fields->index(1, 0, shownPairs)) == 44);
		const auto modeTsv = values.textOf(shownPairs, QStringLiteral("tsv"));
		CHECK(modeTsv.contains(QStringLiteral("wide.Pairs[299].mode\tRun (1)\t")) &&
		      !modeTsv.contains(QStringLiteral("voltage")) && !modeTsv.contains(QStringLiteral("Counts")));
		CHECK(values.setFilter(QString()) == 0);
	}
	{
		// Find for one element far past the first step: its steps are made.
		ValuesContent values(bytes, descriptorBytes);
		auto* fields = values.fields();
		// First, Live with the array folded: a change past its summary still marks it.
		wide.counts[20] = 7777;
		CHECK(values.update(readWhole(wideValues)).isEmpty());
		CHECK(fields->rowCount(fields->index(0, 0, fields->index(0, 0))) == 0 &&
		      fields->data(fields->index(0, 0, fields->index(0, 0)), ValuesTreeModel::ChangedRole).toBool() &&
		      values.changedCount() == 1);
		wide.counts[20] = 20;
		CHECK(values.update(readWhole(wideValues)).isEmpty());
		// Opened, with the change in an element not made yet: marked and counted once.
		fields->fetchMore(fields->index(0, 0, fields->index(0, 0)));
		wide.counts[400] = 7777;
		CHECK(values.update(readWhole(wideValues)).isEmpty());
		CHECK(fields->rowCount(fields->index(0, 0, fields->index(0, 0))) == 3 &&
		      fields->data(fields->index(0, 0, fields->index(0, 0)), ValuesTreeModel::ChangedRole).toBool() &&
		      values.changedCount() == 1);
		wide.counts[400] = 400;
		CHECK(values.update(readWhole(wideValues)).isEmpty());
		// Find for the last element: only its range is made on the way.
		CHECK(values.setFilter(QStringLiteral("Counts[599]")) == 1 && !values.findLimited());
		const auto counts = fields->index(0, 0, fields->index(0, 0));
		const auto last = fields->index(0, 0, counts);
		CHECK(fields->rowCount(counts) == 1 && name(fields, last) == QStringLiteral("[512 … 599]") &&
		      fields->rowCount(last) == 1 && value(fields, fields->index(0, 0, last)) == QStringLiteral("599"));
		// Copied as JSON under Find: through the range, only what is shown.
		const auto shownJson = values.textOf(counts, QStringLiteral("json"));
		CHECK(shownJson.contains(QStringLiteral("599")) && !shownJson.contains(QStringLiteral("598")));
		CHECK(values.setFilter(QString()) == 0 && fields->rowCount(fields->index(0, 0, fields->index(0, 0))) == 3 &&
		      fields->rowCount(fields->index(0, 0, fields->index(0, 0, fields->index(0, 0)))) == 0);
	}
	{
		// The most elements an array may have: 256 ranges, and Live through them.
		// One token is larger than a READ here, so it is read whole.
		for (std::size_t i = 0; i < longest.bytes.size(); ++i)
			longest.bytes[i] = static_cast<std::uint8_t>(i % 251);
		ValuesContent values(readWhole(longValues, 70000), bytesOf(longPacked));
		CHECK(values.valid());
		auto* fields = values.fields();
		const auto array = [&] { return fields->index(0, 0, fields->index(0, 0)); };
		fields->fetchMore(array());
		CHECK(fields->rowCount(array()) == 256 &&
		      name(fields, fields->index(255, 0, array())) == QStringLiteral("[65280 … 65535]"));
		// Find the very last element: one range of 256 made on the way.
		CHECK(values.setFilter(QStringLiteral("Bytes[65535]")) == 1 && !values.findLimited());
		CHECK(values.setFilter(QString()) == 0);
		const auto lastRange = fields->index(255, 0, array());
		CHECK(fields->rowCount(lastRange) == 256 && fields->rowCount(fields->index(254, 0, array())) == 0 &&
		      name(fields, fields->index(255, 0, lastRange)) == QStringLiteral("[65535]") &&
		      value(fields, fields->index(255, 0, lastRange)) == QString::number(65535 % 251));
		// Live reaches it through the range.
		longest.bytes[65535] = 250;
		CHECK(values.update(readWhole(longValues, 70000)).isEmpty());
		CHECK(value(fields, fields->index(255, 0, lastRange)) == QStringLiteral("250") &&
		      fields->data(fields->index(255, 0, lastRange), ValuesTreeModel::ChangedRole).toBool());
	}
}

// A descriptor whose types nest and repeat opens at once: members are made
// when a row is opened, Find makes the rows to its matches up to a bound,
// and Copy writes members from the schema - up to a bound it then names.
void lazyDescriptor()
{
	using telemetry::ux::DescriptorContent;
	QElapsedTimer clock;
	clock.start();
	DescriptorContent decoded(bytesOf(deepPacked));
	const auto openMs = clock.elapsed();
	CHECK(decoded.valid() && decoded.fieldCount() == 20);
	auto* model = decoded.endpoints();
	const auto group = model->index(0, 0, model->index(0, 0));
	CHECK(model->data(group).toString() == QStringLiteral("deep") && model->rowCount(group) == 20);
	const auto first = model->index(0, 0, group);
	CHECK(model->data(first).toString() == QStringLiteral("d000"));
	CHECK(model->rowCount(first) == 0 && model->hasChildren(first) && model->canFetchMore(first));
	model->fetchMore(first);
	CHECK(model->rowCount(first) == 4 && !model->canFetchMore(first));
	const auto a = model->index(0, 0, first);
	CHECK(model->data(a).toString() == QStringLiteral("a") && decoded.pathOf(a) == QStringLiteral("deep.d000.a") &&
	      model->rowCount(a) == 0 && model->hasChildren(a));

	// Copy of an endpoint not opened: its whole member tree, and no rows made.
	const auto second = model->index(1, 0, group);
	clock.restart();
	const auto lines = decoded.textOf(second, QStringLiteral("text"));
	const auto copyOneMs = clock.elapsed();
	CHECK(lines.count(QLatin1Char('\n')) == 1 + 54612 && model->rowCount(second) == 0 && decoded.copyNote().isEmpty());
	CHECK(decoded.textOf(second, QStringLiteral("tsv")).contains(QStringLiteral("deep.d001.a.a.a.a.a.a.a.x\tmember\tf32\t\n")));
	CHECK(decoded.textOf(second, QStringLiteral("json")).contains(QStringLiteral("\"members\"")));

	// Find by path into members not made: the way there made, one match. The
	// search looks at so many members, then says it may have missed some.
	CHECK(decoded.setFilter(QStringLiteral("d002.b.c")) == 1 && decoded.findLimited());
	// Find with more matches than it opens: it stops at the bound and says so.
	clock.restart();
	const auto found = decoded.setFilter(QStringLiteral("y"));
	const auto findMs = clock.elapsed();
	CHECK(found == static_cast<int>(DescriptorContent::maxFindMatches) && decoded.findLimited());
	CHECK(decoded.setFilter(QString()) == 0 && !decoded.findLimited() && model->rowCount(group) == 20);

	// Copy of everything: past the bound, cut short and saying so.
	clock.restart();
	const auto all = decoded.textOfAll(QStringLiteral("text"));
	const auto copyAllMs = clock.elapsed();
	CHECK(all.endsWith(QStringLiteral("rows; the rest was not copied\n")) &&
	      all.count(QLatin1Char('\n')) == static_cast<qsizetype>(DescriptorContent::maxCopiedRows) + 1);
	CHECK(decoded.copyNote().startsWith(QStringLiteral("Copied the first 500000 rows")) && !decoded.findLimitText().isEmpty());
	CHECK(decoded.textOfAll(QStringLiteral("json")).contains(QStringLiteral("\"…\"")));
	std::printf("descriptor %lld bytes, 20 x 54612 member rows: open %lld ms, copy one %lld ms, find %lld ms, "
	            "copy all %lld ms\n",
	            static_cast<long long>(deepPacked.size()), static_cast<long long>(openMs), static_cast<long long>(copyOneMs),
	            static_cast<long long>(findMs), static_cast<long long>(copyAllMs));
	CHECK(openMs < 500 && findMs < 5000 && copyAllMs < 10000);
}

// Ten megabytes of values open, take a snapshot, find and copy within bounds.
void bigValues()
{
	using telemetry::ux::ValuesContent;
	for (std::size_t field = 0; field < waveCount; ++field)
		for (std::size_t i = 0; i < waveLength; ++i)
			waves[field][i] = static_cast<float>(field) + static_cast<float>(i) / 64.0f;
	QElapsedTimer clock;
	clock.start();
	const auto bytes = readWhole(waveValues);
	const auto readMs = clock.elapsed();
	CHECK(bytes.size() == 24 + static_cast<qsizetype>(waveCount * (1 + waveLength * 4)) && bytes.size() > 10000000);
	clock.restart();
	ValuesContent values(bytes, bytesOf(wavePacked));
	const auto openMs = clock.elapsed();
	CHECK(values.valid() && values.summary() == QStringLiteral("160 Fields in 1 group"));
	auto* fields = values.fields();
	const auto group = fields->index(0, 0);
	const auto wave = fields->index(3, 0, group);
	CHECK(fields->data(wave).toString() == QStringLiteral("w003") && fields->rowCount(wave) == 0);
	clock.restart();
	fields->fetchMore(wave);
	fields->fetchMore(fields->index(0, 0, wave));
	const auto openFieldMs = clock.elapsed();
	// 16 000 elements: 63 ranges, the first opened.
	CHECK(fields->rowCount(wave) == 63 && fields->rowCount(fields->index(0, 0, wave)) == 256 &&
	      fields->data(fields->index(1, 0, fields->index(0, 0, wave)), telemetry::ux::ValuesTreeModel::ValueRole).toString() ==
	          QStringLiteral("3.015625"));
	clock.restart();
	CHECK(values.update(bytes).isEmpty() && values.changedCount() == 0);
	const auto snapshotMs = clock.elapsed();
	clock.restart();
	const auto found = values.setFilter(QStringLiteral("[15999]"));
	const auto findMs = clock.elapsed();
	CHECK(found > 0 && values.findLimited());
	CHECK(values.setFilter(QString()) == 0);
	// The rows Find made stay within its bound, even inside one long array.
	int made = 0;
	for (int row = 0; row < fields->rowCount(group); ++row)
		made += fields->rowCount(fields->index(row, 0, group));
	CHECK(made > 0 && made <= static_cast<int>(ValuesContent::maxFindRows + ValuesContent::rangeSize + waveCount));
	// A snapshot fills every row made so far.
	clock.restart();
	CHECK(values.update(bytes).isEmpty());
	const auto snapshotAfterFindMs = clock.elapsed();
	clock.restart();
	const auto all = values.textOfAll(QStringLiteral("tsv"));
	const auto copyMs = clock.elapsed();
	CHECK(all.endsWith(QStringLiteral("rows; the rest was not copied\n")) && !values.copyNote().isEmpty() &&
	      !values.findLimitText().isEmpty());
	// JSON cut short leaves out what it did not reach: no null, which would read as unavailable.
	const auto json = values.textOfAll(QStringLiteral("json"));
	CHECK(json.contains(QStringLiteral("\"…\"")) && !json.contains(QStringLiteral("null")));
	std::printf("values %lld bytes: read %lld ms, open %lld ms, open a Field %lld ms, snapshot %lld ms, find %lld ms, "
	            "snapshot with %d rows made %lld ms, copy %lld ms\n",
	            static_cast<long long>(bytes.size()), static_cast<long long>(readMs), static_cast<long long>(openMs),
	            static_cast<long long>(openFieldMs), static_cast<long long>(snapshotMs), static_cast<long long>(findMs),
	            made, static_cast<long long>(snapshotAfterFindMs), static_cast<long long>(copyMs));
	CHECK(openMs < 1000 && snapshotMs < 1000 && openFieldMs < 200 && findMs < 5000 && copyMs < 10000 &&
	      snapshotAfterFindMs < 2000);
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

// JSON as a tree: the file's own key order, members made when opened and 256
// at a time, Find by name, path or value, a value copied as the file writes
// it, where a broken file is wrong, and a large file opened at once.
void jsonCodec(const QString& folder)
{
	using telemetry::ux::JsonContent;
	using telemetry::ux::JsonTreeModel;
	const auto write = [&](const QString& name, const QByteArray& bytes) {
		const auto path = folder + QLatin1Char('/') + name;
		QFile file(path);
		CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size());
		return path;
	};
	QByteArray list = "[";
	for (int i = 0; i < 600; ++i)
		list += (i ? ", " : "") + QByteArray::number(i);
	list += "]";
	const QByteArray text = "\xEF\xBB\xBF{\n  \"zeta\": 1,\n  \"alpha\": {\"b\": [1, 2.50, \"x\\ny\"], \"a\": null},\n"
	                        "  \"list\": " + list + ",\n  \"flag\": true,\n  \"u\": \"caf\\u00e9\"\n}\n";
	telemetry::ux::OpenFiles open;
	telemetry::ux::installTelemetryCodecs(open);
	const auto local = write(QStringLiteral("settings.json"), text);
	open.open(local, QStringLiteral("/cfg/settings.json"));
	auto* content = open.contentFor(local);
	CHECK(content && content->codecName() == QStringLiteral("JSON") &&
	      content->codecView().toString().endsWith(QStringLiteral("JsonView.qml")));
	auto* json = qobject_cast<JsonContent*>(content->decoded());
	CHECK(json && json->valid() && json->summary() == QStringLiteral("object, 5 keys") && json->regions().size() == 5);
	auto* nodes = json->nodes();
	const auto name = [&](const QModelIndex& index) { return nodes->data(index).toString(); };
	const auto value = [&](const QModelIndex& index) { return nodes->data(index, JsonTreeModel::ValueRole).toString(); };
	const auto type = [&](const QModelIndex& index) { return nodes->data(index, JsonTreeModel::TypeRole).toString(); };
	// The file's order, not the alphabet's.
	CHECK(nodes->rowCount() == 5 && name(nodes->index(0, 0)) == QStringLiteral("zeta") &&
	      name(nodes->index(1, 0)) == QStringLiteral("alpha") && name(nodes->index(4, 0)) == QStringLiteral("u"));
	const auto alpha = nodes->index(1, 0);
	CHECK(type(alpha) == QStringLiteral("object, 2 keys") && value(alpha) == QStringLiteral("{b: […], a: null}") &&
	      nodes->rowCount(alpha) == 0 && nodes->hasChildren(alpha) && nodes->canFetchMore(alpha));
	CHECK(value(nodes->index(0, 0)) == QStringLiteral("1") &&
	      nodes->data(nodes->index(0, 0), JsonTreeModel::NumericRole).toBool());
	// Copied as the file writes it; a string's value without its quotes or escapes.
	CHECK(json->jsonOf(alpha) == QStringLiteral("{\"b\": [1, 2.50, \"x\\ny\"], \"a\": null}"));
	CHECK(json->valueOf(nodes->index(4, 0)) == QStringLiteral("café") &&
	      value(nodes->index(4, 0)) == QStringLiteral("\"caf\\u00e9\"") && json->valueOf(alpha).isEmpty());
	nodes->fetchMore(alpha);
	const auto b = nodes->index(0, 0, alpha);
	CHECK(nodes->rowCount(alpha) == 2 && json->pathOf(b) == QStringLiteral("alpha.b") && type(b) == QStringLiteral("array, 3 items"));
	nodes->fetchMore(b);
	CHECK(value(nodes->index(1, 0, b)) == QStringLiteral("2.50") && json->valueOf(nodes->index(2, 0, b)) == QStringLiteral("x\ny") &&
	      json->pathOf(nodes->index(2, 0, b)) == QStringLiteral("alpha.b[2]"));
	// A long array: ranges of 256, as Eclipse shows one.
	const auto listIndex = [&] { return nodes->index(2, 0); };
	const auto listRange = [&](int row) { return nodes->index(row, 0, listIndex()); };
	CHECK(type(listIndex()) == QStringLiteral("array, 600 items"));
	nodes->fetchMore(listIndex());
	CHECK(nodes->rowCount(listIndex()) == 3 && name(listRange(1)) == QStringLiteral("[256 … 511]") &&
	      type(listRange(2)) == QStringLiteral("88 items") && json->pathOf(listRange(1)).isEmpty() &&
	      json->valueOf(listRange(1)).isEmpty());
	// A range copied as JSON: its elements as the file writes them, in brackets.
	CHECK(QJsonDocument::fromJson(json->jsonOf(listRange(1)).toUtf8()).array().size() == 256 &&
	      json->jsonOf(listRange(1)).startsWith(QStringLiteral("[256, 257")));
	nodes->fetchMore(listRange(1));
	CHECK(nodes->rowCount(listRange(1)) == 256 &&
	      json->rowOf(nodes->index(44, 0, listRange(1))) == QStringLiteral("[300]\t300\tnumber"));
	// Find by value, into rows not made: only the range on the way is made.
	CHECK(json->setFilter(QStringLiteral("599")) == 1 && !json->findLimited());
	CHECK(json->setFilter(QStringLiteral("café")) == 1);
	CHECK(json->setFilter(QStringLiteral("nothing here")) == 0 && json->setFilter(QString()) == 0);
	CHECK(nodes->rowCount(listIndex()) == 3 && nodes->rowCount(listRange(0)) == 0 && nodes->rowCount(listRange(2)) == 88);
	CHECK(json->jsonOfAll() == QString::fromUtf8(text.mid(3)));

	// An object of many keys: ranges named by their first and last key.
	QByteArray keys = "{";
	for (int i = 0; i < 300; ++i)
		keys += (i ? ", " : "") + QByteArrayLiteral("\"k") + QByteArray::number(1000 + i) + "\": " + QByteArray::number(i);
	keys += "}";
	JsonContent keyed(keys);
	CHECK(keyed.valid() && keyed.nodes()->rowCount() == 2);
	const auto keysRange = keyed.nodes()->index(1, 0);
	CHECK(keyed.nodes()->data(keysRange).toString() == QStringLiteral("[256 … 299]") &&
	      keyed.nodes()->data(keysRange, JsonTreeModel::ValueRole).toString() == QStringLiteral("k1256 … k1299") &&
	      keyed.nodes()->data(keysRange, JsonTreeModel::TypeRole).toString() == QStringLiteral("44 keys"));
	CHECK(QJsonDocument::fromJson(keyed.jsonOf(keysRange).toUtf8()).object().size() == 44);
	// More than 65 536 keys: ranges of ranges, each named by its own first and last key.
	{
		QByteArray huge = "{";
		for (int i = 0; i < 66000; ++i)
			huge += (i ? "," : "") + QByteArrayLiteral("\"k") + QByteArray::number(100000 + i) + "\":" + QByteArray::number(i);
		huge += "}";
		JsonContent hugeObject(huge);
		CHECK(hugeObject.valid() && hugeObject.nodes()->rowCount() == 2);
		const auto top = hugeObject.nodes()->index(0, 0);
		CHECK(hugeObject.nodes()->data(top, JsonTreeModel::ValueRole).toString() == QStringLiteral("k100000 … k165535"));
		hugeObject.nodes()->fetchMore(top);
		const auto second = hugeObject.nodes()->index(1, 0, top);
		CHECK(hugeObject.nodes()->rowCount(top) == 256 &&
		      hugeObject.nodes()->data(second).toString() == QStringLiteral("[256 … 511]") &&
		      hugeObject.nodes()->data(second, JsonTreeModel::ValueRole).toString() == QStringLiteral("k100256 … k100511"));
		hugeObject.nodes()->fetchMore(second);
		CHECK(hugeObject.nodes()->data(hugeObject.nodes()->index(0, 0, second)).toString() == QStringLiteral("k100256"));
	}
	keyed.nodes()->fetchMore(keysRange);
	CHECK(keyed.nodes()->rowCount(keysRange) == 44 &&
	      keyed.nodes()->data(keyed.nodes()->index(0, 0, keysRange)).toString() == QStringLiteral("k1256") &&
	      keyed.pathOf(keyed.nodes()->index(43, 0, keysRange)) == QStringLiteral("k1299"));

	// Broken: where, as a person counts; the file stays text.
	const auto broken = write(QStringLiteral("broken.json"), "{\n  \"a\": ,\n}");
	open.open(broken, QStringLiteral("/cfg/broken.json"));
	auto* refused = open.contentFor(broken);
	CHECK(refused && !refused->decoded() && refused->codecError().startsWith(QStringLiteral("Not a valid JSON file:")) &&
	      refused->codecError().contains(QStringLiteral("line 2")) && refused->textual());
	// No extension: its first bytes decide; a .txt that begins with "{" stays text.
	const auto bare = write(QStringLiteral("state"), "[1, 2]");
	open.open(bare);
	CHECK(open.contentFor(bare)->codecName() == QStringLiteral("JSON"));
	const auto notes = write(QStringLiteral("notes.txt"), "{ not json at all");
	open.open(notes);
	CHECK(open.contentFor(notes)->codecName().isEmpty() && open.contentFor(notes)->codecError().isEmpty());
	// A log without an extension that begins with "[": only a guess, so it is
	// text with no word of JSON.
	const auto log = write(QStringLiteral("log"), "[INFO] start\n[INFO] ready\n");
	open.open(log);
	CHECK(open.contentFor(log)->codecName().isEmpty() && open.contentFor(log)->codecError().isEmpty() &&
	      open.contentFor(log)->textual());

	// Escapes as Qt reads them: a quote and a backslash in a key, a surrogate pair, "\/".
	JsonContent escaped(QByteArrayLiteral("{\"k\\\"ey\\\\\": \"\\ud83d\\ude00\", \"slash\": \"a\\/b\"}"));
	CHECK(escaped.valid() && escaped.nodes()->rowCount() == 2);
	const auto first = escaped.nodes()->index(0, 0);
	CHECK(escaped.nodes()->data(first).toString() == QStringLiteral("k\"ey\\") &&
	      escaped.valueOf(first) == QString::fromUtf8("\xF0\x9F\x98\x80") &&
	      escaped.valueOf(escaped.nodes()->index(1, 0)) == QStringLiteral("a/b"));

	// About 10 MB: open at once, find within bounds.
	QByteArray big = "[";
	for (int i = 0; i < 160000; ++i)
		big += (i ? ",\n" : "") + QByteArrayLiteral("{\"id\": ") + QByteArray::number(i) +
		       QByteArrayLiteral(", \"name\": \"item ") + QByteArray::number(i) +
		       QByteArrayLiteral("\", \"samples\": [1.5, 2.5, 3.5]}");
	big += "]";
	QElapsedTimer clock;
	clock.start();
	JsonContent large(big);
	const auto openMs = clock.elapsed();
	// 160 000 items: three ranges of 65 536, each of ranges of 256.
	CHECK(large.valid() && large.summary() == QStringLiteral("array, 160000 items") && large.nodes()->rowCount() == 3 &&
	      large.nodes()->data(large.nodes()->index(2, 0)).toString() == QStringLiteral("[131072 … 159999]"));
	// At the very end: only the ranges on the way are made, so it is reached.
	clock.restart();
	const auto found = large.setFilter(QStringLiteral("item 159999"));
	const auto findMs = clock.elapsed();
	CHECK(found == 1 && !large.findLimited());
	CHECK(large.setFilter(QString()) == 0);
	clock.restart();
	const auto many = large.setFilter(QStringLiteral("samples"));
	const auto manyMs = clock.elapsed();
	CHECK(many > 0 && large.findLimited());
	std::printf("json %lld bytes: open %lld ms, find one at the very end %lld ms, find many %lld ms\n",
	            static_cast<long long>(big.size()), static_cast<long long>(openMs), static_cast<long long>(findMs),
	            static_cast<long long>(manyMs));
	CHECK(openMs < 2000 && findMs < 5000 && manyMs < 5000);
}

// The read-many and batch capability records: every field in words, the
// descriptor they bind to, their regions, and a record this program cannot read.
void capabilityCodec(const QString& folder)
{
	using telemetry::ux::CapabilityContent;
	namespace readMany = telemetry::protocol::encoded::read_many;
	namespace batch = telemetry::protocol::encoded::batch;
	const auto write = [&](const QString& name, const QByteArray& bytes) {
		const auto path = folder + QLatin1Char('/') + name;
		QFile file(path);
		CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size());
		return path;
	};
	std::array<std::byte, readMany::capabilityBytes> trm{};
	CHECK(readMany::writeCapability(trm, readMany::Capability{0x1234abcdu, descriptor.fingerprint(), 32, 130, 514}).status ==
	      telemetry::protocol::encoded::Status::Ok);
	std::array<std::byte, batch::capabilityBytes> tbm{};
	CHECK(batch::writeCapability(tbm, batch::Capability{7, descriptor.fingerprint() ^ 1, 16, 4096, 8192}).status ==
	      telemetry::protocol::encoded::Status::Ok);
	telemetry::ux::OpenFiles open;
	telemetry::ux::installTelemetryCodecs(open);
	telemetry::ux::FileCodecContext context;
	context.connectedFingerprint = [](quint64& fingerprint) {
		fingerprint = descriptor.fingerprint();
		return true;
	};
	open.setContext(context);

	const auto readManyFile = write(QStringLiteral("read-many.bin"), bytesOf(trm));
	open.open(readManyFile, QStringLiteral("/telemetry/read-many.bin"));
	auto* content = open.contentFor(readManyFile);
	CHECK(content && content->codecName() == QStringLiteral("Capability") &&
	      content->codecView().toString().endsWith(QStringLiteral("CapabilityView.qml")));
	auto* record = qobject_cast<CapabilityContent*>(content->decoded());
	CHECK(record && record->valid() && record->kind() == QStringLiteral("Read many") &&
	      record->binding() == QStringLiteral("connected") && record->regions().size() == 9);
	const auto fields = record->fields();
	const auto fieldAt = [&](int i, const char* key) { return fields.at(i).toMap().value(QLatin1String(key)); };
	CHECK(fields.size() == 5 && fieldAt(0, "value").toString() == QStringLiteral("32") && fieldAt(0, "meaning").toString() == QStringLiteral("the protocol's maximum") &&
	      fieldAt(1, "value").toString() == QStringLiteral("130 bytes") &&
	      fieldAt(2, "value").toString() == QStringLiteral("514 bytes") &&
	      fieldAt(3, "value").toString() == QStringLiteral("0x1234ABCD") && !fieldAt(4, "warn").toBool());
	CHECK(record->text().contains(QStringLiteral("Most items in a request\t32")));

	// A batch record bound to another descriptor: said, in the warning colour.
	const auto batchFile = write(QStringLiteral("batch.bin"), bytesOf(tbm));
	open.open(batchFile, QStringLiteral("/telemetry/batch.bin"));
	auto* other = qobject_cast<CapabilityContent*>(open.contentFor(batchFile)->decoded());
	CHECK(other && other->kind() == QStringLiteral("Batch") && other->binding() == QStringLiteral("other") &&
	      other->fields().at(4).toMap().value(QStringLiteral("warn")).toBool() &&
	      other->fields().at(2).toMap().value(QStringLiteral("value")).toString() == QStringLiteral("8192 bytes"));
	// Below the protocol's ceiling: how far, with the unit.
	CHECK(other->fields().at(1).toMap().value(QStringLiteral("meaning")).toString() ==
	      QStringLiteral("the protocol allows up to 65535 bytes"));

	// A version this program does not read: why, and the file stays hex.
	auto wrong = bytesOf(trm);
	wrong[4] = 2;
	const auto wrongFile = write(QStringLiteral("read-many-2.bin"), wrong);
	open.open(wrongFile, QStringLiteral("/telemetry/read-many.bin"));
	auto* refused = open.contentFor(wrongFile);
	CHECK(refused && !refused->decoded() &&
	      refused->codecError().startsWith(QStringLiteral("Not a valid Capability file:")) &&
	      refused->format() == QStringLiteral("Binary"));
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
	lazyValues();
	lazyDescriptor();
	bigValues();
	jsonCodec(folder.path());
	capabilityCodec(folder.path());
	liveValues(folder.path());
	truncatedList();
	std::printf("Device files UX: %u checks, 0 failures\n", checks);
	return 0;
}
