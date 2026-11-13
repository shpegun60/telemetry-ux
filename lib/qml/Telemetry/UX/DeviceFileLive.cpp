/*
 * @file DeviceFileLive.cpp
 * @brief One device file read again and again into memory, for a view that shows it live.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "DeviceFileLive.hpp"
#include "DeviceFilesModel.hpp"
#include "FileContent.hpp"
#include <integration/files/Client.hpp>
#include <algorithm>

namespace telemetry::ux {
namespace files = integration::files;
using Session = integration::qt::ConnectionSession;
using IdentityState = Session::IdentityState;

struct DeviceFileLive::Job {
	struct Sink {
		QByteArray* bytes;

		// Stops past the viewer's limit: a file that keeps growing is not read forever.
		bool operator()(client::Input piece) noexcept
		{
			if (bytes->size() + static_cast<qsizetype>(piece.size()) > FileContent::maxBytes)
				return false;
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
		// Retain the accepted request's reply correlation on the shared session.
		// Cancelling after handoff would require recovery for this raw reply.
		job_->reader->stopAfterCurrent();
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
	// The file belongs to the device connected now: Live reads it from that
	// device only.
	if (session_ && session_->filesSupported() && session_->identityState() != IdentityState::Reading)
		owner();
	if (session_) {
		// Only a read on its way needs the session's progress.
		connect(session_, &Session::progressed, this, [this] {
			if (job_)
				schedulePoll();
		});
		// A connection that comes back resumes a live view that waited for it,
		// once the device has said who it is.
		connect(session_, &Session::stateChanged, this, [this] {
			if (job_)
				schedulePoll();
			if (!session_->filesSupported())
				lostConnection_ = true;
			resume();
		});
		connect(session_, &Session::identityChanged, this, [this] { resume(); });
	}
	if (files_)
		connect(files_, &DeviceFilesModel::stateChanged, this, [this] { resume(); });
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

void DeviceFileLive::owner()
{
	ownerKnown_ = true;
	ownerId_ = session_->identityState() == IdentityState::Known ? session_->deviceId() : QString{};
}

void DeviceFileLive::setRunning(bool running)
{
	if (running_ == running)
		return;
	running_ = running;
	next_.stop();
	if (!running_) {
		// The read on its way finishes and is shown; none follows it.
		problem_.clear();
		waiting_ = false;
	}
	QPointer<DeviceFileLive> alive(this);
	Q_EMIT stateChanged();
	if (!alive || !running_)
		return;
	// Turned on again soon after a read: still the period's spacing.
	if (startedMs_ >= 0 && !job_)
		schedule();
	else
		begin();
}

void DeviceFileLive::setPeriodMs(int periodMs)
{
	periodMs = std::max(periodMs, minimumPeriodMs);
	if (periodMs_ == periodMs)
		return;
	periodMs_ = periodMs;
	QPointer<DeviceFileLive> alive(this);
	Q_EMIT stateChanged();
	if (!alive)
		return;
	if (next_.isActive())
		schedule();
}

void DeviceFileLive::setProblem(const QString& problem, bool waiting)
{
	if (problem_ == problem && waiting_ == waiting)
		return;
	problem_ = problem;
	waiting_ = waiting;
	Q_EMIT stateChanged();
}

void DeviceFileLive::stopBecause(const QString& problem)
{
	running_ = false;
	next_.stop();
	problem_ = problem;
	waiting_ = false;
	Q_EMIT stateChanged();
}

void DeviceFileLive::resume()
{
	if (running_ && !job_ && !next_.isActive())
		begin();
}

void DeviceFileLive::begin()
{
	if (!running_ || job_)
		return;
	if (!session_ || !session_->filesSupported()) {
		lostConnection_ = true;
		setProblem(tr("not connected"), true);
		return;
	}
	// Who the device is comes with the connection; wait for it.
	if (session_->identityState() == IdentityState::Reading || !files_ || files_->loading()) {
		setProblem(files_ ? QString{} : tr("the device's files are not available"));
		return;
	}
	if (!ownerKnown_)
		owner();
	// Only the device this file came from. Without an identity to compare,
	// a device that went and came back may be another one: Live stops.
	const auto id = session_->identityState() == IdentityState::Known ? session_->deviceId() : QString{};
	if (id != ownerId_ || (lostConnection_ && ownerId_.isEmpty())) {
		stopBecause(ownerId_.isEmpty() ? tr("the connection was lost, and this device has no identity to tell it is "
		                                    "the same one")
		                               : id.isEmpty() ? tr("the connected device is not %1, which this file came from")
		                                                    .arg(ownerId_)
		                                              : tr("another device is connected (%1), not %2, which this "
		                                                   "file came from")
		                                                    .arg(id, ownerId_));
		return;
	}
	lostConnection_ = false;
	const auto at = files_->indexOfPath(devicePath_);
	const auto file = files_->fileAt(at);
	if (!at.isValid() || file.isEmpty()) {
		stopBecause(tr("the device no longer lists this file"));
		return;
	}
	// Readable is known once the file was statted; until then it is tried.
	if (file.value(QStringLiteral("sizeKnown")).toBool() && !file.value(QStringLiteral("readable")).toBool()) {
		stopBecause(tr("the device does not allow reading this file"));
		return;
	}
	auto job = std::make_unique<Job>();
	const auto listed = file.value(QStringLiteral("size")).toULongLong();
	job->bytes.reserve(static_cast<qsizetype>(std::min<qulonglong>(listed, FileContent::maxBytes)));
	job->reader = std::make_unique<Job::Reader>(*session_, Job::Sink{&job->bytes});
	job_ = std::move(job);
	startedMs_ = clock_.elapsed();
	(void)job_->reader->start(static_cast<resource::FileIndex>(file.value(QStringLiteral("fileIndex")).toInt()));
	QPointer<DeviceFileLive> alive(this);
	Q_EMIT stateChanged();
	if (alive)
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
	// The session gone: it has finished every request it had; never submit to it.
	if (!session_) {
		job_.reset();
		setProblem(tr("not connected"), true);
		return;
	}
	(void)job_->reader->poll();
	if (!job_->reader->finished())
		return;
	const auto state = job_->reader->state();
	const auto status = job_->reader->clientStatus();
	auto bytes = std::move(job_->bytes);
	job_.reset();
	endedMs_ = clock_.elapsed();
	lastReadMs_ = static_cast<int>(endedMs_ - startedMs_);
	waiting_ = false;
	QPointer<DeviceFileLive> alive(this);

	switch (state) {
		case files::ReadState::Complete:
			++reads_;
			problem_.clear();
			Q_EMIT snapshot(bytes);
			if (!alive)
				return;
			break;
		case files::ReadState::Cancelled:
			break;
		// The device says no: asking again at every period would not change it.
		case files::ReadState::Refused:
			stopBecause(tr("the device refused to read the file"));
			return;
		case files::ReadState::NotReadable:
			stopBecause(tr("the device does not allow reading this file"));
			return;
		case files::ReadState::SinkStopped:
			stopBecause(tr("the file is larger than %1 MiB").arg(FileContent::maxBytes / (1024 * 1024)));
			return;
		case files::ReadState::Malformed:
			problem_ = tr("the device's answers were not valid");
			break;
		case files::ReadState::Stalled:
			problem_ = tr("the device stopped sending the file");
			break;
		default:
			if (status == client::Status::Timeout) {
				problem_ = tr("the device did not answer in time");
			} else {
				problem_ = tr("not connected");
				waiting_ = true;
				lostConnection_ = true;
			}
			break;
	}
	Q_EMIT stateChanged();
	if (!alive)
		return;
	if (running_)
		schedule();
}

void DeviceFileLive::schedule()
{
	// Never before the last read ended; as fast as it reads at the shortest
	// period, 200 ms after it started; a longer period from its end.
	const auto due = (periodMs_ <= minimumPeriodMs ? startedMs_ + minimumPeriodMs : endedMs_ + periodMs_) -
	                 clock_.elapsed();
	next_.start(static_cast<int>(std::max<qint64>(0, due)));
}
} // namespace telemetry::ux
