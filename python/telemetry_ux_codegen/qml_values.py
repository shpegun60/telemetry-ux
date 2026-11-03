"""Emit strict, lossless QVariant bridges for the generated standalone DTOs.

The QML API uses decimal strings for every 64-bit integer, including enum
underlying values. No Qt annotations or dependencies enter the wire DTOs.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""

from . import qml_macros
from tools.telemetry_codegen.emitter_api import cpp_string_literal, reachable_type_ids


def _qstring(value):
    return f"QString::fromUtf8({cpp_string_literal(value)}, {len(value.encode('utf-8'))})"


HELPERS = r"""
namespace conversion {
inline bool fail(QString& error, const QString& reason)
{
    error = reason;
    return false;
}
inline bool at(QString& error, const QString& location)
{
    error = location + QStringLiteral(": ") + error;
    return false;
}
inline bool number(const QVariant& value, double& result, QString& error)
{
    // QVariant's general conversions also accept bool, strings, byte arrays,
    // user converters and enum values. Those are not QML numeric inputs.
    switch (value.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
    case QMetaType::Float:
    case QMetaType::Double: {
        bool ok = false;
        result = value.toDouble(&ok);
        if (ok) return true;
        break;
    }
    default:
        break;
    }
    return fail(error, QStringLiteral("expected a numeric QVariant"));
}
template<class T> inline bool integer(const QVariant& value, T& result, QString& error)
{
    static_assert(std::is_integral_v<T> && sizeof(T) <= 4 && !std::is_same_v<T, bool>);
    double numberValue = 0;
    if (!number(value, numberValue, error)) return false;
    if (!std::isfinite(numberValue) || std::trunc(numberValue) != numberValue ||
        numberValue < static_cast<double>(std::numeric_limits<T>::lowest()) ||
        numberValue > static_cast<double>(std::numeric_limits<T>::max()))
        return fail(error, QStringLiteral("expected an integer in the wire type's range"));
    result = static_cast<T>(numberValue);
    return true;
}
template<class T> inline bool decimal(const QVariant& value, T& result, QString& error)
{
    static_assert(std::is_integral_v<T> && sizeof(T) == 8);
    if (value.typeId() != QMetaType::QString)
        return fail(error, QStringLiteral("expected a canonical decimal QString for a 64-bit integer"));
    const QString text = value.toString();
    const bool negative = !text.isEmpty() && text.front() == QLatin1Char('-');
    const qsizetype first = negative ? 1 : 0;
    if (text.size() == first || (negative && !std::is_signed_v<T>) ||
        (text.size() - first > 1 && text[first] == QLatin1Char('0')) ||
        (negative && text[first] == QLatin1Char('0')))
        return fail(error, QStringLiteral("expected canonical decimal digits without whitespace, plus or leading zeros"));
    const std::uint64_t limit = negative
        ? static_cast<std::uint64_t>(std::numeric_limits<T>::max()) + 1u
        : static_cast<std::uint64_t>(std::numeric_limits<T>::max());
    std::uint64_t magnitude = 0;
    for (qsizetype index = first; index < text.size(); ++index) {
        const ushort digit = text[index].unicode();
        if (digit < '0' || digit > '9')
            return fail(error, QStringLiteral("expected ASCII decimal digits"));
        const auto part = static_cast<std::uint64_t>(digit - '0');
        if (magnitude > limit / 10 || (magnitude == limit / 10 && part > limit % 10))
            return fail(error, QStringLiteral("64-bit decimal integer is outside the wire type's range"));
        magnitude = magnitude * 10 + part;
    }
    if constexpr (std::is_signed_v<T>) {
        if (negative) {
            // Avoid negating the unrepresentable positive magnitude of INT64_MIN.
            result = magnitude == static_cast<std::uint64_t>(std::numeric_limits<T>::max()) + 1u
                ? std::numeric_limits<T>::lowest() : static_cast<T>(-static_cast<T>(magnitude));
        } else {
            result = static_cast<T>(magnitude);
        }
    } else {
        result = static_cast<T>(magnitude);
    }
    return true;
}
template<class T> inline bool floating(const QVariant& value, T& result, QString& error)
{
    double numberValue = 0;
    if (!number(value, numberValue, error)) return false;
    if constexpr (sizeof(T) == 4) {
        if (std::isfinite(numberValue) &&
            std::abs(numberValue) > static_cast<double>(std::numeric_limits<T>::max()))
            return fail(error, QStringLiteral("finite number overflows the binary32 wire type"));
    }
    result = static_cast<T>(numberValue);
    return true;
}
} // namespace conversion

// On failure the output may contain a converted prefix and must not be sent.
// The error is populated only on failure. QVariantMap/List inputs are snapshots;
// no implicit string/boolean/container conversions are admitted.
"""


def generate(schema):
    """Return a complete Qt Core-only converter header, without runtime state."""
    reachable = tuple(index for index in reachable_type_ids(schema) if schema.types[index]["kind"])
    # Preserve case even on projects generating the same descriptor into both
    # foo and Foo namespaces and including both adapters in one translation unit.
    guard = ("TELEMETRY_QML_VALUES_" + schema.namespace.encode("ascii").hex().upper() +
             "_" + schema.descriptor_sha256[:16].upper() + "_HPP")
    include = schema.namespace.replace("::", "/")

    def type_name(index):
        return f"::{schema.namespace}::detail::Type_{index}"

    rows = ["// Generated strict QVariant adapters for Telemetry Descriptor v3. Do not edit.\n",
            "// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.\n",
            f"// Descriptor SHA-256: {schema.descriptor_sha256}\n",
            f"#ifndef {guard}\n#define {guard}\n#pragma once\n\n",
            "// Standalone DTOs/converters expose no Qt meta-object declarations.\n"
            "// moc treats tokens such as Q_OBJECT specially even in enum labels.\n"
            "#ifndef Q_MOC_RUN\n",
            "#include <QMetaType>\n#include <QString>\n#include <QVariant>\n",
            "#include <cmath>\n#include <cstddef>\n#include <cstdint>\n#include <limits>\n#include <type_traits>\n\n",
            qml_macros.push(schema), f"#include <{include}/Codec.hpp>\n",
            f"namespace {schema.namespace}::qml::values {{\n",
            qml_macros.qt_names("QLatin1Char", "QMetaType", "QString", "QVariant", "QVariantMap", "QVariantList"), HELPERS]
    for index in reachable:
        item = schema.types[index]
        type_cpp = type_name(index)
        rows.append(f"inline bool from_{index}(const QVariant& value, {type_cpp}& result, QString& error)\n{{\n")
        kind = item["kind"]
        if kind == 1:
            code = item["code"]
            if code == 1:
                rows.append('    if (value.typeId() != QMetaType::Bool)\n'
                            '        return conversion::fail(error, QStringLiteral("expected a boolean QVariant"));\n'
                            '    result = value.toBool();\n    return true;\n')
            else:
                helper = "decimal" if code in (8, 9) else "floating" if code in (10, 11) else "integer"
                rows.append(f"    return conversion::{helper}(value, result, error);\n")
        elif kind == 2:
            child = item["underlying"]
            rows.append(f"    {type_name(child)} underlying{{}};\n"
                        f"    if (!from_{child}(value, underlying, error)) return false;\n"
                        f"    result = static_cast<{type_cpp}>(underlying);\n    return true;\n")
        elif kind == 3:
            if not item["members"]:
                rows.append("    static_cast<void>(result);\n")
            rows.append('    if (value.typeId() != QMetaType::QVariantMap)\n'
                        '        return conversion::fail(error, QStringLiteral("expected a QVariantMap struct"));\n'
                        '    const QVariantMap fields = value.toMap();\n'
                        f'    if (fields.size() != {len(item["members"])})\n'
                        '        return conversion::fail(error, QStringLiteral("struct has missing or extra members"));\n')
            for ordinal, ((child, raw), member) in enumerate(zip(item["members"], schema.member_names[index])):
                rows.append(f"    const auto key_{ordinal} = {_qstring(raw)};\n"
                            f"    const auto field_{ordinal} = fields.constFind(key_{ordinal});\n"
                            f"    if (field_{ordinal} == fields.cend())\n"
                            f'        return conversion::fail(error, QStringLiteral("missing struct member: ") + key_{ordinal});\n'
                            f"    if (!from_{child}(field_{ordinal}.value(), result.{member}, error))\n"
                            f"        return conversion::at(error, key_{ordinal});\n")
            rows.append("    return true;\n")
        else:
            child, count = item["element"], item["count"]
            rows.append('    if (value.typeId() != QMetaType::QVariantList)\n'
                        '        return conversion::fail(error, QStringLiteral("expected a QVariantList array"));\n'
                        '    const QVariantList items = value.toList();\n'
                        f'    if (items.size() != {count})\n'
                        '        return conversion::fail(error, QStringLiteral("array has incorrect fixed length"));\n'
                        '    for (qsizetype index = 0; index < items.size(); ++index) {\n'
                        f"        if (!from_{child}(items[index], result[static_cast<std::size_t>(index)], error))\n"
                        '            return conversion::at(error, QStringLiteral("[") + QString::number(index) + QLatin1Char(\']\'));\n'
                        '    }\n    return true;\n')
        rows.append("}\n")
        # Empty structs/arrays do not otherwise use the reference parameter.
        rows.append(f"inline QVariant to_{index}(const {type_cpp}& value)\n{{\n")
        if kind == 1:
            code = item["code"]
            if code in (8, 9):
                qt_type = "qulonglong" if code == 8 else "qlonglong"
                rows.append(f"    return QString::number(static_cast<{qt_type}>(value));\n")
            elif code == 1:
                rows.append("    return QVariant::fromValue(value);\n")
            elif code in (6, 10, 11):
                # Every U32 and F32 is exactly representable as a binary64 QML number.
                rows.append("    return QVariant::fromValue(static_cast<double>(value));\n")
            else:
                rows.append("    return QVariant::fromValue(static_cast<int>(value));\n")
        elif kind == 2:
            child = item["underlying"]
            rows.append(f"    return to_{child}(static_cast<{type_name(child)}>(value));\n")
        elif kind == 3:
            rows.append("    QVariantMap fields;\n")
            if not item["members"]:
                rows.append("    static_cast<void>(value);\n")
            for (child, raw), member in zip(item["members"], schema.member_names[index]):
                rows.append(f"    fields.insert({_qstring(raw)}, to_{child}(value.{member}));\n")
            rows.append("    return fields;\n")
        else:
            child, count = item["element"], item["count"]
            rows.append(f"    QVariantList items;\n    items.reserve({count});\n"
                        f"    for (const auto& item : value) items.append(to_{child}(item));\n"
                        "    return items;\n")
        rows.append("}\n\n")
    rows.append(f"}} // namespace {schema.namespace}::qml::values\n\n" + qml_macros.pop(schema) +
                f"\n#endif // Q_MOC_RUN\n#endif // {guard}\n")
    return "".join(rows)
