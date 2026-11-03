"""Optional Qt adapter using the single portable DeviceClient execution queue. MIT."""

from . import qml_macros


SOURCE = r'''// Generated optional adapter over the portable asynchronous queue. Do not edit.
// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
#pragma once
#include "DeviceApi.hpp"
#ifndef Q_MOC_RUN
#include <QByteArray>
#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>
#include <cassert>
#include <cstdlib>
#include <span>

@PUSH@
#include <@PATH@/Schema.hpp>
#include <client/DeviceClient.hpp>

namespace @NS@::qml {
@QT_NAMES@
// Byte-only submissions use the existing generated QML value converters.
// DTO storage is never multiplied by queue depth.
struct QmlClientCodec {
    static constexpr std::size_t scratchBytes = 1, scratchAlign = 1;
    static ::telemetry::client::Input descriptorHeader() noexcept {
        return ::@NS@::schema::descriptorHeader;
    }
};

// Api, adapter, backend and its buffers have one owner thread. Exclusively
// consume requestReady: installing another transport would send work twice.
// The map below correlates tokens only; DeviceClient is the sole execution FIFO.
// Stop producers, close(), and process until drained() BEFORE destruction.
template<class Api = DeviceApi, std::size_t N = 8, std::size_t PayloadBytes = 4096>
class DeviceClientAdapter final : public QObject {
    using Status = ::telemetry::client::Status;
    using Handle = ::telemetry::client::RequestHandle;
    using Core = ::telemetry::client::DeviceClient<QmlClientCodec, N, PayloadBytes>;
public:
    using Packet = ::telemetry::client::Packet;
    explicit DeviceClientAdapter(Api& api, ::telemetry::client::Backend& backend,
        ::telemetry::client::Clock clock, QObject* parent = nullptr)
        : QObject(parent), api_(&api), backend_(backend), clock_(clock), client_(backend, clock)
    {
        ownerThread();
        if (!api.setTransportHandoffTracking(true)) std::abort();
        QObject::connect(&api, &Api::requestReady, this,
            [this](QString token, quint32 session, int operation, quint32 id,
                   quint32 requestType, quint32 responseType, QByteArray payload) {
                submit(token, session, operation, id, requestType, responseType, payload);
            }, Qt::QueuedConnection);
        QObject::connect(&api, &Api::sessionChanged, this,
            [this] { (void)beginSession(); }, Qt::QueuedConnection);
    }
    ~DeviceClientAdapter() override { assert(client_.drained()); }

    [[nodiscard]] std::size_t pending() const noexcept { return client_.pending(); }
    [[nodiscard]] bool recoveryRequired() const noexcept { return backend_.recoveryRequired(); }
    [[nodiscard]] quint32 recoveryGeneration() const noexcept { return backend_.session(); }
    [[nodiscard]] bool drained() const noexcept { return client_.drained(); }
    void close() noexcept { ownerThread(); closed_ = true; client_.close(); }

    [[nodiscard]] bool confirmSessionClosed(quint32 generation) noexcept {
        ownerThread();
        return client_.confirmSessionClosed(generation);
    }

    [[nodiscard]] bool beginSession() {
        ownerThread();
        if (closed_) return false;
        const bool ready = api_ && api_->ready();
        const auto next = ready ? api_->session() : 0;
        if (session_ && session_ != next) {
            backend_.close();
            session_ = 0;
        }
        if (!ready || backend_.recoveryRequired()) return false;
        if (session_ == next) return true;
        // DeviceApi has already checked the observed exact header. The core
        // receives that same identity, bound to this admitted API generation.
        const auto admitted = client_.admitDescriptor(QmlClientCodec::descriptorHeader(), next);
        if (admitted != Status::Ok) return false;
        session_ = next;
        resetQueued_ = false;
        return true;
    }

    void process(quint32 now) {
        ownerThread();
        (void)beginSession();
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (!api_ || !api_->requestActive(it.key(), it->session)) {
                if (it->handle) (void)client_.cancel(it->handle);
                else { it = pending_.erase(it); continue; }
            }
            ++it;
        }
        client_.process(now);
        if (backend_.recoveryRequired() && !resetQueued_) {
            resetQueued_ = true;
            const auto api = api_;
            const auto epoch = api ? api->session() : 0;
            // Completion tasks were queued first. Never invalidate a newer
            // admission that appeared before this queued task is delivered.
            if (api) QMetaObject::invokeMethod(api, [api, epoch] {
                if (api && api->ready() && api->session() == epoch) api->resetSession();
            }, Qt::QueuedConnection);
        }
    }

    [[nodiscard]] Packet nextPacket() {
        ownerThread();
        if (!beginSession()) return {};
        const auto packet = client_.nextPacket();
        auto it = active();
        if (it == pending_.end() || !api_ || !api_->requestActive(it.key(), it->session)) return {};
        return packet;
    }
    // COPY nextPacket bytes into bounded port-owned storage before this call.
    // A false result forbids send/enqueue. Repeated same-tx packets reuse the
    // single API permission, while the backend validates each packet identity.
    [[nodiscard]] bool prepareHandoff(quint32 session, quint32 requestId) {
        ownerThread();
        if (!beginSession()) return false;
        client_.process(clock_.now());
        const auto packet = client_.nextPacket();
        if (!packet || packet.session != session || packet.requestId != requestId) return false;
        auto it = active();
        if (it == pending_.end() || !api_ || !api_->requestActive(it.key(), it->session)) return false;
        if (packet.mayExecute && !it->handedOff) {
            // The API checks its deadline first; the core then makes the final
            // permission decision. Refusal never commits Qt's mayExecute bit.
            const bool accepted = api_->commitTransportHandoff(it.key(), it->session,
                [this, session, requestId]() noexcept {
                    return client_.prepareHandoff(session, requestId);
                });
            if (accepted) it->handedOff = true;
            return accepted;
        }
        return client_.prepareHandoff(session, requestId);
    }
    [[nodiscard]] bool onSendAccepted(quint32 session, quint32 requestId) {
        ownerThread();
        // Record actual complete-frame handoff even if an API deadline elapsed
        // after permission. The backend preserves uncertain execution evidence.
        const bool accepted = client_.onSendAccepted(session, requestId);
        process(clock_.now());
        return accepted;
    }
    [[nodiscard]] bool onReply(::telemetry::client::Input bytes, quint32 session, quint32 requestId) {
        ownerThread();
        if (!beginSession()) return false;
        const bool consumed = client_.onReply(bytes, session, requestId);
        process(clock_.now());
        return consumed;
    }

private:
    struct Pending { Handle handle; quint32 session; bool handedOff = false; };
    typename QHash<QString, Pending>::iterator active() {
        const auto handle = client_.activeHandle();
        if (!handle) return pending_.end();
        for (auto it = pending_.begin(); it != pending_.end(); ++it)
            if (it->handle == handle) return it;
        return pending_.end();
    }
    void ownerThread() const {
        assert(::QThread::currentThread() == thread());
        assert(!api_ || api_->thread() == thread());
    }
    static QString kind(Status status) {
        switch (status) {
        case Status::OutcomeUnknown: return QStringLiteral("OutcomeUnknown");
        case Status::Timeout: return QStringLiteral("Timeout");
        case Status::Cancelled: return QStringLiteral("Cancelled");
        case Status::MalformedReply: return QStringLiteral("Protocol");
        case Status::CodecError: return QStringLiteral("Protocol");
        case Status::QueueFull: return QStringLiteral("QueueFull");
        case Status::PayloadFull: return QStringLiteral("PayloadFull");
        case Status::RecoveryRequired: return QStringLiteral("RecoveryRequired");
        default: return QStringLiteral("Transport");
        }
    }
    void fail(const QString& token, quint32 session, Status status) {
        const auto api = api_;
        const auto error = kind(status);
        if (api) QMetaObject::invokeMethod(api, [api, token, session, error] {
            if (api) api->fail(token, session, error, QStringLiteral("DeviceClient refused or ended request"));
        }, Qt::QueuedConnection);
    }
    void finished(const QString& token, quint32 session, const ::telemetry::client::ResultView& result) {
        // Retain a terminal token until the next owner process cleans it up.
        // A repeated requestReady must never enqueue it again.
        auto found = pending_.find(token);
        auto status = result.status;
        // Qt's monotonic deadline may expire before its timer is delivered,
        // while a transport clock/process tick has not advanced. Cancellation
        // of that inactive token must not steal the API's Timeout result.
        if (status == Status::Cancelled && !closed_ && api_ && api_->ready() &&
            api_->session() == session && !api_->requestActive(token, session)) {
            const bool mayExecute = found != pending_.end() && found->handedOff;
            status = mayExecute && result.operation != ::telemetry::client::Operation::FieldRead
                         ? Status::OutcomeUnknown : Status::Timeout;
        }
        if (found != pending_.end()) found->handle = {};
        if (status != Status::Ok) { fail(token, session, status); return; }
        const auto api = api_;
        const QByteArray payload{reinterpret_cast<const char*>(result.payload.data()),
                                 static_cast<qsizetype>(result.payload.size())};
        const int dispatch = result.dispatch, endpoint = result.endpointStatus;
        // We already share Api's owner thread. Fix the checked result now;
        // complete() itself queues all public notifications/cache side effects.
        if (api) api->complete(token, session, dispatch, endpoint, payload);
    }
    static bool metadata(int operation, quint32 id, quint32 requestType, quint32 responseType,
                         qsizetype inputBytes, quint32& responseBytes) noexcept {
        (void)operation; (void)id; (void)requestType; (void)responseType;
        (void)inputBytes; (void)responseBytes;
@METADATA@
        return false;
    }
    void submit(const QString& token, quint32 session, int operation, quint32 id,
                quint32 requestType, quint32 responseType, const QByteArray& payload) {
        ownerThread();
        if (!api_ || !api_->requestActive(token, session)) return;
        if (pending_.contains(token)) return;
        if (!beginSession()) {
            fail(token, session, backend_.recoveryRequired() ? Status::RecoveryRequired : Status::NotAdmitted);
            return;
        }
        quint32 responseBytes = 0;
        if (!metadata(operation, id, requestType, responseType, payload.size(), responseBytes)) {
            fail(token, session, Status::MalformedReply); return;
        }
        const auto input = std::as_bytes(std::span{payload.constData(), static_cast<std::size_t>(payload.size())});
        const auto submitted = client_.submitEncoded(
            {static_cast<::telemetry::client::Operation>(operation), id, input, responseBytes},
            [this, token, session](const ::telemetry::client::ResultView& result) noexcept {
                finished(token, session, result);
            }, static_cast<std::uint32_t>(api_->requestTimeoutMs()));
        if (!submitted) { fail(token, session, submitted.status); return; }
        pending_.insert(token, Pending{submitted.handle, session});
    }

    QPointer<Api> api_;
    ::telemetry::client::Backend& backend_;
    ::telemetry::client::Clock clock_;
    Core client_;
    QHash<QString, Pending> pending_;
    quint32 session_ = 0;
    bool closed_ = false, resetQueued_ = false;
};
} // namespace @NS@::qml
@POP@
#endif // Q_MOC_RUN
'''


def generate(schema):
    rows = []
    for endpoint in schema.endpoints:
        if endpoint.category == 1:
            operations = []
            if endpoint.capabilities & 1:
                operations.append((4, 0, endpoint.value_type))
            if endpoint.capabilities & 2:
                operations.append((1, endpoint.value_type, 0))
        elif endpoint.category == 2:
            operations = [(2, endpoint.request_type, 0)]
        else:
            operations = [(3, endpoint.request_type, endpoint.response_type)]
        for operation, request, response in operations:
            input_size = schema.types[request]["wire"] if request else 0
            output_size = schema.types[response]["wire"] if response else 0
            rows.append(f"        if (operation == {operation} && id == {endpoint.id} && requestType == {request} && "
                        f"responseType == {response} && inputBytes == {input_size}) {{ "
                        f"responseBytes = {output_size}; return true; }}")
    text = SOURCE
    for key, value in {"@NS@": schema.namespace, "@PATH@": schema.namespace.replace("::", "/"),
                       "@METADATA@": "\n".join(rows), "@PUSH@": qml_macros.push(schema),
                       "@POP@": qml_macros.pop(schema), "@QT_NAMES@": qml_macros.qt_names(
                           "QByteArray", "QHash", "QMetaObject", "QObject", "QPointer", "QString", "QThread")}.items():
        text = text.replace(key, value)
    return text
