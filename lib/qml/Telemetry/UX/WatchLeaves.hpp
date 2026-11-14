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
		// The type each step leads to; the last is the leaf's own.
		std::array<TypeId, 32> types{};
		quint8 depth = 0;
		// The suffix as a saved member: "Gain", "[3]", "Phase[2].Offset".
		[[nodiscard]] QString member() const;
	};
	// nullopt: no schema, an unknown root or an ordinal past the last leaf.
	[[nodiscard]] std::optional<Leaf> locate(TypeId root, quint32 ordinal) const;
	// nullopt unless the text names a leaf under root.
	[[nodiscard]] std::optional<quint32> ordinal(TypeId root, QStringView member) const;
	// The values a member text names under root - a leaf, or every leaf of a
	// struct or array inside it ("Config", "Samples[3]", "Phase[2]") - as
	// ordinals [first, first + count); an empty member names all of root.
	// Otherwise error says why in words, naming the path so far after
	// `path` (the Field's: "device.Config has no member Gainn. Did you mean
	// Gain?", "data.Samples has 64 elements, [0] to [63].").
	struct Range {
		quint32 first = 0;
		quint32 count = 0;
		QString error;
	};
	[[nodiscard]] Range range(TypeId root, QStringView member, const QString& path) const;

	// The steps one level below member inside root ("" for root itself), for
	// completing a typed path: a struct's members in declaration order, or one
	// entry for an array's elements ("[0 … 63]", appended as "["); empty for a
	// leaf or a path that names nothing.
	struct Child {
		QString label;
		QString append;
		TypeId type = 0;
		quint32 leaves = 0;
		bool container = false;
	};
	[[nodiscard]] QVector<Child> children(TypeId root, QStringView member) const;
	// The type's words, as the tree shows them: "f32", "struct (3)", "u16[64]".
	[[nodiscard]] QString typeText(TypeId type) const;

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
