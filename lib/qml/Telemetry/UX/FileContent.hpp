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

// Made by the host, usually through OpenFiles: the library is a static QML
// module without a plugin, so it registers no C++ type for QML to create.
class FileContent final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString localFile READ localFile WRITE setLocalFile NOTIFY changed)
	// The device's own path, for the footer; empty for a file from disk.
	Q_PROPERTY(QString devicePath READ devicePath WRITE setDevicePath NOTIFY changed)
	Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
	Q_PROPERTY(QString error READ error NOTIFY changed)
	Q_PROPERTY(qulonglong size READ size NOTIFY changed)
	// The size as the panels write it: exact bytes below 1 KiB, else IEC units.
	Q_PROPERTY(QString sizeText READ sizeText NOTIFY changed)
	Q_PROPERTY(QDateTime readAt READ readAt NOTIFY changed)
	Q_PROPERTY(bool textual READ textual NOTIFY changed)
	Q_PROPERTY(bool tabular READ tabular NOTIFY changed)
	// What the viewer opens first: "Table", "Text" or "Binary".
	Q_PROPERTY(QString format READ format NOTIFY changed)
	Q_PROPERTY(QString text READ text NOTIFY changed)
	Q_PROPERTY(QAbstractItemModel* table READ table CONSTANT)
	Q_PROPERTY(QAbstractItemModel* hex READ hex CONSTANT)
public:
	// Larger files are not read whole; their first bytes are not a substitute.
	static constexpr qint64 maxBytes = 64 * 1024 * 1024;
	// Above this, text is not offered (a text view of it would not be usable).
	static constexpr qint64 maxTextBytes = 8 * 1024 * 1024;

	explicit FileContent(QObject* parent = nullptr);

	QString localFile() const { return localFile_; }
	void setLocalFile(const QString& path);
	QString devicePath() const { return devicePath_; }
	void setDevicePath(const QString& path);
	bool loaded() const noexcept { return loaded_; }
	QString error() const { return error_; }
	qulonglong size() const noexcept { return static_cast<qulonglong>(bytes_.size()); }
	QString sizeText() const;
	QDateTime readAt() const { return readAt_; }
	bool textual() const noexcept { return textual_; }
	bool tabular() const noexcept { return tabular_; }
	QString format() const;
	QString text() const { return text_; }
	QAbstractItemModel* table() { return &table_; }
	QAbstractItemModel* hex() { return &hex_; }

	// Reads localFile again: after a new download, or Reload.
	Q_INVOKABLE bool reload();
	// Valid UTF-8 with few control characters (tab, CR and LF excepted).
	static bool looksLikeText(const QByteArray& bytes);

Q_SIGNALS:
	void changed();

private:
	QString localFile_, devicePath_, error_, text_;
	QByteArray bytes_;
	QDateTime readAt_;
	bool loaded_ = false, textual_ = false, tabular_ = false;
	FileHexModel hex_;
	FileTableModel table_;
};

// The files open in a viewer, one row each, for a host's tabs: name, device
// path, local file and the FileContent a FileViewer shows (role "content").
class OpenFiles final : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
	enum Role { NameRole = Qt::UserRole + 1, DevicePathRole, LocalFileRole, ContentRole };
	using QAbstractListModel::QAbstractListModel;

	int count() const noexcept { return static_cast<int>(files_.size()); }
	// Opens a local copy of devicePath (empty for a file from disk), or reads
	// it again when that copy is already open; returns its row.
	Q_INVOKABLE int open(const QString& localFile, const QString& devicePath = {});
	Q_INVOKABLE void close(int row);

	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
	void countChanged();

private:
	std::vector<std::unique_ptr<FileContent>> files_;
};
} // namespace telemetry::ux
#endif
