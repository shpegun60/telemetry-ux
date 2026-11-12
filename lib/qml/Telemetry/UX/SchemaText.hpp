/*
 * @file SchemaText.hpp
 * @brief Types and values of a parsed descriptor in words, as the device-file views show them.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * Shared by the Descriptor and Values views so a type reads the same in both
 * ("f32[3]", "enum Idle, Run, Fault", "struct, 4 members") and a value is
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
#include <string_view>

namespace telemetry::ux::schema_text {
QString text(std::string_view view);
// The type in words, short enough for one column.
QString typeText(const runtime::RuntimeSchema& schema, TypeId id);

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
} // namespace telemetry::ux::schema_text
#endif
