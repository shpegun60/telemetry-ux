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
#include <QtCore/QCoreApplication>
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

// What a descriptor file in a folder is to a values file: not there (or of
// another fingerprint), there and read, or there with this fingerprint in its
// header but its bytes not a descriptor this program reads - a copy changed
// after it was built. A broken one is never used.
struct Beside {
	QByteArray bytes;
	bool broken = false;
};
Beside descriptorBeside(const QString& path, quint64 fingerprint)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024)
		return {};
	quint64 found = 0;
	if (!descriptorFingerprint(file.peek(24), found) || found != fingerprint)
		return {};
	auto bytes = file.readAll();
	if (DescriptorContent::check(bytes).kind != DescriptorContent::Problem::None)
		return {{}, true};
	return {std::move(bytes), false};
}

// A descriptor file with this fingerprint that reads, whole; empty otherwise.
QByteArray descriptorIn(const QString& path, quint64 fingerprint)
{
	return descriptorBeside(path, fingerprint).bytes;
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
		    // The reason in the files-only card's words; a fingerprint that
		    // does not match is a copy changed after it was built.
		    if (const auto problem = DescriptorContent::check(bytes);
		        problem.kind != DescriptorContent::Problem::None) {
			    error = problem.kind == DescriptorContent::Problem::Fingerprint
			                ? QCoreApplication::translate("TelemetryCodecs",
			                                              "its fingerprint does not match its content, so it was "
			                                              "changed after it was built")
			                : DescriptorContent::phrase(problem);
			    return nullptr;
		    }
		    auto content = std::make_unique<DescriptorContent>(bytes);
		    if (!content->valid()) {
			    error = content->error();
			    return nullptr;
		    }
		    return content;
	    },
	    QUrl(QStringLiteral("qrc:/qt/qml/Telemetry/UX/DescriptorView.qml"))});
	files.setCodecProblem(QStringLiteral("Descriptor"),
	                      QCoreApplication::translate("TelemetryCodecs",
	                                                  "This descriptor cannot be read: %1. It is shown as %2."));

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
		    QString besideProblem;
		    if (ValuesContent::fingerprintOf(bytes, fingerprint)) {
			    // The descriptor downloaded with it first: the same device, the
			    // same moment. One beside it that cannot be read never blocks
			    // the rest - the connected device's when its fingerprint is this
			    // file's, or one in a device's folder.
			    if (!context.localFile.isEmpty()) {
				    const QFileInfo local(context.localFile);
				    auto beside = descriptorBeside(local.dir().filePath(QStringLiteral("descriptor.bin")), fingerprint);
				    descriptor = std::move(beside.bytes);
				    if (beside.broken)
					    besideProblem =
					        QCoreApplication::translate("TelemetryCodecs",
					                                    "%1 cannot be decoded: %2 beside it cannot be read.")
					            .arg(QFileInfo(context.devicePath.isEmpty() ? context.localFile : context.devicePath)
					                     .fileName(),
					                 local.dir().dirName() + QStringLiteral("/descriptor.bin"));
			    }
			    if (descriptor.isEmpty() && context.descriptor)
				    descriptor = context.descriptor(fingerprint);
		    }
		    auto content = std::make_unique<ValuesContent>(bytes, descriptor);
		    if (content->needsDescriptor())
			    content->setBesideProblem(besideProblem);
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
	// The file list names a file a host's codec reads by that codec: its icon
	// and its Kind.
	if (deviceFiles) {
		const QPointer<OpenFiles> viewer(&files);
		deviceFiles->setCodecLookup([viewer](const QString& devicePath) {
			return viewer ? viewer->codecForPath(devicePath) : QString{};
		});
	}
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
