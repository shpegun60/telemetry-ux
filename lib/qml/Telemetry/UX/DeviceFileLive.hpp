/*
 * @file DeviceFileLive.hpp
 * @brief One device file read again and again into memory, for a view that shows it live.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * Live in the viewer: while running, the file is read whole through the
 * session's one queue, handed out as a snapshot, and read again. A read never
 * overlaps the one before it. At the fastest period, 200 ms, the next read
 * starts when the last one has ended and 200 ms after it started - as fast as
 * the file reads, never faster; a longer period counts from the end of the
 * last read. Nothing is written to disk; the downloaded copy stays as it was.
 *
 * Without a connection it waits and says so, and resumes when the device is
 * back. A read in flight when this goes away is finished by the session.
 */
#ifndef TELEMETRY_UX_DEVICE_FILE_LIVE_HPP
#define TELEMETRY_UX_DEVICE_FILE_LIVE_HPP
#pragma once

#include <integration/qt/ConnectionSession.hpp>
#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <memory>

namespace telemetry::ux {
class DeviceFilesModel;

class DeviceFileLive final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString devicePath READ devicePath CONSTANT)
	Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY stateChanged)
	// minimumPeriodMs: as fast as the file reads; longer: from one read's end
	// to the next read.
	Q_PROPERTY(int periodMs READ periodMs WRITE setPeriodMs NOTIFY stateChanged)
	// A read is on its way.
	Q_PROPERTY(bool reading READ reading NOTIFY stateChanged)
	// Whole reads handed out since this was made.
	Q_PROPERTY(int reads READ reads NOTIFY stateChanged)
	// How long the last whole read took.
	Q_PROPERTY(int lastReadMs READ lastReadMs NOTIFY stateChanged)
	// Why the last read did not give the file, as a reason a sentence can
	// carry ("the device refused to read the file"); empty while it did.
	Q_PROPERTY(QString problem READ problem NOTIFY stateChanged)
	// The reason is that no device is connected: reading resumes by itself
	// when it is back.
	Q_PROPERTY(bool waitingForDevice READ waitingForDevice NOTIFY stateChanged)
public:
	static constexpr int minimumPeriodMs = 200;

	DeviceFileLive(integration::qt::ConnectionSession* session, DeviceFilesModel* files, QString devicePath,
	               QObject* parent = nullptr);
	~DeviceFileLive() override;

	QString devicePath() const { return devicePath_; }
	bool running() const noexcept { return running_; }
	void setRunning(bool running);
	int periodMs() const noexcept { return periodMs_; }
	void setPeriodMs(int periodMs);
	bool reading() const noexcept;
	int reads() const noexcept { return reads_; }
	int lastReadMs() const noexcept { return lastReadMs_; }
	QString problem() const { return problem_; }
	bool waitingForDevice() const noexcept { return waiting_; }

Q_SIGNALS:
	// One whole read of the file, as the device served it.
	void snapshot(const QByteArray& bytes);
	void stateChanged();

private:
	struct Job;
	class Orphan;
	// Starts a read when running, connected and none is on its way.
	void begin();
	// Moves the read on; at its end hands out the bytes and plans the next.
	void poll();
	// poll() on the next turn of the loop: the session tells of progress from
	// inside a completion, where it takes no new request.
	void schedulePoll();
	void schedule();
	void setProblem(const QString& problem, bool waiting = false);

	QPointer<integration::qt::ConnectionSession> session_;
	QPointer<DeviceFilesModel> files_;
	QString devicePath_, problem_;
	std::unique_ptr<Job> job_;
	QElapsedTimer clock_;
	QTimer next_;
	qint64 startedMs_ = -1, endedMs_ = -1;
	int periodMs_ = 1000, reads_ = 0, lastReadMs_ = 0;
	bool running_ = false, pollQueued_ = false, waiting_ = false;
};
} // namespace telemetry::ux
#endif
