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
        qint64 received = -1;
        quint64 demand = 0;
        bool ready = false;
    };
    struct Lease {
        QPointer<QObject> owner;
        QList<quint32> ids;
        QMetaObject::Connection destroyed;
    };
    // One request of a cycle: its Fields and its completion slot.
    struct Pending {
        quint64 epoch = 0;
        std::vector<quint32> ids;
        EndpointPreviewSource::Completion one;
        EndpointPreviewSource::ManyCompletion many;
        // Retained even if admission changes before the callback drains.
        std::shared_ptr<void> backing;
        bool ready() const { return (one && *one) || (many && *many); }
    };
    // The Fields in view read together; their values land when the last
    // portion is in.
    struct Cycle {
        std::vector<read_portions::Portion> portions;
        int cursor = 0;
        quint64 epoch = 0;
        std::vector<quint32> ids;
        QHash<quint32, EndpointPreviewResult> results;
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
    std::optional<Cycle> cycle;
    quint64 epoch = 0, serial = 0;
    qint64 landed = 0, ageTick = -1, invalidatedNs = 0;
    bool landedOnce = false, readSoon = true;
    int intervalMs = intervalDefaultMs;
    bool polling = false;

    int staleMs() const { return std::max(staleFloorMs, 3 * intervalMs); }
    static EndpointPreviewResult failed(client::Status status, QString reason)
    {
        EndpointPreviewResult out;
        out.status = status;
        out.reason = std::move(reason);
        return out;
    }

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
            if (auto it = entries.find(id); it != entries.end())
                it->demand = ++serial;
        }
        // A Field in view with no value yet is read soon, not an interval later.
        for (auto id : ids)
            if (const auto it = entries.constFind(id); it == entries.cend() || !it->ready)
                readSoon = true;
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
    // Values changed under the cache (a write, a new session): the cycle in
    // hand is dropped, and the next comes at its interval, so a run of writes
    // does not turn into a run of reads.
    void invalidate() {
        ++epoch;
        cycle.reset();
        landed = now();
        invalidatedNs = WatchTimestamp::now().monotonicNs;
        for (auto& entry : entries) {
            entry.bytes.clear(); entry.ready = false; entry.received = -1;
            entry.state = QStringLiteral("Not read"); entry.reason.clear();
        }
    }
    // The Fields in view to read this cycle: readable, not in the Watchlist
    // (it supplies them), small enough for a preview, at most fieldLimit.
    std::vector<quint32> cycleIds() {
        std::vector<quint32> ids;
        const auto schema = this->schema();
        for (auto id : demanded) {
            if (int(ids.size()) >= fieldLimit) break;
            if (source.watched && source.watched(id)) continue;
            const auto* field = fieldById(id);
            const auto* type = field ? schema.type(field->typeId) : nullptr;
            if (!type || type->wireBytes > fieldBytesLimit) continue;
            ids.push_back(id);
        }
        return ids;
    }
    void startCycle() {
        auto ids = cycleIds();
        readSoon = false;
        if (ids.empty()) {
            landed = now(); landedOnce = true;
            return;
        }
        const auto schema = this->schema();
        const auto bytesOf = [this, schema](quint32 id) -> std::optional<unsigned> {
            const auto* field = fieldById(id);
            const auto* type = field ? schema.type(field->typeId) : nullptr;
            if (!type) return std::nullopt;
            return type->wireBytes;
        };
        read_portions::Source can;
        can.wholeReads = true;
        can.readMany = bool(source.readMany);
        can.readField = bool(source.read);
        can.readBatch = bool(source.readBatch);
        can.budget = source.readManyBudget;
        can.batchBudget = source.batchBudget;
        can.fieldBytes = [schema, this](quint32 id) -> std::optional<unsigned> {
            const auto* field = fieldById(id);
            const auto* type = field ? schema.type(field->typeId) : nullptr;
            if (!type || (type->kind != TypeKind::Scalar && type->kind != TypeKind::Enum) || type->wireBytes > 8)
                return std::nullopt;
            return type->wireBytes;
        };
        can.valueBytes = bytesOf;
        std::vector<WatchKey> keys;
        for (auto id : ids) keys.push_back(watch_key::whole(id));
        Cycle next;
        next.portions = read_portions::plan(keys, can).portions;
        next.epoch = epoch;
        next.ids = std::move(ids);
        for (auto id : next.ids)
            if (auto* entry = ensure(id))
                if (!entry->ready) entry->state = QStringLiteral("Reading");
        cycle = std::move(next);
    }
    // The next portion, while the session is idle; false when nothing went out.
    bool submitNext() {
        while (cycle && cycle->cursor < int(cycle->portions.size())) {
            if (source.busy && source.busy()) return false;
            const auto& step = cycle->portions[std::size_t(cycle->cursor)];
            Pending next;
            next.epoch = epoch;
            next.backing = source.backing;
            client::Submission submitted{client::Status::InvalidArgument, {}};
            const auto before = epoch;
            switch (step.kind) {
            case read_portions::Portion::Kind::Many:
                next.ids = step.ids;
                next.many = std::make_shared<std::optional<EndpointPreviewManyResult>>();
                pending = next;
                submitted = source.readMany(next.ids, next.many);
                break;
            case read_portions::Portion::Kind::Batch:
                for (const auto& member : step.fields) next.ids.push_back(member.field);
                next.many = std::make_shared<std::optional<EndpointPreviewManyResult>>();
                pending = next;
                submitted = source.readBatch(next.ids, next.many);
                break;
            case read_portions::Portion::Kind::Members:
            case read_portions::Portion::Kind::Field:
                next.ids = {step.field};
                next.one = std::make_shared<std::optional<EndpointPreviewResult>>();
                pending = next;
                submitted = source.read(step.field, next.one);
                break;
            case read_portions::Portion::Kind::Fail:
                ++cycle->cursor;
                continue;
            }
            // read() may synchronously stage completion, retire, or delete q.
            if (epoch != before || !q) return true;
            if (submitted) return true;
            pending.reset();
            if (submitted.status == client::Status::Busy || submitted.status == client::Status::QueueFull)
                return false; // the same portion on the next progress
            for (auto id : next.ids)
                cycle->results.insert(id, failed(submitted.status, tr("Not read: the request was refused.")));
            ++cycle->cursor;
        }
        return false;
    }
    static QString tr(const char* text) { return EndpointPreviews::tr(text); }
    // The cycle's values, all at once.
    void land() {
        const auto schema = this->schema();
        for (auto id : cycle->ids) {
            if (!demanded.contains(id)) continue;
            auto result = cycle->results.value(id, failed(client::Status::NotAdmitted, tr("Not read this cycle.")));
            auto* entry = ensure(id, int(result.bytes.size()));
            if (!entry) {
                if (auto found = entries.find(id); found != entries.end()) found->state = QStringLiteral("Preview limit");
                continue;
            }
            entry->reason = result.reason;
            const auto* field = fieldById(id);
            const runtime::Input bytes{reinterpret_cast<const std::byte*>(result.bytes.constData()), std::size_t(result.bytes.size())};
            if (result.status == client::Status::Ok && result.hasValue && field && result.bytes.size() <= fieldBytesLimit &&
                runtime::DynamicValueView::create(schema, field->typeId, bytes)) {
                entry->bytes = std::move(result.bytes); entry->ready = true;
                entry->state = QStringLiteral("Current"); entry->received = now();
            } else {
                // The last value stays shown, stale; the reason says why.
                entry->state = entry->ready ? QStringLiteral("Stale") : QStringLiteral("Unavailable");
                if (result.status == client::Status::Ok && result.hasValue)
                    entry->reason = tr("Invalid preview payload.");
            }
        }
        cycle.reset();
        landed = now(); landedOnce = true;
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
    p->source = std::move(source); p->cycle.reset(); p->landedOnce = false; p->readSoon = true;
    // The schema outlives the loop: endpoints() refers into it.
    if (p->current()) {
        const auto schema = p->schema();
        for (const auto& field : schema.endpoints(descriptor::Category::Field))
            p->fields.insert(field.id, field);
    }
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
    if (state == QStringLiteral("Current") && age >= p->staleMs()) state = QStringLiteral("Stale");
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
    const runtime::Input bytes{reinterpret_cast<const std::byte*>(it->bytes.constData()) + offset, type->wireBytes};
    const auto view = runtime::DynamicValueView::create(schema, typeId, bytes);
    if (!view) return {};
    // A struct or array folded onto one line, as values.bin writes it; an
    // array of structs, which has none, by its size.
    if (type->kind == TypeKind::Struct || type->kind == TypeKind::Array) {
        const auto summary = schema_text::summaryText(schema, view.value);
        return !summary.isEmpty() ? summary : tr("%1 elements").arg(type->elementCount);
    }
    return schema_text::valueText(schema, view.value).text;
}
int EndpointPreviews::cachedFields() const { return int(d->entries.size()); }
int EndpointPreviews::cachedBytes() const { return d->bytes(); }
bool EndpointPreviews::pending() const { return d->pending.has_value(); }

int EndpointPreviews::interval() const { return d->intervalMs; }
void EndpointPreviews::setInterval(int ms) {
    d->intervalMs = std::clamp(ms, int(intervalMinimumMs), int(intervalMaximumMs));
}
void EndpointPreviews::readNow() {
    d->readSoon = true;
    poll();
}
void EndpointPreviews::poll() {
    auto p = d;
    if (p->polling) return;
    p->polling = true;
    bool tell = false;
    // A reply in: its Fields' results join the cycle's.
    if (p->pending && p->pending->ready()) {
        auto pending = std::move(*p->pending); p->pending.reset();
        if (p->cycle && pending.epoch == p->epoch && p->cycle->epoch == p->epoch) {
            if (pending.one) {
                p->cycle->results.insert(pending.ids.front(), std::move(**pending.one));
            } else {
                auto result = std::move(**pending.many);
                if (result.status == client::Status::Ok && result.items.size() == pending.ids.size()) {
                    for (std::size_t i = 0; i < result.items.size(); ++i) {
                        auto& [id, item] = result.items[i];
                        p->cycle->results.insert(id == pending.ids[i] ? id : pending.ids[i],
                                                 id == pending.ids[i] ? std::move(item)
                                                                      : Impl::failed(client::Status::MalformedReply, tr("The reply named another Field.")));
                    }
                } else {
                    for (auto id : pending.ids)
                        p->cycle->results.insert(id, Impl::failed(result.status == client::Status::Ok ? client::Status::MalformedReply : result.status,
                                                                 tr("Not read: the request failed.")));
                }
            }
            ++p->cycle->cursor;
        }
    }
    const auto now = p->now();
    if (p->current() && !p->pending) {
        if (p->cycle && p->cycle->epoch != p->epoch) p->cycle.reset();
        if (p->cycle && p->cycle->cursor >= int(p->cycle->portions.size())) {
            p->land();
            tell = true;
        }
        const auto since = now - p->landed;
        const bool due = !p->landedOnce || since >= p->intervalMs || (p->readSoon && since >= soonMs);
        if (!p->cycle && !p->demanded.isEmpty() && due && (!p->source.busy || !p->source.busy()) && p->source.read) {
            p->startCycle();
            tell = true;
        }
        if (p->cycle && p->submitNext()) tell = true;
        // A cycle whose portions all failed at submission lands at once.
        if (p->cycle && !p->pending && p->cycle->cursor >= int(p->cycle->portions.size())) {
            p->land();
            tell = true;
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
        // A value inside a struct or array Field is not its Field's preview.
        if (row.leaf || !p->demanded.contains(row.id)) continue;
        const auto s = p->schema();
        const auto* field = p->fieldById(row.id);
        if (!field || field->typeId != row.value.type.typeId) continue;
        if (auto* entry = p->ensure(row.id, row.value.size)) {
            entry->bytes.clear(); entry->ready = false; entry->reason.clear();
            entry->state = QStringLiteral("Unavailable");
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
