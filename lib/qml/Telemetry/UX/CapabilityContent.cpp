/*
 * @file CapabilityContent.cpp
 * @brief A device's capability record (read-many.bin, batch.bin) read for the viewer: what it allows, in words.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "CapabilityContent.hpp"
#include <wire/BatchWire.hpp>
#include <wire/ReadManyWire.hpp>
#include <QtCore/QStringList>
#include <QtCore/QVariantMap>

namespace telemetry::ux {
namespace readMany = telemetry::protocol::encoded::read_many;
namespace batch = telemetry::protocol::encoded::batch;

namespace {
QString groupsOf(quint64 fingerprint)
{
	const auto digits = QStringLiteral("%1").arg(fingerprint, 16, 16, QLatin1Char('0')).toUpper();
	QStringList groups;
	for (qsizetype i = 0; i < digits.size(); i += 4)
		groups << digits.mid(i, 4);
	return groups.join(QLatin1Char(' '));
}

// warn: what the view shows in the warning colour.
QVariantMap field(const QString& name, const QString& value, const QString& meaning, bool warn = false)
{
	return {{QStringLiteral("name"), name},
	        {QStringLiteral("value"), value},
	        {QStringLiteral("meaning"), meaning},
	        {QStringLiteral("warn"), warn}};
}

QVariantMap region(qint64 offset, qint64 length, const QString& name, int depth)
{
	return {{QStringLiteral("offset"), offset},
	        {QStringLiteral("length"), length},
	        {QStringLiteral("name"), name},
	        {QStringLiteral("path"), depth == 0 ? name : CapabilityContent::tr("record › %1").arg(name)},
	        {QStringLiteral("kind"), depth == 0 ? QStringLiteral("header") : QStringLiteral("field")},
	        {QStringLiteral("depth"), depth}};
}

// Both records share a layout; only their magic and their limits differ.
struct Record {
	std::uint32_t session = 0;
	std::uint64_t fingerprint = 0;
	std::uint8_t itemLimit = 0;
	std::uint16_t requestLimit = 0, replyLimit = 0;
};
} // namespace

CapabilityContent::CapabilityContent(const QByteArray& bytes, bool hasConnected, quint64 connected, QObject* parent)
    : QObject(parent)
{
	const std::span<const std::byte> input{reinterpret_cast<const std::byte*>(bytes.constData()),
	                                       static_cast<std::size_t>(bytes.size())};
	regions_ = headerRegions(input);
	Record record;
	std::size_t maxItems = 0, maxRequest = 0, maxReply = 0;
	if (bytes.startsWith("TRM1")) {
		kind_ = tr("Read many");
		purpose_ = tr("The device answers several Field reads in one request.");
		const auto parsed = readMany::parseCapability(input);
		if (!parsed) {
			error_ = tr("not a read-many record this program reads (24 bytes, version 1, limits within the protocol's)");
			return;
		}
		record = {parsed.value.session, parsed.value.fingerprint, parsed.value.itemLimit, parsed.value.requestLimit,
		          parsed.value.replyLimit};
		maxItems = readMany::maxItems;
		maxRequest = readMany::maxRequestPayload;
		maxReply = readMany::maxReplyPayload;
	} else if (bytes.startsWith("TBM1")) {
		kind_ = tr("Batch");
		purpose_ = tr("The device takes Field reads and writes, Commands and Services in one ordered request.");
		const auto parsed = batch::parseCapability(input);
		if (!parsed) {
			error_ = tr("not a batch record this program reads (24 bytes, version 1, limits within the protocol's)");
			return;
		}
		record = {parsed.value.session, parsed.value.fingerprint, parsed.value.itemLimit, parsed.value.requestLimit,
		          parsed.value.replyLimit};
		maxItems = batch::maxItems;
		maxRequest = batch::maxRequestPayload;
		maxReply = batch::maxReplyPayload;
	} else {
		error_ = tr("not a capability record: it starts with neither TRM1 nor TBM1");
		return;
	}
	valid_ = true;
	fingerprintGroups_ = groupsOf(record.fingerprint);
	binding_ = !hasConnected ? QString{} : connected == record.fingerprint ? QStringLiteral("connected")
	                                                                       : QStringLiteral("other");
	const auto bytesText = [](std::size_t value) { return tr("%L1 bytes").arg(value); };
	// Against the protocol's own ceiling: the same, or how far the device stays below it.
	const auto ceiling = [](std::size_t value, std::size_t most, bool bytes) {
		if (value >= most)
			return tr("the protocol's maximum");
		return bytes ? tr("the protocol allows up to %L1 bytes").arg(most) : tr("the protocol allows up to %L1").arg(most);
	};
	fields_ = {
	    field(tr("Most items in a request"), QStringLiteral("%L1").arg(record.itemLimit),
	          ceiling(record.itemLimit, maxItems, false)),
	    field(tr("Largest request"), bytesText(record.requestLimit), ceiling(record.requestLimit, maxRequest, true)),
	    field(tr("Largest reply"), bytesText(record.replyLimit), ceiling(record.replyLimit, maxReply, true)),
	    field(tr("Session"),
	          QStringLiteral("0x") + QStringLiteral("%1").arg(record.session, 8, 16, QLatin1Char('0')).toUpper(),
	          tr("new each time the device starts: the record holds for this run only")),
	    field(tr("Fingerprint"), fingerprintGroups_,
	          binding_ == QStringLiteral("connected") ? tr("matches the connected device")
	          : binding_ == QStringLiteral("other")   ? tr("another firmware's: not the connected device")
	                                                  : tr("the descriptor these limits apply to"),
	          binding_ == QStringLiteral("other")),
	};
}

QString CapabilityContent::text() const
{
	QString out = kind_ + QLatin1Char('\n');
	for (const auto& entry : fields_) {
		const auto row = entry.toMap();
		out += row.value(QStringLiteral("name")).toString() + QLatin1Char('\t') +
		       row.value(QStringLiteral("value")).toString() + QLatin1Char('\n');
	}
	return out;
}

bool CapabilityContent::names(const QString& devicePath)
{
	return devicePath == QStringLiteral("/telemetry/read-many.bin") ||
	       devicePath == QStringLiteral("/telemetry/batch.bin");
}

bool CapabilityContent::signature(const QByteArray& head)
{
	return head.startsWith("TRM1") || head.startsWith("TBM1");
}

QVariantList CapabilityContent::headerRegions(std::span<const std::byte> bytes)
{
	QVariantList regions;
	if (bytes.size() < readMany::capabilityBytes)
		return regions;
	regions.push_back(region(0, 24, tr("record"), 0));
	const struct {
		int offset, length;
		const char* name;
	} fields[] = {{0, 4, "magic"},         {4, 1, "version"},     {5, 1, "itemLimit"},  {6, 2, "profile"},
	              {8, 2, "requestLimit"}, {10, 2, "replyLimit"}, {12, 4, "session"},   {16, 8, "fingerprint"}};
	for (const auto& entry : fields)
		regions.push_back(region(entry.offset, entry.length, QString::fromLatin1(entry.name), 1));
	return regions;
}
} // namespace telemetry::ux
