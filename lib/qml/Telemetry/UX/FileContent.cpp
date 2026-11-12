/*
 * @file FileContent.cpp
 * @brief One file's bytes for the viewers: text when it is text, a table when it is CSV, always hex.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "FileContent.hpp"
#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLocale>
#include <QtCore/QStringDecoder>
#include <algorithm>

namespace telemetry::ux {
void FileHexModel::setBytes(const QByteArray* bytes)
{
	beginResetModel();
	bytes_ = bytes;
	endResetModel();
}

int FileHexModel::rowCount(const QModelIndex& parent) const
{
	if (parent.isValid() || !bytes_)
		return 0;
	return static_cast<int>((bytes_->size() + 15) / 16);
}

QVariant FileHexModel::data(const QModelIndex& index, int role) const
{
	if (!bytes_ || !index.isValid() || index.row() < 0 || index.row() >= rowCount())
		return {};
	const qsizetype offset = static_cast<qsizetype>(index.row()) * 16;
	const qsizetype count = std::min<qsizetype>(16, bytes_->size() - offset);
	switch (role) {
		case OffsetRole:
			return QStringLiteral("%1").arg(offset, 8, 16, QLatin1Char('0')).toUpper();
		case BytesRole: {
			QString text;
			text.reserve(16 * 3 + 1);
			for (qsizetype i = 0; i < 16; ++i) {
				if (i == 8)
					text += QLatin1Char(' ');
				if (i < count)
					text += QStringLiteral("%1 ").arg(static_cast<unsigned char>(bytes_->at(offset + i)), 2, 16,
					                                  QLatin1Char('0')).toUpper();
				else
					text += QStringLiteral("   ");
			}
			return text;
		}
		case AsciiRole: {
			QString text;
			for (qsizetype i = 0; i < count; ++i) {
				const auto byte = static_cast<unsigned char>(bytes_->at(offset + i));
				text += byte >= 0x20 && byte < 0x7f ? QLatin1Char(static_cast<char>(byte)) : QLatin1Char('.');
			}
			return text;
		}
		default:
			return {};
	}
}

QHash<int, QByteArray> FileHexModel::roleNames() const
{
	return {{OffsetRole, "offset"}, {BytesRole, "bytes"}, {AsciiRole, "ascii"}};
}

namespace {
// One CSV record per entry; false when a quote is left open at the end.
bool parseCsv(const QString& text, std::vector<QStringList>& records)
{
	QStringList record;
	QString field;
	bool quoted = false, fieldStarted = false;
	for (qsizetype i = 0; i < text.size(); ++i) {
		const QChar c = text.at(i);
		if (quoted) {
			if (c == QLatin1Char('"')) {
				if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
					field += QLatin1Char('"');
					++i;
				} else
					quoted = false;
			} else
				field += c;
			continue;
		}
		if (c == QLatin1Char('"') && !fieldStarted) {
			quoted = true;
			fieldStarted = true;
		} else if (c == QLatin1Char(',')) {
			record << field;
			field.clear();
			fieldStarted = false;
		} else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
			if (c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n'))
				++i;
			record << field;
			records.push_back(record);
			record.clear();
			field.clear();
			fieldStarted = false;
		} else {
			field += c;
			fieldStarted = true;
		}
	}
	if (quoted)
		return false;
	if (fieldStarted || !record.isEmpty()) {
		record << field;
		records.push_back(record);
	}
	return true;
}
} // namespace

bool FileTableModel::setText(const QString& text)
{
	std::vector<QStringList> records;
	beginResetModel();
	header_.clear();
	rows_.clear();
	numeric_.clear();
	const bool parsed = parseCsv(text, records) && !records.empty() && !records.front().isEmpty();
	if (parsed) {
		header_ = records.front();
		qsizetype columns = header_.size();
		for (std::size_t i = 1; i < records.size(); ++i)
			columns = std::max(columns, records[i].size());
		while (header_.size() < columns)
			header_ << QString();
		rows_.assign(records.begin() + 1, records.end());
		for (auto& row : rows_)
			while (row.size() < columns)
				row << QString();
		numeric_.assign(static_cast<std::size_t>(columns), true);
		for (qsizetype column = 0; column < columns; ++column) {
			bool any = false;
			for (const auto& row : rows_) {
				const auto& cell = row.at(column);
				if (cell.trimmed().isEmpty())
					continue;
				any = true;
				bool number = false;
				cell.trimmed().toDouble(&number);
				if (!number) {
					numeric_[static_cast<std::size_t>(column)] = false;
					break;
				}
			}
			if (!any)
				numeric_[static_cast<std::size_t>(column)] = false;
		}
	}
	endResetModel();
	return parsed;
}

void FileTableModel::clear()
{
	beginResetModel();
	header_.clear();
	rows_.clear();
	numeric_.clear();
	endResetModel();
}

int FileTableModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int FileTableModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(header_.size());
}

const QString& FileTableModel::cell(int row, int column) const noexcept
{
	static const QString none;
	if (row < 0 || row >= rowCount() || column < 0 || column >= columnCount())
		return none;
	return rows_[static_cast<std::size_t>(row)].at(column);
}

QVariant FileTableModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || role != Qt::DisplayRole || index.row() >= rowCount() || index.column() >= columnCount())
		return {};
	return rows_[static_cast<std::size_t>(index.row())].at(index.column());
}

QVariant FileTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole)
		return {};
	if (orientation == Qt::Vertical)
		return section + 1;
	return section >= 0 && section < header_.size() ? QVariant(header_.at(section)) : QVariant();
}

bool FileTableModel::numeric(int column) const
{
	return column >= 0 && column < static_cast<int>(numeric_.size()) && numeric_[static_cast<std::size_t>(column)];
}

// One overwrite, undone and redone. Typing joins the step before it when it
// goes on where that one stopped, or writes again a byte it wrote (a byte's
// second hex digit): one undo takes back what was typed in one go.
class OverwriteStep final : public QUndoCommand {
public:
	OverwriteStep(FileContent& content, qint64 offset, QByteArray before, QByteArray after, bool typing)
	    : content_(content), offset_(offset), before_(std::move(before)), after_(std::move(after)), typing_(typing)
	{
		setText(FileContent::tr("Overwrite %n bytes", nullptr, static_cast<int>(after_.size())));
	}

	int id() const override { return typing_ ? 1 : -1; }

	bool mergeWith(const QUndoCommand* other) override
	{
		const auto* next = static_cast<const OverwriteStep*>(other);
		const auto end = offset_ + after_.size();
		if (!next->typing_ || next->offset_ < offset_ || next->offset_ > end)
			return false;
		for (qsizetype i = 0; i < next->after_.size(); ++i) {
			const auto at = next->offset_ - offset_ + i;
			if (at < after_.size()) {
				after_[at] = next->after_[i];
			} else {
				after_.append(next->after_[i]);
				before_.append(next->before_[i]);
			}
		}
		return true;
	}

	void redo() override { content_.put(offset_, after_); }
	void undo() override { content_.put(offset_, before_); }

private:
	FileContent& content_;
	qint64 offset_;
	QByteArray before_, after_;
	bool typing_;
};

// Several places at once - Replace all: one step, one announcement.
class RunsStep final : public QUndoCommand {
public:
	RunsStep(FileContent& content, std::vector<FileContent::Run> before, std::vector<FileContent::Run> after,
	         const QString& text)
	    : content_(content), before_(std::move(before)), after_(std::move(after))
	{
		setText(text);
	}

	void redo() override { content_.putRuns(after_); }
	void undo() override { content_.putRuns(before_); }

private:
	FileContent& content_;
	std::vector<FileContent::Run> before_, after_;
};

// The whole working copy at once - Restore: before and after kept shared.
class WholeStep final : public QUndoCommand {
public:
	WholeStep(FileContent& content, QByteArray before, QByteArray after, const QString& text)
	    : content_(content), before_(std::move(before)), after_(std::move(after))
	{
		setText(text);
	}

	void redo() override { content_.putWhole(after_); }
	void undo() override { content_.putWhole(before_); }

private:
	FileContent& content_;
	QByteArray before_, after_;
};

FileContent::FileContent(QObject* parent) : QObject(parent), hex_(this), table_(this), lines_(this), rows_(&table_, this)
{
	connect(&undo_, &QUndoStack::canUndoChanged, this, &FileContent::editChanged);
	connect(&undo_, &QUndoStack::canRedoChanged, this, &FileContent::editChanged);
}

void FileContent::setEditing(bool editing)
{
	editing = editing && loaded_ && !loading_;
	if (editing == editing_)
		return;
	editing_ = editing;
	Q_EMIT editChanged();
}

bool FileContent::overwrite(qint64 offset, const QByteArray& bytes, bool typing)
{
	if (!editing_ || !loaded_ || offset < 0 || offset >= bytes_.size() || bytes.isEmpty())
		return false;
	const auto after = bytes.left(bytes_.size() - offset);
	const auto before = bytes_.mid(offset, after.size());
	if (before == after)
		return true;
	undo_.push(new OverwriteStep(*this, offset, before, after, typing));
	return true;
}

bool FileContent::overwriteRuns(const std::vector<Run>& runs, const QString& text)
{
	if (!editing_ || !loaded_)
		return false;
	std::vector<Run> before, after;
	for (const auto& [offset, bytes] : runs) {
		if (offset < 0 || offset >= bytes_.size() || bytes.isEmpty())
			continue;
		auto written = bytes.left(bytes_.size() - offset);
		auto was = bytes_.mid(offset, written.size());
		if (was == written)
			continue;
		before.emplace_back(offset, std::move(was));
		after.emplace_back(offset, std::move(written));
	}
	if (after.empty())
		return !runs.empty();
	undo_.push(new RunsStep(*this, std::move(before), std::move(after), text));
	return true;
}

void FileContent::put(qint64 offset, const QByteArray& bytes)
{
	putRuns({Run{offset, bytes}});
}

void FileContent::putRuns(const std::vector<Run>& runs)
{
	if (runs.empty())
		return;
	Q_EMIT aboutToEdit();
	// The first edit after a read parts the working copy from the one on disk.
	auto* data = bytes_.data();
	const auto* disk = saved_.constData();
	const auto* device = reference_.constData();
	const auto deviceSize = hasReference_ ? reference_.size() : 0;
	qint64 first = runs.front().first, last = first;
	for (const auto& [offset, bytes] : runs) {
		for (qsizetype i = 0; i < bytes.size(); ++i) {
			const auto at = offset + i;
			const auto old = data[at];
			const bool was = at < saved_.size() && old != disk[at];
			data[at] = bytes[i];
			const bool is = at < saved_.size() && data[at] != disk[at];
			edited_ += static_cast<int>(is) - static_cast<int>(was);
			if (at < deviceSize)
				referenceDiffers_ += static_cast<int>(data[at] != device[at]) - static_cast<int>(old != device[at]);
		}
		first = std::min(first, offset);
		last = std::max(last, offset + static_cast<qint64>(bytes.size()));
	}
	Q_EMIT bytesEdited(first, last - first);
	Q_EMIT editChanged();
}

void FileContent::putWhole(const QByteArray& bytes)
{
	Q_EMIT aboutToEdit();
	bytes_ = bytes;
	edited_ = differing(bytes_, saved_);
	referenceDiffers_ = hasReference_ ? differing(bytes_, reference_) : 0;
	Q_EMIT bytesEdited(0, bytes_.size());
	Q_EMIT editChanged();
}

qint64 FileContent::differing(const QByteArray& a, const QByteArray& b) noexcept
{
	const auto common = std::min(a.size(), b.size());
	const auto* left = a.constData();
	const auto* right = b.constData();
	qint64 count = std::abs(static_cast<qint64>(a.size()) - static_cast<qint64>(b.size()));
	for (qsizetype i = 0; i < common; ++i)
		count += left[i] != right[i] ? 1 : 0;
	return count;
}

QString FileContent::restoreReference()
{
	if (!loaded_ || !hasReference_)
		return tr("The device's bytes are not known for this copy.");
	if (reference_.size() != bytes_.size())
		return tr("The device's file was %L1 bytes and this copy is %L2: a copy keeps its size, so read the file "
		          "from the device again instead.")
		    .arg(reference_.size())
		    .arg(bytes_.size());
	if (referenceDiffers_ == 0)
		return tr("This copy already holds the device's bytes.");
	undo_.push(new WholeStep(*this, bytes_, reference_, tr("Restore the device's version")));
	return {};
}

void FileContent::clearReference()
{
	reference_.clear();
	referenceLabel_.clear();
	hasReference_ = false;
	referenceDiffers_ = 0;
}

void FileContent::findReference()
{
	clearReference();
	if (!referenceLookup_ || !*referenceLookup_ || localFile_.isEmpty())
		return;
	if (auto found = (*referenceLookup_)(localFile_, devicePath_)) {
		reference_ = std::move(found->bytes);
		referenceLabel_ = found->label;
		hasReference_ = true;
		referenceDiffers_ = differing(bytes_, reference_);
	}
}

void FileContent::refreshReference()
{
	if (!loaded_)
		return;
	findReference();
	Q_EMIT editChanged();
}

void FileContent::undo()
{
	undo_.undo();
}

void FileContent::redo()
{
	undo_.redo();
}

QString FileContent::save()
{
	if (!loaded_ || localFile_.isEmpty())
		return tr("There is no copy on disk to save to.");
	QSaveFile file(localFile_);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes_) != bytes_.size() || !file.commit())
		return tr("Could not save %1: %2").arg(QFileInfo(localFile_).fileName(), file.errorString());
	saved_ = bytes_;
	edited_ = 0;
	readAt_ = QDateTime::currentDateTime();
	undo_.setClean();
	interpret();
	Q_EMIT editChanged();
	Q_EMIT changed();
	return {};
}

QString FileContent::saveCopyAs(const QUrl& target)
{
	const auto path = target.isLocalFile() ? target.toLocalFile() : target.toString();
	if (!loaded_ || path.isEmpty())
		return tr("Nothing to save.");
	if (QFileInfo(path) == QFileInfo(localFile_))
		return save();
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes_) != bytes_.size() || !file.commit())
		return tr("Could not save %1: %2").arg(QFileInfo(path).fileName(), file.errorString());
	return {};
}

void FileContent::discard()
{
	if (edited_ == 0 && !undo_.canUndo() && !undo_.canRedo())
		return;
	Q_EMIT aboutToEdit();
	bytes_ = saved_;
	edited_ = 0;
	referenceDiffers_ = hasReference_ ? differing(bytes_, reference_) : 0;
	undo_.clear();
	Q_EMIT bytesEdited(0, bytes_.size());
	Q_EMIT editChanged();
}

void FileContent::resetEdits()
{
	const bool had = editing_ || edited_ > 0 || undo_.count() > 0;
	saved_.clear();
	edited_ = 0;
	editing_ = false;
	undo_.clear();
	if (had)
		Q_EMIT editChanged();
}

void FileContent::setLocalFile(const QString& path)
{
	if (path == localFile_)
		return;
	localFile_ = path;
	reload();
}

QObject* FileContent::live()
{
	if (live_ || devicePath_.isEmpty() || !liveFactory_ || !*liveFactory_)
		return live_;
	live_ = (*liveFactory_)(devicePath_, this);
	return live_;
}

void FileContent::setDevicePath(const QString& path)
{
	if (path == devicePath_)
		return;
	devicePath_ = path;
	// A live reader reads the path it was made for.
	if (live_) {
		live_->deleteLater();
		live_ = nullptr;
	}
	// The device's name may say CSV where the local copy's does not.
	if (textual_ && !tabular_ && devicePath_.toLower().endsWith(QStringLiteral(".csv")))
		tabular_ = table_.setText(text_);
	Q_EMIT changed();
}

void FileContent::beginReading(const QString& localFile, const QString& devicePath)
{
	devicePath_ = devicePath;
	localFile_ = localFile;
	loaded_ = textual_ = tabular_ = false;
	loading_ = true;
	progress_ = -1;
	figures_.clear();
	error_.clear();
	text_.clear();
	hex_.setBytes(nullptr);
	bytes_.clear();
	resetEdits();
	clearReference();
	table_.clear();
	forgetDecoded();
	Q_EMIT changed();
}

void FileContent::setReading(qreal progress, const QString& figures)
{
	if (!loading_)
		return;
	progress_ = progress;
	figures_ = figures;
	Q_EMIT changed();
}

void FileContent::finishReading(bool read, const QString& reason)
{
	if (!loading_)
		return;
	loading_ = false;
	if (read) {
		reload();
		return;
	}
	const auto name = QFileInfo(devicePath_.isEmpty() ? localFile_ : devicePath_).fileName();
	error_ = tr("Could not read %1: %2").arg(name, reason);
	Q_EMIT changed();
}

QString FileContent::sizeText() const
{
	if (bytes_.size() < 1024)
		return tr("%n B", nullptr, static_cast<int>(bytes_.size()));
	return QLocale().formattedDataSize(bytes_.size(), 1, QLocale::DataSizeIecFormat);
}

QString FileContent::format() const
{
	if (!loaded_)
		return {};
	if (decoded_)
		return codecName_;
	return tabular_ ? tr("Table") : textual_ ? tr("Text") : tr("Binary");
}

void FileContent::forgetDecoded()
{
	codecName_.clear();
	codecError_.clear();
	codecView_.clear();
	if (decoded_) {
		// A view may still hold a model of it until it hears changed().
		decoded_->deleteLater();
		decoded_ = nullptr;
	}
}

void FileContent::decode()
{
	auto decoded = decodeBytes();
	if (decoded.model) {
		decoded_ = decoded.model.release();
		decoded_->setParent(this);
		codecName_ = decoded.name;
		codecView_ = decoded.view;
		codecError_.clear();
	} else {
		codecError_ = decoded.error;
	}
}

FileContent::Decoded FileContent::decodeBytes() const
{
	Decoded result;
	if (!codecs_ || !loaded_)
		return result;
	// Enough for any signature; the codec's parse decides.
	constexpr qsizetype headBytes = 64;
	const FileIdentity identity{devicePath_, localFile_, bytes_.left(headBytes)};
	// The surest first; the order of registration among equals.
	std::vector<std::pair<FileMatch, const FileCodec*>> candidates;
	for (const auto& codec : *codecs_)
		if (codec.match && codec.decode)
			if (const auto how = codec.match(identity); how != FileMatch::No)
				candidates.emplace_back(how, &codec);
	std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
		return static_cast<int>(a.first) > static_cast<int>(b.first);
	});
	// The host's context, and which file this is.
	FileCodecContext context = context_ ? *context_ : FileCodecContext{};
	context.localFile = localFile_;
	context.devicePath = devicePath_;
	for (const auto& [how, candidate] : candidates) {
		const auto& codec = *candidate;
		QString reason;
		if (auto model = codec.decode(bytes_, context, reason)) {
			result.model = std::move(model);
			result.name = codec.name;
			result.view = codec.view;
			result.error.clear();
			return result;
		}
		// The first codec that took the file for its own says why it could not;
		// one that only guessed says nothing.
		if (result.error.isEmpty() && !reason.isEmpty())
			result.error = tr("Not a valid %1 file: %2").arg(codec.name, reason);
	}
	return result;
}

bool FileContent::looksLikeText(const QByteArray& bytes)
{
	if (bytes.contains('\0'))
		return false;
	auto decoder = QStringDecoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
	const QString decoded = decoder(bytes);
	if (decoder.hasError())
		return false;
	qsizetype control = 0;
	for (const QChar c : decoded)
		if (c.unicode() < 0x20 && c != QLatin1Char('\t') && c != QLatin1Char('\n') && c != QLatin1Char('\r'))
			++control;
	// "Few": at most one in a hundred characters.
	return control * 100 <= decoded.size();
}

bool FileContent::decodeAgainIfWaiting()
{
	if (!decoded_ || !decoded_->property("needsDescriptor").toBool())
		return false;
	auto again = decodeBytes();
	if (again.model && again.model->property("needsDescriptor").toBool() &&
	    again.model->property("connectedFingerprintGroups") == decoded_->property("connectedFingerprintGroups"))
		return false;
	forgetDecoded();
	if (again.model) {
		decoded_ = again.model.release();
		decoded_->setParent(this);
		codecName_ = again.name;
		codecView_ = again.view;
	} else {
		codecError_ = again.error;
	}
	Q_EMIT changed();
	return true;
}

bool FileContent::reload()
{
	loaded_ = textual_ = tabular_ = false;
	error_.clear();
	text_.clear();
	bytes_.clear();
	resetEdits();
	clearReference();
	hex_.setBytes(nullptr);
	table_.clear();
	lines_.clear();
	forgetDecoded();
	if (localFile_.isEmpty()) {
		Q_EMIT changed();
		return false;
	}
	QFile file(localFile_);
	if (!file.open(QIODevice::ReadOnly))
		error_ = tr("Could not read %1: %2").arg(QFileInfo(localFile_).fileName(), file.errorString());
	else if (file.size() > maxBytes)
		error_ = tr("%1 is larger than the viewer reads (%2).")
		             .arg(QFileInfo(localFile_).fileName(),
		                  QLocale().formattedDataSize(maxBytes, 0, QLocale::DataSizeIecFormat));
	else {
		bytes_ = file.readAll();
		saved_ = bytes_;
		loaded_ = true;
		readAt_ = QDateTime::currentDateTime();
		hex_.setBytes(&bytes_);
		interpret();
		// The device's layer, from what was kept when the file was read from it.
		findReference();
		Q_EMIT editChanged();
	}
	Q_EMIT changed();
	return loaded_;
}

void FileContent::interpret()
{
	textual_ = tabular_ = false;
	text_.clear();
	table_.clear();
	lines_.clear();
	forgetDecoded();
	if (bytes_.size() <= maxTextBytes && looksLikeText(bytes_)) {
		textual_ = true;
		text_ = QString::fromUtf8(bytes_);
		lines_.setText(text_);
		const auto name = (devicePath_.isEmpty() ? localFile_ : devicePath_).toLower();
		if (name.endsWith(QStringLiteral(".csv")))
			tabular_ = table_.setText(text_);
	}
	decode();
}

void OpenFiles::addCodec(FileCodec codec)
{
	codecs_.push_back(std::move(codec));
}

QStringList OpenFiles::modifiedNames() const
{
	QStringList names;
	for (const auto& file : files_)
		if (file->modified())
			names << QFileInfo(file->devicePath().isEmpty() ? file->localFile() : file->devicePath()).fileName();
	return names;
}

QString OpenFiles::saveAll()
{
	for (const auto& file : files_)
		if (file->modified())
			if (const auto problem = file->save(); !problem.isEmpty())
				return problem;
	return {};
}

void OpenFiles::discardAll()
{
	for (const auto& file : files_)
		file->discard();
}

QVariantList FileContent::regions() const
{
	return decoded_ ? decoded_->property("regions").toList() : QVariantList{};
}

void OpenFiles::contextChanged()
{
	for (const auto& file : files_)
		file->decodeAgainIfWaiting();
}

int OpenFiles::append()
{
	// Parented, so QML never takes it over when a script reaches it through
	// data(); files_ still decides when it goes.
	auto file = std::make_unique<FileContent>(this);
	file->setCodecs(&codecs_, &context_);
	file->setLiveFactory(&liveFactory_);
	file->setReferenceLookup(&referenceLookup_);
	const auto row = static_cast<int>(files_.size());
	beginInsertRows({}, row, row);
	files_.push_back(std::move(file));
	endInsertRows();
	Q_EMIT countChanged();
	return row;
}

int OpenFiles::open(const QString& localFile, const QString& devicePath)
{
	for (std::size_t row = 0; row < files_.size(); ++row) {
		auto& file = *files_[row];
		if (file.localFile() == localFile && file.devicePath() == devicePath) {
			// Its edits are kept: a host asks before it reads the file again.
			if (!file.modified())
				file.reload();
			const auto changed = index(static_cast<int>(row));
			Q_EMIT dataChanged(changed, changed);
			return static_cast<int>(row);
		}
	}
	const auto row = append();
	auto& file = *files_[static_cast<std::size_t>(row)];
	file.setDevicePath(devicePath);
	file.setLocalFile(localFile);
	const auto changed = index(row);
	Q_EMIT dataChanged(changed, changed);
	return row;
}

int OpenFiles::reading(const QString& localFile, const QString& devicePath)
{
	auto row = rowOf(localFile);
	// Edits not saved are never read over: the host asks first.
	if (row >= 0 && files_[static_cast<std::size_t>(row)]->modified())
		return row;
	if (row < 0)
		row = append();
	auto& file = *files_[static_cast<std::size_t>(row)];
	file.beginReading(localFile, devicePath);
	// A row's loading role follows its content.
	QObject::disconnect(&file, nullptr, this, nullptr);
	connect(&file, &FileContent::changed, this, [this, content = &file] {
		for (std::size_t i = 0; i < files_.size(); ++i)
			if (files_[i].get() == content) {
				const auto changed = index(static_cast<int>(i));
				Q_EMIT dataChanged(changed, changed, {LoadingRole});
			}
	});
	const auto changed = index(row);
	Q_EMIT dataChanged(changed, changed);
	return row;
}

FileContent* OpenFiles::contentFor(const QString& localFile) const
{
	const auto row = rowOf(localFile);
	return row < 0 ? nullptr : files_[static_cast<std::size_t>(row)].get();
}

int OpenFiles::rowOf(const QString& localFile) const
{
	for (std::size_t row = 0; row < files_.size(); ++row)
		if (files_[row]->localFile() == localFile)
			return static_cast<int>(row);
	return -1;
}

void OpenFiles::close(int row)
{
	if (row < 0 || row >= count())
		return;
	beginRemoveRows({}, row, row);
	auto* closing = files_[static_cast<std::size_t>(row)].release();
	files_.erase(files_.begin() + row);
	endRemoveRows();
	// Only now that the views have let go of the row, and later than anything
	// they posted while letting go: its table and hex models go with it.
	QObject::disconnect(closing, nullptr, this, nullptr);
	closing->deleteLater();
	Q_EMIT countChanged();
}

int OpenFiles::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : count();
}

QVariant OpenFiles::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= count())
		return {};
	auto* file = files_[static_cast<std::size_t>(index.row())].get();
	switch (role) {
		case Qt::DisplayRole:
		case NameRole: {
			const auto path = file->devicePath().isEmpty() ? file->localFile() : file->devicePath();
			return QFileInfo(path).fileName();
		}
		case DevicePathRole:
			return file->devicePath();
		case LocalFileRole:
			return file->localFile();
		case ContentRole:
			return QVariant::fromValue<QObject*>(file);
		case LoadingRole:
			return file->loading();
		default:
			return {};
	}
}

QHash<int, QByteArray> OpenFiles::roleNames() const
{
	return {{NameRole, "name"},
	        {DevicePathRole, "devicePath"},
	        {LocalFileRole, "localFile"},
	        {ContentRole, "content"},
	        {LoadingRole, "loading"}};
}
} // namespace telemetry::ux
