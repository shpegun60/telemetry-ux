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
#include <QtCore/QPointer>
#include <QtGui/QUndoStack>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include "FileTextModels.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <utility>
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
	// A cell's text without a QVariant, for sorting and filtering many rows;
	// empty outside the table.
	const QString& cell(int row, int column) const noexcept;

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

// How surely a codec takes a file for its own, weakest first: a guess from
// its first bytes when nothing else says ("{" for JSON in a file without an
// extension), by its place and name ("/telemetry/descriptor.bin", "*.csv"),
// by a signature in its first bytes ("TDS3"), by a format a device declared.
// The surest codec is tried first, then the order of registration; a path
// with the wrong signature is tried and its failed parse says why. A codec
// that only guessed gives no reason when its parse fails: the file was never
// said to be its own.
enum class FileMatch : int { No = 0, Guess = 1, Path = 2, Signature = 3, Format = 4 };

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
	// Logical close precedes model removal and QML context retirement.
	Q_PROPERTY(bool retiring READ retiring NOTIFY retiringChanged)
	// Editing, in Hex only: bytes overwritten in memory - a device file has a
	// fixed size, so nothing is inserted or taken out - undone and redone,
	// and written to localFile by Save. Off until the view turns it on.
	Q_PROPERTY(bool editing READ editing WRITE setEditing NOTIFY editChanged)
	// The bytes that differ from the copy on disk, and whether there are any.
	Q_PROPERTY(qint64 editedBytes READ editedBytes NOTIFY editChanged)
	Q_PROPERTY(bool modified READ modified NOTIFY editChanged)
	Q_PROPERTY(bool canUndo READ canUndo NOTIFY editChanged)
	Q_PROPERTY(bool canRedo READ canRedo NOTIFY editChanged)
	// The device's layer: whether the device's bytes as read are known for
	// this copy, what to call them in a sentence, and how many bytes of the
	// working copy differ from them, edits not saved included. Without them
	// the layer is not known - never taken to be the same.
	Q_PROPERTY(bool hasReference READ hasReference NOTIFY editChanged)
	Q_PROPERTY(QString referenceLabel READ referenceLabel NOTIFY editChanged)
	Q_PROPERTY(qint64 referenceDiffers READ referenceDiffers NOTIFY editChanged)
	Q_PROPERTY(bool referenceWritten READ referenceWritten NOTIFY editChanged)
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
	// What a copy is compared against - for a device file, the device's bytes
	// as read (DeviceBytes) - and its name in a sentence ("the device's bytes
	// as read at 14:02"). Looked up after each read of the copy, with the
	// device's path it was read from (empty for a file from disk).
	struct Reference {
		QByteArray bytes;
		QString label;
		// Read back after this copy was written to the device.
		bool written = false;
	};
	using ReferenceLookup =
	    std::function<std::optional<Reference>(const QString& localFile, const QString& devicePath)>;
	void setReferenceLookup(const ReferenceLookup* lookup) noexcept { referenceLookup_ = lookup; }
	// The device file's live reader, made on first use and kept with this
	// content: null for a file from disk, or when the host reads no device.
	// It offers `running`, `periodMs`, `problem` and the signal
	// `snapshot(bytes)`; a codec view shows its snapshots.
	QObject* live();
	bool retiring() const noexcept { return retiring_; }
	// Stops only the existing reader; never creates one while closing a view.
	// An already accepted read may finish; none follows it.
	Q_INVOKABLE void stopLive();

	// Reads localFile again: after a new download, or Reload. Unsaved edits
	// go with it: a host asks first (modified).
	Q_INVOKABLE bool reload();

	bool editing() const noexcept { return editing_; }
	void setEditing(bool editing);
	qint64 editedBytes() const noexcept { return edited_; }
	bool modified() const noexcept { return edited_ > 0; }
	bool canUndo() const { return undo_.canUndo(); }
	bool canRedo() const { return undo_.canRedo(); }
	// Overwrites bytes at offset, cut at the file's end: one step to undo,
	// joined to the one before when typing goes on where it stopped (or writes
	// a byte's second digit). False when nothing was written: not editing, not
	// loaded, or the offset outside the file.
	Q_INVOKABLE bool overwrite(qint64 offset, const QByteArray& bytes, bool typing = false);
	// Overwrites several places as one step to undo (Replace all), each as
	// overwrite() does; the view hears of it once. `text` names the step.
	// False when nothing was written: not editing, not loaded, or no place
	// inside the file.
	using Run = std::pair<qint64, QByteArray>;
	bool overwriteRuns(const std::vector<Run>& runs, const QString& text);
	// Editing on or off: they change only the bytes in memory.
	Q_INVOKABLE void undo();
	Q_INVOKABLE void redo();
	// Writes the bytes as edited to localFile, through a temporary file so a
	// failure leaves the old copy whole; the text, table and codec are read
	// again from them. Empty when written, else why not.
	Q_INVOKABLE QString save();
	// Writes the bytes as edited to another file; this copy keeps its edits.
	Q_INVOKABLE QString saveCopyAs(const QUrl& file);
	// Drops the edits not saved: the bytes on disk again, nothing to undo.
	Q_INVOKABLE void discard();
	bool hasReference() const noexcept { return hasReference_; }
	QString referenceLabel() const { return referenceLabel_; }
	qint64 referenceDiffers() const noexcept { return referenceDiffers_; }
	bool referenceWritten() const noexcept { return referenceWritten_; }
	// Whether a byte of the working copy differs from the device's as read.
	bool differsFromReference(qint64 offset) const noexcept
	{
		return hasReference_ && offset >= 0 && offset < bytes_.size() &&
		       (offset >= reference_.size() || bytes_[offset] != reference_[offset]);
	}
	// The device's byte at offset as read, or -1 when it is not known.
	int referenceByte(qint64 offset) const noexcept
	{
		return hasReference_ && offset >= 0 && offset < reference_.size()
		           ? static_cast<unsigned char>(reference_[offset])
		           : -1;
	}
	// Restore the device's version: every byte the device's as read, as one
	// step to undo, editing on or off; saved like any edit. Empty when done;
	// otherwise why not (not known, another size, nothing differs).
	Q_INVOKABLE QString restoreReference();
	// Looks for the reference again - the device's bytes kept anew after this
	// copy was written to the device - without reading the copy again.
	Q_INVOKABLE void refreshReference();
	// Whether a byte differs from the copy on disk, for the hex view.
	bool isEdited(qint64 offset) const noexcept
	{
		return offset >= 0 && offset < bytes_.size() && offset < saved_.size() && bytes_[offset] != saved_[offset];
	}
	// Offers the bytes to the codecs again when the model waits for the
	// context (`needsDescriptor`), and takes the new model only when it no
	// longer waits or says something else (`connectedFingerprintGroups`) -
	// an open dialog in the view stays open otherwise. True when it took it.
	bool decodeAgainIfWaiting();
	// Valid UTF-8 with few control characters (tab, CR and LF excepted).
	static bool looksLikeText(const QByteArray& bytes);

Q_SIGNALS:
	void changed();
	void retiringChanged();
	void editChanged();
	// Bytes are about to change in place: a view holding a copy lets it go,
	// so an edit does not copy the whole file.
	void aboutToEdit();
	void bytesEdited(qint64 offset, qint64 length);

private:
	friend class OpenFiles;
	bool retire();
	friend class OverwriteStep;
	friend class RunsStep;
	friend class WholeStep;
	// The working copy replaced whole (Restore), its counts made again.
	void putWhole(const QByteArray& bytes);
	// The reference for localFile, from the lookup, after a read.
	void findReference();
	void clearReference();
	// Bytes that differ between two copies, and their lengths' difference.
	static qint64 differing(const QByteArray& a, const QByteArray& b) noexcept;
	// Writes bytes at offset, the edited count kept: what an undo step does.
	void put(qint64 offset, const QByteArray& bytes);
	// The same for several places, announced once over the span they cover.
	void putRuns(const std::vector<Run>& runs);
	// The text, the table and the codec, from bytes_.
	void interpret();
	// No edits, nothing to undo: after a read.
	void resetEdits();
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
	const ReferenceLookup* referenceLookup_ = nullptr;
	QByteArray reference_;
	QString referenceLabel_;
	bool hasReference_ = false, referenceWritten_ = false;
	qint64 referenceDiffers_ = 0;
	QObject* decoded_ = nullptr; // a child of this
	QPointer<QObject> live_;    // a child of this, for devicePath_
	bool retiring_ = false, retirementReady_ = false;
	QUrl codecView_;
	QByteArray bytes_;
	// The bytes as on disk: what an edit is told from, and Discard goes back to.
	QByteArray saved_;
	qint64 edited_ = 0;
	bool editing_ = false;
	QUndoStack undo_;
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
	// How a copy finds what it is compared against (DeviceBytes::lookup()).
	void setReferenceLookup(FileContent::ReferenceLookup lookup) { referenceLookup_ = std::move(lookup); }
	// Opens a local copy of devicePath (empty for a file from disk), or reads
	// it again when that copy is already open - not one with edits not saved;
	// returns its row.
	Q_INVOKABLE int open(const QString& localFile, const QString& devicePath = {});
	// A tab for a device file whose copy is still being read into localFile:
	// made at once (or reused), in its reading state; returns its row. One
	// with edits not saved is left as it is: the host asks before reading it again.
	Q_INVOKABLE int reading(const QString& localFile, const QString& devicePath);
	// The content that shows localFile, or null.
	FileContent* contentFor(const QString& localFile) const;
	// What the context offers has changed (a device was admitted): every file
	// whose codec waited for it is read again.
	Q_INVOKABLE void contextChanged();
	Q_INVOKABLE void close(int row);
	// The row showing localFile, or -1.
	Q_INVOKABLE int rowOf(const QString& localFile) const;
	// The name of the codec that takes a device path by its place and name
	// alone - before any byte is read - the surest first; empty when none
	// does. For a file list's icons (DeviceFilesModel::setCodecLookup).
	Q_INVOKABLE QString codecForPath(const QString& devicePath) const;
	// The files with edits not saved, by name: a host asks before it quits.
	Q_INVOKABLE QStringList modifiedNames() const;
	// Saves every file with edits; empty when all were written, else the first reason.
	Q_INVOKABLE QString saveAll();
	Q_INVOKABLE void discardAll();

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
	FileContent::ReferenceLookup referenceLookup_;
	std::vector<std::unique_ptr<FileContent>> files_;
};
} // namespace telemetry::ux
#endif
