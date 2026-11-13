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
	bool sizeKnown = false, readable = false, writable = false, selected = false;
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

	// A file Download takes, unless the device said it cannot be read; one not
	// examined yet still counts. What selecting a folder selects.
	bool downloadable() const noexcept
	{
		return !folder && (!sizeKnown || readable);
	}

	// Files below a folder, their total known size, how many are selected and
	// how many can be downloaded.
	void tally(int& files, quint64& bytes, int& chosen, int& downloadables) const noexcept
	{
		if (!folder) {
			++files;
			bytes += size;
			chosen += selected ? 1 : 0;
			downloadables += downloadable() ? 1 : 0;
			return;
		}
		for (const auto& child : children)
			child->tally(files, bytes, chosen, downloadables);
	}

	// A folder (or the tree, from the root) with this path, below this node.
	const Node* folderAt(const QString& wanted) const noexcept
	{
		if (folder && path == wanted)
			return this;
		for (const auto& child : children)
			if (child->folder)
				if (const auto* found = child->folderAt(wanted))
					return found;
		return nullptr;
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

int DeviceFilesModel::selectedCount() const noexcept
{
	return static_cast<int>(
	    std::count_if(files_.begin(), files_.end(), [](const Node* file) { return file->selected; }));
}

qulonglong DeviceFilesModel::selectedBytes() const noexcept
{
	quint64 bytes = 0;
	for (const auto* file : files_)
		if (file->selected)
			bytes += file->size;
	return bytes;
}

int DeviceFilesModel::selectedReadable() const noexcept
{
	return static_cast<int>(std::count_if(files_.begin(), files_.end(), [](const Node* file) {
		return file->selected && file->downloadable();
	}));
}

// A size never breaks across lines: a no-break space joins the number and
// its unit ("1.0 MiB"), whatever the locale wrote between them.
QString DeviceFilesModel::sizeText(quint64 bytes)
{
	if (bytes < 1024)
		return tr("%n B", nullptr, static_cast<int>(bytes)).replace(QLatin1Char(' '), QChar(0x00A0));
	return QLocale()
	    .formattedDataSize(static_cast<qint64>(bytes), 1, QLocale::DataSizeIecFormat)
	    .replace(QLatin1Char(' '), QChar(0x00A0));
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
	Q_EMIT selectionChanged();
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
	// The selection survives a refresh by path; a file not listed any more
	// leaves it, so the counts are only ever of files there now.
	QSet<QString> selectedPaths;
	for (const auto* file : files_)
		if (file->selected)
			selectedPaths.insert(file->path);
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
		at->selected = selectedPaths.contains(at->path);
		files_.push_back(at);
	}
	endResetModel();
	nextStat_ = 0;
	Q_EMIT stateChanged();
	Q_EMIT selectionChanged();
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
		    if (result.succeeded()) {
			    file->size = result.reply.size;
			    file->sizeKnown = true;
			    file->readable = resource::has(result.reply.flags, resource::FileFlag::Readable);
			    file->writable = resource::has(result.reply.flags, resource::FileFlag::Writable);
		    } else
			    file->statError = failure(result);
		    // A selected file stays selected; its size and whether Download
		    // takes it are news to the selection's figures.
		    const bool selected = file->selected;
		    QTimer::singleShot(0, this, [this, epoch, file, selected] {
			    if (epoch != generation_)
				    return;
			    for (const Node* at = file; at && at != root_.get(); at = at->parent) {
				    const auto changed = indexOf(at);
				    Q_EMIT dataChanged(changed, changed);
			    }
			    if (selected)
				    Q_EMIT selectionChanged();
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
}

void DeviceFilesModel::setSelected(Node* file, bool selected, Touched& touched)
{
	if (!file || file->folder || file->selected == selected)
		return;
	file->selected = selected;
	for (const Node* at = file; at && at != root_.get(); at = at->parent)
		touched.insert(at);
}

void DeviceFilesModel::targetsOf(const Node* at, QSet<const Node*>& into) const
{
	if (!at->folder) {
		into.insert(at);
		return;
	}
	for (const auto& child : at->children) {
		if (child->folder)
			targetsOf(child.get(), into);
		else if (child->downloadable())
			into.insert(child.get());
	}
}

void DeviceFilesModel::publishSelection(const Touched& touched)
{
	if (touched.isEmpty())
		return;
	for (const Node* at : touched) {
		const auto changed = indexOf(at);
		Q_EMIT dataChanged(changed, changed, {IsSelectedRole, SelectedInsideRole});
	}
	Q_EMIT selectionChanged();
}

void DeviceFilesModel::select(const QModelIndex& index, bool selected)
{
	const auto* at = index.isValid() ? node(index) : nullptr;
	if (!at)
		return;
	Touched touched;
	if (at->folder && !selected) {
		// Off: every file below, the unreadable ones too.
		for (auto* file : files_)
			for (const Node* up = file->parent; up; up = up->parent)
				if (up == at) {
					setSelected(file, false, touched);
					break;
				}
	} else {
		QSet<const Node*> targets;
		targetsOf(at, targets);
		for (const auto* file : targets)
			setSelected(const_cast<Node*>(file), selected, touched);
	}
	publishSelection(touched);
}

void DeviceFilesModel::selectOnly(const QModelIndex& index)
{
	QSet<const Node*> wanted;
	if (const auto* at = index.isValid() ? node(index) : nullptr)
		targetsOf(at, wanted);
	Touched touched;
	for (auto* file : files_)
		setSelected(file, wanted.contains(file), touched);
	publishSelection(touched);
}

void DeviceFilesModel::toggleSelected(const QModelIndex& index)
{
	const auto* at = index.isValid() ? node(index) : nullptr;
	if (!at)
		return;
	select(index, !isSelected(index));
}

void DeviceFilesModel::selectIndexes(const QVariantList& indexes, bool add)
{
	QSet<const Node*> wanted;
	for (const auto& value : indexes) {
		const auto index = value.value<QModelIndex>();
		if (const auto* at = index.isValid() && index.model() == this ? node(index) : nullptr)
			targetsOf(at, wanted);
	}
	Touched touched;
	for (auto* file : files_)
		if (wanted.contains(file))
			setSelected(file, true, touched);
		else if (!add)
			setSelected(file, false, touched);
	publishSelection(touched);
}

void DeviceFilesModel::selectAll()
{
	Touched touched;
	for (auto* file : files_)
		if (file->downloadable())
			setSelected(file, true, touched);
	publishSelection(touched);
}

void DeviceFilesModel::clearSelection()
{
	Touched touched;
	for (auto* file : files_)
		setSelected(file, false, touched);
	publishSelection(touched);
}

bool DeviceFilesModel::isSelected(const QModelIndex& index) const
{
	const auto* at = index.isValid() ? node(index) : nullptr;
	if (!at)
		return false;
	if (!at->folder)
		return at->selected;
	QSet<const Node*> targets;
	targetsOf(at, targets);
	return !targets.isEmpty() &&
	       std::all_of(targets.begin(), targets.end(), [](const Node* file) { return file->selected; });
}

QStringList DeviceFilesModel::selectedPaths() const
{
	QStringList paths;
	for (const auto* file : files_)
		if (file->selected)
			paths << file->path;
	return paths;
}

QVariantList DeviceFilesModel::selectedFiles(const QModelIndex& current) const
{
	QVariantList chosen;
	bool any = false;
	for (const auto* file : files_) {
		any = any || file->selected;
		if (file->selected && file->downloadable())
			collect(file, chosen);
	}
	if (!any)
		if (const auto* at = node(current); current.isValid() && at && !at->folder)
			collect(at, chosen);
	return chosen;
}

void DeviceFilesModel::markAll(bool marked)
{
	if (marked)
		selectAll();
	else
		clearSelection();
}

QVariantList DeviceFilesModel::selection(const QModelIndex& current) const
{
	return selectedFiles(current);
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

bool DeviceFilesModel::isFile(const QModelIndex& index) const
{
	const auto* at = node(index);
	return index.isValid() && at && !at->folder;
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
	// A folder's path is its prefix: "/logs".
	return path.isEmpty() ? QModelIndex{} : indexOf(root_->folderAt(path));
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
	int files = 0, chosen = 0, downloadables = 0;
	quint64 bytes = 0;
	if (at->folder)
		at->tally(files, bytes, chosen, downloadables);
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
		case IsSelectedRole:
			return !at->folder && at->selected;
		case SelectedInsideRole:
			return at->folder ? chosen : 0;
		case ReadableInsideRole:
			return at->folder ? downloadables : at->downloadable() ? 1 : 0;
		case DownloadableRole:
			return at->folder ? downloadables > 0 : at->downloadable();
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
	        {IsSelectedRole, "isSelected"},
	        {FileCountRole, "fileCount"},
	        {StatErrorRole, "statError"},
	        {SpokenSizeRole, "spokenSize"},
	        {DownloadableRole, "downloadable"},
	        {SelectedInsideRole, "selectedInside"},
	        {ReadableInsideRole, "readableInside"}};
}
} // namespace telemetry::ux
