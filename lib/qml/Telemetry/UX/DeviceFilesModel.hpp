/*
 * @file DeviceFilesModel.hpp
 * @brief The device's files as a tree of their path segments, for the Device files dock.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * Lists the files of a ConnectionSession - with or without telemetry - and
 * then states each one, on the session's own queue. Names are the device's
 * own: the tree is its paths split at '/', nothing renamed or hidden. Folders
 * are virtual and carry their file count and total size.
 */
#ifndef TELEMETRY_UX_DEVICE_FILES_MODEL_HPP
#define TELEMETRY_UX_DEVICE_FILES_MODEL_HPP
#pragma once

#include <integration/qt/ConnectionSession.hpp>
#include <QtCore/QAbstractItemModel>
#include <QtCore/QPointer>
#include <memory>
#include <vector>

namespace telemetry::ux {
class DeviceFilesModel final : public QAbstractItemModel {
	Q_OBJECT
	Q_PROPERTY(bool available READ available NOTIFY stateChanged)
	Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
	Q_PROPERTY(QString error READ error NOTIFY stateChanged)
	Q_PROPERTY(int fileCount READ fileCount NOTIFY stateChanged)
	Q_PROPERTY(int markedCount READ markedCount NOTIFY marksChanged)
	Q_PROPERTY(qulonglong markedBytes READ markedBytes NOTIFY marksChanged)
public:
	enum Role {
		NameRole = Qt::UserRole + 1,
		PathRole,
		IsFolderRole,
		FileIndexRole,
		SizeRole,
		SizeKnownRole,
		SizeTextRole,
		ReadableRole,
		WritableRole,
		AccessTextRole,
		MarkedRole, // Qt::CheckState; a folder's is derived from its files
		FileCountRole,
		StatErrorRole
	};
	Q_ENUM(Role)

	explicit DeviceFilesModel(integration::qt::ConnectionSession* session, QObject* parent = nullptr);
	~DeviceFilesModel() override;

	bool available() const;
	bool loading() const noexcept;
	QString error() const;
	int fileCount() const noexcept;
	int markedCount() const noexcept;
	qulonglong markedBytes() const noexcept;

	// Reads the list again, then the size and access of every file.
	Q_INVOKABLE void refresh();
	// Marks or unmarks a file, or every file of a folder.
	Q_INVOKABLE void setMarked(const QModelIndex& index, bool marked);
	Q_INVOKABLE void markAll(bool marked);
	// The marked files, or the file at current when none is marked, as maps of
	// path, fileIndex, size and readable - what a download needs.
	Q_INVOKABLE QVariantList selection(const QModelIndex& current = {}) const;
	Q_INVOKABLE QModelIndex indexOfPath(const QString& path) const;
	// Whether index is a file, and whether it is marked (a folder: all its files).
	Q_INVOKABLE bool isFile(const QModelIndex& index) const;
	Q_INVOKABLE bool isMarked(const QModelIndex& index) const;
	// A size in the panel's words, for figures the panel composes itself.
	Q_INVOKABLE QString formatSize(qulonglong bytes) const;

	QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
	QModelIndex parent(const QModelIndex& child) const override;
	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	// Size as people read it: exact bytes below 1 KiB, else IEC units.
	static QString sizeText(quint64 bytes);

Q_SIGNALS:
	void stateChanged();
	void marksChanged();

private:
	struct Node;
	Node* node(const QModelIndex& index) const noexcept;
	QModelIndex indexOf(const Node* node) const;
	void reset();
	void connectionChanged();
	void listFrom(quint64 cursor);
	void statNext();
	void build();
	void finish(QString failure = {});
	void refreshFolder(Node* folder);
	void collect(const Node* from, QVariantList& into) const;

	QPointer<integration::qt::ConnectionSession> session_;
	// Expires with the model; a queued completion checks it before touching this.
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
	std::unique_ptr<Node> root_;
	std::vector<Node*> files_; // in the device's order; index = FileIndex
	QStringList paths_;
	QString error_;
	quint64 generation_ = 0;
	std::size_t nextStat_ = 0;
	bool loading_ = false, wasAvailable_ = false;
};
} // namespace telemetry::ux
#endif
