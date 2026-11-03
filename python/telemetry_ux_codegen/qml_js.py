"""Emit bounded snapshots of QML JavaScript values before DTO conversion.

Each own string property is read once through QJSManagedValue, whose pending
exception state distinguishes a thrown primitive from a returned primitive.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""

from . import qml_macros
from tools.telemetry_codegen.emitter_api import reachable_type_ids


BODY = r"""
namespace snapshot_detail {
inline bool fail(QString& error, const QString& reason)
{
    error = reason;
    return false;
}
inline bool at(QString& error, const QString& path)
{
    error = path + QStringLiteral(": ") + error;
    return false;
}
struct State {
    QJSEngine* engine;
    std::size_t remaining = @NODES@;
    QList<QJSValue> ancestors;

    bool exception(QString& error) const
    {
        if (!engine->hasError()) return false;
        // Do not stringify arbitrary thrown objects or execute another getter.
        static_cast<void>(engine->catchError());
        fail(error, QStringLiteral("JavaScript input access threw an exception"));
        return true;
    }
    bool visit(const QVariant& value, QVariant& output, QString& error, std::size_t depth)
    {
        if (depth > @DEPTH@ || remaining == 0)
            return fail(error, QStringLiteral("input exceeds the descriptor's depth or node budget"));
        --remaining;
        return native(value, output, error, depth);
    }
    bool native(const QVariant& value, QVariant& output, QString& error, std::size_t depth)
    {
        if (value.typeId() == ::qMetaTypeId<QJSValue>())
            return javascript(value.value<QJSValue>(), output, error, depth);
        if (value.typeId() == QMetaType::QVariantMap) {
            const QVariantMap fields = value.toMap();
            if (fields.size() > @COLLECTION@)
                return fail(error, QStringLiteral("input struct exceeds the descriptor's collection budget"));
            QVariantMap result;
            for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
                if (it.key().size() > @TEXT@)
                    return fail(error, QStringLiteral("input key exceeds the descriptor's string budget"));
                QVariant child;
                if (!visit(it.value(), child, error, depth + 1)) return at(error, it.key());
                result.insert(it.key(), std::move(child));
            }
            output = std::move(result);
            return true;
        }
        if (value.typeId() == QMetaType::QVariantList) {
            const QVariantList items = value.toList();
            if (items.size() > @COLLECTION@)
                return fail(error, QStringLiteral("input array exceeds the descriptor's collection budget"));
            QVariantList result;
            result.reserve(items.size());
            for (qsizetype index = 0; index < items.size(); ++index) {
                QVariant child;
                if (!visit(items[index], child, error, depth + 1))
                    return at(error, QStringLiteral("[") + QString::number(index) + QLatin1Char(']'));
                result.append(std::move(child));
            }
            output = std::move(result);
            return true;
        }
        switch (value.typeId()) {
        case QMetaType::QString:
            if (value.toString().size() > @TEXT@)
                return fail(error, QStringLiteral("input string exceeds the descriptor's string budget"));
            break;
        case QMetaType::Bool:
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
        case QMetaType::Double:
            break;
        default:
            return fail(error, QStringLiteral("input must contain only numbers, booleans, strings, maps and lists"));
        }
        output = value;
        return true;
    }
    bool javascript(const QJSValue& value, QVariant& output, QString& error, std::size_t depth)
    {
        if (value.isBool()) { output = value.toBool(); return true; }
        if (value.isNumber()) { output = value.toNumber(); return true; }
        if (value.isString()) {
            const auto text = value.toString();
            if (text.size() > @TEXT@)
                return fail(error, QStringLiteral("input string exceeds the descriptor's string budget"));
            output = text;
            return true;
        }
        if (!value.isObject() || value.isNull() || value.isCallable() || value.isQObject() ||
            value.isQMetaObject() || value.isDate() || value.isRegExp() || value.isError() || value.isUrl())
            return fail(error, QStringLiteral("unsupported JavaScript input value"));
        if (!engine)
            return fail(error, QStringLiteral("JavaScript objects require the originating input engine"));
        QJSManagedValue object(value, engine);
        if (object.isUndefined())
            return fail(error, QStringLiteral("JavaScript input belongs to another engine"));
        // QML may pass our own QVariantMap/List snapshots back as native Qt
        // wrappers. RetainJSObjects does not recursively convert generic JS
        // objects, drop functions, replace cycles, or read their getters.
        const QVariant retained = value.toVariant(QJSValue::RetainJSObjects);
        if (retained.typeId() == QMetaType::QVariantMap || retained.typeId() == QMetaType::QVariantList)
            return native(retained, output, error, depth);
        for (const auto& ancestor : std::as_const(ancestors))
            if (ancestor.strictlyEquals(value))
                return fail(error, QStringLiteral("cyclic JavaScript input"));
        const bool array = object.isArray();
        if (!array) {
            const auto prototype = object.prototype();
            if (exception(error)) return false;
            if (!prototype.isNull() &&
                !prototype.toJSValue().strictlyEquals(engine->newObject().prototype()))
                return fail(error, QStringLiteral("expected a plain JavaScript object or an ordinary array"));
        }
        qsizetype length = 0;
        if (array) {
            const QJSValue size = object.property(QStringLiteral("length"));
            if (exception(error)) return false;
            const double number = size.isNumber() ? size.toNumber() : -1;
            if (!std::isfinite(number) || number < 0 || number > @COLLECTION@ || std::trunc(number) != number)
                return fail(error, QStringLiteral("input array exceeds the descriptor's collection budget"));
            length = static_cast<qsizetype>(number);
        }
        QStringList names;
        QBitArray present(length);
        QJSValueIterator iterator(value);
        if (exception(error)) return false;
        while (iterator.hasNext()) {
            iterator.next();
            if (exception(error)) return false;
            const QString key = iterator.name();
            if (array && key == QStringLiteral("length")) continue;
            if (key.size() > @TEXT@ || names.size() >= @COLLECTION@)
                return fail(error, QStringLiteral("input exceeds the descriptor's own-property budget"));
            if (array) {
                bool ok = false;
                const quint32 index = key.toUInt(&ok, 10);
                if (!ok || index >= static_cast<quint32>(length) || QString::number(index) != key)
                    return fail(error, QStringLiteral("input array has an extra own string property"));
                present.setBit(static_cast<qsizetype>(index));
            }
            names.append(key);
        }
        if (array && (names.size() != length || present.count(true) != length))
            return fail(error, QStringLiteral("input array contains holes"));
        ancestors.append(value);
        QVariantMap fields;
        QVariantList items;
        if (array) items.resize(length);
        for (const auto& key : std::as_const(names)) {
            const bool own = object.hasOwnProperty(key);
            if (exception(error)) { ancestors.removeLast(); return false; }
            if (!own) {
                ancestors.removeLast();
                return fail(error, QStringLiteral("JavaScript input changed its own properties during snapshot"));
            }
            // Read precisely once. QJSValue::property and iterator.value catch
            // primitive exceptions and would make `throw 42` a valid number.
            const QJSValue property = object.property(key);
            if (exception(error)) { ancestors.removeLast(); return at(error, key); }
            QVariant child;
            if (!visit(QVariant::fromValue(property), child, error, depth + 1)) {
                ancestors.removeLast();
                return at(error, key);
            }
            if (array) items[static_cast<qsizetype>(key.toUInt())] = std::move(child);
            else fields.insert(key, std::move(child));
        }
        ancestors.removeLast();
        output = array ? QVariant(std::move(items)) : QVariant(std::move(fields));
        return true;
    }
};
} // namespace snapshot_detail

// Call on the originating engine's thread. Native QVariant data needs no engine.
// Snapshot all own string keys, including nonenumerable keys; symbol metadata is
// ignored. No result retains a live QJSValue, QObject, or borrowed native view.
// Depth/node/collection budgets bound traversal, not arbitrary JavaScript getter
// execution. On failure output is unchanged and the caller must not dispatch.
inline bool snapshot(const QVariant& value, QVariant& output, QString& error, QJSEngine* engine = nullptr)
{
    if (engine && engine->thread() != QThread::currentThread())
        return snapshot_detail::fail(error, QStringLiteral("input engine belongs to another thread"));
    if (engine && engine->hasError())
        return snapshot_detail::fail(error, QStringLiteral("input engine already has a pending exception"));
    snapshot_detail::State state{engine, @NODES@, {}};
    QVariant result;
    if (!state.visit(value, result, error, 0)) return false;
    output = std::move(result);
    return true;
}
"""


def generate(schema):
    """Return a complete Qt Qml header with budgets derived from this schema."""
    types = [schema.types[index] for index in reachable_type_ids(schema)]
    nodes = max(1, *(item["nodes"] for item in types))
    depth = max(0, *(item["depth"] for item in types))
    collection = max(1, *(len(item.get("members", ())) if item["kind"] == 3 else
                          item.get("count", 0) for item in types))
    text = max([20] + [len(name.encode("utf-16-le")) // 2
                       for item in types for _, name in item.get("members", ())])
    body = BODY
    for key, value in (("NODES", nodes), ("DEPTH", depth), ("COLLECTION", collection), ("TEXT", text)):
        body = body.replace("@" + key + "@", str(value))
    guard = ("TELEMETRY_QML_INPUT_" + schema.namespace.encode("ascii").hex().upper() +
             "_" + schema.descriptor_sha256[:16].upper() + "_HPP")
    return ("// Generated bounded QML input snapshots for Telemetry Descriptor v3. Do not edit.\n"
            "// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.\n"
            f"// Descriptor SHA-256: {schema.descriptor_sha256}\n"
            f"#ifndef {guard}\n#define {guard}\n#pragma once\n\n"
            "#include <QBitArray>\n#include <QJSEngine>\n#include <QJSManagedValue>\n"
            "#include <QJSValue>\n#include <QJSValueIterator>\n#include <QMetaType>\n"
            "#include <QStringList>\n#include <QThread>\n#include <QVariant>\n"
            "#include <cmath>\n#include <cstddef>\n#include <utility>\n\n"
            f"namespace {schema.namespace}::qml::input {{\n" +
            qml_macros.qt_names("QBitArray", "QJSEngine", "QJSManagedValue", "QJSValue", "QJSValueIterator", "QLatin1Char",
                                "QMetaType", "QString", "QStringList", "QThread", "QVariant", "QVariantMap",
                                "QVariantList", "QList") + body +
            f"}} // namespace {schema.namespace}::qml::input\n\n#endif // {guard}\n")
