/*
 * @file SchemaText.hpp
 * @brief Types and values of a parsed descriptor in words, as the device-file views show them.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * Shared by the Descriptor and Values views and the Device tree so a type
 * reads the same in each ("f32[3]", "enum (2)", "struct (4)") and a value is
 * written one way everywhere: integers in decimal, floats as the shortest
 * text that reads back to the same number, enums by name. Internal to the
 * module; not installed.
 */
#ifndef TELEMETRY_UX_SCHEMA_TEXT_HPP
#define TELEMETRY_UX_SCHEMA_TEXT_HPP
#pragma once

#include <telemetry/runtime/DynamicValueView.hpp>
#include <telemetry/runtime/RuntimeSchema.hpp>
#include <QtCore/QString>
#include <cstddef>
#include <string_view>

namespace telemetry::ux::schema_text {
QString text(std::string_view view);
// The type in words, short enough for one column: "f32", "u8[16]"; a named
// enum or struct by its name (descriptor v3.1), "adc::Config",
// "adc::Config[4]"; an unnamed one by its kind and size, "struct (3)",
// "enum (2)" (the designer's review of 20 November 2026, 14.2).
QString typeText(const runtime::RuntimeSchema& schema, TypeId id);
// A named enum's or struct's name, "adc::Config"; empty for any other type.
QString typeName(const runtime::RuntimeSchema& schema, TypeId id);
// The type with its kind and size, for a tooltip, Details' line 2 and the
// Descriptor view: "struct adc::Config · 3 members", "enum adc::Mode · 3
// values"; an unnamed one, an array or a scalar as typeText: "struct (3)",
// "adc::Mode[4]", "f32".
QString typeWords(const runtime::RuntimeSchema& schema, TypeId id);
// The same for a screen reader, the own name first: "struct Config in adc,
// 3 members", "array of 4 Mode in adc", "f32"; an unnamed one "struct
// with 3 members", as the tree said it before names.
QString typeSpoken(const runtime::RuntimeSchema& schema, TypeId id);
// Every scope of a type text cut to "…::" (14.2's short form): "adc::Config"
// -> "…::Config", "(adc::Reference) -> adc::Offset" -> "(…::Reference) ->
// …::Offset"; a global name and a text without one stay as they are.
QString shortScopes(const QString& text);
// A catalog's group as a key and as a path writes it (PathGrammar.hpp): a
// group tree's group by its groups' names joined by "/" ("demo/adc") -
// tree names hold no path characters, so the two are equal; a flat
// (v3.0) catalog by its name, written escaped as one segment
// ("power.meter", "power\.meter"). Empty for an index out of range.
struct GroupPath {
	QString key, written;
};
GroupPath groupPath(const runtime::RuntimeSchema& schema, descriptor::Category category,
                    std::uint32_t catalogIndex);
// An enum's names, for a tooltip: "Idle, Run"; the first 16, then "…".
// Empty for any other type.
QString enumNames(const runtime::RuntimeSchema& schema, TypeId id);
// A Command's arguments as a signature: "()", "(bool)", "(f32, f32)"; a
// named request struct by its name, "(adc::Reference)".
QString argumentsText(const runtime::RuntimeSchema& schema, TypeId id);
// A Command's (returns false) or a Service's whole signature:
// "(f32, f32) -> f32"; named structs by their names,
// "(adc::Reference) -> adc::Offset".
QString signatureText(const runtime::RuntimeSchema& schema, TypeId request, TypeId response, bool returns);

// How a value is written into JSON.
enum class Json { Text, Number, Boolean };

struct Value {
	QString text;
	Json json = Json::Text;
	// What JSON writes when it differs from text: an enum's code.
	QString jsonText;
};

// A scalar or an enum, read through the view; "?" for anything else.
Value valueText(const runtime::RuntimeSchema& schema, const runtime::DynamicValueView& view);

// A struct or an array folded onto one line: a struct's first four members by
// name, "{Gain 1, Enabled true, …}", nested ones as "{…}" and "[…]"; an array of
// plain values, its first eight, "[1, 2, …]". Empty for anything else.
QString summaryText(const runtime::RuntimeSchema& schema, const runtime::DynamicValueView& view);

// A bound on the rows a copy or a find goes through: a large file stays quick.
// cut says the bound was reached and the rest was left out.
struct RowBudget {
	std::size_t left;
	bool cut = false;

	bool take() noexcept
	{
		if (left == 0) {
			cut = true;
			return false;
		}
		--left;
		return true;
	}
};
} // namespace telemetry::ux::schema_text
#endif
