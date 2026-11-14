/*
 * @file WatchFormat.hpp
 * @brief How a Watchlist value is shown: Original, Dec or Hex, from its exact bits.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The Watchlist keeps every reading as the value's little-endian bits, so a
 * form is chosen without a new read and never through a double:
 *  - Original, as the type means it: an enum by its name, a bool true/false,
 *    numbers as the device sent them (f32 1.5, i16 -1).
 *  - Dec: integers and enum codes in decimal, signed at their width; a bool
 *    1/0; floats as in Original.
 *  - Hex: "0x", uppercase, zero-padded to the type's width, the number's bits
 *    most significant first (not the byte order in memory); f32 and f64 their
 *    raw bits, 0x3FC00000; a bool 0x01; an enum its code.
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

// The value in format; empty without a value. names maps an enum's codes (its
// bits at its width) to their names, for Original.
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
[[nodiscard]] Parsed fromHex(QStringView text, const WatchValueType& type);
} // namespace watch_format

} // namespace telemetry::ux
#endif
