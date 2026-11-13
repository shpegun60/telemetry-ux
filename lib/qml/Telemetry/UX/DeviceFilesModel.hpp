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
#include <QtCore/QSet>
#include <functional>
#include <memory>
#include <vector>

namespace telemetry::ux {
class DeviceFilesModel final : public QAbstractItemModel {
	Q_OBJECT
	Q_PROPERTY(bool available READ available NOTIFY stateChanged)
	Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
	Q_PROPERTY(QString error READ error NOTIFY stateChanged)
	// The device listed more than maxFiles files; the first maxFiles are shown.
	Q_PROPERTY(bool truncated READ truncated NOTIFY stateChanged)
	Q_PROPERTY(int maxFiles READ maxFileCount CONSTANT)
	Q_PROPERTY(int fileCount READ fileCount NOTIFY stateChanged)
	// Every file is the protocol's own, under /telemetry/: the firmware has not
	// registered a log or a settings table yet.
	Q_PROPERTY(bool systemOnly READ systemOnly NOTIFY stateChanged)
	// The selection, as a file explorer's: the files selected, their total
	// size, and how many of them the device lets us read (what Download takes).
	Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
	Q_PROPERTY(qulonglong selectedBytes READ selectedBytes NOTIFY selectionChanged)
	Q_PROPERTY(int selectedReadable READ selectedReadable NOTIFY selectionChanged)
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
		IsSelectedRole, // a selected file; a folder is never selected itself
		FileCountRole,
		StatErrorRole,
		SpokenSizeRole,     // the size as a screen reader should say it
		DownloadableRole,   // a file the device lets us read, or a folder with one
		SelectedInsideRole, // a folder: how many files below it are selected
		ReadableInsideRole, // a folder: how many files below it can be read
		// What the file is, for its icon: "folder"; the program's own
		// "descriptor", "values", "identity", "session", "capability"; the
		// formats "json", "table", "text"; "codec" (a host's codec reads it);
		// "binary" for anything else.
		KindRole,
		KindTextRole,    // the same in words: "Descriptor", "CSV", "6 files"
		ProgramFileRole, // a file the program itself uses
		// Access as three letters, as ls writes them: read, write, and "s"
		// when the device keeps what is written across a restart (a file
		// that can be written only) - "rws", "rw-", "-ws", "-w-", "r--",
		// "---"; "···" while not known, "?" when Stat failed.
		AccessMarkRole,
		AccessWordsRole // the same in words: "read and write, kept across a restart"
	};
	Q_ENUM(Role)

	// The most files the tree holds; a longer list is cut there (truncated).
	static constexpr int maxFiles = 4096;

	explicit DeviceFilesModel(integration::qt::ConnectionSession* session, QObject* parent = nullptr);
	~DeviceFilesModel() override;

	bool available() const;
	bool loading() const noexcept;
	QString error() const;
	bool truncated() const noexcept;
	static constexpr int maxFileCount() noexcept { return maxFiles; }
	int fileCount() const noexcept;
	bool systemOnly() const;
	int selectedCount() const noexcept;
	qulonglong selectedBytes() const noexcept;
	int selectedReadable() const noexcept;

	// Reads the list again, then the size and access of every file.
	Q_INVOKABLE void refresh();

	// The selection holds files only, kept by path across Refresh; a folder
	// stands for its files. A file is selected as it is, readable or not -
	// Download skips what cannot be read; a folder selects the files below it
	// the device lets us read. Each call emits dataChanged for the rows whose
	// state moved, their folders included, and one selectionChanged.
	Q_INVOKABLE void select(const QModelIndex& index, bool selected = true);
	// That row alone: the file, or a folder's readable files.
	Q_INVOKABLE void selectOnly(const QModelIndex& index);
	// A file flips. A folder whose readable files are all selected clears every
	// file below it; any other folder selects its readable files.
	Q_INVOKABLE void toggleSelected(const QModelIndex& index);
	// Several rows at once - the rows of a Shift range, as the view gives them:
	// files as they are, a folder's readable files. With add the rest stays.
	Q_INVOKABLE void selectIndexes(const QVariantList& indexes, bool add = false);
	// Every file the device lets us read; any other selected file stays.
	Q_INVOKABLE void selectAll();
	Q_INVOKABLE void clearSelection();
	// A file: whether it is selected. A folder: whether every readable file
	// below it is, and there is one.
	Q_INVOKABLE bool isSelected(const QModelIndex& index) const;
	// The selected files' paths, in the device's order.
	Q_INVOKABLE QStringList selectedPaths() const;
	// The selected files the device lets us read, or the file at current when
	// none is selected, as maps of path, fileIndex, size and readable - what a
	// download needs.
	Q_INVOKABLE QVariantList selectedFiles(const QModelIndex& current = {}) const;
	// For hosts written against the marks; kept one release.
	Q_INVOKABLE void markAll(bool marked);
	Q_INVOKABLE QVariantList selection(const QModelIndex& current = {}) const;

	// A file's or a folder's index by its device path ("/logs" for a folder).
	Q_INVOKABLE QModelIndex indexOfPath(const QString& path) const;
	Q_INVOKABLE bool isFile(const QModelIndex& index) const;
	// The file at index, as one map of selectedFiles(), whatever is selected;
	// empty for a folder. What Open and a double-click act on.
	Q_INVOKABLE QVariantMap fileAt(const QModelIndex& index) const;
	// The device path of a file or folder ("/logs" for a folder), for Copy path.
	Q_INVOKABLE QString pathAt(const QModelIndex& index) const;
	// Every readable file below index - the whole tree for an invalid index -
	// as maps of selectedFiles(), whatever is selected: Download folder,
	// Download all.
	Q_INVOKABLE QVariantList filesUnder(const QModelIndex& index = {}) const;
	// The name of the codec that takes a device path by its place and name -
	// OpenFiles::codecForPath - or empty: what the Kind role names a file a
	// host's codec reads. Without one, only the program's own files and the
	// formats known by their extension are told apart.
	using CodecLookup = std::function<QString(const QString& devicePath)>;
	void setCodecLookup(CodecLookup lookup);

	// A size in the panel's words, for figures the panel composes itself.
	Q_INVOKABLE QString formatSize(qulonglong bytes) const;
	// The same size as a screen reader should say it: "4.2 kibibytes".
	Q_INVOKABLE QString speakSize(qulonglong bytes) const;

	QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
	QModelIndex parent(const QModelIndex& child) const override;
	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	// Size as people read it: exact bytes below 1 KiB, else IEC units.
	static QString sizeText(quint64 bytes);
	// Size as people hear it: "300 bytes", "4.2 kibibytes".
	static QString spokenSize(quint64 bytes);

Q_SIGNALS:
	void stateChanged();
	void selectionChanged();

private:
	struct Node;
	// The rows a selection call moved, each once, with their folders.
	using Touched = QSet<const Node*>;
	Node* node(const QModelIndex& index) const noexcept;
	QModelIndex indexOf(const Node* node) const;
	void setSelected(Node* file, bool selected, Touched& touched);
	// What a row stands for: a file itself, a folder its readable files.
	void targetsOf(const Node* at, QSet<const Node*>& into) const;
	void publishSelection(const Touched& touched);
	void reset();
	void connectionChanged();
	void listFrom(quint64 cursor);
	void statNext();
	void build();
	void finish(QString failure = {});
	void collect(const Node* from, QVariantList& into) const;

	// What a file is: its kind, its words, whether the program uses it.
	struct Kind {
		QString kind, text;
		bool program = false;
	};
	// Cached in the node; setCodecLookup forgets it.
	const Kind& kindOf(const Node* at) const;
	Kind findKind(const Node* at) const;
	QString markOf(const Node* at) const;

	QPointer<integration::qt::ConnectionSession> session_;
	CodecLookup codecLookup_;
	// Expires with the model; a queued completion checks it before touching this.
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
	std::unique_ptr<Node> root_;
	std::vector<Node*> files_; // in the device's order; index = FileIndex
	QStringList paths_;
	QString error_;
	quint64 generation_ = 0;
	std::size_t nextStat_ = 0;
	bool loading_ = false, wasAvailable_ = false, truncated_ = false;
};
} // namespace telemetry::ux
#endif
