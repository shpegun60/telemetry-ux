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
		parsed.error = isFloat(*code) || *code == ScalarCode::Bool
		    ? tr("%1 has more digits than a %2 holds (%3).").arg(trimmed.toString(), typeName(type)).arg(width)
		    : tr("%1 is past a %2: enter a number %3.").arg(trimmed.toString(), typeName(type), range(type));
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

QString range(const WatchValueType& type)
{
	const auto code = codeOf(type.scalar);
	if (!type.valid() || !code || isFloat(*code) || *code == ScalarCode::Bool || type.width == 0 || type.width > 8)
		return {};
	const unsigned bitsWide = 8 * type.width;
	const std::uint64_t all = bitsWide == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bitsWide) - 1;
	const bool isSigned = *code == ScalarCode::S8 || *code == ScalarCode::S16 || *code == ScalarCode::S32 ||
	                      *code == ScalarCode::S64;
	const std::uint64_t low = isSigned ? std::uint64_t{1} << (bitsWide - 1) : 0;
	const std::uint64_t high = isSigned ? low - 1 : all;
	return tr("from %1 to %2 (%3 to %4)")
	    .arg(detail::scalarEditorBits(low, *code), detail::scalarEditorBits(high, *code), hexAt(low, type.width),
	         hexAt(high, type.width));
}

Encoded encode(QStringView text, const WatchValueType& type)
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
		const auto parsed = fromHex(trimmed, type);
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
		// A whole number past the type says its range; anything else is not one.
		bool whole = !typed.isEmpty();
		for (qsizetype at = 0; at < typed.size() && whole; ++at)
			whole = typed.at(at).isDigit() || (at == 0 && (typed.at(0) == u'-' || typed.at(0) == u'+'));
		out.error = *code == ScalarCode::Bool ? tr("Enter true or false (1 or 0).")
		            : whole && !isFloat(*code)
		                ? tr("%1 is past a %2: enter a number %3.").arg(trimmed, typeName(type), range(type))
		                : tr("%1 is not a number a %2 holds.").arg(trimmed, typeName(type));
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
