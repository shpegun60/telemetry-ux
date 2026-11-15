// A Command's or Service's input: its arguments and its signature. MIT.
#include "RequestArguments.hpp"
#include "SchemaText.hpp"
#include <algorithm>

namespace telemetry::ux::request_arguments {
namespace rt = telemetry::runtime;
namespace {
bool isSigned(WatchScalar scalar)
{
	return scalar == WatchScalar::S8 || scalar == WatchScalar::S16 || scalar == WatchScalar::S32 ||
	       scalar == WatchScalar::S64;
}

// An enum code's bits, zero-extended, as the number they are at width.
qint64 codeOf(quint64 bits, const WatchValueType& type)
{
	if (!isSigned(type.scalar) || type.width >= 8)
		return qint64(bits);
	const auto shift = 64 - 8 * type.width;
	return qint64(bits << shift) >> shift;
}
} // namespace

WatchValueType valueType(const rt::RuntimeSchema& schema, TypeId id)
{
	const auto* type = schema.type(id);
	if (!type || (type->kind != TypeKind::Scalar && type->kind != TypeKind::Enum) || type->wireBytes == 0 ||
	    type->wireBytes > 8)
		return {};
	const auto* scalar = type->kind == TypeKind::Enum ? schema.type(type->relatedTypeId) : type;
	if (!scalar || scalar->kind != TypeKind::Scalar)
		return {};
	WatchScalar code = WatchScalar::Unknown;
	switch (scalar->scalarCode) {
		case ScalarCode::Bool: code = WatchScalar::Boolean; break;
		case ScalarCode::U8: code = WatchScalar::U8; break;
		case ScalarCode::S8: code = WatchScalar::S8; break;
		case ScalarCode::U16: code = WatchScalar::U16; break;
		case ScalarCode::S16: code = WatchScalar::S16; break;
		case ScalarCode::U32: code = WatchScalar::U32; break;
		case ScalarCode::S32: code = WatchScalar::S32; break;
		case ScalarCode::U64: code = WatchScalar::U64; break;
		case ScalarCode::S64: code = WatchScalar::S64; break;
		case ScalarCode::F32: code = WatchScalar::F32; break;
		case ScalarCode::F64: code = WatchScalar::F64; break;
	}
	WatchValueType result{type->id, code, quint8(type->wireBytes), type->kind == TypeKind::Enum};
	return result.valid() ? result : WatchValueType{};
}

std::vector<Argument> arguments(const rt::RuntimeSchema& schema, TypeId request)
{
	std::vector<Argument> out;
	const auto* type = schema.type(request);
	if (!type || type->kind == TypeKind::Void)
		return out;
	const auto one = [&schema](QString name, TypeId id, quint32 offset) {
		Argument argument;
		argument.name = std::move(name);
		const auto* inner = schema.type(id);
		argument.type = id;
		argument.offset = offset;
		argument.bytes = inner ? inner->wireBytes : 0;
		argument.value = valueType(schema, id);
		argument.scalar = argument.value.valid();
		argument.typeText = inner && inner->kind == TypeKind::Enum ? QStringLiteral("enum") : schema_text::typeText(schema, id);
		if (argument.value.scalar == WatchScalar::Boolean && !argument.value.enumeration)
			argument.choices = {QStringLiteral("true"), QStringLiteral("false")};
		if (argument.value.enumeration)
			for (const auto& entry : schema.enumEntries(id)) {
				argument.choices << schema_text::text(entry.name);
				argument.codes << codeOf(entry.codeBits, argument.value);
			}
		return argument;
	};
	if (type->kind != TypeKind::Struct) {
		out.push_back(one({}, request, 0));
		return out;
	}
	for (const auto& member : schema.members(request))
		out.push_back(one(schema_text::text(member.name), member.typeId, member.wireOffset));
	return out;
}

QString signatureText(const rt::RuntimeSchema& schema, TypeId request)
{
	const auto inputs = arguments(schema, request);
	QStringList parts;
	for (std::size_t i = 0; i < inputs.size() && i < 4; ++i)
		parts << (inputs[i].name.isEmpty() ? inputs[i].typeText
		                                   : QStringLiteral("%1: %2").arg(inputs[i].name, inputs[i].typeText));
	if (inputs.size() > 4)
		parts << QStringLiteral("…");
	return QStringLiteral("(%1)").arg(parts.join(QStringLiteral(", ")));
}
} // namespace telemetry::ux::request_arguments
