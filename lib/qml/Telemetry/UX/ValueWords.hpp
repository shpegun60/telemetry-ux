// What a value editor says when its text is not a value of its type. MIT.
#pragma once
#include <QtCore/QCoreApplication>
#include <QtCore/QLocale>
#include <QtCore/QStringList>
#include <cstdint>
#include <limits>

// Details' and the Batch's value cells say it under the cell while the text
// typed cannot be parsed, and only then (family spec C: "validation errors
// only for parse failures"). One sentence per type, naming its range, so the
// same words come from the Dynamic, Static and Inspector models.
namespace telemetry::ux::value_words {
// Scalar codes as the descriptor numbers them, and WatchScalar with them:
// 1 bool, 2 u8, 3 s8, 4 u16, 5 s16, 6 u32, 7 s32, 8 u64, 9 s64, 10 f32, 11 f64.
inline QString words(const char* text)
{
	return QCoreApplication::translate("ValueWords", text);
}

// "a, b or c".
inline QString either(const QStringList& names)
{
	if (names.size() < 2)
		return names.join(QString{});
	return words("%1 or %2").arg(names.mid(0, names.size() - 1).join(QStringLiteral(", ")), names.last());
}

// "Enter a number from 0 to 255.", "Enter a number.", "Enter true or false.",
// "Enter Idle, Run or Stop, or a code." An enum's names are at most four, then
// "…". Empty for a code that is no scalar.
inline QString takes(int code, bool enumeration = false, const QStringList& names = {})
{
	if (enumeration) {
		if (names.isEmpty())
			return words("Enter a code.");
		if (names.size() > 4)
			return words("Enter %1, … or a code.").arg(names.mid(0, 4).join(QStringLiteral(", ")));
		return words("Enter %1, or a code.").arg(either(names));
	}
	const QLocale locale;
	const auto range = [&](qlonglong low, qlonglong high) {
		return words("Enter a number from %1 to %2.").arg(locale.toString(low), locale.toString(high));
	};
	switch (code) {
		case 1:
			return words("Enter true or false.");
		case 2:
			return range(0, std::numeric_limits<std::uint8_t>::max());
		case 3:
			return range(std::numeric_limits<std::int8_t>::min(), std::numeric_limits<std::int8_t>::max());
		case 4:
			return range(0, std::numeric_limits<std::uint16_t>::max());
		case 5:
			return range(std::numeric_limits<std::int16_t>::min(), std::numeric_limits<std::int16_t>::max());
		case 6:
			return range(0, std::numeric_limits<std::uint32_t>::max());
		case 7:
			return range(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
		case 8:
			return words("Enter a number from %1 to %2.")
			    .arg(locale.toString(0), locale.toString(qulonglong(std::numeric_limits<std::uint64_t>::max())));
		case 9:
			return range(std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max());
		case 10:
		case 11:
			return words("Enter a number.");
		default:
			return {};
	}
}
} // namespace telemetry::ux::value_words
