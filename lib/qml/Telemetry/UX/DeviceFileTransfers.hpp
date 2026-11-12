/*
 * @file DeviceFileTransfers.hpp
 * @brief The device's files read and written back: one at a time, with progress, never waiting on the device.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * Each download reads one file through the session's queue with a FileReader
 * and writes it with QSaveFile, so a cancelled or failed download never leaves
 * a truncated file under the real name. The bytes are the device's exactly.
 *
 * An upload writes a saved copy back to the device's file of the same size:
 * the device's file read first and compared with its record (DeviceBytes) -
 * a device changed since it was read stops it unless overwrite says so - then
 * written with a FileWriter, saved when the device keeps files (Savable), and
 * read back; the bytes read back become the record. The check and the write
 * are not one step on the device: the protocol has no lock, so a change in
 * between is not seen.
 *
 * This model owns its readers and writers: one with a request in flight is never
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
class OpenFiles;

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
		Failed,
		// An upload running: checking, writing, saving or reading it back.
		Writing,
		// An upload ended with the copy written; ResultRole says how it was
		// checked and kept.
		Written
	};
	Q_ENUM(State)

	// How an upload ended (OutcomeRole), for the buttons and colour a row
	// offers; None for downloads. Cancelled or Failed is the State.
	enum class Outcome {
		None,
		// Written, read back as the copy.
		Checked,
		// Written, read back with bytes the device keeps its own way.
		ReadBackDiffers,
		// Written, not read back.
		NotChecked,
		// Nothing written: the device's file changed since it was read.
		Changed,
		// Nothing written: no record here of what the device's file was.
		NotKnown,
		// Nothing written: the device's file has another size.
		SizeDiffers,
		// Nothing written: the device does not allow writing it, or refused
		// the content.
		NotWritable,
		// Stopped part way: the device's file may be part old, part new.
		Partial,
		// Nothing written: anything else - the connection, a read, a cancel
		// before writing.
		Failed
	};
	Q_ENUM(Outcome)

	// An upload's steps, in order (StepRole).
	enum class Step {
		Check,
		Write,
		Save,
		Verify
	};
	Q_ENUM(Step)

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
		ActiveRole,
		OpeningRole,  // read to be shown in a viewer, not saved for the user
		UploadRole,   // a copy written to the device
		OutcomeRole,  // Outcome
		StepRole      // Step, while an upload runs
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
	// The same, read to be shown: viewer gets a tab for it at once
	// (OpenFiles::reading), which follows the read's progress and then shows the
	// file, or says why it could not be read. The row reads "Opening".
	// deviceFolder, the device's folder localFile is in, is where the device's
	// bytes as read are kept (setKeepDeviceBytes); without it none are.
	Q_INVOKABLE int openIn(QObject* viewer, const QString& path, int fileIndex, qulonglong size,
	                       const QString& localFile, const QString& deviceFolder = {});
	// Writes localFile, a saved copy inside deviceFolder (the connected
	// device's folder), to the device's file path at fileIndex, whole: the
	// device's file is read first and, unless overwrite, must still be what
	// localFile's record says it was, or nothing is written; then written,
	// saved when the device keeps files, and read back. The bytes read back
	// become the record (setKeepDeviceBytes); the copy is never changed.
	// viewer (an OpenFiles) has the copy's reference refreshed. Returns the
	// row, or -1 when it cannot be queued (no folder, or the copy not in it).
	Q_INVOKABLE int upload(const QString& path, int fileIndex, const QString& localFile,
	                       const QString& deviceFolder = {}, bool overwrite = false,
	                       QObject* viewer = nullptr);
	// Cancels the read that writes localFile, if one is running or waiting;
	// never an upload of it, which a closed tab must not cut part way.
	Q_INVOKABLE void cancelFor(const QString& localFile);
	// The local file for a device path inside folder, keeping the device's own
	// folders below it. Each segment becomes a plain local name (no drive, no
	// "..", no reserved or forbidden characters); empty when the result would
	// still leave the folder. The device's names never choose where files land.
	Q_INVOKABLE static QString localPathFor(const QString& folder, const QString& devicePath);
	// For hosts that pick places with QML file dialogs: a dialog's URL as the
	// local path download() takes, and a local path as a URL to open.
	Q_INVOKABLE static QString localFileOf(const QUrl& url);
	Q_INVOKABLE static QUrl urlOfLocalFile(const QString& path);
	// Makes a local folder and its parents, for a host that offers or shows a
	// place before anything is written there; true when it exists.
	Q_INVOKABLE static bool makeFolder(const QString& path);
	// A folder that exists and holds something, without making it: whether a
	// host's "show the files" has anything to show.
	Q_INVOKABLE static bool folderHasFiles(const QString& path);
	// Save as: copies a local file to another place, replacing what is there,
	// written whole or not at all. Empty when done; otherwise why not, in words.
	Q_INVOKABLE static QString copyFile(const QString& from, const QString& to);
	// The connected device's folder under root, made when missing; empty when
	// it cannot be. "<name> [<id>]" from the device's identity file, or the id
	// alone for a device without a name; a folder of the same id under an
	// earlier name is renamed to it, unless a transfer or a file open in viewer
	// (an OpenFiles) is inside it - then that folder is used as it is. Without
	// an identity, fallbackLabel names it, as a device path would.
	Q_INVOKABLE QString deviceFolder(const QString& root, const QString& fallbackLabel,
	                                 QObject* viewer = nullptr) const;
	// The local file a row writes, as download() was given it; rows move when
	// finished ones are cleared, so a host keys its own records by this.
	Q_INVOKABLE QString localFileAt(int row) const;
	Q_INVOKABLE void cancel(int row);
	// Starts a failed or cancelled download or upload again, in its own row,
	// from the first byte; an upload with overwrite as given ("Write anyway").
	// False when the row is still running, has ended well, or not connected.
	Q_INVOKABLE bool retry(int row, bool overwrite = false);
	Q_INVOKABLE void cancelAll();
	// Forgets the rows that have ended.
	Q_INVOKABLE void clearFinished();
	// Whether a file read for the viewer (openIn, given its device's folder),
	// and a copy written to the device and read back, leave the device's
	// bytes there (DeviceBytes), for the device's layer and Restore; off by
	// default. Any other download that writes a copy with a record removes
	// it: it would no longer be true.
	void setKeepDeviceBytes(bool keep) noexcept { keepDeviceBytes_ = keep; }

	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	// Figures in the designer's words; public for the tests.
	static QString figures(quint64 received, quint64 size, double rate, double secondsLeft,
	                       bool rateTrusted);

Q_SIGNALS:
	void summaryChanged();
	// One row ended; state is Saved, Written, Cancelled or Failed.
	void finished(int row, State state, const QString& result);

private:
	bool keepDeviceBytes_ = false;
	struct Job;
	bool inUse(const QString& folder, QObject* viewer) const;
	int queue(const QString& path, int fileIndex, qulonglong size, const QString& localFile,
	          OpenFiles* viewer, const QString& deviceFolder = {});
	void schedulePoll();
	void poll();
	void startNext();
	bool beginUpload(Job& job);
	// An upload's next step after its reader or writer ended; true once it
	// has ended.
	bool advanceUpload(Job& job);
	void end(Job& job, State state, QString result);
	void publish(int row);

	QPointer<integration::qt::ConnectionSession> session_;
	std::vector<std::unique_ptr<Job>> jobs_;
	QElapsedTimer clock_;
	bool pollQueued_ = false;
};
} // namespace telemetry::ux
#endif
