// Bounded, optional tree previews over the host's existing session queue. MIT.
#pragma once
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

// Qt host seam, like WatchlistSource. No new client, queue or transport.
// All functions run on the session owner thread. read() copies borrowed bytes
// into its completion before native callback return; it never publishes Qt.
struct EndpointPreviewSource {
    using Completion = std::shared_ptr<std::optional<EndpointPreviewResult>>;
    std::shared_ptr<void> backing;
    std::function<bool()> current, busy;
    std::function<runtime::RuntimeSchema()> schema;
    std::function<quint32()> session;
    std::function<bool(quint32)> watched;
    std::function<client::Submission(quint32, Completion)> read;
};

class EndpointPreviews final : public QObject {
    Q_OBJECT
public:
    static constexpr int fieldLimit = 64;
    static constexpr int payloadLimit = 256 * 1024;
    static constexpr int fieldBytesLimit = 8 * 1024;
    static constexpr int leaseLimit = 16;
    static constexpr int demandLimit = 256;
    static constexpr int refreshMs = 5000;
    static constexpr int spacingMs = 500;
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
    // Called LAST by the existing progress owner, after foreground/Watch/Batch.
    // At most one accepted background request; no timer and no retry backlog.
    void poll();
    void ingest(const WatchFrame& frame);
    bool current() const;
    runtime::RuntimeSchema schema() const;
    QVariantMap info(quint32 id) const;
    QString value(quint32 id, TypeId typeId, quint32 offset) const;
    int cachedFields() const;
    int cachedBytes() const;
    bool pending() const;
Q_SIGNALS:
    void sourceChanged();
    // id == 0 is not a sentinel: the signal invalidates the whole local cache.
    void changed();
private:
    struct Impl;
    std::shared_ptr<Impl> d;
};
} // namespace telemetry::ux
