/*
 * @file FileContent.cpp
 * @brief One file's bytes for the viewers: text when it is text, a table when it is CSV, always hex.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "FileContent.hpp"
#include "DeviceBytes.hpp"
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

FileContent::FileContent(QObject* parent)
    : QObject(parent),
      target_([this](const QString&, const QVariantMap& state, const QStringList& keys) { return applyBytes(state, keys); },
              [this] {
	              return writing_ ? tr("Wait for %1 to finish writing to the device.").arg(stepName()) : QString{};
              }),
      hex_(this), table_(this), lines_(this), rows_(&table_, this)
{
	history_.setLimit(1000);
}

void FileContent::setWriting(bool writing)
{
	if (writing_ == writing)
		return;
	writing_ = writing;
	history_.refresh();
	Q_EMIT editChanged();
}

QString FileContent::stepName() const
{
	return QFileInfo(devicePath_.isEmpty() ? localFile_ : devicePath_).fileName();
}

int FileContent::recordBytes(const QString& text, QVariantMap before, QVariantMap after, const QString& mergeKey)
{
	return history_.record(text, {UndoPart{&target_, QStringLiteral("file.bytes"), std::move(before), std::move(after)}},
	                       mergeKey);
}

// A step's bytes back: the whole copy first ("*"), then each place by its
// offset. A place outside the copy - it was read again meanwhile - is skipped.
UndoOutcome FileContent::applyBytes(const QVariantMap& state, const QStringList& keys)
{
	if (!loaded_)
		return {UndoOutcome::Kind::Skipped, tr("%1 is not open").arg(stepName())};
	if (keys.contains(QStringLiteral("*"))) {
		const auto whole = state.value(QStringLiteral("*")).toByteArray();
		if (whole.size() != bytes_.size())
			return {UndoOutcome::Kind::Skipped, tr("%1 has another size now").arg(stepName())};
		putWhole(whole);
		return {};
	}
	std::vector<Run> runs;
	int outside = 0;
	for (const auto& key : keys) {
		bool number = false;
		const auto offset = key.toLongLong(&number);
		const auto bytes = state.value(key).toByteArray();
		if (!number || offset < 0 || bytes.isEmpty() || offset + bytes.size() > bytes_.size()) {
			++outside;
			continue;
		}
		runs.emplace_back(offset, bytes);
	}
	std::sort(runs.begin(), runs.end(), [](const Run& a, const Run& b) { return a.first < b.first; });
	putRuns(runs);
	if (runs.empty())
		return {UndoOutcome::Kind::Skipped, tr("%1 has another size now").arg(stepName())};
	return outside ? UndoOutcome{UndoOutcome::Kind::Partial, tr("%1 has another size now").arg(stepName())}
	               : UndoOutcome{};
}

QString FileContent::readOnlyReason() const
{
	if (devicePath_.isEmpty() || !readOnlyLookup_ || !*readOnlyLookup_)
		return {};
	return (*readOnlyLookup_)(devicePath_);
}

void FileContent::setEditing(bool editing)
{
	editing = editing && loaded_ && !loading_ && readOnlyReason().isEmpty();
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
	const auto key = QString::number(offset);
	if (!typing) {
		put(offset, after);
		recordBytes(after.size() == 1 ? tr("Overwrite 1 byte in %1").arg(stepName())
		                               : tr("Overwrite %L1 bytes in %2").arg(after.size()).arg(stepName()),
		            {{key, before}}, {{key, after}});
		return true;
	}
	// Typing goes on in the step on top while it writes where that one
	// stopped or inside it, within the merge window, and nothing was saved
	// since: the history merges steps of one key, so the key says whether.
	const auto now = history_.now();
	const bool goesOn = typing_.serial != 0 && history_.undoSerial() == typing_.serial && !history_.clean() &&
	                    offset >= typing_.start && offset <= typing_.end && now - typing_.at <= UndoStep::mergeWindowMs;
	if (goesOn) {
		typing_.end = std::max(typing_.end, offset + static_cast<qint64>(after.size()));
	} else {
		typing_ = {0, offset, offset + static_cast<qint64>(after.size()), now,
		           QStringLiteral("file.typing.%1").arg(++typingRuns_)};
	}
	typing_.at = now;
	put(offset, after);
	const auto typed = typing_.end - typing_.start;
	typing_.serial = recordBytes(typed == 1 ? tr("Overwrite 1 byte in %1").arg(stepName())
	                                        : tr("Overwrite %L1 bytes in %2").arg(typed).arg(stepName()),
	                             {{key, before}}, {{key, after}}, typing_.key);
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
	QVariantMap was, is;
	for (std::size_t i = 0; i < after.size(); ++i) {
		const auto key = QString::number(after[i].first);
		was.insert(key, before[i].second);
		is.insert(key, after[i].second);
	}
	putRuns(after);
	recordBytes(tr("%1 in %2").arg(text, stepName()), std::move(was), std::move(is));
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
	const auto before = bytes_;
	putWhole(reference_);
	recordBytes(tr("Restore the device's version in %1").arg(stepName()), {{QStringLiteral("*"), before}},
	            {{QStringLiteral("*"), reference_}});
	return {};
}

void FileContent::clearReference()
{
	reference_.clear();
	referenceLabel_.clear();
	hasReference_ = false;
	referenceWritten_ = false;
	referenceDiffers_ = 0;
	savedDiffers_ = 0;
	referenceAt_ = {};
}

void FileContent::findReference()
{
	clearReference();
	if (!referenceLookup_ || !*referenceLookup_ || localFile_.isEmpty())
		return;
	if (auto found = (*referenceLookup_)(localFile_, devicePath_)) {
		reference_ = std::move(found->bytes);
		referenceLabel_ = found->label;
		referenceWritten_ = found->written;
		referenceAt_ = found->at;
		hasReference_ = true;
		referenceDiffers_ = differing(bytes_, reference_);
		savedDiffers_ = differing(saved_, reference_);
	}
}

void FileContent::setDeviceChanged(bool changed)
{
	if (changed == deviceChanged_)
		return;
	deviceChanged_ = changed;
	Q_EMIT editChanged();
}

QString FileContent::descriptorFingerprint(const QByteArray& bytes)
{
	if (bytes.size() < 24 || !bytes.startsWith("TDS3"))
		return {};
	quint64 fingerprint = 0;
	for (int i = 0; i < 8; ++i)
		fingerprint |= static_cast<quint64>(static_cast<quint8>(bytes[16 + i])) << (8 * i);
	return QStringLiteral("0x") + QStringLiteral("%1").arg(fingerprint, 16, 16, QLatin1Char('0')).toUpper();
}

QString FileContent::fingerprint() const
{
	return loaded_ ? descriptorFingerprint(bytes_) : QString{};
}

QString FileContent::referenceFingerprint() const
{
	return hasReference_ ? descriptorFingerprint(reference_) : QString{};
}

QString FileContent::connectedFingerprint() const
{
	quint64 fingerprint = 0;
	if (!context_ || !context_->connectedFingerprint || !context_->connectedFingerprint(fingerprint))
		return {};
	return QStringLiteral("0x") + QStringLiteral("%1").arg(fingerprint, 16, 16, QLatin1Char('0')).toUpper();
}

void FileContent::refreshReference()
{
	if (!loaded_)
		return;
	findReference();
	Q_EMIT editChanged();
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
	savedAt_ = QFileInfo(localFile_).lastModified();
	savedDiffers_ = referenceDiffers_;
	DeviceBytes::changed();
	history_.setClean();
	interpret();
	Q_EMIT editChanged();
	Q_EMIT changed();
	Q_EMIT saved();
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
	if (edited_ == 0 && history_.count() == 0)
		return;
	Q_EMIT aboutToEdit();
	bytes_ = saved_;
	edited_ = 0;
	referenceDiffers_ = hasReference_ ? differing(bytes_, reference_) : 0;
	history_.clear();
	typing_ = {};
	Q_EMIT bytesEdited(0, bytes_.size());
	Q_EMIT editChanged();
}

void FileContent::resetEdits()
{
	const bool had = editing_ || edited_ > 0 || history_.count() > 0;
	saved_.clear();
	edited_ = 0;
	editing_ = false;
	history_.clear();
	typing_ = {};
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
	if (live_ || retiring_ || devicePath_.isEmpty() || !liveFactory_ || !*liveFactory_)
		return live_;
	live_ = (*liveFactory_)(devicePath_, this);
	return live_;
}

bool FileContent::retire()
{
	if (retiring_)
		return retirementReady_;
	retiring_ = true;
	QPointer<FileContent> alive(this);
	Q_EMIT retiringChanged();
	if (!alive)
		return false;
	stopLive();
	if (!alive)
		return false;
	retirementReady_ = true;
	return true;
}

void FileContent::stopLive()
{
	// The getter is intentionally not used: retirement must not create Live.
	const auto existing = live_;
	if (existing && existing->property("running").toBool())
		existing->setProperty("running", false);
}

void FileContent::setDevicePath(const QString& path)
{
	if (path == devicePath_)
		return;
	const auto previousPath = devicePath_;
	const auto previousLive = live_;
	QPointer<FileContent> alive(this);
	stopLive();
	if (!alive || retiring_ || devicePath_ != previousPath || live_ != previousLive)
		return;
	devicePath_ = path;
	// A live reader reads the path it was made for, never its replacement.
	live_ = nullptr;
	if (previousLive)
		previousLive->deleteLater();
	// The device's name may say CSV where the local copy's does not.
	if (textual_ && !tabular_ && devicePath_.toLower().endsWith(QStringLiteral(".csv")))
		tabular_ = table_.setText(text_);
	// A path the host does not let be edited ends editing; edits stay.
	if (editing_ && !readOnlyReason().isEmpty()) {
		editing_ = false;
		Q_EMIT editChanged();
	}
	Q_EMIT changed();
}

void FileContent::beginReading(const QString& localFile, const QString& devicePath)
{
	const auto previousPath = devicePath_;
	const auto previousFile = localFile_;
	const auto previousLive = live_;
	QPointer<FileContent> alive(this);
	stopLive();
	if (!alive || retiring_ || devicePath_ != previousPath || localFile_ != previousFile || live_ != previousLive)
		return;
	if (devicePath != devicePath_) {
		live_ = nullptr;
		if (previousLive)
			previousLive->deleteLater();
	}
	devicePath_ = devicePath;
	localFile_ = localFile;
	loaded_ = textual_ = tabular_ = false;
	loading_ = true;
	// Read from the device anew: what was known to have changed is read.
	deviceChanged_ = false;
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
		if (result.error.isEmpty() && !reason.isEmpty()) {
			const auto sentence = problems_ ? problems_->value(codec.name) : QString{};
			result.error = sentence.isEmpty() ? tr("Not a valid %1 file: %2").arg(codec.name, reason)
			                                  : sentence.arg(reason, tabular_   ? tr("Table")
			                                                         : textual_ ? tr("Text")
			                                                                    : tr("Hex"));
		}
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
		savedAt_ = QFileInfo(file).lastModified();
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
	// A file may close itself from a view's handler: walk a copy.
	std::vector<QPointer<FileContent>> files;
	for (const auto& file : files_)
		files.emplace_back(file.get());
	for (const auto& file : files) {
		if (file)
			file->decodeAgainIfWaiting();
		if (file)
			Q_EMIT file->contextChanged();
	}
}

int OpenFiles::append()
{
	// Parented, so QML never takes it over when a script reaches it through
	// data(); files_ still decides when it goes.
	auto file = std::make_unique<FileContent>(this);
	file->setCodecs(&codecs_, &context_, &problems_);
	file->setLiveFactory(&liveFactory_);
	file->setReferenceLookup(&referenceLookup_);
	file->setReadOnlyLookup(&readOnlyLookup_);
	connect(file.get(), &FileContent::saved, this,
	        [this, content = file.get()] { Q_EMIT fileSaved(content->localFile()); });
	const auto row = static_cast<int>(files_.size());
	QPointer<OpenFiles> alive(this);
	QPointer<FileContent> added(file.get());
	beginInsertRows({}, row, row);
	files_.push_back(std::move(file));
	endInsertRows();
	if (!alive || !added)
		return -1;
	Q_EMIT countChanged();
	// The host may close this new tab, shift it by closing an earlier one,
	// or destroy the model from countChanged. Return only this same content's
	// current row; an unrelated replacement at the old row is not our file.
	if (!alive || !added)
		return -1;
	for (std::size_t at = 0; at < files_.size(); ++at)
		if (files_[at].get() == added)
			return static_cast<int>(at);
	return -1;
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
	if (row < 0)
		return -1;
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
	if (row < 0)
		return -1;
	QPointer<OpenFiles> alive(this);
	QPointer<FileContent> file(files_[static_cast<std::size_t>(row)].get());
	file->beginReading(localFile, devicePath);
	// Stopping an existing Live reader may synchronously close this document,
	// shift its row, or destroy the model. Never retain that reference/index.
	if (!alive || !file || file->retiring())
		return -1;
	row = rowOf(localFile);
	if (row < 0 || files_[static_cast<std::size_t>(row)].get() != file)
		return -1;
	// A row's loading role follows its content.
	QObject::disconnect(file, &FileContent::changed, this, nullptr);
	connect(file, &FileContent::changed, this, [this, content = file.data()] {
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

QString OpenFiles::codecForPath(const QString& devicePath) const
{
	// No bytes yet: only a codec sure of the place and name answers.
	const FileIdentity identity{devicePath, {}, {}};
	const FileCodec* surest = nullptr;
	auto how = FileMatch::Guess;
	for (const auto& codec : codecs_) {
		if (!codec.match)
			continue;
		if (const auto match = codec.match(identity); match >= FileMatch::Path && (!surest || match > how)) {
			surest = &codec;
			how = match;
		}
	}
	return surest ? surest->name : QString{};
}

void OpenFiles::close(int row)
{
	if (row < 0 || row >= count())
		return;
	QPointer<OpenFiles> alive(this);
	QPointer<FileContent> content(files_[static_cast<std::size_t>(row)].get());
	if (!content->retire())
		return;
	// Retirement is a public notification; a host may close the row itself,
	// or another, which moves this one: it is found again by its content.
	if (!alive || !content)
		return;
	if (row >= count() || files_[static_cast<std::size_t>(row)].get() != content) {
		row = -1;
		for (std::size_t at = 0; at < files_.size(); ++at)
			if (files_[at].get() == content) {
				row = static_cast<int>(at);
				break;
			}
		if (row < 0)
			return;
	}
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
