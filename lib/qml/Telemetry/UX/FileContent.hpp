/*
 * @file FileContent.hpp
 * @brief One file's bytes for the viewers: text when it is text, a table when it is CSV, always hex.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * Reads a local file - a device file's downloaded copy, or one from disk - and
 * offers it the ways the viewers show it:
 * - Hex, always: offset, 16 bytes and their ASCII, one row per 16 bytes, made
 *   on demand from the bytes.
 * - Text, when the bytes are valid UTF-8 with few control characters.
 * - Table, when the file is text named ".csv" (by the device or on disk) and
 *   parses as CSV: the first row names the columns.
 * Qt only; no device, no SDK. Formats with a codec of their own (the
 * descriptor, values.bin) come with their codecs.
 */
#ifndef TELEMETRY_UX_FILE_CONTENT_HPP
#define TELEMETRY_UX_FILE_CONTENT_HPP
#pragma once

#include <QtCore/QAbstractListModel>
#include <QtCore/QAbstractTableModel>
#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include "FileTextModels.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace telemetry::ux {
// The bytes as rows of 16, for the hex viewer.
class FileHexModel final : public QAbstractListModel {
	Q_OBJECT
public:
	enum Role { OffsetRole = Qt::UserRole + 1, BytesRole, AsciiRole };
	using QAbstractListModel::QAbstractListModel;
	void setBytes(const QByteArray* bytes);
	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

private:
	const QByteArray* bytes_ = nullptr;
};

// A CSV file as a table: the first row names the columns.
class FileTableModel final : public QAbstractTableModel {
	Q_OBJECT
public:
	using QAbstractTableModel::QAbstractTableModel;
	// Parses RFC 4180 CSV: quoted fields, "" inside quotes, CRLF or LF; short
	// rows are padded. False when there is no header row or a quote is left
	// open, and the table is then empty.
	bool setText(const QString& text);
	void clear();
	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
	// Every filled cell of the column reads as a number: shown right-aligned.
	Q_INVOKABLE bool numeric(int column) const;

private:
	QStringList header_;
	std::vector<QStringList> rows_;
	std::vector<bool> numeric_;
};

// What a codec knows of a file before it parses it.
struct FileIdentity {
	QString devicePath; // empty for a file from disk
	QString localFile;
	QByteArray head;    // the first bytes, for a signature
};

// How surely a codec takes a file for its own, weakest first: by its place and
// name ("/telemetry/descriptor.bin", "*.csv"), by a signature in its first
// bytes ("TDS3"), by a format a device declared. The surest codec is tried
// first, then the order of registration; a path with the wrong signature is
// tried and its failed parse says why.
enum class FileMatch : int { No = 0, Path = 1, Signature = 2, Format = 3 };

// What a codec may ask for beyond the file's bytes.
struct FileCodecContext {
	// The bytes of a descriptor with this fingerprint - the connected
	// device's, or one in a device's folder - or empty: values.bin needs it.
	std::function<QByteArray(quint64 fingerprint)> descriptor;
	// The connected device's descriptor fingerprint; false when no device
	// with a descriptor is connected. For a view that asks for a descriptor.
	std::function<bool(quint64& fingerprint)> connectedFingerprint;
	// The file being read: its local copy and the device's path (empty for a
	// file from disk). Filled for each read; a host sets only the above.
	QString localFile, devicePath;
};

// A format's codec: its name ("Descriptor"), how surely it takes a file, the
// parse - the proof - that makes the view model, and the QML component that
// shows it (given the model as its required property "decoded", and the
// FileContent as "content"). Tied to a format, not to a file. The model may
// also offer `regions` (the parts of the file, which the hex view tints and
// names) and `findings`, and `needsDescriptor` while it waits for something
// the context did not have yet - OpenFiles::contextChanged() reads it again.
// A parse that fails leaves the file to Text or Hex, with the codec's reason.
// Qt only here; the parser can be anything.
struct FileCodec {
	QString name;
	std::function<FileMatch(const FileIdentity&)> match;
	std::function<std::unique_ptr<QObject>(const QByteArray& bytes, const FileCodecContext& context, QString& error)>
	    decode;
	QUrl view;
};

// Made by the host, usually through OpenFiles: the library is a static QML
// module without a plugin, so it registers no C++ type for QML to create.
class FileContent final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString localFile READ localFile WRITE setLocalFile NOTIFY changed)
	// The device's own path, for the footer; empty for a file from disk.
	Q_PROPERTY(QString devicePath READ devicePath WRITE setDevicePath NOTIFY changed)
	Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
	// The device's file is being read into localFile; the viewer shows progress.
	Q_PROPERTY(bool loading READ loading NOTIFY changed)
	// 0..1, or negative while the size is not known.
	Q_PROPERTY(qreal progress READ progress NOTIFY changed)
	// "96 / 128 KiB · 42 KiB/s", from the read.
	Q_PROPERTY(QString figures READ figures NOTIFY changed)
	Q_PROPERTY(QString error READ error NOTIFY changed)
	Q_PROPERTY(qulonglong size READ size NOTIFY changed)
	// The size as the panels write it: exact bytes below 1 KiB, else IEC units.
	Q_PROPERTY(QString sizeText READ sizeText NOTIFY changed)
	Q_PROPERTY(QDateTime readAt READ readAt NOTIFY changed)
	Q_PROPERTY(bool textual READ textual NOTIFY changed)
	Q_PROPERTY(bool tabular READ tabular NOTIFY changed)
	// What the viewer opens first: a codec's name, "Table", "Text" or "Binary".
	Q_PROPERTY(QString format READ format NOTIFY changed)
	// The codec that read the file, its view model and the component showing
	// it; empty and null when no codec read it.
	Q_PROPERTY(QString codecName READ codecName NOTIFY changed)
	Q_PROPERTY(QObject* decoded READ decoded NOTIFY changed)
	Q_PROPERTY(QUrl codecView READ codecView NOTIFY changed)
	// Why a codec that took the file for its own could not read it.
	Q_PROPERTY(QString codecError READ codecError NOTIFY changed)
	Q_PROPERTY(QString text READ text NOTIFY changed)
	Q_PROPERTY(QAbstractItemModel* table READ table CONSTANT)
	// The text as numbered lines with Find, and the table sorted and filtered.
	Q_PROPERTY(QAbstractItemModel* lines READ lines CONSTANT)
	Q_PROPERTY(QAbstractItemModel* rows READ rows CONSTANT)
	Q_PROPERTY(QAbstractItemModel* hex READ hex CONSTANT)
	// Reads the device's file again and again for a live view; see live().
	Q_PROPERTY(QObject* live READ live NOTIFY changed)
public:
	// Larger files are not read whole; their first bytes are not a substitute.
	static constexpr qint64 maxBytes = 64 * 1024 * 1024;
	// Above this, text is not offered. The text view makes only the lines on
	// screen, so a log of 10 MB reads as easily as a short one.
	static constexpr qint64 maxTextBytes = 16 * 1024 * 1024;

	explicit FileContent(QObject* parent = nullptr);

	QString localFile() const { return localFile_; }
	void setLocalFile(const QString& path);
	QString devicePath() const { return devicePath_; }
	void setDevicePath(const QString& path);
	bool loaded() const noexcept { return loaded_; }
	bool loading() const noexcept { return loading_; }
	qreal progress() const noexcept { return progress_; }
	QString figures() const { return figures_; }
	QString error() const { return error_; }

	// The reading of a device's file, told by whoever reads it (a transfer):
	// begun, progressing, then finished - read from localFile when it worked,
	// else the reason, which the viewer shows with Read again.
	void beginReading(const QString& localFile, const QString& devicePath);
	void setReading(qreal progress, const QString& figures);
	void finishReading(bool read, const QString& reason);
	qulonglong size() const noexcept { return static_cast<qulonglong>(bytes_.size()); }
	QString sizeText() const;
	QDateTime readAt() const { return readAt_; }
	bool textual() const noexcept { return textual_; }
	bool tabular() const noexcept { return tabular_; }
	QString format() const;
	QString text() const { return text_; }
	QAbstractItemModel* table() { return &table_; }
	QAbstractItemModel* lines() { return &lines_; }
	QAbstractItemModel* rows() { return &rows_; }
	QAbstractItemModel* hex() { return &hex_; }
	// The bytes as read: shared, so a viewer's copy costs nothing.
	const QByteArray& bytes() const noexcept { return bytes_; }
	QString codecName() const { return codecName_; }
	QObject* decoded() const noexcept { return decoded_; }
	QUrl codecView() const { return codecView_; }
	QString codecError() const { return codecError_; }
	// The codecs tried on every read, and what they may ask; borrowed from OpenFiles.
	void setCodecs(const std::vector<FileCodec>* codecs, const FileCodecContext* context) noexcept
	{
		codecs_ = codecs;
		context_ = context;
	}
	// A codec's parts of the file, for the hex view: the decoded model's
	// `regions`, or empty.
	Q_INVOKABLE QVariantList regions() const;
	// Makes a reader that reads a device file again and again into memory
	// (Live), parented to the content it is for; null when it cannot.
	using LiveFactory = std::function<QObject*(const QString& devicePath, QObject* parent)>;
	void setLiveFactory(const LiveFactory* factory) noexcept { liveFactory_ = factory; }
	// The device file's live reader, made on first use and kept with this
	// content: null for a file from disk, or when the host reads no device.
	// It offers `running`, `periodMs`, `problem` and the signal
	// `snapshot(bytes)`; a codec view shows its snapshots.
	QObject* live();

	// Reads localFile again: after a new download, or Reload.
	Q_INVOKABLE bool reload();
	// Offers the bytes to the codecs again when the model waits for the
	// context (`needsDescriptor`), and takes the new model only when it no
	// longer waits or says something else (`connectedFingerprintGroups`) -
	// an open dialog in the view stays open otherwise. True when it took it.
	bool decodeAgainIfWaiting();
	// Valid UTF-8 with few control characters (tab, CR and LF excepted).
	static bool looksLikeText(const QByteArray& bytes);

Q_SIGNALS:
	void changed();

private:
	// Offers the bytes to the codecs; the first that reads them wins.
	void decode();
	// The same, made and returned, nothing of this content changed.
	struct Decoded {
		std::unique_ptr<QObject> model;
		QString name, error;
		QUrl view;
	};
	Decoded decodeBytes() const;
	// Drops the decoded model later than the views that still show it.
	void forgetDecoded();

	QString localFile_, devicePath_, error_, text_, figures_, codecName_, codecError_;
	const std::vector<FileCodec>* codecs_ = nullptr;
	const FileCodecContext* context_ = nullptr;
	const LiveFactory* liveFactory_ = nullptr;
	QObject* decoded_ = nullptr; // a child of this
	QObject* live_ = nullptr;    // a child of this, for devicePath_
	QUrl codecView_;
	QByteArray bytes_;
	QDateTime readAt_;
	qreal progress_ = -1;
	bool loaded_ = false, loading_ = false, textual_ = false, tabular_ = false;
	FileHexModel hex_;
	FileTableModel table_;
	TextLinesModel lines_;
	TableRowsModel rows_;
};

// The files open in a viewer, one row each, for a host's tabs: name, device
// path, local file and the FileContent a FileViewer shows (role "content").
class OpenFiles final : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
	enum Role { NameRole = Qt::UserRole + 1, DevicePathRole, LocalFileRole, ContentRole, LoadingRole };
	using QAbstractListModel::QAbstractListModel;

	int count() const noexcept { return static_cast<int>(files_.size()); }
	// A codec every file opened from now on is offered to, after those before it.
	void addCodec(FileCodec codec);
	// What the codecs may ask for (a descriptor by fingerprint).
	void setContext(FileCodecContext context) { context_ = std::move(context); }
	// How a device file is read live, for the views that show it so.
	void setLiveFactory(FileContent::LiveFactory factory) { liveFactory_ = std::move(factory); }
	// Opens a local copy of devicePath (empty for a file from disk), or reads
	// it again when that copy is already open; returns its row.
	Q_INVOKABLE int open(const QString& localFile, const QString& devicePath = {});
	// A tab for a device file whose copy is still being read into localFile:
	// made at once (or reused), in its reading state; returns its row.
	Q_INVOKABLE int reading(const QString& localFile, const QString& devicePath);
	// The content that shows localFile, or null.
	FileContent* contentFor(const QString& localFile) const;
	// What the context offers has changed (a device was admitted): every file
	// whose codec waited for it is read again.
	Q_INVOKABLE void contextChanged();
	Q_INVOKABLE void close(int row);
	// The row showing localFile, or -1.
	Q_INVOKABLE int rowOf(const QString& localFile) const;

	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
	void countChanged();

private:
	// A new row with a new content that knows the codecs; returns the row.
	int append();

	std::vector<FileCodec> codecs_;
	FileCodecContext context_;
	FileContent::LiveFactory liveFactory_;
	std::vector<std::unique_ptr<FileContent>> files_;
};
} // namespace telemetry::ux
#endif
