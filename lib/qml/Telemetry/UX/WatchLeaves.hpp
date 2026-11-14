/*
 * @file WatchLeaves.hpp
 * @brief The values inside a Field's type, numbered as the Watchlist keys them.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * A leaf is a scalar or enum value inside a type; its ordinal is its pre-order
 * position: members in declaration order, elements in index order, depth
 * first. Every subtree is then one range [first, first + count), which the
 * tree's three-state boxes count. A leaf's member text is the grammar of
 * RuntimeSchema::resolve: "Gain", "[3]", "Phase[2].Offset". The schema is a
 * handle into its workspace: valid only while that admission is current.
 */
#ifndef TELEMETRY_UX_WATCH_LEAVES_HPP
#define TELEMETRY_UX_WATCH_LEAVES_HPP
#pragma once

#include <telemetry/runtime/RuntimeSchema.hpp>
#include <QtCore/QString>
#include <QtCore/QStringView>
#include <QtCore/QVector>
#include <array>
#include <cstdint>
#include <optional>

namespace telemetry::ux {

class WatchLeaves {
public:
	// One forward pass: a type refers only to lower ids.
	void reset(const runtime::RuntimeSchema& schema);
	void clear();
	// Whether reset() last saw this admission's schema.
	[[nodiscard]] bool matches(const runtime::RuntimeSchema& schema) const noexcept;
	// Scalar and Enum 1, Struct the sum of its members, Array n times its
	// element, Void and unknown ids 0.
	[[nodiscard]] quint32 count(TypeId type) const noexcept;

	struct Step {
		bool element = false;
		quint32 index = 0;  // a member's position or an element's index
	};
	struct Leaf {
		TypeId type = 0;
		quint32 offset = 0;  // bytes from the root value's start
		QString suffix;      // appended to the Field's path: ".Gain", "[3]"
		std::array<Step, 32> steps{};
		quint8 depth = 0;
		// The suffix as a saved member: "Gain", "[3]", "Phase[2].Offset".
		[[nodiscard]] QString member() const;
	};
	// nullopt: no schema, an unknown root or an ordinal past the last leaf.
	[[nodiscard]] std::optional<Leaf> locate(TypeId root, quint32 ordinal) const;
	// nullopt unless the text names a leaf under root.
	[[nodiscard]] std::optional<quint32> ordinal(TypeId root, QStringView member) const;

	// What a Field's values are, for the words a box speaks: a struct of
	// scalars has members, an array of scalars elements, anything deeper values.
	enum class Kind { Members, Elements, Values };
	[[nodiscard]] Kind kind(TypeId root) const noexcept;

private:
	runtime::RuntimeSchema schema_;
	QVector<quint32> counts_;
};

} // namespace telemetry::ux
#endif
