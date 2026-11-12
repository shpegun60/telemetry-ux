/*
 * @file DeviceFileTransfers.cpp
 * @brief The device's files read and written back: one at a time, with progress, never waiting on the device.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "DeviceFileTransfers.hpp"
#include "DeviceBytes.hpp"
#include "DeviceFilesModel.hpp"
#include "FileContent.hpp"
#include <integration/files/Client.hpp>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QLocale>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>
#include <algorithm>
#include <cmath>

namespace telemetry::ux {
namespace files = integration::files;
using Session = integration::qt::ConnectionSession;

struct DeviceFileTransfers::Job {
	struct Sink {
		Job* job;

		bool operator()(client::Input piece) noexcept
		{
			++job->pieces;
			const auto bytes = static_cast<qint64>(piece.size());
			// An upload reads the device's file into memory, at most the
			// copy's size: a longer one is not the file to write.
			if (job->upload) {
				if (job->device.size() + bytes > job->local.size())
					return false;
				job->device.append(reinterpret_cast<const char*>(piece.data()), bytes);
				return true;
			}
			return job->file.write(reinterpret_cast<const char*>(piece.data()), bytes) == bytes;
		}
	};

	using Reader = files::FileReader<Session, Sink>;
	using Writer = files::FileWriter<Session>;

	// Keeps a job whose reader still has a request in flight after the model
	// has gone; the session drains it, and this ends with it.
	class Orphan;

	QString path, name, localFile, result;
	// The device's folder localFile is in, for the device's bytes as read.
	QString deviceFolder;
	int fileIndex = -1;
	quint64 size = 0, lastReceived = 0;
	State state = State::Waiting;
	// Read to be shown: the viewer's tab follows this job (openIn).
	QPointer<OpenFiles> viewer;
	bool opening = false, viewerTold = false;
	QSaveFile file;
	std::unique_ptr<Reader> reader;
	// An upload: the copy's bytes, the device's as read, the writer. A
	// cancel is kept until the step that is running ends, so the next one
	// never starts after it.
	bool upload = false, overwrite = false, kept = false, untilRestart = false, cancelRequested = false;
	Step step = Step::Check;
	Outcome outcome = Outcome::None;
	QByteArray local, device;
	// Why the device did not keep what was written, when it did not.
	QString unkept;
	std::unique_ptr<Writer> writer;
	quint64 sent = 0;
	qint64 startedMs = 0, sampleMs = 0, publishedMs = -1000, dataMs = 0;
	double rate = 0, shownLeft = -1;
	unsigned pieces = 0;

	bool active() const noexcept
	{
		return state == State::Waiting || state == State::Reading || state == State::Writing;
	}

	quint64 received() const noexcept
	{
		if (upload) {
			if (step == Step::Write)
				return writer ? writer->written() : sent;
			if (reader)
				return reader->received();
			return step == Step::Save ? size : sent;
		}
		return reader ? reader->received() : lastReceived;
	}

	// Its reader and writer have no request in flight.
	bool idle() const noexcept
	{
		return (!reader || reader->finished()) && (!writer || writer->finished());
	}
};

class DeviceFileTransfers::Job::Orphan final : public QObject {
public:
	Orphan(std::unique_ptr<Job> job, Session* session) : QObject(session), job_(std::move(job))
	{
		connect(session, &Session::progressed, this, [this] {
			check();
		});
		connect(session, &Session::stateChanged, this, [this] {
			check();
		});
	}

private:
	void check()
	{
		if (job_->idle())
			deleteLater();
	}

	std::unique_ptr<Job> job_;
};

namespace {
constexpr qint64 publishEveryMs = 500;
constexpr double rateWindowSeconds = 2.0;

// Received and total in the total's unit, written once: "96 / 128 KiB", or as
// pattern words it ("%1 of %2 %3").
QString pairInUnit(quint64 received, quint64 total, const QString& pattern = QStringLiteral("%1 / %2 %3"))
{
	static const char* const units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
	int unit = 0;
	double scale = 1;
	while (unit < 4 && static_cast<double>(total) >= scale * 1024) {
		scale *= 1024;
		++unit;
	}
	const auto number = [&](quint64 bytes) {
		const double value = static_cast<double>(bytes) / scale;
		return QLocale().toString(value, 'f', unit == 0 || value >= 10 ? 0 : 1);
	};
	return QString(pattern).arg(number(received), number(total), QString::fromLatin1(units[unit]));
}

// In the same units as every size the panel shows: B below 1 KiB.
QString rateText(double bytesPerSecond)
{
	if (bytesPerSecond < 1024)
		return DeviceFileTransfers::tr("%1 B/s").arg(static_cast<int>(bytesPerSecond));
	return DeviceFileTransfers::tr("%1/s").arg(
	    QLocale().formattedDataSize(static_cast<qint64>(bytesPerSecond), 0,
	                                QLocale::DataSizeIecFormat));
}

// Two parts of a progress line, joined so a translator can reorder them.
QString joined(const QString& first, const QString& second)
{
	return DeviceFileTransfers::tr("%1 · %2", "joins two parts of a progress line").arg(first, second);
}

QString leftText(double seconds)
{
	if (seconds < 60) {
		const int rounded = std::max(5, static_cast<int>(std::ceil(seconds / 5.0)) * 5);
		return DeviceFileTransfers::tr("about %1 s left").arg(rounded);
	}
	return DeviceFileTransfers::tr("about %n min left", nullptr,
	                               static_cast<int>(std::lround(seconds / 60.0)));
}
} // namespace

DeviceFileTransfers::DeviceFileTransfers(Session* session, QObject* parent)
    : QAbstractListModel(parent), session_(session)
{
	clock_.start();
	if (session_) {
		// The session signals from inside its own progress pass, when it takes
		// no new request; poll on the next turn of the event loop instead.
		connect(session_, &Session::progressed, this, &DeviceFileTransfers::schedulePoll);
		connect(session_, &Session::stateChanged, this, &DeviceFileTransfers::schedulePoll);
	}
}

DeviceFileTransfers::~DeviceFileTransfers()
{
	for (auto& job : jobs_) {
		// An upload cut short past its check may have changed the device's
		// file: its record would no longer be true.
		if (job->upload && job->state == State::Writing && job->step != Step::Check)
			DeviceBytes::forget(job->localFile);
		if (job->reader)
			job->reader->cancel();
		if (job->writer)
			job->writer->cancel();
		// A reader or writer whose request is still in flight must outlive it.
		if (!job->idle() && session_)
			new Job::Orphan(std::move(job), session_);
	}
}

int DeviceFileTransfers::activeCount() const noexcept
{
	return static_cast<int>(std::count_if(jobs_.begin(), jobs_.end(), [](const auto& job) {
		return job->active();
	}));
}

QString DeviceFileTransfers::summary() const
{
	const auto running = std::find_if(jobs_.begin(), jobs_.end(), [](const auto& job) {
		return job->state == State::Reading || job->state == State::Writing;
	});
	if (running == jobs_.end())
		return {};
	const auto& job = **running;
	const auto others = activeCount() - 1;
	const auto progress = summaryProgress();
	// Whole patterns, one per case; the device's name goes in through a single
	// arg() call, so nothing in it is ever read as a placeholder.
	if (job.upload) {
		if (progress < 0)
			return others > 0 ? tr("Writing %1 · %n more", nullptr, others).arg(job.name)
			                  : tr("Writing %1").arg(job.name);
		const auto percent = QString::number(static_cast<int>(std::floor(progress * 100)));
		return others > 0 ? tr("Writing %1 · %2 % · %n more", nullptr, others).arg(job.name, percent)
		                  : tr("Writing %1 · %2 %").arg(job.name, percent);
	}
	if (progress < 0)
		return others > 0 ? tr("Downloading %1 · %n more", nullptr, others).arg(job.name)
		                  : tr("Downloading %1").arg(job.name);
	const auto percent = QString::number(static_cast<int>(std::floor(progress * 100)));
	return others > 0 ? tr("Downloading %1 · %2 % · %n more", nullptr, others).arg(job.name, percent)
	                  : tr("Downloading %1 · %2 %").arg(job.name, percent);
}

qreal DeviceFileTransfers::summaryProgress() const
{
	for (const auto& job : jobs_) {
		if (job->state == State::Writing)
			return job->step != Step::Save && job->size > 0 && job->received() <= job->size
			           ? static_cast<qreal>(job->received()) / static_cast<qreal>(job->size)
			           : -1;
		if (job->state == State::Reading) {
			const auto received = job->received();
			return job->size > 0 && received <= job->size
			           ? static_cast<qreal>(received) / static_cast<qreal>(job->size)
			           : -1;
		}
	}
	return -1;
}

QString DeviceFileTransfers::localPathFor(const QString& folder, const QString& devicePath)
{
	// The device names its files, so its names decide nothing about where they
	// land: every segment is made a plain local name, and the result must stay
	// inside the chosen folder.
	static const QStringList reserved{
	    QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),  QStringLiteral("NUL"),
	    QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
	    QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
	    QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
	    QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
	    QStringLiteral("LPT8"), QStringLiteral("LPT9")};
	static const QString forbidden = QStringLiteral("<>:\"|?*");
	auto normalized = devicePath;
	normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
	QStringList safe;
	for (auto segment : normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
		if (segment == QStringLiteral(".") || segment == QStringLiteral(".."))
			continue;
		for (auto& character : segment)
			if (character.unicode() < 0x20 || forbidden.contains(character))
				character = QLatin1Char('_');
		// Windows drops trailing dots and spaces, which could merge two names.
		while (segment.endsWith(QLatin1Char('.')) || segment.endsWith(QLatin1Char(' ')))
			segment.chop(1);
		if (segment.isEmpty())
			segment = QStringLiteral("_");
		if (reserved.contains(segment.section(QLatin1Char('.'), 0, 0).toUpper()))
			segment.prepend(QLatin1Char('_'));
		safe << segment;
	}
	if (safe.isEmpty())
		safe << QStringLiteral("file");
	const QDir root(folder);
	const auto path = QDir::cleanPath(root.filePath(safe.join(QLatin1Char('/'))));
	const auto relative = root.relativeFilePath(path);
	// "..foo" is a name; only ".." itself, or a path through it, leaves the folder.
	if (relative.isEmpty() || relative == QStringLiteral("..") ||
	    relative.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(relative))
		return {};
	return path;
}

QString DeviceFileTransfers::localFileOf(const QUrl& url)
{
	return url.isLocalFile() ? url.toLocalFile() : QString{};
}

QUrl DeviceFileTransfers::urlOfLocalFile(const QString& path)
{
	return QUrl::fromLocalFile(path);
}

bool DeviceFileTransfers::makeFolder(const QString& path)
{
	return !path.isEmpty() && QDir().mkpath(path);
}

QString DeviceFileTransfers::copyFile(const QString& from, const QString& to)
{
	if (from.isEmpty() || to.isEmpty())
		return tr("No file to save.");
	if (QFileInfo(from).canonicalFilePath() == QFileInfo(to).canonicalFilePath() && QFileInfo::exists(to))
		return {};
	QFile source(from);
	if (!source.open(QIODevice::ReadOnly))
		return tr("Could not read %1: %2").arg(QFileInfo(from).fileName(), source.errorString());
	QSaveFile target(to);
	if (!target.open(QIODevice::WriteOnly))
		return tr("Could not write %1: %2").arg(QFileInfo(to).fileName(), target.errorString());
	while (!source.atEnd()) {
		const auto piece = source.read(1024 * 1024);
		if (piece.isEmpty() && source.error() != QFileDevice::NoError)
			return tr("Could not read %1: %2").arg(QFileInfo(from).fileName(), source.errorString());
		if (target.write(piece) != piece.size())
			return tr("Could not write %1: %2").arg(QFileInfo(to).fileName(), target.errorString());
	}
	if (!target.commit())
		return tr("Could not write %1: %2").arg(QFileInfo(to).fileName(), target.errorString());
	return {};
}

bool DeviceFileTransfers::folderHasFiles(const QString& path)
{
	return !path.isEmpty() && QDir(path).exists() && !QDir(path).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot);
}

namespace {
// The label a device's folder is named after: "<name> [<id>]", or the id alone.
QString identityLabel(const integration::qt::ConnectionSession& session)
{
	// The id is already a plain name (the wire allows nothing else); the name
	// is the device's text: one segment, cut short for the path limit.
	const auto id = session.deviceId();
	auto name = session.deviceName().simplified();
	name.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
	if (name.size() > 32)
		name = name.left(32).trimmed();
	return name.isEmpty() ? id : name + QStringLiteral(" [%1]").arg(id);
}
} // namespace

QString DeviceFileTransfers::deviceFolderName() const
{
	if (!session_ || session_->identityState() != integration::qt::ConnectionSession::IdentityState::Known)
		return {};
	return QFileInfo(localPathFor(QDir::rootPath(), identityLabel(*session_))).fileName();
}

QString DeviceFileTransfers::deviceFolder(const QString& root, const QString& fallbackLabel, QObject* viewer) const
{
	const auto made = [](const QString& path) {
		return makeFolder(path) ? path : QString{};
	};
	if (!session_ || session_->identityState() != integration::qt::ConnectionSession::IdentityState::Known)
		return made(localPathFor(root, fallbackLabel));
	const auto id = session_->deviceId();
	const auto suffix = QStringLiteral(" [%1]").arg(id);
	const auto target = localPathFor(root, identityLabel(*session_));
	if (target.isEmpty())
		return {};
	// The same device under an earlier name keeps its folder, renamed when
	// nothing in it is in use; otherwise it is used as it is.
	const QDir folder(root);
	const auto wanted = QFileInfo(target).fileName();
	for (const auto& entry : folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		if (entry == wanted)
			break;
		if (entry.compare(id, Qt::CaseInsensitive) != 0 && !entry.endsWith(suffix, Qt::CaseInsensitive))
			continue;
		const auto earlier = folder.filePath(entry);
		if (inUse(earlier, viewer) || !QDir(root).rename(entry, wanted))
			return earlier;
		break;
	}
	return made(target);
}

bool DeviceFileTransfers::inUse(const QString& folder, QObject* viewer) const
{
	const auto inside = [prefix = QDir::cleanPath(folder) + QLatin1Char('/')](const QString& path) {
		return QDir::cleanPath(path).startsWith(prefix, Qt::CaseInsensitive);
	};
	for (const auto& job : jobs_)
		if (job->active() && inside(job->localFile))
			return true;
	if (const auto* open = qobject_cast<const OpenFiles*>(viewer))
		for (int row = 0; row < open->rowCount(); ++row)
			if (inside(open->data(open->index(row), OpenFiles::LocalFileRole).toString()))
				return true;
	return false;
}

QString DeviceFileTransfers::localFileAt(int row) const
{
	if (row < 0 || row >= static_cast<int>(jobs_.size()))
		return {};
	return jobs_[static_cast<std::size_t>(row)]->localFile;
}

int DeviceFileTransfers::outcomeAt(int row) const
{
	if (row < 0 || row >= static_cast<int>(jobs_.size()))
		return static_cast<int>(Outcome::None);
	return static_cast<int>(jobs_[static_cast<std::size_t>(row)]->outcome);
}

int DeviceFileTransfers::download(const QString& path, int fileIndex, qulonglong size,
                                  const QString& localFile)
{
	return queue(path, fileIndex, size, localFile, nullptr);
}

int DeviceFileTransfers::openIn(QObject* viewer, const QString& path, int fileIndex, qulonglong size,
                                const QString& localFile, const QString& deviceFolder)
{
	auto* files = qobject_cast<OpenFiles*>(viewer);
	if (!files)
		return -1;
	return queue(path, fileIndex, size, localFile, files, deviceFolder);
}

int DeviceFileTransfers::upload(const QString& path, int fileIndex, const QString& localFile,
                                const QString& deviceFolder, bool overwrite, QObject* viewer)
{
	if (!session_ || !session_->filesSupported() || fileIndex < 0 || localFile.isEmpty() ||
	    !DeviceBytes::inside(deviceFolder, localFile))
		return -1;
	auto job = std::make_unique<Job>();
	job->path = path;
	job->name = path.section(QLatin1Char('/'), -1);
	job->fileIndex = fileIndex;
	job->size = static_cast<quint64>(QFileInfo(localFile).size());
	job->localFile = localFile;
	job->deviceFolder = deviceFolder;
	job->upload = true;
	job->overwrite = overwrite;
	if (QFile copy(localFile); copy.open(QIODevice::ReadOnly))
		job->local = copy.readAll();
	job->viewer = qobject_cast<OpenFiles*>(viewer);
	const auto row = static_cast<int>(jobs_.size());
	beginInsertRows({}, row, row);
	jobs_.push_back(std::move(job));
	endInsertRows();
	startNext();
	Q_EMIT summaryChanged();
	return row;
}

bool DeviceFileTransfers::uploading(const QString& localFile) const
{
	return std::any_of(jobs_.begin(), jobs_.end(), [&](const auto& job) {
		return job->upload && job->active() && job->localFile == localFile;
	});
}

void DeviceFileTransfers::cancelFor(const QString& localFile)
{
	for (int row = static_cast<int>(jobs_.size()) - 1; row >= 0; --row) {
		const auto& job = *jobs_[static_cast<std::size_t>(row)];
		if (job.active() && !job.upload && job.localFile == localFile)
			cancel(row);
	}
}

int DeviceFileTransfers::queue(const QString& path, int fileIndex, qulonglong size, const QString& localFile,
                               OpenFiles* viewer, const QString& deviceFolder)
{
	if (!session_ || !session_->filesSupported() || fileIndex < 0 || localFile.isEmpty())
		return -1;
	auto job = std::make_unique<Job>();
	job->path = path;
	job->name = path.section(QLatin1Char('/'), -1);
	job->fileIndex = fileIndex;
	job->size = size;
	job->localFile = localFile;
	job->deviceFolder = deviceFolder;
	if (viewer) {
		// The tab is there before the first byte, reading.
		viewer->reading(localFile, path);
		job->viewer = viewer;
		job->opening = true;
	}
	const auto row = static_cast<int>(jobs_.size());
	beginInsertRows({}, row, row);
	jobs_.push_back(std::move(job));
	endInsertRows();
	startNext();
	Q_EMIT summaryChanged();
	return row;
}

void DeviceFileTransfers::startNext()
{
	if (std::any_of(jobs_.begin(), jobs_.end(), [](const auto& job) {
		    return job->state == State::Reading || job->state == State::Writing;
	    }))
		return;
	for (std::size_t row = 0; row < jobs_.size(); ++row) {
		auto& job = *jobs_[row];
		if (job.state != State::Waiting)
			continue;
		if (job.upload) {
			const bool started = session_ && beginUpload(job);
			if (!started && !session_) {
				job.outcome = Outcome::Failed;
				end(job, State::Failed, tr("Not written: not connected"));
			}
			publish(static_cast<int>(row));
			if (!started) {
				Q_EMIT finished(static_cast<int>(row), job.state, job.result);
				continue;
			}
			poll();
			return;
		}
		QDir().mkpath(QFileInfo(job.localFile).absolutePath());
		job.file.setFileName(job.localFile);
		if (!job.file.open(QIODevice::WriteOnly)) {
			end(job, State::Failed,
			    tr("Could not write %1: %2").arg(QDir::toNativeSeparators(job.localFile),
			                                     job.file.errorString()));
			publish(static_cast<int>(row));
			continue;
		}
		if (!session_) {
			job.file.cancelWriting();
			end(job, State::Failed, tr("Stopped: not connected"));
			publish(static_cast<int>(row));
			continue;
		}
		job.reader = std::make_unique<Job::Reader>(*session_, Job::Sink{&job});
		job.state = State::Reading;
		job.startedMs = job.sampleMs = job.dataMs = clock_.elapsed();
		(void)job.reader->start(static_cast<resource::FileIndex>(job.fileIndex));
		publish(static_cast<int>(row));
		poll();
		return;
	}
}

// The copy read whole, then the device's file read into memory, to be
// compared with its record before anything is written.
bool DeviceFileTransfers::beginUpload(Job& job)
{
	job.outcome = Outcome::Failed;
	job.cancelRequested = false;
	// Writes go by index; the index must still name this path.
	const auto paths = session_->discoveredPaths();
	if (job.fileIndex >= paths.size() || paths[job.fileIndex] != job.path) {
		end(job, State::Failed, tr("Not written: the device's list of files is not the one this was opened from"));
		return false;
	}
	// Read when it was asked for (upload) or tried again (retry).
	if (job.local.isNull()) {
		QFile source(job.localFile);
		if (!source.open(QIODevice::ReadOnly)) {
			end(job, State::Failed,
			    tr("Not written: could not read %1: %2").arg(QDir::toNativeSeparators(job.localFile), source.errorString()));
			return false;
		}
		job.local = source.readAll();
	}
	job.size = static_cast<quint64>(job.local.size());
	job.device.clear();
	job.step = Step::Check;
	job.kept = job.untilRestart = false;
	job.unkept.clear();
	job.sent = 0;
	job.reader = std::make_unique<Job::Reader>(*session_, Job::Sink{&job});
	job.state = State::Writing;
	job.startedMs = job.sampleMs = job.dataMs = clock_.elapsed();
	(void)job.reader->start(static_cast<resource::FileIndex>(job.fileIndex));
	return true;
}

namespace {
// Received and total in the total's unit: "5.0 of 64 KiB", as the bar shows them.
QString ofInUnit(quint64 done, quint64 total)
{
	// A little of a large file would read "0.0 of 1.0 MiB": each in its own unit then.
	double scale = 1;
	for (int unit = 0; unit < 4 && static_cast<double>(total) >= scale * 1024; ++unit)
		scale *= 1024;
	if (done > 0 && static_cast<double>(done) < scale / 20)
		return DeviceFileTransfers::tr("%1 of %2").arg(DeviceFilesModel::sizeText(done), DeviceFilesModel::sizeText(total));
	return pairInUnit(done, total, DeviceFileTransfers::tr("%1 of %2 %3", "done of total, then the unit"));
}

// When a record was read, for a sentence: "12:33" today, otherwise with its date.
QString whenText(const QDateTime& at)
{
	const QLocale locale;
	const auto time = locale.toString(at.time(), QLocale::ShortFormat);
	return at.date() == QDate::currentDate()
	           ? time
	           : DeviceFileTransfers::tr("%1 %2").arg(locale.toString(at.date(), QLocale::ShortFormat), time);
}

// Why a read of the device's file did not complete, as the end of a sentence.
QString readFailure(files::ReadState state, client::Status clientStatus)
{
	switch (state) {
		case files::ReadState::Cancelled:
			return DeviceFileTransfers::tr("cancelled");
		case files::ReadState::SinkStopped:
			return DeviceFileTransfers::tr("the device's file is longer than this copy");
		case files::ReadState::Refused:
			return DeviceFileTransfers::tr("the device refused to read it");
		case files::ReadState::NotReadable:
			return DeviceFileTransfers::tr("the device does not allow reading it");
		case files::ReadState::Malformed:
			return DeviceFileTransfers::tr("the device's answers were not valid");
		case files::ReadState::Stalled:
			return DeviceFileTransfers::tr("the device stopped sending the file");
		default:
			return clientStatus == client::Status::Timeout ? DeviceFileTransfers::tr("the device did not answer in time")
			                                               : DeviceFileTransfers::tr("the connection dropped");
	}
}

// Why a write or a save did not complete, as the end of a sentence.
QString writeFailure(files::WriteState state, client::Status clientStatus, resource::Status deviceStatus)
{
	switch (state) {
		case files::WriteState::Cancelled:
			return DeviceFileTransfers::tr("cancelled");
		case files::WriteState::NotWritable:
			return DeviceFileTransfers::tr("the device does not allow writing it");
		case files::WriteState::SizeMismatch:
			return DeviceFileTransfers::tr("the device's file is not this copy's size");
		case files::WriteState::Refused:
			return deviceStatus == resource::Status::InvalidData
			           ? DeviceFileTransfers::tr("the device refused the content")
			           : DeviceFileTransfers::tr("the device refused it (code %1)").arg(static_cast<int>(deviceStatus));
		case files::WriteState::Malformed:
			return DeviceFileTransfers::tr("the device's answers were not valid");
		case files::WriteState::Stalled:
			return DeviceFileTransfers::tr("the device stopped taking the file");
		default:
			return clientStatus == client::Status::Timeout || clientStatus == client::Status::OutcomeUnknown
			           ? DeviceFileTransfers::tr("the device did not answer in time")
			           : DeviceFileTransfers::tr("the connection dropped");
	}
}
} // namespace

bool DeviceFileTransfers::advanceUpload(Job& job)
{
	const auto index = static_cast<resource::FileIndex>(job.fileIndex);
	const auto ending = [&](State state, Outcome outcome, QString result) {
		job.reader.reset();
		job.writer.reset();
		job.local.clear();
		job.device.clear();
		job.outcome = outcome;
		end(job, state, std::move(result));
		// The copy's reference follows the record: kept anew, or gone.
		if (job.viewer)
			if (auto* content = job.viewer->contentFor(job.localFile))
				content->refreshReference();
		return true;
	};
	// Past the check the device's file may have changed: its record goes.
	const auto unknown = [&] {
		DeviceBytes::forget(job.localFile);
	};
	// Written whole; what the device did to keep it, for the end of a line.
	const auto keeping = [&](const QString& line) {
		if (job.untilRestart)
			return tr("%1 · kept until the device restarts").arg(line);
		if (!job.kept)
			return tr("%1 · may be lost when the device restarts: %2").arg(line, job.unkept);
		return line;
	};
	const auto notChecked = [&](const QString& why) {
		unknown();
		return ending(State::Written, Outcome::NotChecked, keeping(tr("Written · not checked: %1").arg(why)));
	};
	if (!session_) {
		if (job.step != Step::Check)
			unknown();
		return ending(State::Failed, job.step == Step::Check ? Outcome::Failed : Outcome::Partial,
		              tr("Stopped: not connected"));
	}
	if (job.reader) {
		if (!job.reader->finished())
			return false;
		const auto state = job.reader->state();
		const auto status = job.reader->clientStatus();
		const auto listed = static_cast<quint64>(job.reader->size());
		job.reader.reset();
		if (job.step == Step::Check) {
			const auto otherSize = [&](quint64 device) {
				return ending(State::Failed, Outcome::SizeDiffers,
				              tr("Not written: the device's file is %1; this copy is %2; the device cannot change "
				                 "a file's size")
				                  .arg(DeviceFilesModel::sizeText(device), DeviceFilesModel::sizeText(job.size)));
			};
			// Longer than the copy: read no further, and say both sizes.
			if (state == files::ReadState::SinkStopped)
				return otherSize(listed);
			if (state == files::ReadState::Cancelled || (state == files::ReadState::Complete && job.cancelRequested))
				return ending(State::Cancelled, Outcome::Failed, tr("Cancelled · nothing written"));
			if (state != files::ReadState::Complete)
				return ending(State::Failed, Outcome::Failed,
				              tr("Not written: the device's file could not be read first: %1")
				                  .arg(readFailure(state, status)));
			if (job.device.size() != job.local.size())
				return otherSize(static_cast<quint64>(job.device.size()));
			if (!job.overwrite && !DeviceBytes::matches(job.localFile, job.path, job.device)) {
				const auto record = DeviceBytes::find(job.localFile);
				if (record && record->devicePath == job.path)
					return ending(State::Failed, Outcome::Changed,
					              tr("Not written: the device's file changed after it was read at %1")
					                  .arg(whenText(record->readAt)));
				return ending(State::Failed, Outcome::NotKnown,
				              tr("Not written: nothing here records the device's file to compare with"));
			}
			job.step = Step::Write;
			job.writer = std::make_unique<Job::Writer>(*session_);
			(void)job.writer->start(index,
			                        client::Input{reinterpret_cast<const std::byte*>(job.local.constData()),
			                                      static_cast<std::size_t>(job.local.size())});
			return false;
		}
		// Read back: what the device holds now is the record.
		if (state == files::ReadState::Cancelled || job.cancelRequested)
			return notChecked(tr("cancelled"));
		if (state != files::ReadState::Complete)
			return notChecked(readFailure(state, status));
		if (job.device.size() != job.local.size())
			return notChecked(tr("read back %1 of %2").arg(DeviceFilesModel::sizeText(static_cast<quint64>(job.device.size())),
			                                               DeviceFilesModel::sizeText(job.size)));
		qint64 differing = 0;
		for (qsizetype at = 0; at < job.local.size(); ++at)
			differing += job.device[at] != job.local[at];
		const auto notKept = keepDeviceBytes_ ? DeviceBytes::keepBytes(job.deviceFolder, job.localFile, job.path,
		                                                               job.device, QDateTime::currentDateTime(), true)
		                                      : tr("not kept");
		if (!notKept.isEmpty())
			unknown();
		// Whole phrases: without a translation %n would read "1 byte(s)".
		const auto line = differing == 0   ? tr("Written · checked")
		                  : differing == 1 ? tr("Written · 1 byte reads back different")
		                                   : tr("Written · %1 bytes read back different")
		                                         .arg(QLocale().toString(differing));
		return ending(State::Written, differing == 0 ? Outcome::Checked : Outcome::ReadBackDiffers, keeping(line));
	}
	if (!job.writer || !job.writer->finished())
		return false;
	const auto state = job.writer->state();
	const auto reason = writeFailure(state, job.writer->clientStatus(), job.writer->deviceStatus());
	if (job.step == Step::Write) {
		job.sent = job.writer->written();
		if (state == files::WriteState::Complete) {
			if (job.cancelRequested)
				return notChecked(tr("cancelled"));
			job.step = Step::Save;
			(void)job.writer->save(index);
			return false;
		}
		// Nothing reached the file when its Stat stopped it - or never came
		// back, so no WRITE left - or the device refused the first piece;
		// anything else may have changed it.
		const bool statted = job.writer->size() == job.size && job.size != 0;
		const bool untouched = state == files::WriteState::NotWritable ||
		                       state == files::WriteState::SizeMismatch || (job.sent == 0 && !statted) ||
		                       ((state == files::WriteState::Refused || state == files::WriteState::Cancelled) &&
		                        job.sent == 0);
		if (untouched) {
			if (state == files::WriteState::Cancelled)
				return ending(State::Cancelled, Outcome::Failed, tr("Cancelled · nothing written"));
			const bool refused = state == files::WriteState::NotWritable || state == files::WriteState::Refused;
			return ending(State::Failed, refused ? Outcome::NotWritable : Outcome::Failed,
			              tr("Not written: %1").arg(reason));
		}
		unknown();
		if (state == files::WriteState::Cancelled)
			return ending(State::Cancelled, Outcome::Partial,
			              tr("Cancelled at %1. The device's %2 may now be part old, part new.")
			                  .arg(ofInUnit(job.sent, job.size), job.name));
		return ending(State::Failed, Outcome::Partial,
		              tr("Writing stopped at %1: %2. The device's %3 may now be part old, part new.")
		                  .arg(ofInUnit(job.sent, job.size), reason, job.name));
	}
	// Saved, or why not; then read back either way, unless cancelled.
	job.kept = state == files::WriteState::Complete;
	job.untilRestart = state == files::WriteState::NotWritable;
	if (!job.kept && !job.untilRestart)
		job.unkept = reason;
	job.writer.reset();
	if (job.cancelRequested || state == files::WriteState::Cancelled)
		return notChecked(tr("cancelled"));
	job.step = Step::Verify;
	job.device.clear();
	job.reader = std::make_unique<Job::Reader>(*session_, Job::Sink{&job});
	(void)job.reader->start(index);
	return false;
}

void DeviceFileTransfers::schedulePoll()
{
	if (pollQueued_)
		return;
	pollQueued_ = true;
	QTimer::singleShot(0, this, [this] {
		pollQueued_ = false;
		poll();
	});
}

void DeviceFileTransfers::poll()
{
	for (std::size_t row = 0; row < jobs_.size(); ++row) {
		auto& job = *jobs_[row];
		if (job.state == State::Writing) {
			// A cancel waits for the request on its way, then stops before
			// the next: one already sent is never cancelled in the queue,
			// which would leave a WRITE's outcome not known and the session
			// to recover.
			if (job.cancelRequested && job.reader && !job.reader->inFlight())
				job.reader->cancel();
			if (job.cancelRequested && job.writer && !job.writer->inFlight())
				job.writer->cancel();
			if (job.reader)
				(void)job.reader->poll();
			if (job.writer)
				(void)job.writer->poll();
			// A step may end and start the next at once: go on while it does.
			const auto stepBefore = job.step;
			if (!advanceUpload(job)) {
				if (job.step != stepBefore) {
					schedulePoll();
					publish(static_cast<int>(row));
				} else if (clock_.elapsed() - job.publishedMs >= publishEveryMs) {
					publish(static_cast<int>(row));
				}
				continue;
			}
			publish(static_cast<int>(row));
			Q_EMIT finished(static_cast<int>(row), job.state, job.result);
			startNext();
			Q_EMIT summaryChanged();
			return;
		}
		if (job.state != State::Reading || !job.reader)
			continue;
		(void)job.reader->poll();
		const auto now = clock_.elapsed();
		const auto received = job.reader->received();
		if (received != job.lastReceived) {
			const double seconds = static_cast<double>(now - job.sampleMs) / 1000.0;
			if (seconds > 0) {
				const double instant = static_cast<double>(received - job.lastReceived) / seconds;
				const double weight = 1.0 - std::exp(-seconds / rateWindowSeconds);
				job.rate = job.rate == 0 ? instant : weight * instant + (1 - weight) * job.rate;
			}
			job.lastReceived = received;
			job.sampleMs = job.dataMs = now;
		}
		if (job.reader->finished()) {
			const auto state = job.reader->state();
			if (state == files::ReadState::Complete) {
				const bool written = job.file.commit();
				// A file read for the viewer: the device's bytes as read, kept in
				// the device's folder the host named. Any other write of a copy
				// with a record removes it, since it would no longer be true.
				QString notKept;
				if (written) {
					if (keepDeviceBytes_ && job.opening && !job.deviceFolder.isEmpty())
						notKept = DeviceBytes::keep(job.deviceFolder, job.localFile, job.path,
						                            QDateTime::currentDateTime());
					else
						DeviceBytes::forget(job.localFile);
					if (!notKept.isEmpty())
						DeviceBytes::forget(job.localFile);
				}
				if (!written)
					end(job, State::Failed,
					    tr("Could not write %1: %2")
					        .arg(QDir::toNativeSeparators(job.localFile), job.file.errorString()));
				else if (job.size != 0 && received > job.size)
					end(job, State::Saved,
					    tr("Saved %1. The device listed %2; the file grew while it was read.")
					        .arg(DeviceFilesModel::sizeText(received),
					             DeviceFilesModel::sizeText(job.size)));
				else
					end(job, State::Saved,
					    !notKept.isEmpty() ? tr("Opened in the Viewer · the device's bytes were not kept: %1").arg(notKept)
					    : job.opening      ? tr("Opened in the Viewer")
					                       : tr("Saved"));
			} else {
				const auto writeError = job.file.errorString();
				job.file.cancelWriting();
				switch (state) {
					case files::ReadState::Cancelled:
						end(job, State::Cancelled, tr("Cancelled · partial file removed"));
						break;
					case files::ReadState::SinkStopped:
						end(job, State::Failed,
						    tr("Could not write %1: %2")
						        .arg(QDir::toNativeSeparators(job.localFile), writeError));
						break;
					case files::ReadState::Refused:
						end(job, State::Failed, tr("Stopped: the device refused to read it"));
						break;
					case files::ReadState::NotReadable:
						end(job, State::Failed, tr("Stopped: the device does not allow reading it"));
						break;
					case files::ReadState::Malformed:
						end(job, State::Failed, tr("Stopped: the device's answers were not valid"));
						break;
					case files::ReadState::Stalled:
						end(job, State::Failed, tr("Stopped: the device stopped sending the file"));
						break;
					default:
						end(job, State::Failed,
						    job.reader->clientStatus() == client::Status::Timeout
						        ? tr("Stopped: the device did not answer in time")
						        : tr("Stopped: the connection dropped"));
						break;
				}
			}
			job.reader.reset();
			publish(static_cast<int>(row));
			Q_EMIT finished(static_cast<int>(row), job.state, job.result);
			startNext();
			Q_EMIT summaryChanged();
			return;
		}
		if (now - job.publishedMs >= publishEveryMs)
			publish(static_cast<int>(row));
	}
}

void DeviceFileTransfers::end(Job& job, State state, QString result)
{
	job.state = state;
	job.result = std::move(result);
}

void DeviceFileTransfers::publish(int row)
{
	auto& job = *jobs_[static_cast<std::size_t>(row)];
	job.publishedMs = clock_.elapsed();
	const auto changed = index(row);
	Q_EMIT dataChanged(changed, changed);
	Q_EMIT summaryChanged();
	// A file read to be shown: its tab follows, then shows it or says why not.
	if (!job.opening || job.viewerTold || !job.viewer)
		return;
	auto* content = job.viewer->contentFor(job.localFile);
	if (!content)
		return;
	if (job.active())
		content->setReading(data(changed, ProgressRole).toReal(), data(changed, FiguresRole).toString());
	else {
		job.viewerTold = true;
		content->finishReading(job.state == State::Saved, job.result);
	}
}

void DeviceFileTransfers::cancel(int row)
{
	if (row < 0 || row >= static_cast<int>(jobs_.size()))
		return;
	auto& job = *jobs_[static_cast<std::size_t>(row)];
	if (job.state == State::Waiting) {
		end(job, State::Cancelled, tr("Cancelled"));
		publish(row);
		Q_EMIT finished(row, job.state, job.result);
	} else if (job.state == State::Reading && job.reader) {
		job.reader->cancel();
		poll();
	} else if (job.state == State::Writing) {
		job.cancelRequested = true;
		poll();
	}
}

bool DeviceFileTransfers::retry(int row, bool overwrite)
{
	if (row < 0 || row >= static_cast<int>(jobs_.size()))
		return false;
	auto& job = *jobs_[static_cast<std::size_t>(row)];
	if (job.active() || job.state == State::Saved ||
	    (job.state == State::Written && job.outcome != Outcome::NotChecked) || !session_ ||
	    !session_->filesSupported())
		return false;
	// The same row starts again from the first byte, under the same name.
	job.state = State::Waiting;
	job.result.clear();
	job.lastReceived = 0;
	job.pieces = 0;
	job.rate = 0;
	job.shownLeft = -1;
	job.publishedMs = -1000;
	job.sent = 0;
	if (job.upload) {
		job.overwrite = job.overwrite || overwrite;
		job.outcome = Outcome::None;
		job.local = QByteArray();
		if (QFile copy(job.localFile); copy.open(QIODevice::ReadOnly))
			job.local = copy.readAll();
	}
	publish(row);
	startNext();
	return true;
}

void DeviceFileTransfers::cancelAll()
{
	for (int row = static_cast<int>(jobs_.size()) - 1; row >= 0; --row)
		cancel(row);
}

void DeviceFileTransfers::clearFinished()
{
	for (int row = static_cast<int>(jobs_.size()) - 1; row >= 0; --row) {
		if (jobs_[static_cast<std::size_t>(row)]->active())
			continue;
		beginRemoveRows({}, row, row);
		jobs_.erase(jobs_.begin() + row);
		endRemoveRows();
	}
	Q_EMIT summaryChanged();
}

QString DeviceFileTransfers::figures(quint64 received, quint64 size, double rate,
                                     double secondsLeft, bool rateTrusted)
{
	if (size != 0 && received > size)
		return tr("%1 (more than the listed %2: the file is growing)")
		    .arg(DeviceFilesModel::sizeText(received), DeviceFilesModel::sizeText(size));
	QString text = size != 0 ? pairInUnit(received, size) : DeviceFilesModel::sizeText(received);
	if (rateTrusted && rate > 0)
		text = joined(text, rateText(rate));
	if (secondsLeft > 0)
		text = joined(text, leftText(secondsLeft));
	return text;
}

int DeviceFileTransfers::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(jobs_.size());
}

QVariant DeviceFileTransfers::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(jobs_.size()))
		return {};
	auto& job = *jobs_[static_cast<std::size_t>(index.row())];
	const auto received = job.received();
	switch (role) {
		case Qt::DisplayRole:
		case NameRole:
			return job.name;
		case PathRole:
			return job.path;
		case LocalPathRole:
			return QDir::toNativeSeparators(job.localFile);
		case StateRole:
			return static_cast<int>(job.state);
		case ReceivedRole:
			return QVariant::fromValue<qulonglong>(received);
		case SizeRole:
			return QVariant::fromValue<qulonglong>(job.size);
		case ProgressRole:
			if (job.state == State::Saved || job.state == State::Written)
				return 1.0;
			if (job.state == State::Writing)
				return job.step != Step::Save && job.size > 0 && received <= job.size
				           ? static_cast<double>(received) / static_cast<double>(job.size)
				           : -1.0;
			if (job.state == State::Waiting)
				return 0.0;
			return job.size > 0 && received <= job.size
			           ? static_cast<double>(received) / static_cast<double>(job.size)
			           : -1.0;
		case FiguresRole: {
			if (job.state == State::Writing) {
				switch (job.step) {
					case Step::Check:
						return joined(tr("checking"), pairInUnit(received, job.size));
					case Step::Write:
						return pairInUnit(received, job.size);
					case Step::Save:
						return tr("asking the device to keep it");
					case Step::Verify:
						return joined(tr("reading it back to check"), pairInUnit(received, job.size));
				}
			}
			if (job.state != State::Reading)
				return job.state == State::Waiting ? tr("waiting") : QString{};
			const auto now = clock_.elapsed();
			if (now - job.dataMs >= 3000 && job.pieces > 0)
				return joined(figures(received, job.size, 0, -1, false), tr("waiting for the device…"));
			const bool trusted = now - job.startedMs >= 2000 && job.pieces >= 3 && job.rate > 0;
			double left = -1;
			if (trusted && job.size > received) {
				const double estimate = static_cast<double>(job.size - received) / job.rate;
				const double total = static_cast<double>(now - job.startedMs) / 1000.0 + estimate;
				if (total > 10) {
					// Moves only by a tenth or more, so it never jitters upwards.
					if (job.shownLeft < 0 || std::abs(estimate - job.shownLeft) >= 0.1 * job.shownLeft)
						job.shownLeft = estimate;
					left = job.shownLeft;
				}
			}
			return figures(received, job.size, job.rate, left, trusted);
		}
		case ResultRole:
			return job.result;
		case ActiveRole:
			return job.active();
		case OpeningRole:
			return job.opening;
		case UploadRole:
			return job.upload;
		case OutcomeRole:
			return static_cast<int>(job.outcome);
		case StepRole:
			return static_cast<int>(job.step);
		default:
			return {};
	}
}

QHash<int, QByteArray> DeviceFileTransfers::roleNames() const
{
	return {{NameRole, "name"},         {PathRole, "path"},       {LocalPathRole, "localPath"},
	        {StateRole, "state"},       {ReceivedRole, "received"}, {SizeRole, "size"},
	        {ProgressRole, "progress"}, {FiguresRole, "figures"},   {ResultRole, "result"},
	        {ActiveRole, "active"},     {OpeningRole, "opening"},   {UploadRole, "upload"},
	        {OutcomeRole, "outcome"},   {StepRole, "step"}};
}
} // namespace telemetry::ux
