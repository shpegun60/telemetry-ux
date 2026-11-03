"""Emit the optional Qt adapter for the existing typed, asynchronous RpcClient.

The generated QObject API and RpcClient share one owner thread. Packet I/O stays
application owned; this adapter neither blocks for replies nor invents RPC wire.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""

from . import qml_macros


PREFIX = r'''// Generated optional QML adapter for the existing RpcClient. Do not edit.
// SPDX-License-Identifier: MIT
#pragma once

// This template adds no meta-object declarations; keep standalone DTO tokens
// outside moc's parser while preserving the public QObject API it consumes.
#include "DeviceApi.hpp"
#ifndef Q_MOC_RUN
#include <QByteArray>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>
#include <cassert>
#include <memory>
#include <span>

@SHIELD_PUSH@
#include <@PATH@/RpcBindings.hpp>
#include <resource/protocol/Wire.hpp>

namespace @NS@::qml {

// Api, this adapter, the exclusively borrowed client and its buffers use one
// owner thread. The client and buffers must outlive this adapter. Socket/serial
// framing, packet ownership and complete-frame reply pairing remain application
// responsibilities. process() is nonblocking; use a timer/event-loop callback.
template<class Api = DeviceApi>
class RpcAdapter final : public QObject {
    using Client = ::telemetry::client::rpc::RpcClient;
    using Status = ::telemetry::client::rpc::RpcClientStatus;
    using State = ::telemetry::client::rpc::RpcClientState;
    using Packet = ::telemetry::client::rpc::RpcClientPacket;

public:
    explicit RpcAdapter(Api& api, Client& client, QObject* parent = nullptr)
        : QObject(parent), api_(&api), client_(client)
    {
        assert(api.thread() == thread());
        QObject::connect(&api, &Api::requestReady, this,
            [this](QString token, quint32 session, int operation, quint32 id,
                   quint32 requestType, quint32 responseType, QByteArray payload) {
                submit(token, session, operation, id, requestType, responseType, payload);
            }, Qt::QueuedConnection);
        QObject::connect(&api, &Api::sessionChanged, this,
            [this] { (void)beginSession(); }, Qt::QueuedConnection);
        // The native generation is deliberately private. Retained non-idle
        // state requires explicit remote closure before this adapter can reuse
        // it; zero means its previous local generation is unknown here.
        if (client_.state() != State::Idle) {
            if (client_.active()) client_.reset();
            requireRecovery(0);
        }
    }

    ~RpcAdapter() override
    {
        // The exclusively borrowed client/buffers still exist by contract.
        // Queued callbacks capture only Api's guarded pointer, never this.
        loseSession(session_);
    }

    [[nodiscard]] bool recoveryRequired() const noexcept { return recovery_; }
    [[nodiscard]] quint32 recoveryGeneration() const noexcept { return recoveryGeneration_; }

    // The application has excluded/drained old frames and released the remote
    // session (for RpcSessionPool: release/drain, then acquire a fresh handle).
    // This confirmation only removes a gate. Admit a fresh header separately;
    // it never replays an invocation or asserts that its old effect was undone.
    [[nodiscard]] bool confirmSessionClosed(quint32 generation)
    {
        ownerThread();
        if (!recovery_ || generation != recoveryGeneration_ || client_.active()) return false;
        recovery_ = false;
        recoveryGeneration_ = 0;
        return true;
    }

    // Opt in before requests. In this mode prepareHandoff() MUST be called
    // before the transport may accept a complete packet, not after sending it.
    [[nodiscard]] bool enableHandoffTracking()
    {
        ownerThread();
        if (!api_ || !token_.isEmpty() || !api_->setTransportHandoffTracking(true)) return false;
        trackedHandoff_ = true;
        return true;
    }

    // A local generation alone does not reopen/reset a remote mailbox. Once a
    // previous admitted native session ends, explicit closure is mandatory.
    [[nodiscard]] Status beginSession()
    {
        ownerThread();
        cancelInactive();
        if (recovery_) return Status::NoSession;
        const bool ready = api_ && api_->ready();
        const auto next = ready ? api_->session() : 0;
        if (session_ != 0 && session_ != next) {
            const auto previous = session_;
            client_.loseSession(previous);
            settle();
            requireRecovery(previous);
            return Status::NoSession;
        }
        if (!ready)
            return Status::NoSession;
        if (session_ == next)
            return Status::Ok;
        const auto status = client_.startSession(next);
        if (status == Status::Ok)
            session_ = next;
        return status;
    }

    void process(quint32 nowMs)
    {
        ownerThread();
        if (beginSession() != Status::Ok) return;
        client_.process(nowMs);
        settle();
    }

    [[nodiscard]] Packet nextPacket()
    {
        ownerThread();
        if (beginSession() != Status::Ok) return {};
        auto packet = client_.nextPacket();
        settle();
        return packet;
    }

    [[nodiscard]] bool prepareHandoff(quint32 generation, quint32 requestId)
    {
        ownerThread();
        if (beginSession() != Status::Ok || token_.isEmpty()) return false;
        const auto packet = client_.nextPacket();
        if (!packet || packet.generation != generation || packet.requestId != requestId) return false;
        if (!api_ || !api_->requestActive(token_, tokenSession_)) return false;
        if (finalWrite(packet) && !finalPrepared_) {
            if (trackedHandoff_ && !api_->transportHandoffAccepted(token_, tokenSession_)) return false;
            // Monotonic within this invocation, including identical retries.
            // Recovery must also honor the interval before onSendAccepted().
            finalPrepared_ = true;
        }
        return true;
    }

    [[nodiscard]] bool onSendAccepted(quint32 generation, quint32 requestId)
    {
        ownerThread();
        if (recovery_) return false;
        const auto packet = client_.nextPacket();
        if (!packet || packet.generation != generation || packet.requestId != requestId) return false;
        const bool missingGate = trackedHandoff_ && finalWrite(packet) && !finalPrepared_;
        // Actual handoff already happened: record it before a deadline/session
        // check can cancel the native request and discard that evidence.
        const bool accepted = client_.onSendAccepted(generation, requestId);
        if (!accepted) return false;
        if (finalWrite(packet)) finalPrepared_ = true;
        if (missingGate) {
            client_.loseSession(generation);
            settle();
            requireRecovery(generation);
            return false; // Contract violation; sending cannot be undone here.
        }
        (void)beginSession();
        settle();
        return accepted;
    }

    [[nodiscard]] bool onReply(::resource::Input reply, quint32 generation, quint32 requestId)
    {
        ownerThread();
        if (beginSession() != Status::Ok) return false;
        const bool consumed = client_.onReply(reply, generation, requestId);
        settle();
        return consumed;
    }

    // Deliver the exact terminal evidence before invalidating the admitted API.
    // After final handoff, loss may mean OutcomeUnknown rather than Cancelled.
    void loseSession(quint32 generation)
    {
        ownerThread();
        if (generation != session_ || session_ == 0)
            return;
        client_.loseSession(generation);
        settle();
        requireRecovery(generation);
    }

private:
    void ownerThread() const
    {
        assert(::QThread::currentThread() == thread());
        assert(!api_ || api_->thread() == thread());
    }

    void cancelInactive()
    {
        if (!token_.isEmpty() && (!api_ || !api_->requestActive(token_, tokenSession_))) {
            // Api has ended this invocation (or its deadline elapsed). It owns
            // the timeout/reset result. Stop future handoffs and retain the
            // native Cancelled/OutcomeUnknown diagnostic without a replay.
            // If expiry has occurred but Api's timer has not run, settle its
            // correct deadline outcome before the queued admission reset.
            const bool unknown = operation_ != 4 && (!trackedHandoff_ || finalPrepared_);
            fail(token_, tokenSession_, unknown ? QStringLiteral("OutcomeUnknown") : QStringLiteral("Timeout"),
                 QStringLiteral("Request ended before another packet handoff; no replay"));
            client_.reset();
            token_.clear();
            if (finalPrepared_ || client_.status() == Status::OutcomeUnknown)
                requireRecovery(session_);
        }
    }

    static bool finalWrite(const Packet& packet) noexcept
    {
        return packet.bytes.size() >= ::resource::protocol::wire::writeRequestHeaderSize &&
               packet.bytes[0] == std::byte(::resource::protocol::Op::Write) &&
               packet.bytes[13] == std::byte{1};
    }

    void requireRecovery(quint32 generation)
    {
        if (recovery_) return;
        recovery_ = true;
        recoveryGeneration_ = generation;
        session_ = 0;
        if (generation != 0) client_.loseSession(generation);
        const auto api = api_;
        const auto epoch = api ? api->session() : 0;
        // Terminal failure was queued first by settle()/cancelInactive().
        // A later re-admission cannot clear this adapter's closure obligation.
        if (api)
            QMetaObject::invokeMethod(api, [api, epoch] {
                if (api && api->ready() && api->session() == epoch) api->resetSession();
            }, Qt::QueuedConnection);
    }

    static QString failureKind(Status status)
    {
        switch (status) {
        case Status::OutcomeUnknown: return QStringLiteral("OutcomeUnknown");
        case Status::Cancelled: return QStringLiteral("Cancelled");
        case Status::Timeout: return QStringLiteral("Timeout");
        case Status::MalformedReply: return QStringLiteral("Protocol");
        default: return QStringLiteral("Transport");
        }
    }

    void fail(const QString& token, quint32 session, const QString& kind, const QString& message)
    {
        const auto api = api_;
        if (api)
            QMetaObject::invokeMethod(api, [api, token, session, kind, message] {
                if (api)
                    api->fail(token, session, kind, message);
            }, Qt::QueuedConnection);
    }

    void started(const QString& token, quint32 session, int operation, Status status)
    {
        if (status != Status::Ok) {
            fail(token, session, failureKind(status),
                 QStringLiteral("RpcClient begin status=%1").arg(static_cast<int>(status)));
            return;
        }
        token_ = token;
        tokenSession_ = session;
        operation_ = operation;
        finalPrepared_ = false;
    }

    template<class Q, class Start>
    void value(const QString& token, quint32 session, int operation,
               const QByteArray& payload, Start start)
    {
        if (static_cast<std::size_t>(payload.size()) != ::@NS@::codec::wireSize<Q>) {
            fail(token, session, QStringLiteral("Protocol"), QStringLiteral("Request length mismatch"));
            return;
        }
        // Qt desktop ownership: large DTOs never become an automatic temporary.
        const std::unique_ptr<Q> request{new Q};
        const auto input = std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(payload.constData()),
            static_cast<std::size_t>(payload.size())};
        const auto decoded = ::@NS@::codec::decode(input, *request);
        if (decoded != ::@NS@::codec::Status::Ok) {
            fail(token, session, QStringLiteral("Protocol"), QStringLiteral("Invalid request payload"));
            return;
        }
        started(token, session, operation, start(*request));
    }

    void settle()
    {
        if (token_.isEmpty() || client_.active())
            return;
        const QString token = token_;
        const quint32 session = tokenSession_;
        token_.clear();
        const auto diagnostic = client_.diagnostic();
        if (!client_.completed() || diagnostic.status != Status::Ok) {
            const auto kind = operation_ == 4 && diagnostic.status == Status::OutcomeUnknown ? failureKind(diagnostic.cause) :
                finalPrepared_ && operation_ != 4 ? QStringLiteral("OutcomeUnknown") : failureKind(diagnostic.status);
            fail(token, session, kind,
                 QStringLiteral("RpcClient status=%1 cause=%2 resource=%3 codec=%4")
                     .arg(static_cast<int>(diagnostic.status)).arg(static_cast<int>(diagnostic.cause))
                     .arg(static_cast<int>(diagnostic.resourceStatus)).arg(static_cast<int>(diagnostic.codec)));
            if (finalPrepared_ || diagnostic.status == Status::OutcomeUnknown)
                requireRecovery(session);
            return;
        }
        if (static_cast<int>(diagnostic.operation) != operation_) {
            fail(token, session, QStringLiteral("Protocol"), QStringLiteral("Completion operation mismatch"));
            return;
        }
        // RpcClient has already validated the complete response, declared wire
        // length, operation, ID and family status. Api validates the payload's
        // generated DTO only after its own status/token/session checks.
        const auto bytes = client_.resultBytes();
        const QByteArray payload{reinterpret_cast<const char*>(bytes.data()),
                                 static_cast<qsizetype>(bytes.size())};
        const auto api = api_;
        const int dispatch = static_cast<int>(diagnostic.dispatch);
        const int endpoint = diagnostic.endpointStatus;
        // Same owner thread: settle the checked result before a later reset.
        // Api::complete queues public notifications/cache side effects itself.
        if (api)
            api->complete(token, session, dispatch, endpoint, payload);
    }

    void submit(const QString& token, quint32 session, int operation, quint32 id,
                quint32 requestType, quint32 responseType, const QByteArray& payload)
    {
        ownerThread();
        // Empty schemas still emit a usable adapter and compile with -Werror.
        (void)operation; (void)id; (void)requestType; (void)responseType; (void)payload;
        if (!api_ || !api_->requestActive(token, session)) {
            // Api owns deadline/reset completion. A queued transport callback
            // can precede its expiry timer even though the deadline elapsed;
            // do not replace Timeout/OutcomeUnknown with a competing failure.
            return;
        }
        if (token == token_ && session == tokenSession_)
            return; // Repeated delivery is never an instruction to execute again.
        if (!token_.isEmpty()) {
            fail(token, session, QStringLiteral("Transport"), QStringLiteral("RpcClient is Busy"));
            return;
        }
        const auto ready = beginSession();
        if (ready != Status::Ok) {
            fail(token, session, failureKind(ready),
                 QStringLiteral("RpcClient session status=%1").arg(static_cast<int>(ready)));
            return;
        }
'''


SUFFIX = r'''
        fail(token, session, QStringLiteral("Protocol"), QStringLiteral("Unknown generated endpoint or operation"));
    }

    QPointer<Api> api_;
    Client& client_;
    quint32 session_ = 0;
    QString token_;
    quint32 tokenSession_ = 0;
    int operation_ = 0;
    bool trackedHandoff_ = false, finalPrepared_ = false, recovery_ = false;
    quint32 recoveryGeneration_ = 0;
};
} // namespace @NS@::qml
'''


def generate(schema):
    """Return one optional header; RPC is not included by the main QML module."""
    ns = schema.namespace
    rows = [PREFIX.replace("@NS@", ns).replace("@PATH@", ns.replace("::", "/"))
            .replace("@SHIELD_PUSH@", qml_macros.push(schema))]
    for endpoint in schema.endpoints:
        category = ("", "fields", "commands", "services")[endpoint.category]
        name = f"::{ns}::{endpoint.group}::{category}::{endpoint.symbol}"
        if endpoint.category == 1:
            operations = ((4, 0, endpoint.value_type, "beginReadField"),
                          (1, endpoint.value_type, 0, "beginWriteField"))
        elif endpoint.category == 2:
            operations = ((2, endpoint.request_type, 0, "beginCommand"),)
        else:
            operations = ((3, endpoint.request_type, endpoint.response_type, "beginService"),)
        for operation, request, response, method in operations:
            rows.append(f"        if (operation == {operation} && id == {endpoint.id}) {{\n")
            condition = f"requestType != {request} || responseType != {response}"
            if request == 0:
                condition += " || !payload.isEmpty()"
            rows.append(f"            if ({condition}) {{\n"
                        '                fail(token, session, QStringLiteral("Protocol"), QStringLiteral("Endpoint type metadata mismatch"));\n'
                        "                return;\n            }\n")
            binding = f"::{ns}::rpc::binding({name}, ::telemetry::client::rpc::trustedRpcMapping)"
            if request == 0:
                rows.append(f"            started(token, session, operation, client_.{method}({binding}));\n")
            else:
                typename = f"::{ns}::detail::Type_{request}"
                rows.append(f"            value<{typename}>(token, session, operation, payload,\n"
                            f"                [this](const {typename}& request) {{ return client_.{method}({binding}, request); }});\n")
            rows.append("            return;\n        }\n")
    rows.append(SUFFIX.replace("@NS@", ns))
    rows.append(qml_macros.pop(schema) + "\n#endif // Q_MOC_RUN\n")
    return "".join(rows)
