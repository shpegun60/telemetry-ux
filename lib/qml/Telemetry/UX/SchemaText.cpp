/*
 * @file SchemaText.cpp
 * @brief Types and values of a parsed descriptor in words, as the device-file views show them.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "SchemaText.hpp"
#include "PathGrammar.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QStringList>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>

namespace telemetry::ux::schema_text {
namespace rt = telemetry::runtime;

namespace {
QString tr(const char* source)
{
	return QCoreApplication::translate("SchemaText", source);
}

QString scalarName(ScalarCode code)
{
	switch (code) {
		case ScalarCode::Bool:
			return QStringLiteral("bool");
		case ScalarCode::U8:
			return QStringLiteral("u8");
		case ScalarCode::S8:
			return QStringLiteral("i8");
		case ScalarCode::U16:
			return QStringLiteral("u16");
		case ScalarCode::S16:
			return QStringLiteral("i16");
		case ScalarCode::U32:
			return QStringLiteral("u32");
		case ScalarCode::S32:
			return QStringLiteral("i32");
		case ScalarCode::U64:
			return QStringLiteral("u64");
		case ScalarCode::S64:
			return QStringLiteral("i64");
		case ScalarCode::F32:
			return QStringLiteral("f32");
		case ScalarCode::F64:
			return QStringLiteral("f64");
	}
	return QStringLiteral("?");
}

// The shortest text that reads back to the same number; words for the rest.
template<class F>
Value floating(F value)
{
	if (std::isnan(value))
		return {QStringLiteral("NaN"), Json::Text, {}};
	if (std::isinf(value))
		return {value > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity"), Json::Text, {}};
	char buffer[64];
	const auto written = std::to_chars(buffer, buffer + sizeof(buffer), value);
	if (written.ec != std::errc{})
		return {QStringLiteral("?"), Json::Text, {}};
	return {QString::fromLatin1(buffer, static_cast<qsizetype>(written.ptr - buffer)), Json::Number, {}};
}

template<class T>
Value integer(const rt::DynamicValueView& view, bool underlying)
{
	const auto read = underlying ? view.enumUnderlying<T>() : view.get<T>();
	if (!read)
		return {QStringLiteral("?"), Json::Text, {}};
	if constexpr (std::is_signed_v<T>)
		return {QString::number(static_cast<qlonglong>(read.value)), Json::Number, {}};
	else
		return {QString::number(static_cast<qulonglong>(read.value)), Json::Number, {}};
}

Value number(const rt::DynamicValueView& view, ScalarCode code, bool underlying)
{
	switch (code) {
		case ScalarCode::Bool: {
			const auto read = view.get<bool>();
			return read ? Value{read.value ? QStringLiteral("true") : QStringLiteral("false"), Json::Boolean, {}}
			            : Value{QStringLiteral("?"), Json::Text, {}};
		}
		case ScalarCode::U8:
			return integer<std::uint8_t>(view, underlying);
		case ScalarCode::S8:
			return integer<std::int8_t>(view, underlying);
		case ScalarCode::U16:
			return integer<std::uint16_t>(view, underlying);
		case ScalarCode::S16:
			return integer<std::int16_t>(view, underlying);
		case ScalarCode::U32:
			return integer<std::uint32_t>(view, underlying);
		case ScalarCode::S32:
			return integer<std::int32_t>(view, underlying);
		case ScalarCode::U64:
			return integer<std::uint64_t>(view, underlying);
		case ScalarCode::S64:
			return integer<std::int64_t>(view, underlying);
		case ScalarCode::F32: {
			const auto read = view.get<float>();
			return read ? floating(read.value) : Value{QStringLiteral("?"), Json::Text, {}};
		}
		case ScalarCode::F64: {
			const auto read = view.get<double>();
			return read ? floating(read.value) : Value{QStringLiteral("?"), Json::Text, {}};
		}
	}
	return {QStringLiteral("?"), Json::Text, {}};
}
} // namespace

QString text(std::string_view view)
{
	return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

QString typeText(const rt::RuntimeSchema& schema, TypeId id)
{
	const auto* type = schema.type(id);
	if (!type)
		return QStringLiteral("?");
	switch (type->kind) {
		case TypeKind::Void:
			return QStringLiteral("()");
		case TypeKind::Scalar:
			return scalarName(type->scalarCode);
		case TypeKind::Enum:
			// A name says what it is; without one it is counted, as a struct
			// is: the names are enumNames' (family A7).
			if (!type->name.empty())
				return text(type->name);
			return QStringLiteral("enum (%1)").arg(schema.enumEntries(id).size());
		case TypeKind::Struct:
			if (!type->name.empty())
				return text(type->name);
			return QStringLiteral("struct (%1)").arg(type->memberCount);
		case TypeKind::Array:
			return QStringLiteral("%1[%2]").arg(typeText(schema, type->relatedTypeId)).arg(type->elementCount);
	}
	return QStringLiteral("?");
}

QString typeName(const rt::RuntimeSchema& schema, TypeId id)
{
	return text(schema.typeName(id));
}

namespace {
QString counted(qsizetype count, const char* one, const char* many)
{
	return count == 1 ? tr(one) : tr(many).arg(count);
}

// A struct's or an enum's size in words: "3 members", "1 value".
QString sizeWords(const rt::RuntimeSchema& schema, const rt::RuntimeType& type)
{
	return type.kind == TypeKind::Struct
	           ? counted(type.memberCount, "1 member", "%1 members")
	           : counted(static_cast<qsizetype>(schema.enumEntries(type.id).size()), "1 value", "%1 values");
}

// "adc::Config" read own name first: "Config in adc"; a global "Config" as it is.
QString spokenName(const QString& name)
{
	const auto cut = name.lastIndexOf(QStringLiteral("::"));
	return cut < 0 ? name : tr("%1 in %2").arg(name.mid(cut + 2), name.left(cut));
}
} // namespace

QString typeWords(const rt::RuntimeSchema& schema, TypeId id)
{
	const auto* type = schema.type(id);
	if (!type || (type->kind != TypeKind::Struct && type->kind != TypeKind::Enum) || type->name.empty())
		return typeText(schema, id);
	const auto word = type->kind == TypeKind::Struct ? QStringLiteral("struct") : QStringLiteral("enum");
	return word + QLatin1Char(' ') + text(type->name) + QStringLiteral(" · ") + sizeWords(schema, *type);
}

QString typeSpoken(const rt::RuntimeSchema& schema, TypeId id)
{
	const auto* type = schema.type(id);
	if (!type)
		return typeText(schema, id);
	if (type->kind == TypeKind::Array) {
		const auto* element = schema.type(type->relatedTypeId);
		const auto inner = element && !element->name.empty() ? spokenName(text(element->name))
		                                                     : typeText(schema, type->relatedTypeId);
		return tr("array of %1 %2").arg(type->elementCount).arg(inner);
	}
	if (type->kind != TypeKind::Struct && type->kind != TypeKind::Enum)
		return typeText(schema, id);
	const auto word = type->kind == TypeKind::Struct ? tr("struct") : tr("enum");
	const auto name = text(type->name);
	// Without a name, as the tree said it before names: "struct with 3 members".
	if (name.isEmpty())
		return tr("%1 with %2").arg(word, sizeWords(schema, *type));
	return word + QLatin1Char(' ') + spokenName(name) + QStringLiteral(", ") + sizeWords(schema, *type);
}

QString shortScopes(const QString& text)
{
	// A scope is a run of name characters ended by "::"; names hold no ":"
	// and none of ( ) , [ ] or a space (15.3), so the run starts after one.
	QString out;
	out.reserve(text.size());
	qsizetype at = 0;
	while (at < text.size()) {
		const auto colons = text.indexOf(QStringLiteral("::"), at);
		if (colons < 0) {
			out += QStringView(text).mid(at);
			break;
		}
		auto start = colons;
		while (start > at) {
			const auto c = text.at(start - 1);
			if (c == QLatin1Char('(') || c == QLatin1Char(')') || c == QLatin1Char(',') || c == QLatin1Char(' ') ||
			    c == QLatin1Char('[') || c == QLatin1Char(']') || c == QLatin1Char('>'))
				break;
			--start;
		}
		out += QStringView(text).mid(at, start - at);
		out += start < colons ? QStringLiteral("…::") : QStringLiteral("::");
		at = colons + 2;
	}
	return out;
}

GroupPath groupPath(const rt::RuntimeSchema& schema, descriptor::Category category, std::uint32_t catalogIndex)
{
	const auto catalogs = schema.catalogs(category);
	if (catalogIndex >= catalogs.size())
		return {};
	if (!schema.groupTree()) {
		const auto name = text(catalogs[catalogIndex].name);
		return {name, path_grammar::escaped(name)};
	}
	// Up the parents of a tree's group: every family lists the same groups.
	const auto groups = schema.groups();
	QStringList names;
	for (auto at = catalogIndex; at < groups.size() && names.size() <= static_cast<qsizetype>(groups.size());
	     at = groups[at].parent)
		names.prepend(text(groups[at].name));
	QStringList written;
	for (const auto& name : std::as_const(names))
		written << path_grammar::escaped(name);
	return {names.join(QLatin1Char('/')), written.join(QLatin1Char('/'))};
}

QString enumNames(const rt::RuntimeSchema& schema, TypeId id)
{
	const auto* type = schema.type(id);
	if (!type || type->kind != TypeKind::Enum)
		return {};
	QStringList names;
	const auto entries = schema.enumEntries(id);
	for (const auto& entry : entries.first(std::min<std::size_t>(entries.size(), 16)))
		names << text(entry.name);
	if (entries.size() > 16)
		names << QStringLiteral("…");
	return names.join(QStringLiteral(", "));
}

namespace {
// A member's type inside a signature: an unnamed enum is "enum", the rest
// (a named enum by its name) as typeText.
QString memberText(const rt::RuntimeSchema& schema, TypeId id)
{
	const auto* type = schema.type(id);
	if (type && type->kind == TypeKind::Enum && type->name.empty())
		return QStringLiteral("enum");
	return typeText(schema, id);
}
} // namespace

QString argumentsText(const rt::RuntimeSchema& schema, TypeId id)
{
	const auto* type = schema.type(id);
	if (!type)
		return QStringLiteral("?");
	if (type->kind == TypeKind::Void)
		return QStringLiteral("()");
	// A named request says what it is: "(adc::Reference)".
	if (type->kind != TypeKind::Struct || !type->name.empty())
		return QStringLiteral("(%1)").arg(memberText(schema, id));
	QStringList parts;
	const auto members = schema.members(id);
	for (const auto& member : members.first(std::min<std::size_t>(members.size(), 4)))
		parts << memberText(schema, member.typeId);
	if (members.size() > 4)
		parts << QStringLiteral("…");
	return QStringLiteral("(%1)").arg(parts.join(QStringLiteral(", ")));
}

QString signatureText(const rt::RuntimeSchema& schema, TypeId request, TypeId response, bool returns)
{
	if (!returns)
		return argumentsText(schema, request);
	const auto* result = schema.type(response);
	// A named response by its name, "-> adc::Offset"; an unnamed one by its members.
	const auto out = !result || result->kind == TypeKind::Void                 ? QStringLiteral("()")
	                 : result->kind == TypeKind::Struct && result->name.empty() ? argumentsText(schema, response)
	                                                                            : memberText(schema, response);
	// ASCII: a fallback font's arrow would make the row taller.
	return argumentsText(schema, request) + QStringLiteral(" -> ") + out;
}

Value valueText(const rt::RuntimeSchema& schema, const rt::DynamicValueView& view)
{
	const auto* type = schema.type(view.typeId());
	if (!type)
		return {QStringLiteral("?"), Json::Text, {}};
	switch (type->kind) {
		case TypeKind::Scalar:
			return number(view, type->scalarCode, false);
		case TypeKind::Enum: {
			// "Run (1)": the name and the code; a code the descriptor does not
			// name says so. JSON takes the code.
			const auto* underlying = schema.type(type->relatedTypeId);
			const auto code = underlying ? number(view, underlying->scalarCode, true) : Value{QStringLiteral("?"), Json::Text, {}};
			const auto name = view.enumName();
			const auto shown = name && name.value.known ? QStringLiteral("%1 (%2)").arg(text(name.value.name), code.text)
			                                            : tr("%1 (no name)").arg(code.text);
			return {shown, code.json, code.text};
		}
		default:
			return {QStringLiteral("?"), Json::Text, {}};
	}
}
namespace {
bool leafType(const runtime::RuntimeSchema& schema, std::uint32_t typeId)
{
	const auto* type = schema.type(typeId);
	return type && (type->kind == TypeKind::Scalar || type->kind == TypeKind::Enum);
}
} // namespace

QString summaryText(const runtime::RuntimeSchema& schema, const runtime::DynamicValueView& view)
{
	const auto* type = schema.type(view.typeId());
	if (!type)
		return {};
	QStringList shown;
	if (type->kind == TypeKind::Struct) {
		const auto members = schema.members(view.typeId());
		for (std::uint32_t i = 0; i < members.size(); ++i) {
			if (shown.size() == 4) {
				shown << QStringLiteral("…");
				break;
			}
			const auto name = text(members[i].name);
			const auto member = view.member(i);
			if (!member) {
				shown << name + QStringLiteral(" ?");
				continue;
			}
			const auto* memberType = schema.type(members[i].typeId);
			shown << name + QLatin1Char(' ') +
			             (leafType(schema, members[i].typeId) ? valueText(schema, member.value).text
			              : memberType && memberType->kind == TypeKind::Array ? QStringLiteral("[…]")
			                                                                   : QStringLiteral("{…}"));
		}
		return QLatin1Char('{') + shown.join(QStringLiteral(", ")) + QLatin1Char('}');
	}
	if (type->kind == TypeKind::Array && leafType(schema, type->relatedTypeId)) {
		for (std::uint32_t i = 0; i < type->elementCount; ++i) {
			if (shown.size() == 8) {
				shown << QStringLiteral("…");
				break;
			}
			const auto element = view.index(i);
			shown << (element ? valueText(schema, element.value).text : QStringLiteral("?"));
		}
		return QLatin1Char('[') + shown.join(QStringLiteral(", ")) + QLatin1Char(']');
	}
	return {};
}

} // namespace telemetry::ux::schema_text
