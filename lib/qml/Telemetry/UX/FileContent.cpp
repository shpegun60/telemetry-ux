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
	return tabular_ ? tr("Table") : textual_ ? tr("Text") : tr("Binary");
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
	}
	Q_EMIT changed();
	return loaded_;
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
	auto file = std::make_unique<FileContent>();
	file->setDevicePath(devicePath);
	file->setLocalFile(localFile);
	const auto row = static_cast<int>(files_.size());
	beginInsertRows({}, row, row);
	files_.push_back(std::move(file));
	endInsertRows();
	Q_EMIT countChanged();
	return row;
}

void OpenFiles::close(int row)
{
	if (row < 0 || row >= count())
		return;
	beginRemoveRows({}, row, row);
	// The view lets go of the content in the removal; delete it after that.
	files_[static_cast<std::size_t>(row)].release()->deleteLater();
	files_.erase(files_.begin() + row);
	endRemoveRows();
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
		default:
			return {};
	}
}

QHash<int, QByteArray> OpenFiles::roleNames() const
{
	return {{NameRole, "name"}, {DevicePathRole, "devicePath"}, {LocalFileRole, "localFile"}, {ContentRole, "content"}};
}
} // namespace telemetry::ux
