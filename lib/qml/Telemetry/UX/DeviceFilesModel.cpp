/*
 * @file DeviceFilesModel.cpp
 * @brief The device's files as a tree of their path segments, for the Device files dock.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "DeviceFilesModel.hpp"
#include <integration/files/Client.hpp>
#include <QtCore/QLocale>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <algorithm>

namespace telemetry::ux {
namespace files = integration::files;

struct DeviceFilesModel::Node {
	QString name, path;
	Node* parent = nullptr;
	std::vector<std::unique_ptr<Node>> children;
	bool folder = false;
	int fileIndex = -1;
	quint64 size = 0;
	bool sizeKnown = false, readable = false, writable = false, marked = false;
	QString statError;

	int row() const noexcept
	{
		if (!parent)
			return 0;
		for (std::size_t i = 0; i < parent->children.size(); ++i)
			if (parent->children[i].get() == this)
				return static_cast<int>(i);
		return 0;
	}

	// A file can be marked for download unless the device said it cannot be
	// read; one not examined yet still can.
	bool markable() const noexcept
	{
		return !folder && (!sizeKnown || readable);
	}

	// Files below a folder, their total known size, how many are marked and
	// how many could be.
	void tally(int& files, quint64& bytes, int& marks, int& markables) const noexcept
	{
		if (!folder) {
			++files;
			bytes += size;
			marks += marked ? 1 : 0;
			markables += markable() ? 1 : 0;
			return;
		}
		for (const auto& child : children)
			child->tally(files, bytes, marks, markables);
	}

	void mark(bool value) noexcept
	{
		marked = value && markable();
		for (const auto& child : children)
			child->mark(value);
	}
};

namespace {
// Why a request did not complete, in words; the number stays out of the text.
QString clientWords(client::Status status)
{
	switch (status) {
		case client::Status::Timeout:
			return DeviceFilesModel::tr("the device did not answer in time");
		case client::Status::Closed:
		case client::Status::SessionLost:
		case client::Status::TransportError:
			return DeviceFilesModel::tr("the connection dropped");
		case client::Status::Busy:
		case client::Status::QueueFull:
			return DeviceFilesModel::tr("the connection was busy");
		case client::Status::Cancelled:
			return DeviceFilesModel::tr("it was cancelled");
		case client::Status::MalformedReply:
		case client::Status::CodecError:
			return DeviceFilesModel::tr("the device's reply was not a valid answer");
		case client::Status::NotAdmitted:
		case client::Status::Unsupported:
			return DeviceFilesModel::tr("this connection cannot read files");
		default:
			return DeviceFilesModel::tr("the request did not complete");
	}
}

// Why the device refused a request, in words.
QString deviceWords(resource::Status status)
{
	switch (status) {
		case resource::Status::InvalidFile:
			return DeviceFilesModel::tr("the device has no such file");
		case resource::Status::NotReadable:
			return DeviceFilesModel::tr("the device does not allow reading it");
		case resource::Status::InvalidCursor:
		case resource::Status::CursorExpired:
			return DeviceFilesModel::tr("the device lost its place in the list");
		case resource::Status::BufferTooSmall:
			return DeviceFilesModel::tr("the device's answer did not fit");
		case resource::Status::InternalError:
			return DeviceFilesModel::tr("the device reported an internal error");
		default:
			return DeviceFilesModel::tr("the device refused it");
	}
}

// What a failed file request means, in words.
template<class Reply>
QString failure(const files::Result<Reply>& result)
{
	if (result.operation.status != client::Status::Ok)
		return clientWords(result.operation.status);
	if (!result.parsed)
		return DeviceFilesModel::tr("the device's reply was not a valid answer");
	return deviceWords(result.reply.status);
}
} // namespace

DeviceFilesModel::DeviceFilesModel(integration::qt::ConnectionSession* session, QObject* parent)
    : QAbstractItemModel(parent), session_(session), root_(std::make_unique<Node>())
{
	root_->folder = true;
	if (session_) {
		connect(session_, &integration::qt::ConnectionSession::stateChanged, this,
		        &DeviceFilesModel::connectionChanged);
		connect(session_, &integration::qt::ConnectionSession::admissionChanged, this,
		        &DeviceFilesModel::connectionChanged);
	}
	connectionChanged();
}

DeviceFilesModel::~DeviceFilesModel()
{
	// Completions still queued check the generation through their guard.
	++generation_;
}

bool DeviceFilesModel::available() const
{
	return session_ && session_->filesSupported();
}

bool DeviceFilesModel::loading() const noexcept
{
	return loading_;
}

QString DeviceFilesModel::error() const
{
	return error_;
}

bool DeviceFilesModel::truncated() const noexcept
{
	return truncated_;
}

int DeviceFilesModel::fileCount() const noexcept
{
	return static_cast<int>(files_.size());
}

bool DeviceFilesModel::systemOnly() const
{
	return !files_.empty() && std::all_of(files_.begin(), files_.end(), [](const Node* file) {
		return file->path.startsWith(QStringLiteral("/telemetry/"));
	});
}

int DeviceFilesModel::markedCount() const noexcept
{
	int files = 0, marks = 0, markables = 0;
	quint64 bytes = 0;
	root_->tally(files, bytes, marks, markables);
	return marks;
}

qulonglong DeviceFilesModel::markedBytes() const noexcept
{
	quint64 bytes = 0;
	for (const auto* file : files_)
		if (file->marked)
			bytes += file->size;
	return bytes;
}

QString DeviceFilesModel::sizeText(quint64 bytes)
{
	if (bytes < 1024)
		return tr("%n B", nullptr, static_cast<int>(bytes));
	return QLocale().formattedDataSize(static_cast<qint64>(bytes), 1, QLocale::DataSizeIecFormat);
}

QString DeviceFilesModel::spokenSize(quint64 bytes)
{
	if (bytes < 1024)
		return tr("%n byte(s)", nullptr, static_cast<int>(bytes));
	double value = static_cast<double>(bytes);
	int unit = 0;
	while (value >= 1024 && unit < 4) {
		value /= 1024;
		++unit;
	}
	const auto number = QLocale().toString(value, 'f', 1);
	switch (unit) {
		case 1:
			return tr("%1 kibibytes").arg(number);
		case 2:
			return tr("%1 mebibytes").arg(number);
		case 3:
			return tr("%1 gibibytes").arg(number);
		default:
			return tr("%1 tebibytes").arg(number);
	}
}

void DeviceFilesModel::connectionChanged()
{
	const bool now = available();
	if (now == wasAvailable_)
		return;
	wasAvailable_ = now;
	if (now)
		refresh();
	else
		reset();
}

void DeviceFilesModel::reset()
{
	++generation_;
	beginResetModel();
	root_ = std::make_unique<Node>();
	root_->folder = true;
	files_.clear();
	paths_.clear();
	endResetModel();
	truncated_ = false;
	loading_ = false;
	error_.clear();
	nextStat_ = 0;
	Q_EMIT stateChanged();
	Q_EMIT marksChanged();
}

void DeviceFilesModel::refresh()
{
	if (!available()) {
		reset();
		return;
	}
	// The rows stay until the new list replaces them, so a refresh reads as
	// "Refreshing…" over the tree rather than an empty panel; replies to the
	// previous pass are ignored by their generation.
	++generation_;
	paths_.clear();
	error_.clear();
	truncated_ = false;
	nextStat_ = 0;
	loading_ = true;
	Q_EMIT stateChanged();
	listFrom(0);
}

void DeviceFilesModel::listFrom(quint64 cursor)
{
	// The session may be destroyed between two requests of a chain.
	if (!session_) {
		finish(tr("the connection is gone"));
		return;
	}
	const auto epoch = generation_;
	const auto submitted = files::list(
	    *session_, cursor,
	    [this, guard = std::weak_ptr<int>(alive_), epoch, cursor](const files::ListResult& result) noexcept {
		    // The model may be gone while its request still waited in the queue.
		    if (guard.expired() || epoch != generation_)
			    return;
		    if (!result.succeeded()) {
			    const auto text = failure(result);
			    QTimer::singleShot(0, this, [this, epoch, text] {
				    if (epoch == generation_)
					    finish(text);
			    });
			    return;
		    }
		    for (const auto path : result.reply.paths) {
			    // A device listing more than the panel holds is shown cut, and says so.
			    if (paths_.size() >= maxFiles) {
				    truncated_ = true;
				    break;
			    }
			    paths_ << QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
		    }
		    const auto next = result.reply.next;
		    const bool eof = result.reply.eof || truncated_;
		    // A cursor that does not move would ask for the same page for ever.
		    const bool stuck = !eof && next == cursor;
		    // The next request goes from the owner loop, never from a completion.
		    QTimer::singleShot(0, this, [this, epoch, next, eof, stuck] {
			    if (epoch != generation_)
				    return;
			    if (stuck)
				    finish(tr("the device's file list did not advance"));
			    else if (eof) {
				    build();
				    statNext();
			    } else
				    listFrom(next);
		    });
	    });
	if (submitted)
		return;
	if (submitted.status == client::Status::Busy || submitted.status == client::Status::QueueFull)
		QTimer::singleShot(20, this, [this, epoch, cursor] {
			if (epoch == generation_)
				listFrom(cursor);
		});
	else
		finish(clientWords(submitted.status));
}

void DeviceFilesModel::build()
{
	// Marks survive a refresh by path; a file not listed any more drops its own.
	QSet<QString> markedPaths;
	for (const auto* file : files_)
		if (file->marked)
			markedPaths.insert(file->path);
	beginResetModel();
	root_ = std::make_unique<Node>();
	root_->folder = true;
	files_.clear();
	for (int i = 0; i < paths_.size(); ++i) {
		const auto& path = paths_[i];
		auto parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
		// A path with no name in it ("", "/") is still a file: shown at the top,
		// under a name that says it has none, with its path kept as it came.
		if (parts.isEmpty())
			parts << tr("(no name)");
		Node* at = root_.get();
		QString prefix;
		for (int p = 0; p < parts.size(); ++p) {
			prefix += QLatin1Char('/') + parts[p];
			const bool last = p + 1 == parts.size();
			Node* next = nullptr;
			if (!last)
				for (const auto& child : at->children)
					if (child->folder && child->name == parts[p]) {
						next = child.get();
						break;
					}
			if (!next) {
				auto created = std::make_unique<Node>();
				created->name = parts[p];
				created->path = last ? path : prefix;
				created->parent = at;
				created->folder = !last;
				if (last)
					created->fileIndex = i;
				next = created.get();
				at->children.push_back(std::move(created));
			}
			at = next;
		}
		at->marked = markedPaths.contains(at->path);
		files_.push_back(at);
	}
	endResetModel();
	nextStat_ = 0;
	Q_EMIT stateChanged();
	Q_EMIT marksChanged();
}

void DeviceFilesModel::statNext()
{
	if (nextStat_ >= files_.size()) {
		finish();
		return;
	}
	if (!session_) {
		finish(tr("the connection is gone"));
		return;
	}
	const auto epoch = generation_;
	const auto position = nextStat_;
	auto* file = files_[position];
	const auto submitted = files::stat(
	    *session_, static_cast<resource::FileIndex>(file->fileIndex),
	    [this, guard = std::weak_ptr<int>(alive_), epoch, file](const files::StatResult& result) noexcept {
		    if (guard.expired() || epoch != generation_)
			    return;
		    const bool wasMarked = file->marked;
		    if (result.succeeded()) {
			    file->size = result.reply.size;
			    file->sizeKnown = true;
			    file->readable = resource::has(result.reply.flags, resource::FileFlag::Readable);
			    file->writable = resource::has(result.reply.flags, resource::FileFlag::Writable);
			    // Marked before the device said it cannot be read: not any more.
			    file->marked = file->marked && file->readable;
		    } else
			    file->statError = failure(result);
		    const bool marksMoved = wasMarked != file->marked;
		    QTimer::singleShot(0, this, [this, epoch, file, marksMoved] {
			    if (epoch != generation_)
				    return;
			    for (const Node* at = file; at && at != root_.get(); at = at->parent) {
				    const auto changed = indexOf(at);
				    Q_EMIT dataChanged(changed, changed);
			    }
			    if (marksMoved)
				    Q_EMIT marksChanged();
			    ++nextStat_;
			    statNext();
		    });
	    });
	if (submitted)
		return;
	if (submitted.status == client::Status::Busy || submitted.status == client::Status::QueueFull)
		QTimer::singleShot(20, this, [this, epoch, position] {
			if (epoch == generation_ && position == nextStat_)
				statNext();
		});
	else
		finish(clientWords(submitted.status));
}

void DeviceFilesModel::finish(QString failureText)
{
	loading_ = false;
	error_ = std::move(failureText);
	Q_EMIT stateChanged();
	Q_EMIT marksChanged();
}

void DeviceFilesModel::setMarked(const QModelIndex& index, bool marked)
{
	auto* at = node(index);
	if (!at || at == root_.get())
		return;
	at->mark(marked);
	const auto first = indexOf(at);
	Q_EMIT dataChanged(first, first, {MarkedRole});
	refreshFolder(at);
	for (const Node* up = at->parent; up && up != root_.get(); up = up->parent) {
		const auto changed = indexOf(up);
		Q_EMIT dataChanged(changed, changed, {MarkedRole});
	}
	Q_EMIT marksChanged();
}

void DeviceFilesModel::refreshFolder(Node* folder)
{
	for (const auto& child : folder->children) {
		const auto changed = indexOf(child.get());
		Q_EMIT dataChanged(changed, changed, {MarkedRole});
		refreshFolder(child.get());
	}
}

void DeviceFilesModel::markAll(bool marked)
{
	root_->mark(marked);
	refreshFolder(root_.get());
	Q_EMIT marksChanged();
}

void DeviceFilesModel::collect(const Node* from, QVariantList& into) const
{
	if (!from->folder) {
		into.push_back(QVariantMap{{QStringLiteral("path"), from->path},
		                           {QStringLiteral("name"), from->name},
		                           {QStringLiteral("fileIndex"), from->fileIndex},
		                           {QStringLiteral("size"), QVariant::fromValue<qulonglong>(from->size)},
		                           {QStringLiteral("sizeKnown"), from->sizeKnown},
		                           {QStringLiteral("readable"), from->readable},
		                           {QStringLiteral("writable"), from->writable},
		                           {QStringLiteral("access"), !from->sizeKnown ? QString{} : from->readable && from->writable ? tr("read, write") : from->readable ? tr("read") : from->writable ? tr("write only") : tr("no access")}});
		return;
	}
	for (const auto& child : from->children)
		collect(child.get(), into);
}

QVariantList DeviceFilesModel::selection(const QModelIndex& current) const
{
	QVariantList chosen;
	for (const auto* file : files_)
		if (file->marked)
			collect(file, chosen);
	if (chosen.isEmpty())
		if (const auto* at = node(current); at && at != root_.get() && !at->folder)
			collect(at, chosen);
	return chosen;
}

bool DeviceFilesModel::isFile(const QModelIndex& index) const
{
	const auto* at = node(index);
	return index.isValid() && at && !at->folder;
}

bool DeviceFilesModel::isMarked(const QModelIndex& index) const
{
	return index.isValid() && data(index, MarkedRole).toInt() == Qt::Checked;
}

QVariantMap DeviceFilesModel::fileAt(const QModelIndex& index) const
{
	const auto* at = node(index);
	if (!index.isValid() || !at || at->folder)
		return {};
	QVariantList one;
	collect(at, one);
	return one.front().toMap();
}

QString DeviceFilesModel::pathAt(const QModelIndex& index) const
{
	const auto* at = node(index);
	return index.isValid() && at ? at->path : QString{};
}

QVariantList DeviceFilesModel::filesUnder(const QModelIndex& index) const
{
	const auto* from = index.isValid() ? node(index) : root_.get();
	QVariantList found;
	if (!from)
		return found;
	collect(from, found);
	// Only what can be read: a write-only file is listed, never downloaded.
	QVariantList readable;
	for (const auto& entry : found)
		if (entry.toMap().value(QStringLiteral("readable")).toBool() ||
		    !entry.toMap().value(QStringLiteral("sizeKnown")).toBool())
			readable.push_back(entry);
	return readable;
}

QString DeviceFilesModel::formatSize(qulonglong bytes) const
{
	return sizeText(bytes);
}

QString DeviceFilesModel::speakSize(qulonglong bytes) const
{
	return spokenSize(bytes);
}

QModelIndex DeviceFilesModel::indexOfPath(const QString& path) const
{
	for (const auto* file : files_)
		if (file->path == path)
			return indexOf(file);
	return {};
}

DeviceFilesModel::Node* DeviceFilesModel::node(const QModelIndex& index) const noexcept
{
	return index.isValid() ? static_cast<Node*>(index.internalPointer()) : root_.get();
}

QModelIndex DeviceFilesModel::indexOf(const Node* at) const
{
	if (!at || at == root_.get())
		return {};
	return createIndex(at->row(), 0, const_cast<Node*>(at));
}

QModelIndex DeviceFilesModel::index(int row, int column, const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	if (!owner || column != 0 || row < 0 || row >= static_cast<int>(owner->children.size()))
		return {};
	return createIndex(row, 0, owner->children[static_cast<std::size_t>(row)].get());
}

QModelIndex DeviceFilesModel::parent(const QModelIndex& child) const
{
	const auto* at = node(child);
	if (!child.isValid() || !at || !at->parent || at->parent == root_.get())
		return {};
	return indexOf(at->parent);
}

int DeviceFilesModel::rowCount(const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	return owner ? static_cast<int>(owner->children.size()) : 0;
}

int DeviceFilesModel::columnCount(const QModelIndex&) const
{
	return 1;
}

QVariant DeviceFilesModel::data(const QModelIndex& index, int role) const
{
	const auto* at = node(index);
	if (!index.isValid() || !at)
		return {};
	int files = 0, marks = 0, markables = 0;
	quint64 bytes = 0;
	if (at->folder)
		at->tally(files, bytes, marks, markables);
	switch (role) {
		case Qt::DisplayRole:
		case NameRole:
			return at->name;
		case PathRole:
			return at->path;
		case IsFolderRole:
			return at->folder;
		case FileIndexRole:
			return at->fileIndex;
		case SizeRole:
			return QVariant::fromValue<qulonglong>(at->folder ? bytes : at->size);
		case SizeKnownRole:
			return at->folder || at->sizeKnown;
		case SizeTextRole:
			// One column of sizes; a folder's file count is in its access column.
			if (at->folder)
				return sizeText(bytes);
			if (!at->statError.isEmpty())
				return tr("unavailable");
			return at->sizeKnown ? sizeText(at->size) : QStringLiteral("—");
		case ReadableRole:
			return at->readable;
		case WritableRole:
			return at->writable;
		case AccessTextRole:
			if (at->folder)
				return files == 1 ? tr("1 file") : tr("%n files", nullptr, files);
			if (!at->sizeKnown)
				return QString{};
			if (at->readable && at->writable)
				return tr("read, write");
			if (at->readable)
				return tr("read");
			return at->writable ? tr("write only") : tr("no access");
		case MarkedRole:
			if (!at->folder)
				return at->marked ? Qt::Checked : Qt::Unchecked;
			// Of the files that can be marked, so a folder never sticks halfway.
			return marks == 0 ? Qt::Unchecked : marks == markables ? Qt::Checked : Qt::PartiallyChecked;
		case MarkableRole:
			return at->folder ? markables > 0 : at->markable();
		case FileCountRole:
			return files;
		case StatErrorRole:
			return at->statError;
		case SpokenSizeRole:
			if (at->folder)
				return (files == 1 ? tr("1 file, %1") : tr("%n files, %1", nullptr, files)).arg(spokenSize(bytes));
			if (!at->statError.isEmpty())
				return tr("size unavailable");
			return at->sizeKnown ? spokenSize(at->size) : tr("size not read yet");
		default:
			return {};
	}
}

QHash<int, QByteArray> DeviceFilesModel::roleNames() const
{
	return {{NameRole, "name"},
	        {PathRole, "path"},
	        {IsFolderRole, "isFolder"},
	        {FileIndexRole, "fileIndex"},
	        {SizeRole, "size"},
	        {SizeKnownRole, "sizeKnown"},
	        {SizeTextRole, "sizeText"},
	        {ReadableRole, "readable"},
	        {WritableRole, "writable"},
	        {AccessTextRole, "accessText"},
	        {MarkedRole, "marked"},
	        {FileCountRole, "fileCount"},
	        {StatErrorRole, "statError"},
	        {SpokenSizeRole, "spokenSize"},
	        {MarkableRole, "markable"}};
}
} // namespace telemetry::ux
