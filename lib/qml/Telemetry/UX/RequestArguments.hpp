// A Command's or Service's input: its arguments and its signature. MIT.
#pragma once
#include "WatchFrame.hpp"
#include <telemetry/runtime/RuntimeSchema.hpp>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <vector>

// A Command's or a Service's request type as its arguments, for the words the
// Batch's "+" row shows beside it - "(value: f32, factor: f32)" - and for the
// scalar or enum type a value takes. Pure: a schema and a request type in.
// BA-4b removed the typed values (parse, check, Given, Item, Result): values
// are set on the Batch item, where they start at their initial values.
namespace telemetry::ux::request_arguments {
// One input: a top-level member of the request type, in declared order. A
// request type that is itself a scalar or an enum is one argument whose name
// is empty.
struct Argument {
	QString name;
	QString typeText; // "f32", "bool", "enum", "struct (3)", "u16[300]"
	TypeId type = 0;
	quint32 offset = 0, bytes = 0;
	bool scalar = false;   // a scalar or an enum; otherwise a struct or an array
	WatchValueType value;  // for a scalar: how its text becomes bits
	QStringList choices;   // a bool's true and false, or an enum's names
	QList<qint64> codes;   // an enum's codes, beside its names
};
// The scalar or enum type a value of type id is, for typed text; invalid
// for anything else.
[[nodiscard]] WatchValueType valueType(const runtime::RuntimeSchema& schema, TypeId id);
[[nodiscard]] std::vector<Argument> arguments(const runtime::RuntimeSchema& schema, TypeId request);
// "(value: f32, factor: f32)", the names with their types, at most four.
[[nodiscard]] QString signatureText(const runtime::RuntimeSchema& schema, TypeId request);
} // namespace telemetry::ux::request_arguments
