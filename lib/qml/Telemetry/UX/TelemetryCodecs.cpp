/*
 * @file TelemetryCodecs.cpp
 * @brief The codecs of the Telemetry formats.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "TelemetryCodecs.hpp"
#include "CapabilityContent.hpp"
#include "DescriptorContent.hpp"
#include "DeviceFileLive.hpp"
#include "DeviceFilesModel.hpp"
#include "FileContent.hpp"
#include "JsonContent.hpp"
#include "ValuesContent.hpp"
#include <integration/qt/ConnectionSession.hpp>
#include <integration/qt/ConnectionStorage.hpp>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <memory>

namespace telemetry::ux {
namespace {
// The fingerprint in a descriptor file's header (TDS3, little-endian at 16).
bool descriptorFingerprint(const QByteArray& head, quint64& fingerprint)
{
	if (head.size() < 24 || !head.startsWith("TDS3"))
		return false;
	fingerprint = 0;
	for (int i = 0; i < 8; ++i)
		fingerprint |= static_cast<quint64>(static_cast<quint8>(head[16 + i])) << (8 * i);
	return true;
}

// A descriptor file with this fingerprint, whole; empty when it is not one.
QByteArray descriptorIn(const QString& path, quint64 fingerprint)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024)
		return {};
	quint64 found = 0;
	if (!descriptorFingerprint(file.peek(24), found) || found != fingerprint)
		return {};
	return file.readAll();
}
} // namespace

void installTelemetryCodecs(OpenFiles& files)
{
	files.addCodec(FileCodec{
	    QStringLiteral("Descriptor"),
	    [](const FileIdentity& identity) {
		    // Its signature is surer than its name: a renamed copy still opens.
		    if (identity.head.startsWith("TDS3"))
			    return FileMatch::Signature;
		    return DescriptorContent::names(identity.devicePath) ? FileMatch::Path : FileMatch::No;
	    },
	    [](const QByteArray& bytes, const FileCodecContext&, QString& error) -> std::unique_ptr<QObject> {
		    auto content = std::make_unique<DescriptorContent>(bytes);
		    if (!content->valid()) {
			    error = content->error();
			    return nullptr;
		    }
		    return content;
	    },
	    QUrl(QStringLiteral("qrc:/qt/qml/Telemetry/UX/DescriptorView.qml"))});

	files.addCodec(FileCodec{
	    QStringLiteral("Values"),
	    [](const FileIdentity& identity) {
		    if (identity.head.startsWith("TVL3"))
			    return FileMatch::Signature;
		    return ValuesContent::names(identity.devicePath) ? FileMatch::Path : FileMatch::No;
	    },
	    [](const QByteArray& bytes, const FileCodecContext& context, QString& error) -> std::unique_ptr<QObject> {
		    quint64 fingerprint = 0;
		    QByteArray descriptor;
		    if (ValuesContent::fingerprintOf(bytes, fingerprint)) {
			    // The descriptor downloaded with it first: the same device, the same moment.
			    if (!context.localFile.isEmpty())
				    descriptor = descriptorIn(QFileInfo(context.localFile).dir().filePath(QStringLiteral("descriptor.bin")),
				                              fingerprint);
			    if (descriptor.isEmpty() && context.descriptor)
				    descriptor = context.descriptor(fingerprint);
		    }
		    auto content = std::make_unique<ValuesContent>(bytes, descriptor);
		    quint64 connected = 0;
		    if (content->needsDescriptor() && context.connectedFingerprint && context.connectedFingerprint(connected))
			    content->setConnectedFingerprint(connected);
		    if (!content->valid() && !content->needsDescriptor()) {
			    error = content->error();
			    return nullptr;
		    }
		    return content;
	    },
	    QUrl(QStringLiteral("qrc:/qt/qml/Telemetry/UX/ValuesView.qml"))});

	files.addCodec(FileCodec{
	    QStringLiteral("Capability"),
	    [](const FileIdentity& identity) {
		    if (CapabilityContent::signature(identity.head))
			    return FileMatch::Signature;
		    return CapabilityContent::names(identity.devicePath) ? FileMatch::Path : FileMatch::No;
	    },
	    [](const QByteArray& bytes, const FileCodecContext& context, QString& error) -> std::unique_ptr<QObject> {
		    quint64 connected = 0;
		    const bool hasConnected = context.connectedFingerprint && context.connectedFingerprint(connected);
		    auto content = std::make_unique<CapabilityContent>(bytes, hasConnected, connected);
		    if (!content->valid()) {
			    error = content->error();
			    return nullptr;
		    }
		    return content;
	    },
	    QUrl(QStringLiteral("qrc:/qt/qml/Telemetry/UX/CapabilityView.qml"))});

	// JSON is no Telemetry format, but a device's files are often JSON.
	installJsonCodec(files);
}

void connectTelemetryDevice(OpenFiles& files, integration::qt::ConnectionSession* session,
                            DeviceFilesModel* deviceFiles, std::function<QStringList()> devicesRoots)
{
	const QPointer<integration::qt::ConnectionSession> connected(session);
	FileCodecContext context;
	context.descriptor = [connected, devicesRoots](quint64 fingerprint) -> QByteArray {
		// The connected device's own, when it is the one.
		if (connected) {
			if (const auto storage = connected->storage();
			    storage && storage->schema.valid() && storage->schema.fingerprint() == fingerprint)
				return storage->descriptor;
		}
		// One downloaded from a device: <root>/<device>/telemetry/descriptor.bin.
		for (const auto& root : devicesRoots ? devicesRoots() : QStringList{}) {
			if (root.isEmpty())
				continue;
			// The most recently used device first.
			const auto folders = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
			for (const auto& folder : folders) {
				const auto found = descriptorIn(
				    QDir(folder.absoluteFilePath()).filePath(QStringLiteral("telemetry/descriptor.bin")), fingerprint);
				if (!found.isEmpty())
					return found;
			}
		}
		return {};
	};
	context.connectedFingerprint = [connected](quint64& fingerprint) {
		if (!connected)
			return false;
		const auto storage = connected->storage();
		if (!storage || !storage->schema.valid())
			return false;
		fingerprint = storage->schema.fingerprint();
		return true;
	};
	files.setContext(std::move(context));
	const QPointer<DeviceFilesModel> listed(deviceFiles);
	files.setLiveFactory([connected, listed](const QString& devicePath, QObject* parent) -> QObject* {
		if (!connected || devicePath.isEmpty())
			return nullptr;
		return new DeviceFileLive(connected, listed, devicePath, parent);
	});
	// A file that waits for its descriptor gets another try whenever the
	// connection changes: a device admitted may have it, and what the view
	// says about the connected device stays true.
	if (session) {
		auto connectedBefore = std::make_shared<bool>(session->ready());
		QObject::connect(session, &integration::qt::ConnectionSession::stateChanged, &files,
		                 [&files, connected, connectedBefore] {
			                 const bool now = connected && connected->ready();
			                 if (now == *connectedBefore)
				                 return;
			                 *connectedBefore = now;
			                 files.contextChanged();
		                 });
	}
}
} // namespace telemetry::ux
