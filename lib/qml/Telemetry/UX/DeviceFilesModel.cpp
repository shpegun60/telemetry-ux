/*
 * @file DeviceFilesModel.cpp
 * @brief The device's files as a tree of their path segments, for the Device files dock.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "DeviceFilesModel.hpp"
#include "CapabilityContent.hpp"
#include "DescriptorContent.hpp"
#include "ValuesContent.hpp"
#include <integration/files/Client.hpp>
#include <QtCore/QLocale>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace telemetry::ux {
namespace files = integration::files;

struct DeviceFilesModel::Node {
	quintptr token = 0;
	QString name, path;
	Node* parent = nullptr;
	int rowInParent = 0;
	std::vector<std::unique_ptr<Node>> children;
	bool folder = false;
	int fileIndex = -1;
	quint64 size = 0;
	bool sizeKnown = false, readable = false, writable = false, selected = false;
	// What is written persists across a restart (SAVE); otherwise it is kept
	// only until the device restarts.
	bool savable = false;
	// What the file is, worked out the first time a row asks: a host's
	// codec may be slow to say.
	mutable bool kindKnown = false;
	mutable DeviceFilesModel::Kind kind;
	QString statError;

	int row() const noexcept
	{
		return rowInParent;
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

// The access as ls writes it - read, write, and "s" when the device keeps
// what is written across a restart, on a file that can be written only -
// "?" when Stat failed, three middle dots before it answers. "-" in ASCII,
// never a dash a fallback font would draw taller.
QString accessMark(bool sizeKnown, bool statFailed, bool readable, bool writable, bool savable)
{
	if (statFailed)
		return QStringLiteral("?");
	if (!sizeKnown)
		return QStringLiteral("\u00b7\u00b7\u00b7");
	return QString(readable ? QLatin1Char('r') : QLatin1Char('-')) + (writable ? QLatin1Char('w') : QLatin1Char('-')) +
	       (writable && savable ? QLatin1Char('s') : QLatin1Char('-'));
}

// The same in words, for Properties and a screen reader.
QString accessWords(const QString& mark)
{
	if (mark == QLatin1String("rws"))
		return DeviceFilesModel::tr("read and write, kept across a restart");
	if (mark == QLatin1String("rw-"))
		return DeviceFilesModel::tr("read and write, kept until the device restarts");
	if (mark == QLatin1String("-ws"))
		return DeviceFilesModel::tr("write only, kept across a restart");
	if (mark == QLatin1String("-w-"))
		return DeviceFilesModel::tr("write only, kept until the device restarts");
	if (mark == QLatin1String("r--"))
		return DeviceFilesModel::tr("read only");
	if (mark == QLatin1String("---"))
		return DeviceFilesModel::tr("no access");
	return DeviceFilesModel::tr("access not known");
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
	const auto epoch = ++generation_;
	QPointer<DeviceFilesModel> alive(this);
	changing_ = true;
	beginResetModel();
	nodes_.clear();
	root_ = std::make_unique<Node>();
	root_->folder = true;
	files_.clear();
	paths_.clear();
	changing_ = false;
	endResetModel();
	if (!alive || epoch != generation_)
		return;
	truncated_ = false;
	loading_ = false;
	error_.clear();
	nextStat_ = 0;
	Q_EMIT stateChanged();
	if (!alive || epoch != generation_)
		return;
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
	const auto epoch = ++generation_;
	QPointer<DeviceFilesModel> alive(this);
	paths_.clear();
	error_.clear();
	truncated_ = false;
	nextStat_ = 0;
	loading_ = true;
	Q_EMIT stateChanged();
	if (!alive || epoch != generation_)
		return;
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
				    QPointer<DeviceFilesModel> alive(this);
				    if (build() && alive && epoch == generation_)
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

bool DeviceFilesModel::build()
{
	const auto epoch = generation_;
	QPointer<DeviceFilesModel> alive(this);
	// Reserve enough identities even if no folder can be shared. Exhaustion is
	// terminal for this model; wrapping would make an old index alias new data.
	quintptr remaining = nextToken_ == 0 ? 0 : std::numeric_limits<quintptr>::max() - nextToken_ + 1;
	for (const auto& path : paths_) {
		const auto required = static_cast<quintptr>(path.count(QLatin1Char('/'))) + 1;
		if (required > remaining) {
			finish(tr("the file tree's index identities are exhausted"));
			return false;
		}
		remaining -= required;
	}
	// The selection survives a refresh by path; a file not listed any more
	// leaves it, so the counts are only ever of files there now.
	QSet<QString> selectedPaths;
	for (const auto* file : files_)
		if (file->selected)
			selectedPaths.insert(file->path);
	changing_ = true;
	beginResetModel();
	nodes_.clear();
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
				created->token = nextToken_++;
				created->name = parts[p];
				created->path = last ? path : prefix;
				created->parent = at;
				created->rowInParent = static_cast<int>(at->children.size());
				created->folder = !last;
				if (last)
					created->fileIndex = i;
				next = created.get();
				nodes_.insert(created->token, next);
				at->children.push_back(std::move(created));
			}
			at = next;
		}
		at->selected = selectedPaths.contains(at->path);
		files_.push_back(at);
	}
	changing_ = false;
	endResetModel();
	if (!alive || epoch != generation_)
		return false;
	nextStat_ = 0;
	Q_EMIT stateChanged();
	if (!alive || epoch != generation_)
		return false;
	Q_EMIT selectionChanged();
	return alive && epoch == generation_;
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
			    file->savable = resource::has(result.reply.flags, resource::FileFlag::Savable);
		    } else
			    file->statError = failure(result);
		    // A selected file stays selected; its size and whether Download
		    // takes it are news to the selection's figures.
		    const bool selected = file->selected;
		    QTimer::singleShot(0, this, [this, epoch, file, selected] {
			    if (epoch != generation_)
				    return;
			    QPointer<DeviceFilesModel> alive(this);
			    for (const Node* at = file; at && at != root_.get(); at = at->parent) {
				    const auto changed = indexOf(at);
				    Q_EMIT dataChanged(changed, changed);
				    if (!alive || epoch != generation_)
					    return;
			    }
			    if (selected) {
				    Q_EMIT selectionChanged();
				    if (!alive || epoch != generation_)
					    return;
			    }
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
	const auto epoch = generation_;
	QPointer<DeviceFilesModel> alive(this);
	for (const Node* at : touched) {
		const auto changed = indexOf(at);
		Q_EMIT dataChanged(changed, changed, {IsSelectedRole, SelectedInsideRole});
		if (!alive || epoch != generation_)
			return;
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
	if (changing_ || (index.isValid() && !node(index)))
		return;
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
	if (changing_)
		return;
	QSet<const Node*> wanted;
	bool knownTarget = false;
	for (const auto& value : indexes) {
		const auto index = value.value<QModelIndex>();
		if (const auto* at = index.isValid() ? node(index) : nullptr) {
			knownTarget = true;
			targetsOf(at, wanted);
		}
	}
	if (!indexes.isEmpty() && !knownTarget)
		return;
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
	if (changing_)
		return;
	Touched touched;
	for (auto* file : files_)
		if (file->downloadable())
			setSelected(file, true, touched);
	publishSelection(touched);
}

void DeviceFilesModel::clearSelection()
{
	if (changing_)
		return;
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
	if (changing_)
		return paths;
	for (const auto* file : files_)
		if (file->selected)
			paths << file->path;
	return paths;
}

QVariantList DeviceFilesModel::selectedFiles(const QModelIndex& current) const
{
	QVariantList chosen;
	if (changing_)
		return chosen;
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
		                           {QStringLiteral("access"), !from->sizeKnown ? QString{} : from->readable && from->writable ? tr("read, write") : from->readable ? tr("read") : from->writable ? tr("write only") : tr("no access")},
		                           {QStringLiteral("accessMark"), markOf(from)},
		                           {QStringLiteral("accessWords"), accessWords(markOf(from))}});
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
	if (changing_)
		return {};
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

// Every file asks again; the rows shown say so, one announcement per
// folder over its rows. A codec added to the viewer later shows from the
// next Refresh or the next lookup set.
void DeviceFilesModel::setCodecLookup(CodecLookup lookup)
{
	codecLookup_ = std::move(lookup);
	if (changing_)
		return;
	const auto epoch = generation_;
	QPointer<DeviceFilesModel> alive(this);
	QList<QPair<QModelIndex, QModelIndex>> ranges;
	// Finish traversing borrowed nodes before the first public notification.
	const std::function<void(Node*)> forget = [&](Node* folder) {
		for (const auto& child : folder->children)
			if (child->folder)
				forget(child.get());
			else
				child->kindKnown = false;
		if (!folder->children.empty()) {
			const auto parent = indexOf(folder);
			ranges.append({index(0, 0, parent), index(static_cast<int>(folder->children.size()) - 1, 0, parent)});
		}
	};
	forget(root_.get());
	for (const auto& range : ranges) {
		Q_EMIT dataChanged(range.first, range.second, {KindRole, KindTextRole, ProgramFileRole});
		if (!alive || epoch != generation_)
			return;
	}
}

QString DeviceFilesModel::markOf(const Node* at) const
{
	return accessMark(at->sizeKnown, !at->statError.isEmpty(), at->readable, at->writable, at->savable);
}

DeviceFilesModel::Kind DeviceFilesModel::kindOf(const Node* at) const
{
	if (at->kindKnown)
		return at->kind;
	const auto epoch = generation_;
	QPointer<const DeviceFilesModel> alive(this);
	const auto found = findKind(at);
	// A host codec lookup is a direct user callback, just like a signal receiver.
	if (!alive || epoch != generation_)
		return {};
	at->kind = found;
	at->kindKnown = true;
	return found;
}

DeviceFilesModel::Kind DeviceFilesModel::findKind(const Node* at) const
{
	if (at->folder)
		return {QStringLiteral("folder"), {}, false};
	const auto path = at->path;
	const auto name = at->name;
	const auto lookup = codecLookup_;
	const auto words = [](const QString& kind) {
		if (kind == QLatin1String("descriptor"))
			return tr("Descriptor");
		if (kind == QLatin1String("values"))
			return tr("Values");
		if (kind == QLatin1String("identity"))
			return tr("Identity");
		if (kind == QLatin1String("session"))
			return tr("Session");
		if (kind == QLatin1String("capability"))
			return tr("Capability");
		if (kind == QLatin1String("json"))
			return tr("JSON");
		if (kind == QLatin1String("table"))
			return tr("CSV");
		if (kind == QLatin1String("text"))
			return tr("Text");
		return tr("Binary");
	};
	// The program's own files, by the names their codecs know them by - so
	// an icon never drifts from what opens the file - and by their place.
	QString program;
	if (DescriptorContent::names(path))
		program = QStringLiteral("descriptor");
	else if (ValuesContent::names(path))
		program = QStringLiteral("values");
	else if (CapabilityContent::names(path))
		program = QStringLiteral("capability");
	else if (path == QLatin1String("/telemetry/identity.bin"))
		program = QStringLiteral("identity");
	else if (path == QLatin1String("/telemetry/session.bin"))
		program = QStringLiteral("session");
	if (!program.isEmpty())
		return {program, words(program), true};
	// A host's codec that takes the file by its place and name.
	if (lookup) {
		const auto codec = lookup(path);
		if (codec == QLatin1String("JSON"))
			return {QStringLiteral("json"), words(QStringLiteral("json")), false};
		if (!codec.isEmpty() && codec != QLatin1String("Descriptor") && codec != QLatin1String("Values") &&
		    codec != QLatin1String("Capability"))
			return {QStringLiteral("codec"), codec, false};
	}
	// The formats a view of the program's own opens, by their extension.
	const auto suffix = name.section(QLatin1Char('.'), -1).toLower();
	const bool hasSuffix = name.contains(QLatin1Char('.'));
	static const QStringList textual{
	    QStringLiteral("txt"),  QStringLiteral("log"),  QStringLiteral("md"),   QStringLiteral("ini"),
	    QStringLiteral("cfg"),  QStringLiteral("conf"), QStringLiteral("yaml"), QStringLiteral("yml"),
	    QStringLiteral("toml"), QStringLiteral("xml"),  QStringLiteral("html"), QStringLiteral("htm"),
	    QStringLiteral("c"),    QStringLiteral("h"),    QStringLiteral("cpp"),  QStringLiteral("hpp"),
	    QStringLiteral("py"),   QStringLiteral("sh")};
	QString kind = QStringLiteral("binary");
	if (hasSuffix && suffix == QLatin1String("json"))
		kind = QStringLiteral("json");
	else if (hasSuffix && (suffix == QLatin1String("csv") || suffix == QLatin1String("tsv")))
		kind = QStringLiteral("table");
	else if (hasSuffix && textual.contains(suffix))
		kind = QStringLiteral("text");
	return {kind, words(kind), false};
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
	if (changing_)
		return {};
	for (const auto* file : files_)
		if (file->path == path)
			return indexOf(file);
	// A folder's path is its prefix: "/logs".
	return path.isEmpty() ? QModelIndex{} : indexOf(root_->folderAt(path));
}

DeviceFilesModel::Node* DeviceFilesModel::node(const QModelIndex& index) const noexcept
{
	if (changing_)
		return nullptr;
	if (!index.isValid())
		return root_.get();
	if (index.model() != this || index.column() != 0)
		return nullptr;
	auto* at = nodes_.value(index.internalId(), nullptr);
	return at && at->row() == index.row() ? at : nullptr;
}

QModelIndex DeviceFilesModel::indexOf(const Node* at) const
{
	if (!at || at == root_.get())
		return {};
	return createIndex(at->row(), 0, at->token);
}

QModelIndex DeviceFilesModel::index(int row, int column, const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	if (!owner || column != 0 || row < 0 || row >= static_cast<int>(owner->children.size()))
		return {};
	return createIndex(row, 0, owner->children[static_cast<std::size_t>(row)]->token);
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

int DeviceFilesModel::columnCount(const QModelIndex& parentIndex) const
{
	return node(parentIndex) ? 1 : 0;
}

Qt::ItemFlags DeviceFilesModel::flags(const QModelIndex& index) const
{
	return index.isValid() && node(index) ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
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
		case KindRole:
		case KindTextRole:
		case ProgramFileRole: {
			if (role == KindTextRole && at->folder)
				return files == 1 ? tr("1 file") : tr("%n files", nullptr, files);
			const auto kind = kindOf(at);
			// A host lookup may have retired the source during this query.
			if (kind.kind.isNull())
				return {};
			if (role == KindRole)
				return kind.kind;
			return role == KindTextRole ? QVariant(kind.text) : QVariant(kind.program);
		}
		case AccessMarkRole:
			return at->folder ? QString{} : markOf(at);
		case AccessWordsRole:
			return at->folder ? QString{} : accessWords(markOf(at));
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
	        {ReadableInsideRole, "readableInside"},
	        {KindRole, "kind"},
	        {KindTextRole, "kindText"},
	        {ProgramFileRole, "programFile"},
	        {AccessMarkRole, "accessMark"},
	        {AccessWordsRole, "accessWords"}};
}
} // namespace telemetry::ux
