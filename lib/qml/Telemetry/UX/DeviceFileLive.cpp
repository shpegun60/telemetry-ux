/*
 * @file DeviceFileLive.cpp
 * @brief One device file read again and again into memory, for a view that shows it live.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "DeviceFileLive.hpp"
#include "DeviceFilesModel.hpp"
#include <integration/files/Client.hpp>
#include <algorithm>

namespace telemetry::ux {
namespace files = integration::files;
using Session = integration::qt::ConnectionSession;

struct DeviceFileLive::Job {
	struct Sink {
		QByteArray* bytes;

		bool operator()(client::Input piece) noexcept
		{
			bytes->append(reinterpret_cast<const char*>(piece.data()), static_cast<qsizetype>(piece.size()));
			return true;
		}
	};

	using Reader = files::FileReader<Session, Sink>;

	QByteArray bytes;
	std::unique_ptr<Reader> reader;
};

// Keeps a read that is still on its way after its owner has gone; the session
// finishes it, and this goes with it.
class DeviceFileLive::Orphan final : public QObject {
public:
	Orphan(std::unique_ptr<Job> job, Session* session) : QObject(session), job_(std::move(job))
	{
		job_->reader->cancel();
		// Polled on the loop's next turn, never inside the session's completion.
		connect(session, &Session::progressed, this, [this] { QTimer::singleShot(0, this, [this] { check(); }); });
		connect(session, &Session::stateChanged, this, [this] { QTimer::singleShot(0, this, [this] { check(); }); });
		QTimer::singleShot(0, this, [this] { check(); });
	}

private:
	void check()
	{
		(void)job_->reader->poll();
		if (job_->reader->finished())
			deleteLater();
	}

	std::unique_ptr<Job> job_;
};

DeviceFileLive::DeviceFileLive(Session* session, DeviceFilesModel* files, QString devicePath, QObject* parent)
    : QObject(parent), session_(session), files_(files), devicePath_(std::move(devicePath))
{
	clock_.start();
	next_.setSingleShot(true);
	// Exact: a coarse timer may fire early, and reads must start the period apart.
	next_.setTimerType(Qt::PreciseTimer);
	connect(&next_, &QTimer::timeout, this, [this] { begin(); });
	if (session_) {
		connect(session_, &Session::progressed, this, [this] { schedulePoll(); });
		// A connection that comes back resumes a live view that waited for it.
		connect(session_, &Session::stateChanged, this, [this] {
			schedulePoll();
			if (running_ && !job_ && !next_.isActive())
				begin();
		});
	}
	if (files_)
		connect(files_, &DeviceFilesModel::stateChanged, this, [this] {
			if (running_ && !job_ && !next_.isActive())
				begin();
		});
}

DeviceFileLive::~DeviceFileLive()
{
	if (job_ && job_->reader && !job_->reader->finished()) {
		if (session_)
			new Orphan(std::move(job_), session_);
		// Without a session there is no request left to complete.
	}
}

bool DeviceFileLive::reading() const noexcept
{
	return job_ != nullptr;
}

void DeviceFileLive::setRunning(bool running)
{
	if (running_ == running)
		return;
	running_ = running;
	if (!running_) {
		// The read on its way finishes and is shown; none follows it.
		next_.stop();
		setProblem({});
	}
	Q_EMIT stateChanged();
	if (running_)
		begin();
}

void DeviceFileLive::setPeriodMs(int periodMs)
{
	periodMs = std::max(periodMs, minimumPeriodMs);
	if (periodMs_ == periodMs)
		return;
	periodMs_ = periodMs;
	Q_EMIT stateChanged();
	if (next_.isActive())
		schedule();
}

void DeviceFileLive::setProblem(const QString& problem)
{
	if (problem_ == problem)
		return;
	problem_ = problem;
	Q_EMIT stateChanged();
}

void DeviceFileLive::begin()
{
	if (!running_ || job_)
		return;
	if (!session_ || !session_->filesSupported()) {
		setProblem(tr("Waiting for the device: not connected"));
		return;
	}
	if (!files_ || files_->loading()) {
		setProblem(files_ ? QString{} : tr("The device's files are not available"));
		return;
	}
	const auto at = files_->indexOfPath(devicePath_);
	const auto file = files_->fileAt(at);
	if (!at.isValid() || file.isEmpty()) {
		setProblem(tr("The device no longer lists this file"));
		return;
	}
	// Readable is known once the file was statted; until then it is tried.
	if (file.value(QStringLiteral("sizeKnown")).toBool() && !file.value(QStringLiteral("readable")).toBool()) {
		setProblem(tr("The device does not allow reading this file"));
		return;
	}
	auto job = std::make_unique<Job>();
	job->bytes.reserve(static_cast<qsizetype>(file.value(QStringLiteral("size")).toULongLong()));
	job->reader = std::make_unique<Job::Reader>(*session_, Job::Sink{&job->bytes});
	job_ = std::move(job);
	startedMs_ = clock_.elapsed();
	(void)job_->reader->start(static_cast<resource::FileIndex>(file.value(QStringLiteral("fileIndex")).toInt()));
	Q_EMIT stateChanged();
	poll();
}

void DeviceFileLive::schedulePoll()
{
	if (pollQueued_)
		return;
	pollQueued_ = true;
	QTimer::singleShot(0, this, [this] {
		pollQueued_ = false;
		poll();
	});
}

void DeviceFileLive::poll()
{
	if (!job_)
		return;
	(void)job_->reader->poll();
	if (!job_->reader->finished())
		return;
	const auto state = job_->reader->state();
	const auto status = job_->reader->clientStatus();
	auto bytes = std::move(job_->bytes);
	job_.reset();
	endedMs_ = clock_.elapsed();
	lastReadMs_ = static_cast<int>(endedMs_ - startedMs_);
	switch (state) {
		case files::ReadState::Complete:
			++reads_;
			problem_.clear();
			Q_EMIT snapshot(bytes);
			break;
		case files::ReadState::Cancelled:
			break;
		case files::ReadState::Refused:
			problem_ = tr("The device refused to read the file");
			break;
		case files::ReadState::NotReadable:
			problem_ = tr("The device does not allow reading this file");
			break;
		case files::ReadState::Malformed:
			problem_ = tr("The device's answers were not valid");
			break;
		case files::ReadState::Stalled:
			problem_ = tr("The device stopped sending the file");
			break;
		default:
			problem_ = status == client::Status::Timeout ? tr("The device did not answer in time")
			                                             : tr("Waiting for the device: not connected");
			break;
	}
	Q_EMIT stateChanged();
	if (running_)
		schedule();
}

void DeviceFileLive::schedule()
{
	// Never before this read ended (it has); as fast as it reads at the
	// shortest period, 200 ms after it started; a longer period from its end.
	const auto due = (periodMs_ <= minimumPeriodMs ? startedMs_ + minimumPeriodMs : endedMs_ + periodMs_) -
	                 clock_.elapsed();
	next_.start(static_cast<int>(std::max<qint64>(0, due)));
}
} // namespace telemetry::ux
