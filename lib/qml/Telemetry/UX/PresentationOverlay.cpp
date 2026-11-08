// Shared Qt presentation-overlay parser and plain-text formatting. MIT.
#include "PresentationOverlay.hpp"
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <cmath>
#include <limits>

namespace telemetry::ux {
namespace {
QString key(int category, quint32 id, const QString& role, const QVariantList& path)
{
	QJsonArray parts;
	for (const auto& part : path) {
		if (part.metaType().id() == QMetaType::QString)
			parts.append(part.toString());
		else {
			const auto type = part.metaType().id();
			if (type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong &&
			    type != QMetaType::ULongLong)
				return {};
			bool ok = false;
			const auto number = part.toULongLong(&ok);
			if (!ok || number > std::numeric_limits<quint32>::max())
				return {};
			parts.append(static_cast<double>(number));
		}
	}
	return QString::number(category) + QLatin1Char(':') + QString::number(id) + QLatin1Char(':') +
	       role + QLatin1Char(':') +
	       QString::fromUtf8(QJsonDocument(parts).toJson(QJsonDocument::Compact));
}

bool uint32(const QJsonValue& value)
{
	if (!value.isDouble())
		return false;
	const auto number = value.toDouble();
	return std::isfinite(number) && number >= 0 && number <= std::numeric_limits<quint32>::max() &&
	       std::floor(number) == number;
}

bool allowedKeys(const QJsonObject& object, const QSet<QString>& allowed)
{
	for (auto it = object.begin(); it != object.end(); ++it)
		if (!allowed.contains(it.key()))
			return false;
	return true;
}
} // namespace

struct PresentationOverlay::Data {
	QString fingerprint;
	QHash<QString, PresentationHints> entries;
};

bool PresentationOverlay::replace(const QByteArray& json, QString& error)
{
	error.clear();
	if (json.isEmpty()) {
		data_.reset();
		return true;
	}
	const auto fail = [&](const QString& message) {
		error = message;
		return false;
	};
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(json, &parse);
	if (parse.error != QJsonParseError::NoError || !document.isObject())
		return fail(QStringLiteral("Presentation must be a JSON object"));
	const auto object = document.object();
	if (!allowedKeys(object, {QStringLiteral("format"), QStringLiteral("version"),
	                          QStringLiteral("fingerprint"), QStringLiteral("entries")}) ||
	    object.value(QStringLiteral("format")) != QStringLiteral("telemetry-presentation") ||
	    object.value(QStringLiteral("version")) != 1 ||
	    !object.value(QStringLiteral("fingerprint")).isString() ||
	    !QRegularExpression(QStringLiteral("\\A0x[0-9a-f]{16}\\z"))
	         .match(object.value(QStringLiteral("fingerprint")).toString())
	         .hasMatch() ||
	    !object.value(QStringLiteral("entries")).isArray())
		return fail(QStringLiteral("Invalid presentation format, version or fingerprint"));
	const auto entries = object.value(QStringLiteral("entries")).toArray();
	if (entries.size() > 4096)
		return fail(QStringLiteral("Presentation exceeds 4096 entries"));
	auto next = std::make_shared<Data>();
	next->fingerprint = object.value(QStringLiteral("fingerprint")).toString();
	const QSet<QString> keys{QStringLiteral("category"), QStringLiteral("id"),
	                         QStringLiteral("role"),     QStringLiteral("path"),
	                         QStringLiteral("label"),    QStringLiteral("unit"),
	                         QStringLiteral("help"),     QStringLiteral("minimum"),
	                         QStringLiteral("maximum"),  QStringLiteral("step")};
	const QRegularExpression decimal(QStringLiteral("\\A-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?\\z"));
	for (const auto& value : entries) {
		if (!value.isObject())
			return fail(QStringLiteral("Presentation entry must be an object"));
		const auto entry = value.toObject();
		const auto category = entry.value(QStringLiteral("category"));
		const auto id = entry.value(QStringLiteral("id"));
		const auto role = entry.value(QStringLiteral("role")).toString();
		if (!allowedKeys(entry, keys) || !uint32(category) || category.toInt() < 1 ||
		    category.toInt() > 3 || !uint32(id) ||
		    !entry.value(QStringLiteral("role")).isString() ||
		    (category.toInt() == 1 ? role != QStringLiteral("value")
		     : category.toInt() == 2
		         ? role != QStringLiteral("request")
		         : role != QStringLiteral("request") && role != QStringLiteral("response")) ||
		    !entry.value(QStringLiteral("path")).isArray())
			return fail(QStringLiteral("Invalid presentation selector"));
		const auto pathArray = entry.value(QStringLiteral("path")).toArray();
		if (pathArray.size() > 64)
			return fail(QStringLiteral("Presentation path exceeds 64 steps"));
		QVariantList path;
		for (const auto& part : pathArray) {
			if (part.isString())
				path.append(part.toString());
			else if (uint32(part))
				path.append(QVariant::fromValue(static_cast<quint32>(part.toDouble())));
			else
				return fail(QStringLiteral("Presentation paths contain names or uint32 indices"));
		}
		PresentationHints hints;
		bool any = false;
		const auto string = [&](const QString& name, int maximum, QString& output,
		                        bool numeric = false) {
			if (!entry.contains(name))
				return true;
			any = true;
			const auto item = entry.value(name);
			if (!item.isString() || item.toString().size() > maximum)
				return false;
			output = item.toString();
			return !numeric || decimal.match(output).hasMatch();
		};
		if (!string(QStringLiteral("label"), 256, hints.label) ||
		    !string(QStringLiteral("unit"), 64, hints.unit) ||
		    !string(QStringLiteral("help"), 4096, hints.help) ||
		    !string(QStringLiteral("minimum"), 128, hints.minimum, true) ||
		    !string(QStringLiteral("maximum"), 128, hints.maximum, true) ||
		    !string(QStringLiteral("step"), 128, hints.step, true) || !any)
			return fail(QStringLiteral("Invalid presentation text or decimal hint"));
		hints.hasLabel = entry.contains(QStringLiteral("label"));
		if (!hints.step.isEmpty() &&
		    (hints.step.startsWith(QLatin1Char('-')) ||
		     !hints.step.contains(QRegularExpression(QStringLiteral("[1-9]")))))
			return fail(QStringLiteral("Presentation step must be positive"));
		const auto selector =
		    key(category.toInt(), static_cast<quint32>(id.toDouble()), role, path);
		if (next->entries.contains(selector))
			return fail(QStringLiteral("Duplicate presentation selector"));
		next->entries.insert(selector, std::move(hints));
	}
	data_ = std::move(next);
	return true;
}

PresentationHints PresentationOverlay::hints(const QString& fingerprint, int category, quint32 id,
                                             const QString& role, const QVariantList& path) const
{
	return data_ && !fingerprint.isEmpty() && data_->fingerprint == fingerprint
	           ? data_->entries.value(key(category, id, role, path))
	           : PresentationHints{};
}

QString PresentationOverlay::fingerprint() const
{
	return data_ ? data_->fingerprint : QString{};
}

QString PresentationHints::displayName(const QString& original) const
{
	return hasLabel && !label.isEmpty() ? label : original;
}

QString PresentationHints::displayDetail(const QString& original, const QString& detail) const
{
	QStringList parts{detail};
	if (displayName(original) != original)
		parts.append(QStringLiteral("Name: ") + original);
	if (!unit.isEmpty())
		parts.append(QStringLiteral("Unit: ") + unit);
	if (!help.isEmpty())
		parts.append(help);
	QStringList advisory;
	if (!minimum.isEmpty())
		advisory.append(QStringLiteral("minimum ") + minimum);
	if (!maximum.isEmpty())
		advisory.append(QStringLiteral("maximum ") + maximum);
	if (!step.isEmpty())
		advisory.append(QStringLiteral("step ") + step);
	if (!advisory.isEmpty())
		parts.append(QStringLiteral("Advisory only: ") + advisory.join(QStringLiteral(", ")) +
		             QStringLiteral(". Device validation still applies."));
	return parts.join(QStringLiteral(" / "));
}

QString PresentationHints::displayValue(const QString& value) const
{
	return unit.isEmpty() || value.isEmpty() ? value : value + QLatin1Char(' ') + unit;
}
} // namespace telemetry::ux
