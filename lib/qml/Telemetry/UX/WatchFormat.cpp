/*
 * @file WatchFormat.cpp
 * @brief How a Watchlist value is shown: Original, Dec or Hex, from its exact bits.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "WatchFormat.hpp"
#include "ScalarEditorMetadata.hpp"
#include "ScalarEditor.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QStringList>

namespace telemetry::ux::watch_format {
namespace {

std::optional<ScalarCode> codeOf(WatchScalar scalar)
{
	switch (scalar) {
		case WatchScalar::Unknown: return std::nullopt;
		case WatchScalar::Boolean: return ScalarCode::Bool;
		case WatchScalar::U8: return ScalarCode::U8;
		case WatchScalar::S8: return ScalarCode::S8;
		case WatchScalar::U16: return ScalarCode::U16;
		case WatchScalar::S16: return ScalarCode::S16;
		case WatchScalar::U32: return ScalarCode::U32;
		case WatchScalar::S32: return ScalarCode::S32;
		case WatchScalar::U64: return ScalarCode::U64;
		case WatchScalar::S64: return ScalarCode::S64;
		case WatchScalar::F32: return ScalarCode::F32;
		case WatchScalar::F64: return ScalarCode::F64;
	}
	return std::nullopt;
}

QString typeName(const WatchValueType& type)
{
	static const char* const names[] = {"?", "bool", "u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64", "f32", "f64"};
	const auto at = static_cast<unsigned>(type.scalar);
	return QString::fromLatin1(at < std::size(names) ? names[at] : "?");
}

QString tr(const char* text)
{
	return QCoreApplication::translate("WatchFormat", text);
}

} // namespace

std::optional<WatchNumberFormat> parse(QStringView name)
{
	if (name == u"original")
		return WatchNumberFormat::Original;
	if (name == u"dec")
		return WatchNumberFormat::Dec;
	if (name == u"hex")
		return WatchNumberFormat::Hex;
	return std::nullopt;
}

QString name(WatchNumberFormat format)
{
	switch (format) {
		case WatchNumberFormat::Original: return QStringLiteral("original");
		case WatchNumberFormat::Dec: return QStringLiteral("dec");
		case WatchNumberFormat::Hex: return QStringLiteral("hex");
	}
	return QStringLiteral("original");
}

QString text(const WatchValueBits& value, WatchNumberFormat format, const QHash<quint64, QString>* names)
{
	if (!value.type.valid() || value.size != value.type.width)
		return {};
	const auto code = codeOf(value.type.scalar);
	if (!code)
		return {};
	std::uint64_t bits = 0;
	for (unsigned i = 0; i < value.size; ++i)
		bits |= std::uint64_t(std::to_integer<unsigned char>(value.bytes[i])) << (8 * i);
	switch (format) {
		case WatchNumberFormat::Hex:
			return QStringLiteral("0x") +
			       QString::number(bits, 16).toUpper().rightJustified(2 * value.type.width, QLatin1Char('0'));
		case WatchNumberFormat::Dec:
			if (*code == ScalarCode::Bool)
				return bits == 1 ? QStringLiteral("1") : bits == 0 ? QStringLiteral("0") : QString{};
			return detail::scalarEditorBits(bits, *code);
		case WatchNumberFormat::Original:
			if (value.type.enumeration) {
				// Without the type's names, nothing here to say: the row's own text.
				if (!names)
					return {};
				const auto digits = detail::scalarEditorBits(bits, *code);
				const auto found = names ? names->constFind(bits) : QHash<quint64, QString>::const_iterator{};
				if (names && found != names->cend())
					return found.value();
				return digits.isEmpty() ? QString{} : digits + QStringLiteral(" (unknown)");
			}
			return detail::scalarEditorBits(bits, *code);
	}
	return {};
}

bool isHex(QStringView text)
{
	const auto trimmed = text.trimmed();
	return trimmed.size() >= 2 && trimmed.at(0) == u'0' && (trimmed.at(1) == u'x' || trimmed.at(1) == u'X');
}

Parsed fromHex(QStringView text, const WatchValueType& type)
{
	Parsed parsed;
	const auto code = codeOf(type.scalar);
	if (!type.valid() || !code) {
		parsed.error = tr("This value's type is not known yet; read it once, then write it in hex.");
		return parsed;
	}
	const auto trimmed = text.trimmed();
	if (!isHex(trimmed)) {
		parsed.error = tr("Write hex digits after 0x, such as 0x1F.");
		return parsed;
	}
	QString digits;
	for (const auto c : trimmed.mid(2)) {
		if (c == u'_')
			continue;
		const bool hex = (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
		if (!hex) {
			parsed.error = tr("%1 is not a hex digit.").arg(QString(c));
			return parsed;
		}
		digits.append(c);
	}
	while (digits.size() > 1 && digits.startsWith(QLatin1Char('0')))
		digits.remove(0, 1);
	const auto width = 2 * qsizetype(type.width);
	if (digits.isEmpty()) {
		parsed.error = tr("Write hex digits after 0x, such as 0x1F.");
		return parsed;
	}
	if (digits.size() > width) {
		parsed.error = tr("%1 has more digits than a %2 holds (%3).").arg(trimmed.toString(), typeName(type)).arg(width);
		return parsed;
	}
	bool ok = false;
	const auto bits = digits.toULongLong(&ok, 16);
	if (!ok) {
		parsed.error = tr("Write hex digits after 0x, such as 0x1F.");
		return parsed;
	}
	if (*code == ScalarCode::Bool && bits > 1) {
		parsed.error = tr("A bool is 0x00 or 0x01.");
		return parsed;
	}
	parsed.canonical = detail::scalarEditorBits(bits, *code);
	if (parsed.canonical.isEmpty()) {
		parsed.error = tr("%1 is not a value a %2 holds.").arg(trimmed.toString(), typeName(type));
		return parsed;
	}
	// A write takes text: a float must come back to the same bits through it.
	// A NaN's payload does not, so it is refused rather than changed.
	const auto exact = [&]<class F, class B>() {
		F number{};
		return detail::parseScalar(parsed.canonical, number) && std::bit_cast<B>(number) == static_cast<B>(bits);
	};
	const bool kept = *code == ScalarCode::F32   ? exact.template operator()<float, std::uint32_t>()
	                  : *code == ScalarCode::F64 ? exact.template operator()<double, std::uint64_t>()
	                                             : true;
	if (!kept) {
		parsed.error = tr("%1 is a NaN a write can't keep exactly.").arg(trimmed.toString());
		parsed.canonical.clear();
	}
	return parsed;
}

} // namespace telemetry::ux::watch_format
