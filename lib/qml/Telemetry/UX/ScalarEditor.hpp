// Exact text conversion shared by Dynamic panels and the Inspector. MIT.
#pragma once
#include <telemetry/runtime/DynamicValueWriter.hpp>
#include <QtCore/QString>
#include <charconv>
#include <type_traits>

namespace telemetry::ux::detail {
template<class T>
bool parseScalar(const QString& text, T& value)
{
	const auto input = text.trimmed().toUtf8();
	if constexpr (std::is_same_v<T, bool>) {
		if (input == "true" || input == "1")
			value = true;
		else if (input == "false" || input == "0")
			value = false;
		else
			return false;
		return true;
	} else {
		const auto parsed =
		    std::from_chars(input.constData(), input.constData() + input.size(), value);
		return parsed.ec == std::errc{} && parsed.ptr == input.constData() + input.size();
	}
}

template<class Editor, class Node>
runtime::Status setScalarText(Editor& editor, const Node& node, ScalarCode code,
                              const QString& text, bool& parsed)
{
	auto status = runtime::Status::TypeMismatch;
	parsed = false;
	const auto set = [&]<class T>() {
		T number{};
		if ((parsed = parseScalar(text, number)))
			status = editor.setScalar(node, number);
	};
	switch (code) {
		case ScalarCode::Bool:
			set.template operator()<bool>();
			break;
		case ScalarCode::U8:
			set.template operator()<std::uint8_t>();
			break;
		case ScalarCode::S8:
			set.template operator()<std::int8_t>();
			break;
		case ScalarCode::U16:
			set.template operator()<std::uint16_t>();
			break;
		case ScalarCode::S16:
			set.template operator()<std::int16_t>();
			break;
		case ScalarCode::U32:
			set.template operator()<std::uint32_t>();
			break;
		case ScalarCode::S32:
			set.template operator()<std::int32_t>();
			break;
		case ScalarCode::U64:
			set.template operator()<std::uint64_t>();
			break;
		case ScalarCode::S64:
			set.template operator()<std::int64_t>();
			break;
		case ScalarCode::F32:
			set.template operator()<float>();
			break;
		case ScalarCode::F64:
			set.template operator()<double>();
			break;
	}
	return status;
}
} // namespace telemetry::ux::detail
