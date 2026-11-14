/*
 * @file WatchKey.hpp
 * @brief What a Watchlist row watches: a whole Field, or one value inside it.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The low word is the Field's endpoint id; the high word is 0 for the whole
 * Field, else 1 + the leaf's pre-order ordinal among the Field type's scalar
 * and enum values (members in declaration order, elements in index order,
 * depth first). A scalar Field is watched whole, so its key is its id, as
 * before keys; a struct or an array is never a row, its values are. A leaf
 * ordinal stays below 2^18, so a key stays below 2^51: exact as a QML number
 * and as an SQLite INTEGER. Keys hold under one descriptor fingerprint only;
 * what is saved is a path, never a key.
 */
#ifndef TELEMETRY_UX_WATCH_KEY_HPP
#define TELEMETRY_UX_WATCH_KEY_HPP
#pragma once

#include <QtCore/QtTypes>

namespace telemetry::ux {
using WatchKey = quint64;

namespace watch_key {
[[nodiscard]] constexpr WatchKey whole(quint32 field) noexcept
{
	return field;
}
// ordinal: the leaf's pre-order position among the Field type's values.
[[nodiscard]] constexpr WatchKey member(quint32 field, quint32 ordinal) noexcept
{
	return WatchKey{field} | (WatchKey{ordinal} + 1) << 32;
}
[[nodiscard]] constexpr quint32 field(WatchKey key) noexcept
{
	return static_cast<quint32>(key & 0xffffffffu);
}
// 0 for a whole Field, else 1 + the ordinal.
[[nodiscard]] constexpr quint32 leaf(WatchKey key) noexcept
{
	return static_cast<quint32>(key >> 32);
}
[[nodiscard]] constexpr bool isWhole(WatchKey key) noexcept
{
	return leaf(key) == 0;
}
// Only for a key that is not whole.
[[nodiscard]] constexpr quint32 ordinal(WatchKey key) noexcept
{
	return leaf(key) - 1;
}
} // namespace watch_key
} // namespace telemetry::ux
#endif
