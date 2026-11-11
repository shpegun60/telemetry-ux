/*
 * @file DeviceFileTransfers.cpp
 * @brief Downloads of the device's files: one at a time, with progress, never blocking.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "DeviceFileTransfers.hpp"
#include "DeviceFilesModel.hpp"
#include <integration/files/Client.hpp>
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
			return job->file.write(reinterpret_cast<const char*>(piece.data()), bytes) == bytes;
		}
	};

	using Reader = files::FileReader<Session, Sink>;

	// Keeps a job whose reader still has a request in flight after the model
	// has gone; the session drains it, and this ends with it.
	class Orphan;

	QString path, name, localFile, result;
	int fileIndex = -1;
	quint64 size = 0, lastReceived = 0;
	State state = State::Waiting;
	QSaveFile file;
	std::unique_ptr<Reader> reader;
	qint64 startedMs = 0, sampleMs = 0, publishedMs = -1000, dataMs = 0;
	double rate = 0, shownLeft = -1;
	unsigned pieces = 0;

	bool active() const noexcept
	{
		return state == State::Waiting || state == State::Reading;
	}

	quint64 received() const noexcept
	{
		return reader ? reader->received() : lastReceived;
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
		if (!job_->reader || job_->reader->finished())
			deleteLater();
	}

	std::unique_ptr<Job> job_;
};

namespace {
constexpr qint64 publishEveryMs = 500;
constexpr double rateWindowSeconds = 2.0;

// Received and total in the total's unit, written once: "96 / 128 KiB".
QString pairInUnit(quint64 received, quint64 total)
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
	return QStringLiteral("%1 / %2 %3").arg(number(received), number(total), QString::fromLatin1(units[unit]));
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
		if (!job->reader)
			continue;
		job->reader->cancel();
		// A reader whose request is still in flight must outlive it.
		if (!job->reader->finished() && session_)
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
		return job->state == State::Reading;
	});
	if (running == jobs_.end())
		return {};
	const auto& job = **running;
	const auto others = activeCount() - 1;
	const auto progress = summaryProgress();
	// Whole patterns, one per case; the device's name goes in through a single
	// arg() call, so nothing in it is ever read as a placeholder.
	if (progress < 0)
		return others > 0 ? tr("Downloading %1 · %n more", nullptr, others).arg(job.name)
		                  : tr("Downloading %1").arg(job.name);
	const auto percent = QString::number(static_cast<int>(std::floor(progress * 100)));
	return others > 0 ? tr("Downloading %1 · %2 % · %n more", nullptr, others).arg(job.name, percent)
	                  : tr("Downloading %1 · %2 %").arg(job.name, percent);
}

qreal DeviceFileTransfers::summaryProgress() const
{
	for (const auto& job : jobs_)
		if (job->state == State::Reading) {
			const auto received = job->received();
			return job->size > 0 && received <= job->size
			           ? static_cast<qreal>(received) / static_cast<qreal>(job->size)
			           : -1;
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

int DeviceFileTransfers::download(const QString& path, int fileIndex, qulonglong size,
                                  const QString& localFile)
{
	if (!session_ || !session_->filesSupported() || fileIndex < 0 || localFile.isEmpty())
		return -1;
	auto job = std::make_unique<Job>();
	job->path = path;
	job->name = path.section(QLatin1Char('/'), -1);
	job->fileIndex = fileIndex;
	job->size = size;
	job->localFile = localFile;
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
		    return job->state == State::Reading;
	    }))
		return;
	for (std::size_t row = 0; row < jobs_.size(); ++row) {
		auto& job = *jobs_[row];
		if (job.state != State::Waiting)
			continue;
		QDir().mkpath(QFileInfo(job.localFile).absolutePath());
		job.file.setFileName(job.localFile);
		if (!job.file.open(QIODevice::WriteOnly)) {
			end(job, State::Failed,
			    tr("Could not write %1: %2").arg(QDir::toNativeSeparators(job.localFile),
			                                     job.file.errorString()));
			continue;
		}
		if (!session_) {
			job.file.cancelWriting();
			end(job, State::Failed, tr("Stopped: not connected"));
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
				if (!job.file.commit())
					end(job, State::Failed,
					    tr("Could not write %1: %2")
					        .arg(QDir::toNativeSeparators(job.localFile), job.file.errorString()));
				else if (job.size != 0 && received > job.size)
					end(job, State::Saved,
					    tr("Saved %1. The device listed %2; the file grew while it was read.")
					        .arg(DeviceFilesModel::sizeText(received),
					             DeviceFilesModel::sizeText(job.size)));
				else
					end(job, State::Saved, tr("Saved"));
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
	}
}

bool DeviceFileTransfers::retry(int row)
{
	if (row < 0 || row >= static_cast<int>(jobs_.size()))
		return false;
	auto& job = *jobs_[static_cast<std::size_t>(row)];
	if (job.active() || job.state == State::Saved || !session_ || !session_->filesSupported())
		return false;
	// The same row starts again from the first byte, under the same name.
	job.state = State::Waiting;
	job.result.clear();
	job.lastReceived = 0;
	job.pieces = 0;
	job.rate = 0;
	job.shownLeft = -1;
	job.publishedMs = -1000;
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
			if (job.state == State::Saved)
				return 1.0;
			if (job.state == State::Waiting)
				return 0.0;
			return job.size > 0 && received <= job.size
			           ? static_cast<double>(received) / static_cast<double>(job.size)
			           : -1.0;
		case FiguresRole: {
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
		default:
			return {};
	}
}

QHash<int, QByteArray> DeviceFileTransfers::roleNames() const
{
	return {{NameRole, "name"},         {PathRole, "path"},       {LocalPathRole, "localPath"},
	        {StateRole, "state"},       {ReceivedRole, "received"}, {SizeRole, "size"},
	        {ProgressRole, "progress"}, {FiguresRole, "figures"},   {ResultRole, "result"},
	        {ActiveRole, "active"}};
}
} // namespace telemetry::ux
