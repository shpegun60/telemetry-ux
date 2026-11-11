/*
 * @file DeviceFileTransfers.hpp
 * @brief Downloads of the device's files: one at a time, with progress, never blocking.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * Each download reads one file through the session's queue with a FileReader
 * and writes it with QSaveFile, so a cancelled or failed download never leaves
 * a truncated file under the real name. The bytes are the device's exactly.
 * This model owns its readers: a reader with a request in flight is never
 * destroyed; when the model goes first, the session keeps it until it ends.
 * That relies on the session draining in its own destructor; destroying the
 * session from inside one of its own callbacks is outside its lifecycle
 * contract (the SDK's AsyncClient.md) and is not supported here either.
 */
#ifndef TELEMETRY_UX_DEVICE_FILE_TRANSFERS_HPP
#define TELEMETRY_UX_DEVICE_FILE_TRANSFERS_HPP
#pragma once

#include <integration/qt/ConnectionSession.hpp>
#include <QtCore/QAbstractListModel>
#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QUrl>
#include <memory>
#include <vector>

namespace telemetry::ux {
class DeviceFileTransfers final : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int activeCount READ activeCount NOTIFY summaryChanged)
	Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged)
	// Of the task the summary names: 0..1, or -1 while it is indeterminate.
	Q_PROPERTY(qreal summaryProgress READ summaryProgress NOTIFY summaryChanged)
public:
	enum class State {
		Waiting,
		Reading,
		Saved,
		Cancelled,
		Failed
	};
	Q_ENUM(State)

	enum Role {
		NameRole = Qt::UserRole + 1,
		PathRole,
		LocalPathRole,
		StateRole,
		ReceivedRole,
		SizeRole,
		ProgressRole, // 0..1, or -1 while indeterminate
		FiguresRole,  // "96 / 128 KiB · 42 KiB/s · about 1 s left"
		ResultRole,   // the line that replaces the bar when it ends
		ActiveRole
	};
	Q_ENUM(Role)

	explicit DeviceFileTransfers(integration::qt::ConnectionSession* session,
	                             QObject* parent = nullptr);
	~DeviceFileTransfers() override;

	int activeCount() const noexcept;
	QString summary() const;
	qreal summaryProgress() const;

	// Queues one file: the device's path and index, its size from Stat, and
	// the file to write. Returns the row, or -1 when it cannot be queued.
	Q_INVOKABLE int download(const QString& path, int fileIndex, qulonglong size,
	                         const QString& localFile);
	// The local file for a device path inside folder, keeping the device's own
	// folders below it. Each segment becomes a plain local name (no drive, no
	// "..", no reserved or forbidden characters); empty when the result would
	// still leave the folder. The device's names never choose where files land.
	Q_INVOKABLE static QString localPathFor(const QString& folder, const QString& devicePath);
	// For hosts that pick places with QML file dialogs: a dialog's URL as the
	// local path download() takes, and a local path as a URL to open.
	Q_INVOKABLE static QString localFileOf(const QUrl& url);
	Q_INVOKABLE static QUrl urlOfLocalFile(const QString& path);
	Q_INVOKABLE void cancel(int row);
	// Starts a failed or cancelled download again, in its own row, from the
	// first byte. False when the row is still running, saved, or not connected.
	Q_INVOKABLE bool retry(int row);
	Q_INVOKABLE void cancelAll();
	// Forgets the rows that have ended.
	Q_INVOKABLE void clearFinished();

	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	// Figures in the designer's words; public for the tests.
	static QString figures(quint64 received, quint64 size, double rate, double secondsLeft,
	                       bool rateTrusted);

Q_SIGNALS:
	void summaryChanged();
	// One download ended; state is Saved, Cancelled or Failed.
	void finished(int row, State state, const QString& result);

private:
	struct Job;
	void schedulePoll();
	void poll();
	void startNext();
	void end(Job& job, State state, QString result);
	void publish(int row);

	QPointer<integration::qt::ConnectionSession> session_;
	std::vector<std::unique_ptr<Job>> jobs_;
	QElapsedTimer clock_;
	bool pollQueued_ = false;
};
} // namespace telemetry::ux
#endif
