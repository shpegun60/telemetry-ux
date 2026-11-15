// A Command's or Service's input typed as name=value, checked and encoded. MIT.
#pragma once
#include "WatchFrame.hpp"
#include <telemetry/runtime/RuntimeSchema.hpp>
#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <vector>

// BA-2: a line of the Batch's "+" row names a Command or a Service and then its
// values - `control.Scale value=2 factor=3` - so it becomes a Batch item
// without Details. Pure: a schema, a request type and the text after the path
// in; each argument's state, the line's words and, when all is well, the
// request's bytes out. The words are the designer's (15 November 2026).
namespace telemetry::ux::request_arguments {
// One input: a top-level member of the request type, in declared order. A
// request type that is itself a scalar or an enum is one argument whose name
// is empty.
struct Argument {
	QString name;
	QString typeText; // "f32", "bool", "enum", "struct (3)", "u16[300]"
	TypeId type = 0;
	quint32 offset = 0, bytes = 0;
	bool scalar = false;   // typed in the row; otherwise filled in Details
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

// A value typed after the path: name=value, or a value with no name.
struct Given {
	QString name, text;
};
[[nodiscard]] std::vector<Given> parse(QStringView tail);

struct Item {
	enum class Status : quint8 { Missing, Ok, Error, Aggregate };
	Status status = Status::Missing;
	// canonical is said and shown (an enum by its name); editorText is what a
	// Details editor takes for the same bits (an enum by its code).
	QString name, typeText, text, canonical, editorText, error;
};
struct Result {
	std::vector<Item> items; // one an argument, declared order
	// A problem of the line as a whole: an argument it does not have, one
	// given twice, values with no names, input to a request that takes none.
	QString error;
	bool unnamed = false;    // values without names; named() writes them in
	QString named;           // the line with the names written in, when unnamed
	bool aggregate = false;  // an argument only Details fills
	QByteArray request;      // every argument given and valid: the bytes
	// What the line says, quiet or not: what is missing, or why not.
	QString message;
	// While values are missing: the argument Enter goes to, and what Enter
	// writes - typing comes off the end of the line (a name or a value half
	// typed) and adding goes on; adding empty, Enter writes nothing.
	QString next, typing, adding;
	bool ok() const { return error.isEmpty() && !request.isNull(); }
};
// path is the endpoint's path, for the words.
[[nodiscard]] Result check(const runtime::RuntimeSchema& schema, TypeId request, const QString& path, QStringView tail);
} // namespace telemetry::ux::request_arguments
