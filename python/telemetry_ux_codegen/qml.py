"""Emit a Qt 6 QML module over the unchanged standalone C++ SDK.

Properties are confirmed local snapshots. Requests cross an asynchronous,
session-scoped application boundary; this module owns no socket or RPC loop.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""

from . import qml_macros, qml_values
from tools.telemetry_codegen.emitter_api import KEYWORDS, identifier


RESERVED = KEYWORDS | frozenset("""
objectName objectNameChanged destroyed parent children deleteLater setParent thread
metaObject staticMetaObject qt_metacall qt_metacast tr connect disconnect
qt_staticMetaObjectContent qt_staticMetaObjectStaticContent qt_staticMetaObjectRelocatingContent
qt_qmlMarker_anonymous
event eventFilter timerEvent childEvent customEvent connectNotify disconnectNotify
property signal readonly required component on id ready readyChanged session
sessionChanged schemaFingerprint descriptorSha256 requestTimeoutMs
requestTimeoutMsChanged setRequestTimeoutMs admitDescriptor resetSession
requestReady operationFinished complete fail submit invalidate DeviceApi
deliver expire Decoder Finish Pending Result m_device m_pending m_timer
m_nextToken m_session m_ready m_timeout
snapshotInput setInputEngine m_inputEngine requestActive
setTransportHandoffTracking transportHandoffAccepted m_trackHandoff
commitTransportHandoff
qApp qGuiApp qAppName
qint8 quint8 qint16 quint16 qint32 quint32 qint64 quint64 qsizetype qlonglong qulonglong ushort
signals slots emit foreach forever qml qmlAttachedProperties
""".split())
GROUP_RESERVED = RESERVED - {"m_device"}


def _lower(name):
    value = identifier(name)
    return value[0].lower() + value[1:]


def _title(name):
    return name[0].upper() + name[1:]


def _family(stem, category, configurator=False):
    names = {stem, stem + "Finished"}
    if category == 1:
        names.update((stem + "Valid", stem + "Changed", "refresh" + _title(stem),
                      "write" + _title(stem), "m_" + stem, "m_" + stem + "Valid"))
        if configurator:
            names.update((stem + "CanonicalPayload", "m_" + stem + "CanonicalPayload",
                          "write" + _title(stem) + "Preserving"))
    return names


def _allocate(items, family, reserved=RESERVED):
    bases = [_lower(name) for name, _ in items]
    all_bases, occupied, result = set(bases), set(reserved), []
    for base, (_, category) in zip(bases, items):
        stem, suffix = base, 2
        while (family(stem, category) & occupied or
               (stem != base and stem in all_bases) or
               (family(stem, category) - {stem}) & all_bases):
            stem, suffix = base + "_" + str(suffix), suffix + 1
        occupied.update(family(stem, category))
        result.append(stem)
    return result


def layout(schema, configurator=False):
    groups = list(dict.fromkeys(catalog["name"] for catalogs in schema.raw["catalogs"] for catalog in catalogs))
    group_names = _allocate([(name, 0) for name in groups], lambda name, _: {name, "m_" + name}, GROUP_RESERVED)
    result = []
    for index, (name, symbol) in enumerate(zip(groups, group_names)):
        endpoints = [ep for ep in schema.endpoints if ep.catalog_name == name]
        symbols = _allocate([(ep.name, ep.category) for ep in endpoints],
                            lambda stem, category: _family(stem, category, configurator))
        result.append(dict(name=name, symbol=symbol, class_name=f"Group_{index}Api",
                           endpoints=list(zip(endpoints, symbols))))
    return result


def symbol_mapping(schema, configurator=False):
    return {"module_uri": "Telemetry.Generated." + schema.namespace.replace("::", "."),
            "groups": [{"original_name": group["name"], "property": group["symbol"],
                        "endpoints": [{"category": ep.category, "id": ep.id,
                                       "original_name": ep.name, "property_or_method": stem}
                                      for ep, stem in group["endpoints"]]}
                       for group in layout(schema, configurator)]}


def _ep(schema, endpoint):
    return (f"::{schema.namespace}::{endpoint.group}::" +
            ("", "fields", "commands", "services")[endpoint.category] + "::" + endpoint.symbol)


def _type(schema, index):
    return f"::{schema.namespace}::detail::Type_{index}"


def _prefix(schema, filename, includes=""):
    guard = ("TELEMETRY_GENERATED_QML_" + schema.namespace.encode("ascii").hex().upper() +
             "_" + schema.descriptor_sha256[:16].upper() + "_" + filename.upper() + "_HPP")
    return ("// Generated Qt/QML adapter for Telemetry Descriptor v3. Do not edit.\n"
            f"// Descriptor SHA-256: {schema.descriptor_sha256}\n"
            "// SPDX-License-Identifier: MIT\n" + f"#ifndef {guard}\n#define {guard}\n#pragma once\n" + includes)


HEADER = r'''
#include <QtCore/QByteArray>
#include <QtCore/QDeadlineTimer>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QVariant>
#include <QtQml/qqmlregistration.h>
#include <QtQml/QJSEngine>
#include <functional>
#include <cstddef>

namespace @NS@::qml {
@QT_NAMES@
// Result preserves accepted work and transport uncertainty independently of success.
struct Result {
    bool success = false, accepted = false;
    QVariant value;
    QString errorKind, message;
    int dispatchStatus = -1, endpointStatus = -1;
};
class DeviceApi;
'''


DEVICE = r'''
// One instance represents one admitted peer/session. Call properties and methods
// on its owner thread. complete()/fail() also accept worker-thread calls safely.
// The application supplies asynchronous transport; property reads never dispatch.
class DeviceApi : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(DeviceApi)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged FINAL)
    Q_PROPERTY(quint32 session READ session NOTIFY sessionChanged FINAL)
    Q_PROPERTY(QString schemaFingerprint READ schemaFingerprint CONSTANT FINAL)
    Q_PROPERTY(QString descriptorSha256 READ descriptorSha256 CONSTANT FINAL)
    Q_PROPERTY(int requestTimeoutMs READ requestTimeoutMs WRITE setRequestTimeoutMs
               NOTIFY requestTimeoutMsChanged FINAL)
@GROUP_PROPERTIES@
public:
    using Decoder = std::function<bool(const QByteArray&, QVariant&, QString&)>;
    using Finish = std::function<void(const QString&, const Result&)>;
    explicit DeviceApi(QObject* parent = nullptr);
    bool ready() const noexcept { return m_ready; }
    quint32 session() const noexcept { return m_session; }
    QString schemaFingerprint() const;
    QString descriptorSha256() const;
    int requestTimeoutMs() const noexcept { return m_timeout; }
    void setRequestTimeoutMs(int value);
    // Set explicitly for a C++ instance exposed as a context property. A QML-
    // created instance discovers its owning engine automatically.
    void setInputEngine(QJSEngine* engine) { m_inputEngine = engine; }
    bool snapshotInput(const QVariant& input, QVariant& output, QString& error) const;
    bool requestActive(const QString& token, quint32 session) const;
    // Opt in before any pending work; existing transports remain conservative.
    bool setTransportHandoffTracking(bool enabled);
    // Owner-thread permission BEFORE accepting a job that may execute/send.
    // A false result forbids that handoff; one token receives permission once.
    bool transportHandoffAccepted(const QString& token, quint32 session);
@GROUP_GETTERS@
    // Admission is declared schema identity, not authentication or a body hash.
    Q_INVOKABLE bool admitDescriptor(const QByteArray& header);
    Q_INVOKABLE void resetSession();
    Q_INVOKABLE void complete(const QString& token, quint32 session, int dispatch,
                              int endpoint, const QByteArray& payload);
    Q_INVOKABLE void fail(const QString& token, quint32 session,
                          const QString& kind, const QString& message);
    QString submit(int operation, quint32 id, quint32 requestType, quint32 responseType,
                   QByteArray payload, quint32 responseBytes, Decoder decoder, Finish finish,
                   const QString& validationError = {});
Q_SIGNALS:
    void readyChanged();
    void sessionChanged();
    void requestTimeoutMsChanged();
    void requestReady(QString token, quint32 session, int operation, quint32 id,
                      quint32 requestType, quint32 responseType, QByteArray payload);
    void operationFinished(QString token, QVariantMap result);
private:
    template<class, std::size_t, std::size_t> friend class DeviceClientAdapter;
    // The generated adapter supplies ONLY its non-reentrant core permission
    // check. No event pumping or API mutation is allowed inside this callback.
    // Commit once both checks pass in this owner-thread operation. A second
    // deadline check after core success would move uncertainty into the core.
    template<class Permit>
    bool commitTransportHandoff(const QString& token, quint32 epoch, Permit&& permit) {
        if (!requestActive(token, epoch)) return false;
        auto it = m_pending.find(token);
        if (it->handoffAccepted) return false;
        QPointer<DeviceApi> alive(this);
        if (!permit() || !alive) return false;
        // Defensive lifetime/session guards, not a second deadline gate.
        if (!m_ready || m_session != epoch) return false;
        it = m_pending.find(token);
        if (it == m_pending.end() || it->session != epoch || !it->offered || it->handoffAccepted) return false;
        it->handoffAccepted = true;
        return true;
    }
    struct Pending {
        quint32 session;
        int operation;
        quint32 id, responseBytes;
        QString key;
        Decoder decoder;
        Finish finish;
        QDeadlineTimer deadline;
        bool offered = false, handoffTracked = false, handoffAccepted = false;
        bool mayExecute() const noexcept { return offered && (!handoffTracked || handoffAccepted); }
    };
    void deliver(const QString& token, Pending pending, Result result);
    void expire();
    void invalidate();
    QHash<QString, Pending> m_pending;
    QTimer m_timer;
    QPointer<QJSEngine> m_inputEngine;
    quint64 m_nextToken = 0;
    quint32 m_session = 0;
    bool m_ready = false;
    bool m_trackHandoff = false;
    int m_timeout = 5000;
@GROUP_MEMBERS@
};
} // namespace @NS@::qml
'''


SOURCE = r'''
// Generated asynchronous Qt/QML request and confirmed-cache implementation.
// SPDX-License-Identifier: MIT
#include "DeviceApi.hpp"
#include "QmlInput.hpp"
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QThread>
#include <QtQml/QQmlEngine>
#include <limits>
#include <memory>
#include <span>
#include <utility>

@MACROS_PUSH@
#include "QmlValues.hpp"
#include <@INCLUDE@/Endpoints.hpp>
namespace @NS@::qml {
@QT_SOURCE_NAMES@
namespace {
[[maybe_unused]] std::span<const std::byte> byteView(const QByteArray& bytes) {
    return {reinterpret_cast<const std::byte*>(bytes.constData()), static_cast<std::size_t>(bytes.size())};
}
[[maybe_unused]] std::span<std::byte> byteOutput(QByteArray& bytes) {
    return {reinterpret_cast<std::byte*>(bytes.data()), static_cast<std::size_t>(bytes.size())};
}
QVariantMap resultMap(const Result& result, int operation, quint32 id) {
    return {{QStringLiteral("success"), result.success}, {QStringLiteral("accepted"), result.accepted},
            {QStringLiteral("value"), result.value}, {QStringLiteral("errorKind"), result.errorKind},
            {QStringLiteral("message"), result.message}, {QStringLiteral("dispatchStatus"), result.dispatchStatus},
            {QStringLiteral("endpointStatus"), result.endpointStatus}, {QStringLiteral("operation"), operation},
            {QStringLiteral("id"), id}};
}
}
DeviceApi::DeviceApi(QObject* parent) : QObject(parent), m_timer(this) {
    m_timer.setInterval(10);
    QObject::connect(&m_timer, &QTimer::timeout, this, &DeviceApi::expire);
@GROUP_INIT@
}
QString DeviceApi::schemaFingerprint() const { return QStringLiteral("@FINGERPRINT@"); }
QString DeviceApi::descriptorSha256() const { return QStringLiteral("@SHA@"); }
bool DeviceApi::snapshotInput(const QVariant& value, QVariant& output, QString& error) const {
    auto* engine = m_inputEngine ? m_inputEngine.data() : ::qjsEngine(this);
    return ::@NS@::qml::input::snapshot(value, output, error, engine);
}
bool DeviceApi::requestActive(const QString& token, quint32 epoch) const {
    if (QThread::currentThread() != thread() || !m_ready || epoch != m_session) return false;
    const auto it = m_pending.constFind(token);
    return it != m_pending.cend() && it->session == epoch && it->offered && !it->deadline.hasExpired();
}
bool DeviceApi::setTransportHandoffTracking(bool enabled) {
    if (QThread::currentThread() != thread() || !m_pending.isEmpty()) return false;
    m_trackHandoff = enabled;
    return true;
}
bool DeviceApi::transportHandoffAccepted(const QString& token, quint32 epoch) {
    if (QThread::currentThread() != thread() || !m_ready || epoch != m_session) return false;
    auto it = m_pending.find(token);
    if (it == m_pending.end() || it->session != epoch || !it->offered ||
        it->deadline.hasExpired() || it->handoffAccepted) return false;
    it->handoffAccepted = true;
    return true;
}
void DeviceApi::setRequestTimeoutMs(int value) {
    if (value < 1 || value > 300000 || value == m_timeout) return;
    m_timeout = value;
    Q_EMIT requestTimeoutMsChanged();
}
void DeviceApi::invalidate() {
    QPointer<DeviceApi> alive(this);
@INVALIDATE@
}
void DeviceApi::resetSession() {
    if (QThread::currentThread() != thread()) return;
    QPointer<DeviceApi> alive(this);
    const bool wasReady = m_ready;
    m_ready = false;
    // Never wrap an epoch into an old peer's request namespace.
    if (m_session != std::numeric_limits<quint32>::max()) ++m_session;
    const auto pending = std::exchange(m_pending, {});
    m_timer.stop();
    invalidate();
    if (!alive) return;
    if (wasReady) Q_EMIT readyChanged();
    if (!alive) return;
    Q_EMIT sessionChanged();
    if (!alive) return;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        Result result;
        result.errorKind = it->mayExecute() && it->operation != 4 ? QStringLiteral("OutcomeUnknown") : QStringLiteral("Cancelled");
        result.message = QStringLiteral("Session ended; no operation is replayed");
        deliver(it.key(), it.value(), result);
    }
}
bool DeviceApi::admitDescriptor(const QByteArray& header) {
    if (QThread::currentThread() != thread()) return false;
    QPointer<DeviceApi> alive(this);
    const auto previousSession = m_session;
    resetSession();
    if (!alive) return false;
    // Session notifications may reset or admit another peer synchronously.
    // Never attach this header to the epoch created by that newer operation.
    if (previousSession == std::numeric_limits<quint32>::max() || m_session != previousSession + 1 ||
        m_session == std::numeric_limits<quint32>::max() || !::@NS@::schema::matchesHeader(::@NS@::qml::byteView(header))) return false;
    const auto epoch = m_session;
    m_ready = true;
    Q_EMIT readyChanged();
    return alive && m_ready && m_session == epoch;
}
QString DeviceApi::submit(int operation, quint32 id, quint32 requestType, quint32 responseType,
                        QByteArray payload, quint32 responseBytes, Decoder decoder, Finish finish,
                        const QString& validationError) {
    if (QThread::currentThread() != thread() || m_nextToken == std::numeric_limits<quint64>::max()) return {};
    const auto token = QString::number(++m_nextToken);
    const auto epoch = m_session;
    const auto key = QString::number(operation == 4 ? 1 : operation) + QLatin1Char(':') + QString::number(id);
    Pending pending{epoch, operation, id, responseBytes, key, std::move(decoder), std::move(finish), QDeadlineTimer(m_timeout)};
    pending.handoffTracked = m_trackHandoff;
    Result refusal;
    if (!validationError.isEmpty()) {
        refusal.errorKind = QStringLiteral("Validation");
        refusal.message = validationError;
    } else if (!m_ready) {
        refusal.errorKind = QStringLiteral("Schema");
        refusal.message = QStringLiteral("Admit this peer's Descriptor header before requesting endpoints");
    } else if (m_pending.size() >= 64) {
        refusal.errorKind = QStringLiteral("Busy");
        refusal.message = QStringLiteral("At most 64 requests may be pending");
    } else {
        for (const auto& active : std::as_const(m_pending)) if (active.key == key) {
            refusal.errorKind = QStringLiteral("Busy");
            refusal.message = QStringLiteral("An operation for this endpoint is pending");
            break;
        }
    }
    if (!refusal.errorKind.isEmpty()) {
        deliver(token, std::move(pending), std::move(refusal));
        return token;
    }
    m_pending.insert(token, std::move(pending));
    if (!m_timer.isActive()) m_timer.start();
    QMetaObject::invokeMethod(this, [this, token, epoch, operation, id, requestType, responseType, payload = std::move(payload)] {
        auto it = m_pending.find(token);
        if (it == m_pending.end() || it->session != epoch || m_session != epoch || !m_ready) return;
        if (it->deadline.hasExpired()) { fail(token, epoch, QStringLiteral("Timeout"), QStringLiteral("Deadline expired before transport dispatch")); return; }
        it->offered = true;
        Q_EMIT requestReady(token, epoch, operation, id, requestType, responseType, payload);
    }, Qt::QueuedConnection);
    return token;
}
void DeviceApi::deliver(const QString& token, Pending pending, Result result) {
    QMetaObject::invokeMethod(this, [this, token, pending = std::move(pending), result = std::move(result)]() mutable {
        // Terminal evidence was fixed on the owner thread. Session freshness
        // gates field cache changes, never the already checked public result.
        QPointer<DeviceApi> alive(this);
        pending.finish(token, result);
        if (alive) Q_EMIT operationFinished(token, ::@NS@::qml::resultMap(result, pending.operation, pending.id));
    }, Qt::QueuedConnection);
}
void DeviceApi::complete(const QString& token, quint32 epoch, int dispatch, int endpoint, const QByteArray& payload) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, token, epoch, dispatch, endpoint, payload] { complete(token, epoch, dispatch, endpoint, payload); }, Qt::QueuedConnection);
        return;
    }
    auto it = m_pending.find(token);
    if (it == m_pending.end() || it->session != epoch || epoch != m_session || !it->offered) return;
    if (it->deadline.hasExpired()) {
        fail(token, epoch, it->mayExecute() && it->operation != 4 ? QStringLiteral("OutcomeUnknown") : QStringLiteral("Timeout"),
             QStringLiteral("Completion arrived after the request deadline"));
        return;
    }
    auto pending = std::move(it.value());
    m_pending.erase(it);
    if (m_pending.isEmpty()) m_timer.stop();
    Result result;
    result.dispatchStatus = dispatch;
    result.endpointStatus = endpoint;
    if (dispatch != 0) {
        result.errorKind = QStringLiteral("Dispatch");
        result.message = QStringLiteral("Dispatch status %1").arg(dispatch);
    } else if (endpoint != 0 && !(pending.operation == 2 && endpoint == 1)) {
        result.errorKind = QStringLiteral("Endpoint");
        result.message = QStringLiteral("Endpoint status %1").arg(endpoint);
    } else if (payload.size() != static_cast<qsizetype>(pending.responseBytes)) {
        result.errorKind = QStringLiteral("Codec");
        result.message = QStringLiteral("Response length mismatch");
    } else if (pending.decoder && !pending.decoder(payload, result.value, result.message)) {
        result.errorKind = QStringLiteral("Codec");
    } else {
        result.accepted = pending.operation == 2 && endpoint == 1;
        result.success = !result.accepted;
    }
    deliver(token, std::move(pending), std::move(result));
}
void DeviceApi::fail(const QString& token, quint32 epoch, const QString& kind, const QString& message) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, token, epoch, kind, message] { fail(token, epoch, kind, message); }, Qt::QueuedConnection);
        return;
    }
    auto it = m_pending.find(token);
    if (it == m_pending.end() || it->session != epoch || epoch != m_session) return;
    auto pending = std::move(it.value());
    m_pending.erase(it);
    if (m_pending.isEmpty()) m_timer.stop();
    Result result;
    result.errorKind = kind.isEmpty() ? QStringLiteral("Transport") : kind;
    result.message = message;
    deliver(token, std::move(pending), std::move(result));
}
void DeviceApi::expire() {
    const auto tokens = m_pending.keys();
    for (const auto& token : tokens) {
        auto it = m_pending.constFind(token);
        if (it == m_pending.cend() || !it->deadline.hasExpired()) continue;
        const bool unknown = it->mayExecute() && it->operation != 4;
        fail(token, it->session, unknown ? QStringLiteral("OutcomeUnknown") : QStringLiteral("Timeout"),
             QStringLiteral("Request deadline expired; no retry was issued"));
    }
}
@GROUP_SOURCE@
} // namespace @NS@::qml
@MACROS_POP@
'''


def _group_header(group, configurator=False):
    name = group["class_name"]
    rows = [f"// Cached endpoint facade owned by DeviceApi; snapshots contain no live references.\nclass {name} final : public QObject {{\n    Q_OBJECT\n    QML_ANONYMOUS\n"]
    for ep, stem in group["endpoints"]:
        if ep.category == 1:
            rows.append(f"    Q_PROPERTY(QVariant {stem} READ {stem} NOTIFY {stem}Changed FINAL)\n"
                        f"    Q_PROPERTY(bool {stem}Valid READ {stem}Valid NOTIFY {stem}Changed FINAL)\n")
    rows.append(f"public:\n    explicit {name}(DeviceApi* device);\n    void invalidate();\n")
    for ep, stem in group["endpoints"]:
        if ep.category == 1:
            rows.append(f"    QVariant {stem}() const {{ return m_{stem}; }}\n"
                        f"    bool {stem}Valid() const noexcept {{ return m_{stem}Valid; }}\n")
            if configurator:
                rows.append(f"    QByteArray {stem}CanonicalPayload() const {{ return m_{stem}CanonicalPayload; }}\n")
            if ep.capabilities & 1:
                rows.append(f"    Q_INVOKABLE QString refresh{_title(stem)}();\n")
            if ep.capabilities & 2:
                rows.append(f"    Q_INVOKABLE QString write{_title(stem)}(const QVariant& value);\n")
                if configurator:
                    rows.append(f"    Q_INVOKABLE QString write{_title(stem)}Preserving(const QVariant& value, const QByteArray& originalPayload);\n")
        else:
            argument = "const QVariant& value" if ep.request_type else ""
            rows.append(f"    Q_INVOKABLE QString {stem}({argument});\n")
    rows.append("Q_SIGNALS:\n")
    for ep, stem in group["endpoints"]:
        if ep.category == 1:
            rows.append(f"    void {stem}Changed();\n")
        rows.append(f"    void {stem}Finished(QString token, bool success, bool accepted, QVariant value, QString errorKind, QString message);\n")
    rows.append("private:\n    DeviceApi* m_device;\n")
    for ep, stem in group["endpoints"]:
        if ep.category == 1:
            rows.append(f"    QVariant m_{stem};\n    bool m_{stem}Valid = false;\n")
            if configurator:
                rows.append(f"    QByteArray m_{stem}CanonicalPayload;\n")
    rows.append("};\n")
    return "".join(rows)


def _decoder(schema, index):
    if not index:
        return "{}"
    return ("[](const QByteArray& payload, QVariant& out, QString& error) { " +
            "auto decoded = std::make_unique<" + _type(schema, index) + ">();\n"
            f"        if (::{schema.namespace}::codec::decode(::{schema.namespace}::qml::byteView(payload), *decoded) != ::{schema.namespace}::codec::Status::Ok) {{\n"
            '            error = QStringLiteral("Invalid canonical response value"); return false; }\n'
            f"        out = ::{schema.namespace}::qml::values::to_{index}(*decoded); return true; }}")


def _group_source(schema, group, configurator=False):
    name = group["class_name"]
    rows = [f"{name}::{name}(DeviceApi* device) : QObject(device), m_device(device) {{\n"
            "    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);\n}\n",
            f"void {name}::invalidate() {{\n    QPointer<{name}> alive(this);\n"]
    for ep, stem in group["endpoints"]:
        if ep.category == 1:
            clear_payload = f" m_{stem}CanonicalPayload.clear();" if configurator else ""
            rows.append(f"    if (m_{stem}Valid || m_{stem}.isValid()) {{ m_{stem}Valid = false; m_{stem}.clear();{clear_payload} Q_EMIT {stem}Changed(); if (!alive) return; }}\n")
    rows.append("}\n")
    for ep, stem in group["endpoints"]:
        operations = []
        if ep.category == 1:
            if ep.capabilities & 1:
                operations.append(("refresh" + _title(stem), 4, 0, ep.value_type))
            if ep.capabilities & 2:
                operations.append(("write" + _title(stem), 1, ep.value_type, 0))
        else:
            operations.append((stem, ep.category, ep.request_type, ep.response_type))
        if configurator and ep.category == 1 and ep.capabilities & 2:
            operations.append(("write" + _title(stem) + "Preserving", 1, ep.value_type, 0))
        for method, operation, request, response in operations:
            preserving = configurator and ep.category == 1 and method == "write" + _title(stem) + "Preserving"
            argument = "const QVariant& value" if request else ""
            if preserving:
                argument += ", const QByteArray& originalPayload"
            rows.append(f"QString {name}::{method}({argument}) {{\n    QPointer<{name}> alive(this);\n    const auto endpoint = {_ep(schema, ep)};\n"
                        "    QString error;\n    QByteArray payload;\n")
            if preserving:
                rows.append("    // Own the seed before QJS getters/converters can reenter and replace its source.\n"
                            "    const QByteArray original(originalPayload.constData(), originalPayload.size());\n")
            if request:
                rows.append(f"    auto input = std::make_unique<{_type(schema, request)}>();\n"
                            "    const auto inputSession = m_device->session();\n"
                            "    QVariant snapshot;\n"
                            "    const bool snapped = m_device->snapshotInput(value, snapshot, error);\n"
                            "    if (!alive) return {};\n"
                            "    if (m_device->session() != inputSession) error = QStringLiteral(\"Session changed during request validation\");\n"
                            f"    if (snapped && error.isEmpty() && ::{schema.namespace}::qml::values::from_{request}(snapshot, *input, error)) {{\n"
                            f"        payload.resize(::{schema.namespace}::codec::wireSize<{_type(schema, request)}>);\n"
                            f"        if (::{schema.namespace}::codec::{'encodePreserving' if preserving else 'encode'}(*input, "
                            + (f"::{schema.namespace}::qml::byteView(original), " if preserving else "")
                            + f"::{schema.namespace}::qml::byteOutput(payload)) != ::{schema.namespace}::codec::Status::Ok)\n"
                            '            error = QStringLiteral("Cannot encode request");\n    }\n')
            req_id = "endpoint.valueTypeId" if operation == 1 else ("endpoint.requestTypeId" if request else "0")
            resp_id = "endpoint.valueTypeId" if operation == 4 else ("endpoint.responseTypeId" if response else "0")
            resp_bytes = "endpoint.valueBytes" if operation == 4 else ("endpoint.responseBytes" if response else "0")
            cache_epoch = ", epoch = m_device->session()" if operation in (1, 4) else ""
            rows.append(f"    return m_device->submit({operation}, endpoint.id, {req_id}, {resp_id}, std::move(payload), {resp_bytes},\n"
                        f"        {_decoder(schema, response)},\n        [self = QPointer<{name}>(this){cache_epoch}](const QString& token, const Result& result) {{\n            if (!self) return;\n")
            if operation == 4:
                payload_cache = f" self->m_{stem}CanonicalPayload = result.canonicalPayload;" if configurator else ""
                rows.append(f"            if (result.success && self->m_device->ready() && self->m_device->session() == epoch) {{ self->m_{stem} = result.value;{payload_cache} self->m_{stem}Valid = true; Q_EMIT self->{stem}Changed(); if (!self) return; }}\n")
            if operation == 1:
                clear_payload = f" self->m_{stem}CanonicalPayload.clear();" if configurator else ""
                rows.append(f"            if (result.success && self->m_device->ready() && self->m_device->session() == epoch && (self->m_{stem}Valid || self->m_{stem}.isValid())) {{ self->m_{stem}.clear();{clear_payload} self->m_{stem}Valid = false; Q_EMIT self->{stem}Changed(); if (!self) return; }}\n")
            rows.append(f"            Q_EMIT self->{stem}Finished(token, result.success, result.accepted, result.value, result.errorKind, result.message);\n"
                        "        }, error);\n}\n")
    return "".join(rows)


def generate(schema, configurator=False):
    """The qml target depends on the byte-identical existing cpp target."""
    from . import qml_js, qml_rpc, qml_client
    qml_macros.validate_namespace(schema)
    groups = layout(schema, configurator)
    include = schema.namespace.replace("::", "/")
    qt_names = qml_macros.qt_names("QByteArray", "QDeadlineTimer", "QHash", "QObject", "QPointer",
                                  "QString", "QTimer", "QVariant", "QVariantMap", "QMetaObject", "QJSEngine")
    header = (_prefix(schema, "DEVICEAPI", "\n") +
              HEADER.replace("@NS@", schema.namespace).replace("@QT_NAMES@", qt_names))
    header += "".join(_group_header(group, configurator) for group in groups)
    if configurator:
        header = header.replace("    QVariant value;\n", "    QVariant value;\n    QByteArray canonicalPayload;\n", 1)
    substitutions = {
        "@GROUP_PROPERTIES@": "\n".join(f'    Q_PROPERTY({g["class_name"]}* {g["symbol"]} READ {g["symbol"]} CONSTANT FINAL)' for g in groups),
        "@GROUP_GETTERS@": "\n".join(f'    {g["class_name"]}* {g["symbol"]}() const noexcept {{ return m_{g["symbol"]}; }}' for g in groups),
        "@GROUP_MEMBERS@": "\n".join(f'    QPointer<{g["class_name"]}> m_{g["symbol"]};' for g in groups),
        "@GROUP_INIT@": "\n".join(f'    m_{g["symbol"]} = new {g["class_name"]}(this);' for g in groups),
        "@INVALIDATE@": "\n".join(f'    if (m_{g["symbol"]}) m_{g["symbol"]}->invalidate();\n    if (!alive) return;' for g in groups),
        "@GROUP_SOURCE@": "".join(_group_source(schema, group, configurator) for group in groups),
        "@FINGERPRINT@": f"0x{schema.fingerprint:016x}", "@SHA@": schema.descriptor_sha256,
        "@INCLUDE@": include, "@NS@": schema.namespace,
        "@MACROS_PUSH@": qml_macros.push(schema), "@MACROS_POP@": qml_macros.pop(schema),
        "@QT_SOURCE_NAMES@": qml_macros.qt_names("QThread", "QQmlEngine", "QMetaObject", "QPointer", "QLatin1Char"),
    }
    tail, source = DEVICE, SOURCE
    for key, value in substitutions.items():
        tail, source = tail.replace(key, value), source.replace(key, value)
    if configurator:
        source = source.replace('{QStringLiteral("id"), id}};',
                                '{QStringLiteral("id"), id}, {QStringLiteral("canonicalPayload"), result.canonicalPayload}};')
        source = source.replace('        result.success = !result.accepted;\n',
                                '        result.success = !result.accepted;\n'
                                '        result.canonicalPayload = QByteArray(payload.constData(), payload.size());\n')
        source = source.replace('if (alive) Q_EMIT operationFinished(token, ::@NS@::qml::resultMap(result, pending.operation, pending.id));'.replace('@NS@', schema.namespace),
                                'if (alive) { auto outcome = ::' + schema.namespace + '::qml::resultMap(result, pending.operation, pending.id); outcome.insert(QStringLiteral("session"), pending.session); Q_EMIT operationFinished(token, outcome); }')
    # A signal is an ordinary generated C++ method. Avoid using a macro here
    # when a perfectly valid DTO member itself happens to be named Q_EMIT.
    source = source.replace("Q_EMIT ", "")
    header += tail + "\n#endif\n"
    target = "telemetry_qml_" + schema.namespace.replace("::", "__")
    uri = "Telemetry.Generated." + schema.namespace.replace("::", ".")
    cmake = f'''# Generated Qt 6 module; the core SDK has no Qt or RPC dependency.
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.25)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Qml)
qt_standard_project_setup(REQUIRES 6.12)
qt_add_qml_module({target} STATIC
    URI {uri}
    VERSION 1.0
    SOURCES DeviceApi.hpp DeviceApi.cpp QmlValues.hpp QmlInput.hpp)
target_compile_features({target} PUBLIC cxx_std_20)
target_include_directories({target} PUBLIC "${{CMAKE_CURRENT_SOURCE_DIR}}" "${{CMAKE_CURRENT_SOURCE_DIR}}/../cpp")
target_link_libraries({target} PUBLIC Qt6::Core Qt6::Qml)
'''
    return {"qml/DeviceApi.hpp": header, "qml/DeviceApi.cpp": source,
            "qml/QmlValues.hpp": qml_values.generate(schema),
            "qml/QmlInput.hpp": qml_js.generate(schema),
            "qml/QmlRpcAdapter.hpp": qml_rpc.generate(schema),
            "qml/DeviceClientAdapter.hpp": qml_client.generate(schema), "qml/CMakeLists.txt": cmake}
