/*
 * @file WatchFormat.hpp
 * @brief How a Watchlist value is shown: Original, Dec or Hex, from its exact bits.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The Watchlist keeps every reading as the value's little-endian bits, so a
 * form is chosen without a new read and never through a double (WL-9 B1):
 *  - Original, as the type means it: integers in decimal; a float in the
 *    shortest decimal that reads back to the same bits (f32 0.1); a bool or
 *    an enum its word with its code, as its choice list labels it:
 *    "false (0)", "Idle (2)", "Unknown (7)", a bool byte of 2 "true (2)".
 *  - Dec: as Original, except that a float shows every significant digit
 *    its bits hold, 9 for f32 and 17 for f64, trailing zeros dropped
 *    (0.100000001): the stored value, not the rounded one.
 *  - Hex: "0x", uppercase, zero-padded to the type's width, the number's bits
 *    most significant first (not the byte order in memory); f32 and f64 their
 *    raw bits, 0x3FC00000; a signed type its two's complement. A bool or an
 *    enum keeps its word, only its code turns: "Idle (0x02)", "Off (0xFF)".
 * No digit grouping and "." as the decimal separator in every form.
 * A write may be typed in hex whatever is shown: fromHex turns it into the
 * value's canonical text, the form the provider's write takes. Qt only.
 */
#ifndef TELEMETRY_UX_WATCH_FORMAT_HPP
#define TELEMETRY_UX_WATCH_FORMAT_HPP
#pragma once

#include "WatchFrame.hpp"
#include <QtCore/QHash>
#include <QtCore/QString>
#include <QtCore/QStringView>
#include <optional>

namespace telemetry::ux {

enum class WatchNumberFormat : quint8 { Original, Dec, Hex };

namespace watch_format {
// "original", "dec" or "hex"; nullopt for anything else.
[[nodiscard]] std::optional<WatchNumberFormat> parse(QStringView name);
[[nodiscard]] QString name(WatchNumberFormat format);

// A value as its two parts: a bool's or an enum's word ("Idle", "false",
// "Unknown") and its code in the form ("2", "0x02"); a number has its text as
// the code and no word. names maps an enum's codes (its bits at its width) to
// their names; without them an enum has no word, and Original nothing at all.
struct Parts {
	QString word;
	QString code;
};
[[nodiscard]] Parts parts(const WatchValueBits& value, WatchNumberFormat format,
                          const QHash<quint64, QString>* names = nullptr);
// The value in format, "Idle (2)" or "0.1"; empty without a value.
[[nodiscard]] QString text(const WatchValueBits& value, WatchNumberFormat format,
                           const QHash<quint64, QString>* names = nullptr);

// Text that starts with 0x or 0X.
[[nodiscard]] bool isHex(QStringView text);
// Typed hex for a value of type: its canonical text (a float's exact value,
// an integer or code in decimal, true/false), or why not in words. Leading
// zeros and "_" are allowed; a significant digit past the type's width is not.
struct Parsed {
	QString canonical;
	QString error;
};
// form is the row's: a number out of range says the range in it (WL-8).
[[nodiscard]] Parsed fromHex(QStringView text, const WatchValueType& type,
                             WatchNumberFormat form = WatchNumberFormat::Original);

// What a typed value would write - decimal, hex (0x...), true or false - as
// the value's bits at its type, or why not in words: a number out of range
// says the type's range in decimal and in hex, the row's form (form) first,
// without the typed text ("u32 takes 0 to 4294967295 (0x00000000 to
// 0xFFFFFFFF).", WL-9 C3): the text is in the editor or held in the cell.
// Empty text gives neither. An enum takes its code; its names are the editor's.
struct Encoded {
	WatchValueBits value;
	QString error;
};
[[nodiscard]] Encoded encode(QStringView text, const WatchValueType& type,
                             WatchNumberFormat form = WatchNumberFormat::Original);
// "0 to 65535 (0x0000 to 0xFFFF)" for an integer type, "0x0000 to 0xFFFF (0 to
// 65535)" when form is Hex; empty for others.
[[nodiscard]] QString range(const WatchValueType& type, WatchNumberFormat form = WatchNumberFormat::Original);
} // namespace watch_format

} // namespace telemetry::ux
#endif
