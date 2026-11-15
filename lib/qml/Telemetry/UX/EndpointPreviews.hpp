// Bounded, optional tree previews over the host's existing session queue. MIT.
#pragma once
#include "ReadPortions.hpp"
#include "WatchFrame.hpp"
#include <client/Backend.hpp>
#include <telemetry/runtime/DynamicValueView.hpp>
#include <QtCore/QObject>
#include <QtCore/QVariantMap>
#include <functional>
#include <memory>
#include <optional>

namespace telemetry::ux {
struct EndpointPreviewResult {
    client::Status status = client::Status::NotAdmitted;
    bool hasValue = false;
    QByteArray bytes;
    QString reason;
};

// Several Fields read in one request: each Field's result in the order asked.
// status is the request's; a Field the device refused carries its own.
struct EndpointPreviewManyResult {
    client::Status status = client::Status::NotAdmitted;
    std::vector<std::pair<quint32, EndpointPreviewResult>> items;
};

// Qt host seam, like WatchlistSource. No new client, queue or transport.
// All functions run on the session owner thread. read() copies borrowed bytes
// into its completion before native callback return; it never publishes Qt.
// Optional: readMany (scalars, with its budget) and readBatch (whole Fields as
// a Batch of FieldReads, with its budget); without them each Field is read
// on its own.
struct EndpointPreviewSource {
    using Completion = std::shared_ptr<std::optional<EndpointPreviewResult>>;
    using ManyCompletion = std::shared_ptr<std::optional<EndpointPreviewManyResult>>;
    std::shared_ptr<void> backing;
    std::function<bool()> current, busy;
    std::function<runtime::RuntimeSchema()> schema;
    std::function<quint32()> session;
    std::function<bool(quint32)> watched;
    std::function<client::Submission(quint32, Completion)> read;
    std::function<std::optional<read_portions::Budget>()> readManyBudget, batchBudget;
    std::function<client::Submission(std::span<const std::uint32_t>, ManyCompletion)> readMany, readBatch;
};

class EndpointPreviews final : public QObject {
    Q_OBJECT
public:
    static constexpr int fieldLimit = 64;
    static constexpr int payloadLimit = 256 * 1024;
    static constexpr int fieldBytesLimit = 8 * 1024;
    static constexpr int leaseLimit = 16;
    static constexpr int demandLimit = 256;
    // A cycle reads every Field in view together, then the next starts an
    // interval after it landed (PV-2). A value is stale after three intervals,
    // and never sooner than staleFloorMs.
    static constexpr int intervalDefaultMs = 2000, intervalMinimumMs = 1000, intervalMaximumMs = 10000;
    static constexpr int staleFloorMs = 5000;
    // Fields newly in view, or Read now, are read at the next turn, but never
    // sooner than soonMs after the last cycle landed: scrolling cannot turn
    // previews into a stream of requests.
    static constexpr int soonMs = 500;
    using Clock = std::function<qint64()>;

    explicit EndpointPreviews(QObject* parent = nullptr, Clock clock = {});
    ~EndpointPreviews() override;
    void setSource(EndpointPreviewSource source);
    void retire();
    void invalidateValues();
    // A view lease replaces its demand atomically. Only readable root Field IDs
    // are retained. Visible children request their owning Field, never a leaf.
    bool setDemand(QObject* view, const QList<quint32>& ids);
    void clearDemand(QObject* view);
    // Called LAST by the existing progress owner, after foreground/Watch/Batch,
    // which takes in a reply; and by its own single-shot timer, armed only
    // while Fields are in view, for the next cycle falling due and for the
    // next whole second, when the ages shown move on (the session runs only
    // when it has work, so its progress is no clock). At most one accepted
    // background request, sent only while the session is idle; no retry
    // backlog. A cycle reads the Fields in view in as few requests as the
    // device allows - scalars by ReadMany, the rest whole in a Batch, each on
    // its own without them - and its values land together when the last reply
    // is in.
    void poll();
    int interval() const;
    void setInterval(int ms); // clamped to intervalMinimumMs..intervalMaximumMs
    // Read the Fields in view now, without waiting for the interval.
    void readNow();
    void ingest(const WatchFrame& frame);
    bool current() const;
    runtime::RuntimeSchema schema() const;
    QVariantMap info(quint32 id) const;
    QString value(quint32 id, TypeId typeId, quint32 offset) const;
    int cachedFields() const;
    int cachedBytes() const;
    bool pending() const;
    // In how many ms poll() runs on its own timer; -1 while it is off (no
    // Field in view, or no current source).
    int nextPollInMs() const;
Q_SIGNALS:
    void sourceChanged();
    // id == 0 is not a sentinel: the signal invalidates the whole local cache.
    void changed();
private:
    struct Impl;
    std::shared_ptr<Impl> d;
};
} // namespace telemetry::ux
