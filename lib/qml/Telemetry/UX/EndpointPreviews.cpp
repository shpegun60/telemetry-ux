// Owned tree cache. Native callbacks only fill a detached completion slot. MIT.
#include "EndpointPreviews.hpp"
#include "SchemaText.hpp"
#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QThread>
#include <algorithm>
#include <limits>

namespace telemetry::ux {
struct EndpointPreviews::Impl {
    struct Entry {
        QByteArray bytes;
        QString state = QStringLiteral("Not read"), reason;
        qint64 received = -1, attempted = -refreshMs;
        quint64 demand = 0;
        bool ready = false;
    };
    struct Lease {
        QPointer<QObject> owner;
        QList<quint32> ids;
        QMetaObject::Connection destroyed;
    };
    struct Pending {
        quint32 id = 0;
        quint64 epoch = 0, demand = 0;
        EndpointPreviewSource::Completion done;
        // Retained even if admission changes before the callback drains.
        std::shared_ptr<void> backing;
    };
    QPointer<EndpointPreviews> q;
    EndpointPreviewSource source;
    QElapsedTimer elapsed;
    Clock now;
    QList<Lease> leases;
    QList<quint32> demanded;
    QHash<quint32, Entry> entries;
    QHash<quint32, runtime::RuntimeEndpoint> fields;
    std::optional<Pending> pending;
    quint64 epoch = 0, serial = 0;
    qint64 nextRead = 0, ageTick = -1, invalidatedNs = 0;
    int turn = 0;
    bool polling = false;

    bool current() const {
        return source.current && source.current() && source.schema && source.schema().valid();
    }
    runtime::RuntimeSchema schema() const { return current() ? source.schema() : runtime::RuntimeSchema{}; }
    int bytes() const {
        int n = 0;
        for (const auto& entry : entries) n += int(entry.bytes.size());
        return n;
    }
    bool eligible(quint32 id) const {
        const auto* field = fieldById(id);
        return field && (field->capabilities & quint8(descriptor::Capability::Readable)) != 0;
    }
    const runtime::RuntimeEndpoint* fieldById(quint32 id) const {
        if (!current()) return nullptr;
        const auto it = fields.constFind(id);
        return it == fields.cend() ? nullptr : &it.value();
    }
    void collect() {
        QList<quint32> ids;
        for (const auto& lease : leases) {
            if (!lease.owner) continue;
            for (auto id : lease.ids)
                if (!ids.contains(id)) ids.append(id);
        }
        for (auto id : ids) {
            if (demanded.contains(id)) continue;
            if (auto it = entries.find(id); it != entries.end()) {
                it->demand = ++serial;
                if (it->state == QStringLiteral("Reading")) {
                    it->state = it->ready ? QStringLiteral("Stale") : QStringLiteral("Queued");
                    it->attempted = now() - refreshMs;
                }
            }
        }
        demanded = std::move(ids);
    }
    Entry* ensure(quint32 id, int reserve = 0) {
        auto found = entries.find(id);
        const int previous = found == entries.end() ? 0 : int(found->bytes.size());
        while ((found == entries.end() && entries.size() >= fieldLimit) ||
               bytes() - previous + reserve > payloadLimit) {
            auto oldest = entries.end();
            for (auto it = entries.begin(); it != entries.end(); ++it)
                if (it.key() != id && !demanded.contains(it.key()) &&
                    (oldest == entries.end() || it->received < oldest->received)) oldest = it;
            if (oldest == entries.end()) return nullptr;
            entries.erase(oldest);
            found = entries.find(id);
        }
        if (found == entries.end()) {
            Entry entry;
            entry.demand = ++serial;
            found = entries.insert(id, std::move(entry));
        }
        return &found.value();
    }
    void invalidate() {
        ++epoch;
        invalidatedNs = WatchTimestamp::now().monotonicNs;
        for (auto& entry : entries) {
            entry.bytes.clear(); entry.ready = false; entry.received = -1;
            entry.state = QStringLiteral("Not read"); entry.reason.clear();
            entry.attempted = now() - refreshMs;
        }
        nextRead = std::max(nextRead, now());
    }
};

EndpointPreviews::EndpointPreviews(QObject* parent, Clock clock)
    : QObject(parent), d(std::make_shared<Impl>()) {
    d->q = this; d->elapsed.start();
    if (clock) d->now = std::move(clock);
    else {
        const auto timer = d->elapsed;
        d->now = [timer] { return timer.elapsed(); };
    }
}
EndpointPreviews::~EndpointPreviews() {
    d->q.clear();
    for (auto& lease : d->leases) QObject::disconnect(lease.destroyed);
}
void EndpointPreviews::setSource(EndpointPreviewSource source) {
    auto p = d;
    p->invalidate();
    for (auto& lease : p->leases) QObject::disconnect(lease.destroyed);
    p->leases.clear(); p->demanded.clear(); p->entries.clear(); p->fields.clear();
    p->source = std::move(source); p->turn = 0;
    if (p->current()) for (const auto& field : p->schema().endpoints(descriptor::Category::Field))
        p->fields.insert(field.id, field);
    Q_EMIT sourceChanged();
    if (p->q) Q_EMIT p->q->changed();
}
void EndpointPreviews::retire() { setSource({}); }
void EndpointPreviews::invalidateValues() {
    auto p = d; p->invalidate(); Q_EMIT changed();
}
bool EndpointPreviews::current() const { return d->current(); }
runtime::RuntimeSchema EndpointPreviews::schema() const { return d->schema(); }
bool EndpointPreviews::setDemand(QObject* view, const QList<quint32>& ids) {
    auto p = d;
    if (!view || view->thread() != thread() || ids.size() > demandLimit) return false;
    QList<quint32> accepted;
    if (p->current()) for (auto id : ids)
        if (p->eligible(id) && !accepted.contains(id)) accepted.append(id);
    auto lease = std::find_if(p->leases.begin(), p->leases.end(), [view](const auto& l) { return l.owner == view; });
    if (lease == p->leases.end()) {
        if (accepted.isEmpty()) return true;
        if (p->leases.size() >= leaseLimit) return false;
        Impl::Lease added;
        added.owner = view;
        added.destroyed = QObject::connect(view, &QObject::destroyed, this, [this, view] { clearDemand(view); });
        p->leases.append(std::move(added)); lease = std::prev(p->leases.end());
    }
    if (lease->ids == accepted) return true;
    lease->ids = std::move(accepted);
    p->collect();
    Q_EMIT changed();
    return true;
}
void EndpointPreviews::clearDemand(QObject* view) {
    auto p = d;
    const auto count = p->leases.size();
    p->leases.removeIf([view](auto& lease) {
        if (lease.owner && lease.owner != view) return false;
        QObject::disconnect(lease.destroyed); return true;
    });
    p->collect();
    if (count != p->leases.size()) Q_EMIT changed();
}

QVariantMap EndpointPreviews::info(quint32 id) const {
    auto p = d;
    QVariantMap out{{QStringLiteral("state"), QStringLiteral("Not read")},
                    {QStringLiteral("reason"), QString{}}, {QStringLiteral("ageMs"), -1},
                    {QStringLiteral("hasValue"), false}};
    if (!p->current()) { out[QStringLiteral("state")] = QStringLiteral("Stale"); return out; }
    const auto* field = p->fieldById(id);
    const auto* type = field ? p->schema().type(field->typeId) : nullptr;
    if (!type || !p->eligible(id)) { out[QStringLiteral("state")] = QStringLiteral("Not readable"); return out; }
    if (type->wireBytes > fieldBytesLimit) {
        out[QStringLiteral("state")] = QStringLiteral("Open Details");
        out[QStringLiteral("reason")] = tr("Value exceeds the 8 KiB tree preview limit."); return out;
    }
    const auto it = p->entries.constFind(id);
    if (it == p->entries.cend()) {
        if (p->demanded.contains(id)) out[QStringLiteral("state")] =
            p->entries.size() >= fieldLimit ? QStringLiteral("Preview limit") : QStringLiteral("Queued");
        return out;
    }
    auto state = it->state;
    const qint64 age = it->received < 0 ? -1 : std::max(qint64{0}, p->now() - it->received);
    if (state == QStringLiteral("Current") && age >= refreshMs) state = QStringLiteral("Stale");
    out[QStringLiteral("state")] = state;
    out[QStringLiteral("reason")] = it->reason;
    out[QStringLiteral("ageMs")] = age;
    out[QStringLiteral("hasValue")] = it->ready;
    return out;
}
QString EndpointPreviews::value(quint32 id, TypeId typeId, quint32 offset) const {
    auto p = d;
    if (!p->current()) return {};
    const auto it = p->entries.constFind(id);
    if (it == p->entries.cend() || !it->ready) return {};
    const auto schema = p->schema();
    const auto* type = schema.type(typeId);
    if (!type || offset > quint32(it->bytes.size()) || type->wireBytes > quint32(it->bytes.size()) - offset) return {};
    if (type->kind == TypeKind::Struct) return type->memberCount == 1 ? tr("1 member") : tr("%1 members").arg(type->memberCount);
    if (type->kind == TypeKind::Array) return tr("%1 elements").arg(type->elementCount);
    const runtime::Input bytes{reinterpret_cast<const std::byte*>(it->bytes.constData()) + offset, type->wireBytes};
    const auto view = runtime::DynamicValueView::create(schema, typeId, bytes);
    return view ? schema_text::valueText(schema, view.value).text : QString{};
}
int EndpointPreviews::cachedFields() const { return int(d->entries.size()); }
int EndpointPreviews::cachedBytes() const { return d->bytes(); }
bool EndpointPreviews::pending() const { return d->pending.has_value(); }

void EndpointPreviews::poll() {
    auto p = d;
    if (p->polling) return;
    p->polling = true;
    bool tell = false;
    if (p->pending && *p->pending->done) {
        auto pending = std::move(*p->pending); p->pending.reset();
        auto found = p->entries.find(pending.id);
        if (pending.epoch == p->epoch && p->current() && p->demanded.contains(pending.id) &&
            found != p->entries.end() && found->demand == pending.demand) {
            auto result = std::move(**pending.done);
            found->ready = false; found->bytes.clear(); found->reason = result.reason;
            found->state = QStringLiteral("Unavailable");
            if (result.status == client::Status::Ok && result.hasValue) {
                const auto schema = p->schema();
                const auto* field = p->fieldById(pending.id);
                const runtime::Input bytes{reinterpret_cast<const std::byte*>(result.bytes.constData()), std::size_t(result.bytes.size())};
                if (field && result.bytes.size() <= fieldBytesLimit &&
                    runtime::DynamicValueView::create(schema, field->typeId, bytes)) {
                    if (auto* entry = p->ensure(pending.id, int(result.bytes.size()))) {
                        entry->bytes = std::move(result.bytes); entry->ready = true;
                        entry->state = QStringLiteral("Current"); entry->received = p->now();
                    } else found->state = QStringLiteral("Preview limit");
                } else found->reason = tr("Invalid preview payload.");
            }
            tell = true;
        } else if (found != p->entries.end() && found->state == QStringLiteral("Reading")) {
            found->state = found->ready ? QStringLiteral("Stale") : QStringLiteral("Not read");
            found->attempted = p->now() - refreshMs;
            tell = true;
        }
    }
    const auto now = p->now();
    if (p->current() && !p->pending && now >= p->nextRead && !p->demanded.isEmpty() &&
        p->source.read && (!p->source.busy || !p->source.busy())) {
        // Even an all-watched/limited demand set is scanned at most twice/s.
        p->nextRead = now + spacingMs;
        const auto ids = p->demanded;
        for (int n = 0; n < ids.size(); ++n) {
            const auto id = ids[(p->turn + n) % ids.size()];
            if (p->source.watched && p->source.watched(id)) continue;
            const auto schema = p->schema();
            const auto* field = p->fieldById(id);
            const auto* type = field ? schema.type(field->typeId) : nullptr;
            if (!type || type->wireBytes > fieldBytesLimit) continue;
            auto* entry = p->ensure(id, int(type->wireBytes));
            if (!entry || now - entry->attempted < refreshMs) continue;
            entry->attempted = now; entry->state = QStringLiteral("Reading");
            auto done = std::make_shared<std::optional<EndpointPreviewResult>>();
            p->pending = Impl::Pending{id, p->epoch, entry->demand, done, p->source.backing};
            const auto epoch = p->epoch;
            const auto submitted = p->source.read(id, done);
            // read() may synchronously stage completion, retire, or delete q.
            if (p->epoch != epoch || !p->q) break;
            if (!submitted) {
                p->pending.reset();
                entry = p->ensure(id);
                if (entry) {
                    entry->state = entry->ready ? QStringLiteral("Stale") : QStringLiteral("Queued");
                    entry->reason = tr("Waiting for the session queue.");
                    entry->attempted = now - refreshMs;
                }
            }
            p->turn = (p->turn + n + 1) % ids.size(); tell = true; break;
        }
    }
    if (p->current() && !p->demanded.isEmpty() && now / 1000 != p->ageTick) {
        p->ageTick = now / 1000; tell = true;
    }
    p->polling = false;
    if (tell && p->q) Q_EMIT p->q->changed();
}

void EndpointPreviews::ingest(const WatchFrame& frame) {
    auto p = d;
    if (!p->current() || frame.phase != WatchCompletionPhase::Current || !frame.correlated || !frame.exact ||
        frame.native.client != 0 || frame.native.dispatch != 0 || frame.native.endpoint != 0 ||
        frame.native.codec != 0 || frame.native.value != 0 || frame.presentationStatus != 0 ||
        frame.source.nativeSession != (p->source.session ? p->source.session() : 0) ||
        frame.source.fingerprint != p->schema().fingerprint() ||
        frame.source.schemaGeneration != p->schema().generation() ||
        frame.startedNs < p->invalidatedNs || frame.count < 0 || frame.count > int(frame.rows.size())) return;
    bool tell = false;
    for (int n = 0; n < frame.count; ++n) {
        const auto& row = frame.rows[n];
        if (!p->demanded.contains(row.id)) continue;
        const auto s = p->schema();
        const auto* field = p->fieldById(row.id);
        if (!field || field->typeId != row.value.type.typeId) continue;
        if (auto* entry = p->ensure(row.id, row.value.size)) {
            entry->bytes.clear(); entry->ready = false; entry->reason.clear();
            entry->state = QStringLiteral("Unavailable"); entry->attempted = p->now();
            const runtime::Input bytes{row.value.bytes.data(), row.value.size};
            if (row.itemStatus == 0 && row.valueStatus == 0 && row.value.size &&
                row.value.type.valid() && row.value.size == row.value.type.width &&
                runtime::DynamicValueView::create(s, field->typeId, bytes)) {
                entry->bytes = QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
                entry->ready = true; entry->state = QStringLiteral("Current"); entry->received = p->now();
            }
            tell = true;
        }
    }
    if (tell && p->q) Q_EMIT p->q->changed();
}
} // namespace telemetry::ux
