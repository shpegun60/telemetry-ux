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
#include <array>
#include <bit>
#include <cstdint>

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

QString hexAt(std::uint64_t bits, unsigned width)
{
	return QStringLiteral("0x") + QString::number(bits, 16).toUpper().rightJustified(2 * int(width), QLatin1Char('0'));
}

bool isFloat(ScalarCode code)
{
	return code == ScalarCode::F32 || code == ScalarCode::F64;
}

struct Bounds {
	std::uint64_t low = 0, high = 0, all = 0;
	bool isSigned = false;
};

std::optional<Bounds> boundsOf(const WatchValueType& type, ScalarCode code)
{
	if (isFloat(code) || code == ScalarCode::Bool || type.width == 0 || type.width > 8)
		return std::nullopt;
	Bounds out;
	const unsigned bitsWide = 8 * type.width;
	out.all = bitsWide == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bitsWide) - 1;
	out.isSigned = code == ScalarCode::S8 || code == ScalarCode::S16 || code == ScalarCode::S32 || code == ScalarCode::S64;
	out.low = out.isSigned ? std::uint64_t{1} << (bitsWide - 1) : 0;
	out.high = out.isSigned ? out.low - 1 : out.all;
	return out;
}

// "“65536” is out of range for u16. Enter a number from 0 to 65535 (0x0000 to
// 0xFFFF)." - in the row's form: a row shown in hex says the hex range first.
// Whole sentences, so a translation can order them as it needs.
QString outOfRangeText(const QString& typed, const WatchValueType& type, ScalarCode code, WatchNumberFormat form)
{
	const auto bounds = boundsOf(type, code);
	if (!bounds)
		return tr("“%1” is out of range for %2.").arg(typed, typeName(type));
	return tr("“%1” is out of range for %2. Enter a number from %3.").arg(typed, typeName(type), range(type, form));
}

bool asciiDigits(QStringView text)
{
	if (text.isEmpty())
		return false;
	for (const auto c : text)
		if (c < u'0' || c > u'9')
			return false;
	return true;
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
				return digits.isEmpty() ? QString{} : tr("Unknown (%1)").arg(digits);
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

Parsed fromHex(QStringView text, const WatchValueType& type, WatchNumberFormat form)
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
		parsed.error = isFloat(*code) || *code == ScalarCode::Bool
		    ? tr("“%1”: %2 takes at most %3 hex digits.").arg(trimmed.toString(), typeName(type)).arg(width)
		    : outOfRangeText(trimmed.toString(), type, *code, form);
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


QString range(const WatchValueType& type, WatchNumberFormat form)
{
	const auto code = codeOf(type.scalar);
	const auto bounds = type.valid() && code ? boundsOf(type, *code) : std::nullopt;
	if (!bounds)
		return {};
	const auto low = detail::scalarEditorBits(bounds->low, *code);
	const auto high = detail::scalarEditorBits(bounds->high, *code);
	const auto hexLow = hexAt(0, type.width), hexHigh = hexAt(bounds->all, type.width);
	if (form == WatchNumberFormat::Hex)
		return bounds->isSigned ? tr("%1 to %2 as bits, or %3 to %4").arg(hexLow, hexHigh, low, high)
		                        : tr("%1 to %2 (%3 to %4)").arg(hexLow, hexHigh, low, high);
	return bounds->isSigned ? tr("%1 to %2, or %3 to %4 as bits").arg(low, high, hexLow, hexHigh)
	                        : tr("%1 to %2 (%3 to %4)").arg(low, high, hexLow, hexHigh);
}

Encoded encode(QStringView text, const WatchValueType& type, WatchNumberFormat form)
{
	Encoded out;
	const auto code = codeOf(type.scalar);
	const auto trimmed = text.trimmed().toString();
	if (trimmed.isEmpty())
		return out;
	if (!type.valid() || !code) {
		out.error = tr("This value's type is not known yet; read it once first.");
		return out;
	}
	auto typed = trimmed;
	if (isHex(trimmed)) {
		const auto parsed = fromHex(trimmed, type, form);
		if (!parsed.error.isEmpty()) {
			out.error = parsed.error;
			return out;
		}
		typed = parsed.canonical;
	}
	std::uint64_t bits = 0;
	const auto as = [&]<class T, class B>() {
		T number{};
		if (!detail::parseScalar(typed, number))
			return false;
		bits = static_cast<std::uint64_t>(std::bit_cast<B>(number));
		return true;
	};
	bool ok = false;
	switch (*code) {
		case ScalarCode::Bool: {
			bool value = false;
			ok = detail::parseScalar(typed, value);
			bits = value ? 1 : 0;
			break;
		}
		case ScalarCode::U8: ok = as.template operator()<std::uint8_t, std::uint8_t>(); break;
		case ScalarCode::S8: ok = as.template operator()<std::int8_t, std::uint8_t>(); break;
		case ScalarCode::U16: ok = as.template operator()<std::uint16_t, std::uint16_t>(); break;
		case ScalarCode::S16: ok = as.template operator()<std::int16_t, std::uint16_t>(); break;
		case ScalarCode::U32: ok = as.template operator()<std::uint32_t, std::uint32_t>(); break;
		case ScalarCode::S32: ok = as.template operator()<std::int32_t, std::uint32_t>(); break;
		case ScalarCode::U64: ok = as.template operator()<std::uint64_t, std::uint64_t>(); break;
		case ScalarCode::S64: ok = as.template operator()<std::int64_t, std::uint64_t>(); break;
		case ScalarCode::F32: ok = as.template operator()<float, std::uint32_t>(); break;
		case ScalarCode::F64: ok = as.template operator()<double, std::uint64_t>(); break;
		default: break;
	}
	if (!ok) {
		// Why, in the words that help: a sign written that a value can't take,
		// a fraction where only whole numbers go, a number past the type.
		const bool negative = typed.startsWith(u'-');
		const auto digits = QStringView(typed).mid(negative || typed.startsWith(u'+') ? 1 : 0);
		double asDouble = 0;
		if (*code == ScalarCode::Bool)
			out.error = tr("Enter true or false (1 or 0).");
		else if (typed.startsWith(u'+'))
			out.error = tr("“%1”: write it without the + sign.").arg(trimmed);
		else if (isFloat(*code))
			out.error = detail::parseScalar(typed, asDouble) ? tr("“%1” is out of range for %2.").arg(trimmed, typeName(type))
			                                                 : tr("“%1” is not a number for %2.").arg(trimmed, typeName(type));
		else if (asciiDigits(digits))
			out.error = outOfRangeText(trimmed, type, *code, form);
		else if (detail::parseScalar(typed, asDouble))
			out.error = tr("“%1”: %2 takes whole numbers only.").arg(trimmed, typeName(type));
		else
			out.error = tr("“%1” is not a number for %2.").arg(trimmed, typeName(type));
		return out;
	}
	std::array<std::byte, 8> bytes{};
	for (unsigned i = 0; i < type.width && i < bytes.size(); ++i)
		bytes[i] = std::byte((bits >> (8 * i)) & 0xff);
	out.value.type = type;
	if (!out.value.assign(bytes.data(), type.width))
		out.error = tr("This value's type is not known yet; read it once first.");
	return out;
}

} // namespace telemetry::ux::watch_format
