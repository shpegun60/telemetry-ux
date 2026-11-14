/*
 * @file SchemaText.hpp
 * @brief Types and values of a parsed descriptor in words, as the device-file views show them.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * Shared by the Descriptor and Values views and the Device tree so a type
 * reads the same in each ("f32[3]", "enum Idle, Run", "struct (4)") and a value is
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
// The type in words, short enough for one column: "f32", "struct (3)",
// "u8[16]", "enum Idle, Run".
QString typeText(const runtime::RuntimeSchema& schema, TypeId id);
// A Command's arguments as a signature: "()", "(bool)", "(f32, f32)".
QString argumentsText(const runtime::RuntimeSchema& schema, TypeId id);
// A Command's (returns false) or a Service's whole signature:
// "(f32, f32) -> f32".
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
