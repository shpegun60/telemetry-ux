/*
 * @file FileContent.cpp
 * @brief One file's bytes for the viewers: text when it is text, a table when it is CSV, always hex.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "FileContent.hpp"
#include <QtCore/QFile>
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

FileContent::FileContent(QObject* parent) : QObject(parent), hex_(this), table_(this) {}

void FileContent::setLocalFile(const QString& path)
{
	if (path == localFile_)
		return;
	localFile_ = path;
	reload();
}

void FileContent::setDevicePath(const QString& path)
{
	if (path == devicePath_)
		return;
	devicePath_ = path;
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
	if (!codecs_ || !loaded_)
		return;
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
	static const FileCodecContext none;
	for (const auto& [how, candidate] : candidates) {
		const auto& codec = *candidate;
		QString reason;
		if (auto model = codec.decode(bytes_, context_ ? *context_ : none, reason)) {
			decoded_ = model.release();
			decoded_->setParent(this);
			codecName_ = codec.name;
			codecView_ = codec.view;
			codecError_.clear();
			return;
		}
		// The first codec that took the file for its own says why it could not.
		if (codecError_.isEmpty())
			codecError_ = tr("Not a valid %1 file: %2").arg(codec.name, reason);
	}
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

bool FileContent::reload()
{
	loaded_ = textual_ = tabular_ = false;
	error_.clear();
	text_.clear();
	bytes_.clear();
	hex_.setBytes(nullptr);
	table_.clear();
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
		loaded_ = true;
		readAt_ = QDateTime::currentDateTime();
		hex_.setBytes(&bytes_);
		if (bytes_.size() <= maxTextBytes && looksLikeText(bytes_)) {
			textual_ = true;
			text_ = QString::fromUtf8(bytes_);
			const auto name = (devicePath_.isEmpty() ? localFile_ : devicePath_).toLower();
			if (name.endsWith(QStringLiteral(".csv")))
				tabular_ = table_.setText(text_);
		}
		decode();
	}
	Q_EMIT changed();
	return loaded_;
}

void OpenFiles::addCodec(FileCodec codec)
{
	codecs_.push_back(std::move(codec));
}

QVariantList FileContent::regions() const
{
	return decoded_ ? decoded_->property("regions").toList() : QVariantList{};
}

int OpenFiles::append()
{
	// Parented, so QML never takes it over when a script reaches it through
	// data(); files_ still decides when it goes.
	auto file = std::make_unique<FileContent>(this);
	file->setCodecs(&codecs_, &context_);
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
