// Qt-owned scalar editing facts shared by snapshot and Watch editors. MIT.
#pragma once
#include "SchemaText.hpp"
#include <QtCore/QHash>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <type_traits>

namespace telemetry::ux::detail {
template<class T> QString scalarEditorText(T value)
{
	if constexpr (std::is_same_v<T, bool>)
		return value ? QStringLiteral("true") : QStringLiteral("false");
	else {
		if constexpr (std::is_floating_point_v<T>) {
			using Bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
			constexpr int fraction = sizeof(T) == 4 ? 23 : 52;
			constexpr Bits exponent = sizeof(T) == 4 ? Bits{0xff} : Bits{0x7ff};
			const auto bits = std::bit_cast<Bits>(value);
			const auto magnitude = bits & ((std::numeric_limits<Bits>::max)() >> 1);
			const bool negative = (bits >> (sizeof(T) * 8 - 1)) != 0;
			if (magnitude == 0) return negative ? QStringLiteral("-0") : QStringLiteral("0");
			if ((magnitude >> fraction) == exponent) {
				if ((magnitude & ((Bits{1} << fraction) - 1)) != 0) return QStringLiteral("NaN");
				return negative ? QStringLiteral("-inf") : QStringLiteral("inf");
			}
		}
		std::array<char, 64> buffer{};
		const auto result = [&] {
			if constexpr (std::is_floating_point_v<T>)
				return std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
				                     std::chars_format::general, std::numeric_limits<T>::max_digits10);
			else
				return std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
		}();
		return result.ec == std::errc{} ? QString::fromLatin1(buffer.data(), result.ptr - buffer.data())
		                                : QString{};
	}
}

// Descriptor enum codes are unsigned bits of their declared width. Interpret
// signed narrow codes at that width, rather than casting zero-extended -1 to s64.
inline QString scalarEditorBits(std::uint64_t bits, ScalarCode code)
{
	switch (code) {
		case ScalarCode::Bool: return bits <= 1 ? scalarEditorText(bits == 1) : QString{};
		case ScalarCode::U8: return scalarEditorText(static_cast<std::uint8_t>(bits));
		case ScalarCode::S8: return scalarEditorText(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(bits)));
		case ScalarCode::U16: return scalarEditorText(static_cast<std::uint16_t>(bits));
		case ScalarCode::S16: return scalarEditorText(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(bits)));
		case ScalarCode::U32: return scalarEditorText(static_cast<std::uint32_t>(bits));
		case ScalarCode::S32: return scalarEditorText(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits)));
		case ScalarCode::U64: return scalarEditorText(bits);
		case ScalarCode::S64: return scalarEditorText(std::bit_cast<std::int64_t>(bits));
		case ScalarCode::F32: return scalarEditorText(std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
		case ScalarCode::F64: return scalarEditorText(std::bit_cast<double>(bits));
	}
	return {};
}

inline QVariantList booleanEditorChoices()
{
	return {
	    QVariantMap{{QStringLiteral("label"), QStringLiteral("false (0)")},
	                {QStringLiteral("text"), QStringLiteral("false")}},
	    QVariantMap{{QStringLiteral("label"), QStringLiteral("true (1)")},
	                {QStringLiteral("text"), QStringLiteral("true")}}};
}

class ScalarEditorMetadata final {
public:
	struct Type {
		QString label;
		QString kind = QStringLiteral("text");
		QVariantList choices;
		bool truncated = false;
	};
	using EndpointIdentity = std::array<std::pair<quintptr, std::size_t>, 3>;
	static constexpr std::size_t choiceLimit = 256;

	void reset(const runtime::RuntimeSchema& schema)
	{
		types_.clear(); fields_.clear(); endpoints_ = {};
		if (!schema.valid()) return;
		std::size_t n = 0;
		for (const auto category : {descriptor::Category::Field, descriptor::Category::Command,
		                            descriptor::Category::Service}) {
			const auto endpoints = schema.endpoints(category);
			endpoints_[n++] = {reinterpret_cast<quintptr>(endpoints.data()), endpoints.size()};
			if (category == descriptor::Category::Field)
				for (const auto& endpoint : endpoints) fields_.insert(endpoint.id, endpoint.typeId);
		}
		for (TypeId id = 0; const auto* type = schema.type(id); ++id) {
			Type copied;
			copied.label = schema_text::typeText(schema, id);
			if (type->kind == TypeKind::Scalar && type->scalarCode == ScalarCode::Bool) {
				copied.kind = QStringLiteral("bool"); copied.choices = booleanEditorChoices();
			} else if (type->kind == TypeKind::Enum) {
				const auto* underlying = schema.type(type->relatedTypeId);
				if (underlying && underlying->kind == TypeKind::Scalar) {
					copied.kind = QStringLiteral("enum");
					const auto entries = schema.enumEntries(id);
					copied.truncated = entries.size() > choiceLimit;
					for (const auto& entry : entries.first((std::min)(entries.size(), choiceLimit))) {
						const auto code = scalarEditorBits(entry.codeBits, underlying->scalarCode);
						if (code.isEmpty()) continue;
						copied.choices.append(QVariantMap{
						    {QStringLiteral("label"), schema_text::text(entry.name) + QStringLiteral(" (") + code + QLatin1Char(')')},
						    {QStringLiteral("text"), code}});
					}
				}
			}
			types_.insert(id, std::move(copied));
		}
	}
	const Type* type(TypeId id) const
	{
		const auto found = types_.constFind(id);
		return found == types_.cend() ? nullptr : &found.value();
	}
	const Type* field(quint32 id) const
	{
		const auto found = fields_.constFind(id);
		return found == fields_.cend() ? nullptr : type(found.value());
	}
	bool matches(const EndpointIdentity& identity) const
	{
		for (std::size_t i = 0; i < endpoints_.size(); ++i)
			if (endpoints_[i].second != identity[i].second ||
			    (identity[i].second && endpoints_[i].first != identity[i].first)) return false;
		return true;
	}

private:
	QHash<TypeId, Type> types_;
	QHash<quint32, TypeId> fields_;
	// Opaque identity tokens only. They are never dereferenced or used as schema ownership.
	EndpointIdentity endpoints_{};
};
} // namespace telemetry::ux::detail
