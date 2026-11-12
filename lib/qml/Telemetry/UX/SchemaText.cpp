/*
 * @file SchemaText.cpp
 * @brief Types and values of a parsed descriptor in words, as the device-file views show them.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "SchemaText.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QStringList>
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
			return tr("nothing");
		case TypeKind::Scalar:
			return scalarName(type->scalarCode);
		case TypeKind::Enum: {
			QStringList names;
			for (const auto& entry : schema.enumEntries(id))
				names << text(entry.name);
			const auto shown = names.mid(0, 4).join(QStringLiteral(", "));
			return names.size() > 4 ? tr("enum %1, …").arg(shown) : tr("enum %1").arg(shown);
		}
		case TypeKind::Struct:
			return type->memberCount == 1 ? tr("struct, 1 member") : tr("struct, %1 members").arg(type->memberCount);
		case TypeKind::Array:
			return QStringLiteral("%1[%2]").arg(typeText(schema, type->relatedTypeId)).arg(type->elementCount);
	}
	return QStringLiteral("?");
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
} // namespace telemetry::ux::schema_text
