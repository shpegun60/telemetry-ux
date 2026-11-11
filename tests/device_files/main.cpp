// The Device files models over a real session and the native TCP server:
// the tree of the device's paths, marks and selection, downloads written
// exactly, cancel, a model destroyed mid-download, and a files-only device. MIT.
#include <Telemetry/UX/DeviceFileTransfers.hpp>
#include <Telemetry/UX/DeviceFilesModel.hpp>
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
	CHECK(await(
	    [&] {
		    return files.available() && !files.loading() && files.fileCount() > 0;
	    },
	    120000));
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
	destroyedMidway(session, files, folder.path());
	session.disconnect();
	CHECK(await([&] {
		return !files.available();
	}));
	filesOnly(folder.path());
	localPaths(folder.path());
	truncatedList();
	std::printf("Device files UX: %u checks, 0 failures\n", checks);
	return 0;
}
