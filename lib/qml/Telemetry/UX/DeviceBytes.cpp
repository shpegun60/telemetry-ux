/*
 * @file DeviceBytes.cpp
 * @brief The device's bytes as read, kept in the device's folder and found again.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "DeviceBytes.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace telemetry::ux {
namespace {

// The records' folder kept out of the way of the user's files.
void hide(const QString& folder)
{
#ifdef Q_OS_WIN
	const auto native = QDir::toNativeSeparators(folder);
	const auto* path = reinterpret_cast<const wchar_t*>(native.utf16());
	const auto attributes = GetFileAttributesW(path);
	if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_HIDDEN))
		SetFileAttributesW(path, attributes | FILE_ATTRIBUTE_HIDDEN);
#else
	Q_UNUSED(folder);
#endif
}

QString tr(const char* text)
{
	return QCoreApplication::translate("DeviceBytes", text);
}

QString absolute(const QString& path)
{
	return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// Folders above a copy that are searched for its record.
constexpr int maxLevels = 16;

// Where localFile's record goes inside deviceFolder, as bytesFile; otherwise
// why it cannot go there.
QString placeOf(const QString& deviceFolder, const QString& localFile, QString& bytesFile)
{
	if (deviceFolder.isEmpty() || localFile.isEmpty())
		return tr("No device folder to keep the device's bytes in.");
	const QDir device(absolute(deviceFolder));
	const auto relative = device.relativeFilePath(absolute(localFile));
	if (relative.isEmpty() || relative.startsWith(QStringLiteral("..")) || QDir::isAbsolutePath(relative))
		return tr("The copy is not inside the device's folder.");
	bytesFile = device.filePath(DeviceBytes::folderName() + QLatin1Char('/') + relative);
	return {};
}

} // namespace

QString DeviceBytes::folderName()
{
	return QStringLiteral(".device-bytes");
}

QString DeviceBytes::keep(const QString& deviceFolder, const QString& localFile, const QString& devicePath,
                          const QDateTime& readAt)
{
	// Where it would go is checked before the copy is read.
	QString bytesFile;
	if (const auto refused = placeOf(deviceFolder, localFile, bytesFile); !refused.isEmpty())
		return refused;
	QFile source(localFile);
	if (!source.open(QIODevice::ReadOnly))
		return source.errorString();
	return keepBytes(deviceFolder, localFile, devicePath, source.readAll(), readAt);
}

bool DeviceBytes::matches(const QString& localFile, const QString& devicePath, const QByteArray& bytes)
{
	const auto record = find(localFile);
	return record && record->devicePath == devicePath && record->size == bytes.size() &&
	       record->sha256 == QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

bool DeviceBytes::inside(const QString& deviceFolder, const QString& localFile)
{
	QString bytesFile;
	return placeOf(deviceFolder, localFile, bytesFile).isEmpty();
}

QString DeviceBytes::keepBytes(const QString& deviceFolder, const QString& localFile, const QString& devicePath,
                               const QByteArray& bytes, const QDateTime& readAt, bool written)
{
	QString bytesFile;
	if (const auto refused = placeOf(deviceFolder, localFile, bytesFile); !refused.isEmpty())
		return refused;
	if (!QDir().mkpath(QFileInfo(bytesFile).absolutePath()))
		return tr("Could not make the folder for %1.").arg(QDir::toNativeSeparators(bytesFile));
	hide(QDir(absolute(deviceFolder)).filePath(folderName()));
	QSaveFile data(bytesFile);
	if (!data.open(QIODevice::WriteOnly) || data.write(bytes) != bytes.size() || !data.commit())
		return data.errorString();
	const QJsonObject record{
	    {QStringLiteral("devicePath"), devicePath},
	    {QStringLiteral("size"), static_cast<double>(bytes.size())},
	    {QStringLiteral("readAt"), readAt.toString(Qt::ISODateWithMs)},
	    {QStringLiteral("written"), written},
	    {QStringLiteral("sha256"),
	     QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
	};
	QSaveFile meta(bytesFile + QStringLiteral(".json"));
	const auto json = QJsonDocument(record).toJson();
	if (!meta.open(QIODevice::WriteOnly) || meta.write(json) != json.size() || !meta.commit())
		return meta.errorString();
	return {};
}

namespace {

// Where localFile's record is: the nearest `.device-bytes` above it holding
// one, as its bytes file; empty when there is none.
QString recordOf(const QString& localFile)
{
	if (localFile.isEmpty())
		return {};
	const auto file = absolute(localFile);
	// A record's own bytes are never copies with records of their own.
	if (file.contains(QLatin1Char('/') + DeviceBytes::folderName() + QLatin1Char('/')))
		return {};
	QDir folder = QFileInfo(file).absoluteDir();
	for (int level = 0; level < maxLevels; ++level) {
		const auto bytesFile =
		    folder.filePath(DeviceBytes::folderName() + QLatin1Char('/') + folder.relativeFilePath(file));
		if (QFileInfo::exists(bytesFile + QStringLiteral(".json")))
			return bytesFile;
		if (!folder.cdUp())
			break;
	}
	return {};
}

} // namespace

void DeviceBytes::forget(const QString& localFile)
{
	const auto bytesFile = recordOf(localFile);
	if (bytesFile.isEmpty())
		return;
	QFile::remove(bytesFile + QStringLiteral(".json"));
	QFile::remove(bytesFile);
}

std::optional<DeviceBytes::Record> DeviceBytes::find(const QString& localFile)
{
	const auto bytesFile = recordOf(localFile);
	{
		QFile meta(bytesFile + QStringLiteral(".json"));
		if (!bytesFile.isEmpty() && meta.open(QIODevice::ReadOnly)) {
			const auto object = QJsonDocument::fromJson(meta.readAll()).object();
			Record record;
			record.bytesFile = bytesFile;
			record.devicePath = object.value(QStringLiteral("devicePath")).toString();
			record.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble(-1));
			record.readAt = QDateTime::fromString(object.value(QStringLiteral("readAt")).toString(), Qt::ISODateWithMs);
			record.sha256 = QByteArray::fromHex(object.value(QStringLiteral("sha256")).toString().toLatin1());
			record.written = object.value(QStringLiteral("written")).toBool();
			// Bytes gone or cut, or a record that does not read: none.
			if (record.size < 0 || record.sha256.size() != 32 || QFileInfo(bytesFile).size() != record.size)
				return std::nullopt;
			return record;
		}
	}
	return std::nullopt;
}

std::optional<FileContent::Reference> DeviceBytes::reference(const QString& localFile, const QString& devicePath)
{
	const auto record = find(localFile);
	if (!record || (!devicePath.isEmpty() && record->devicePath != devicePath))
		return std::nullopt;
	QFile data(record->bytesFile);
	if (!data.open(QIODevice::ReadOnly))
		return std::nullopt;
	auto bytes = data.readAll();
	if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != record->sha256)
		return std::nullopt;
	FileContent::Reference reference;
	reference.bytes = std::move(bytes);
	reference.written = record->written;
	const QLocale locale;
	const auto time = locale.toString(record->readAt.time(), QLocale::ShortFormat);
	const bool today = record->readAt.date() == QDate::currentDate();
	const auto date = locale.toString(record->readAt.date(), QLocale::ShortFormat);
	// Whole phrases, so a translator can reorder each.
	auto when = record->written ? (today ? tr("the device's bytes as written today at %1").arg(time)
	                                     : tr("the device's bytes as written on %1 at %2").arg(date, time))
	                            : (today ? tr("the device's bytes as read today at %1").arg(time)
	                                     : tr("the device's bytes as read on %1 at %2").arg(date, time));
	// The date and time stay on one line wherever the sentence wraps.
	const auto from = when.indexOf(QRegularExpression(QStringLiteral("\\d")));
	if (from >= 0) {
		auto tail = when.mid(from);
		tail.replace(QLatin1Char(' '), QChar(0x00a0));
		tail.replace(QLatin1Char('-'), QChar(0x2011));
		when = when.left(from) + tail;
	}
	reference.label = when;
	return reference;
}

FileContent::ReferenceLookup DeviceBytes::lookup()
{
	return [](const QString& localFile, const QString& devicePath) { return reference(localFile, devicePath); };
}

} // namespace telemetry::ux
