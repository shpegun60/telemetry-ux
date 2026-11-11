/*
 * @file TelemetryCodecs.cpp
 * @brief The codecs of the Telemetry formats.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "TelemetryCodecs.hpp"
#include "DescriptorContent.hpp"
#include "FileContent.hpp"

namespace telemetry::ux {
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
	    QUrl(QStringLiteral("qrc:/qt-project.org/imports/Telemetry/UX/DescriptorView.qml"))});
}
} // namespace telemetry::ux
