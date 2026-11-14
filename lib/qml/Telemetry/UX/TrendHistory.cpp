// Bounded host-presentation history over existing Watchlist results. MIT.
#include "TrendHistory.hpp"
#include "WatchSample.hpp"
#include <QtCore/QThread>

namespace telemetry::ux {
TrendHistory::TrendHistory(QObject* parent) : QObject(parent) { clock_.start(); }
int TrendHistory::role(const QByteArray& name) const
{
    if (!source_) return -1;
    const auto roles = source_->roleNames();
    for (auto i = roles.cbegin(); i != roles.cend(); ++i)
        if (i.value() == name) return i.key();
    return -1;
}
bool TrendHistory::setState(const QString& value)
{
    if (state_ == value) return false;
    state_ = value;
    return true;
}
bool TrendHistory::updateStep()
{
    const int categorical = role("categorical");
    const bool value = source_ && selected_.isValid() && categorical >= 0 &&
        source_->data(selected_, categorical).toBool();
    if (step_ == value) return false;
    step_ = value;
    gap_ = true;
    return true;
}
void TrendHistory::publish(unsigned flags)
{
    pendingSignals_ |= flags;
    if (publishing_) return;
    publishing_ = true;
    QPointer<TrendHistory> alive(this);
    while (pendingSignals_) {
        const auto bit = pendingSignals_ & (~pendingSignals_ + 1);
        pendingSignals_ &= ~bit;
        switch (bit) {
            case StateChanged: Q_EMIT stateChanged(); break;
            case PointsChanged: Q_EMIT pointsChanged(); break;
            case RowChanged: Q_EMIT rowChanged(); break;
            case SourceChanged: Q_EMIT sourceChanged(); break;
            case StepChanged: Q_EMIT stepChanged(); break;
            default: break;
        }
        if (!alive) return;
    }
    publishing_ = false;
}
void TrendHistory::setSource(QAbstractItemModel* source)
{
    if (QThread::currentThread() != thread()) return;
    const bool wrongThread = source && source->thread() != thread();
    if (wrongThread) source = nullptr;
    if (source_ == source) {
        if (wrongThread && setState(QStringLiteral("Watchlist must share the Trend owner thread")))
            publish(StateChanged);
        return;
    }
    if (source_) disconnect(source_, nullptr, this, nullptr);
    source_ = source;
    selected_ = QModelIndex{};
    revision_.clear(); epoch_.clear(); gap_ = true; points_.clear();
    if (source_) {
        connect(source_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& first, const QModelIndex& last, const QList<int>&) {
                if (selected_.isValid() && !first.parent().isValid() && !last.parent().isValid() &&
                    selected_.row() >= first.row() && selected_.row() <= last.row()) inspect();
            });
        // A reset (a disconnect, a new list) is not a lost Field: the plot
        // says what to do next once the list is back.
        connect(source_, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
            selected_ = QModelIndex{}; revision_.clear(); epoch_.clear(); gap_ = true;
            publish(RowChanged | (updateStep() ? StepChanged : 0u));
        });
        connect(source_, &QAbstractItemModel::modelReset, this, [this] {
            if (!selected_.isValid() && setState(idleText())) publish(StateChanged);
        });
        // With nothing chosen, the line follows whether anything is watched.
        const auto rowsChanged = [this] {
            unsigned flags = RowChanged;
            if (!selected_.isValid() && points_.isEmpty() && setState(idleText())) flags |= StateChanged;
            publish(flags);
        };
        connect(source_, &QAbstractItemModel::rowsInserted, this, rowsChanged);
        connect(source_, &QAbstractItemModel::rowsRemoved, this, rowsChanged);
        connect(source_, &QAbstractItemModel::rowsMoved, this, [this] { publish(RowChanged); });
        connect(source_, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex& parent, int first, int last) {
                if (!parent.isValid() && selected_.isValid() && selected_.row() >= first && selected_.row() <= last)
                    detachSelection();
            });
        connect(source_, &QObject::destroyed, this, [this] {
            source_ = nullptr;
            selected_ = QModelIndex{}; revision_.clear(); epoch_.clear(); gap_ = true;
            unsigned flags = RowChanged | SourceChanged;
            if (updateStep()) flags |= StepChanged;
            if (setState(idleText())) flags |= StateChanged;
            publish(flags);
        });
    }
    unsigned flags = PointsChanged | RowChanged | SourceChanged;
    if (updateStep()) flags |= StepChanged;
    if (setState(wrongThread ? QStringLiteral("Watchlist must share the Trend owner thread")
                             : idleText())) flags |= StateChanged;
    publish(flags);
}
QString TrendHistory::idleText() const
{
    return source_ && source_->rowCount() > 0 ? QStringLiteral("Choose a Field above.")
                                              : QStringLiteral("Watch a Field to plot it.");
}
void TrendHistory::detachSelection()
{
    selected_ = QModelIndex{}; revision_.clear(); epoch_.clear(); gap_ = true;
    unsigned flags = RowChanged;
    if (updateStep()) flags |= StepChanged;
    if (setState(QStringLiteral("This Field is no longer watched."))) flags |= StateChanged;
    publish(flags);
}
void TrendHistory::setRow(int row)
{
    if (QThread::currentThread() != thread()) return;
    const auto index = source_ && row >= 0 ? source_->index(row, 0) : QModelIndex{};
    if (index == selected_) return;
    selected_ = index; gap_ = true; epoch_.clear(); revision_.clear(); points_.clear();
    bool changed;
    if (selected_.isValid() && role("sampleRevision") >= 0) {
        revision_ = source_->data(selected_, role("sampleRevision")).toString();
        epoch_ = source_->data(selected_, role("sampleEpoch")).toString();
        changed = setState(QStringLiteral("No readings yet. Read once or Start."));
    } else changed = setState(idleText());
    publish(PointsChanged | RowChanged | (changed ? StateChanged : 0u) |
            (updateStep() ? StepChanged : 0u));
}
void TrendHistory::clear()
{
    if (QThread::currentThread() != thread()) return;
    points_.clear(); gap_ = true;
    publish(PointsChanged);
}
void TrendHistory::inspect()
{
    if (!source_ || !selected_.isValid()) return;
    const unsigned typedFlags = updateStep() ? StepChanged : 0u;
    // The number comes from the typed value (sampleValue/sampleExact), never from
    // the display text, and the status from its stable code, never its wording.
    const int statusRole = role("status"), codeRole = role("statusCode"),
              revisionRole = role("sampleRevision"), epochRole = role("sampleEpoch"),
              textRole = role("sampleText"), valueRole = role("sampleValue"),
              exactRole = role("sampleExact");
    if (statusRole < 0 || codeRole < 0 || revisionRole < 0 || epochRole < 0 || textRole < 0 ||
        valueRole < 0 || exactRole < 0) {
        gap_ = true;
        publish(typedFlags | (setState(QStringLiteral("This provider has no sample history support")) ? StateChanged : 0u));
        return;
    }
    // A missing or unknown code counts as a failure, so it breaks the line.
    const QVariant codeValue = source_->data(selected_, codeRole);
    bool codeOk = false;
    const int codeNumber = codeValue.toInt(&codeOk);
    const auto code = codeOk && codeNumber >= int(WatchStatus::NotRead) && codeNumber <= int(WatchStatus::Failed)
                          ? static_cast<WatchStatus>(codeNumber) : WatchStatus::Failed;
    if (code != WatchStatus::Ok) {
        if (code != WatchStatus::Pending && code != WatchStatus::NotRead) gap_ = true;
        const QString status = source_->data(selected_, statusRole).toString();
        publish(typedFlags | (setState(status.isEmpty() ? QStringLiteral("No reading") : status) ? StateChanged : 0u));
        return;
    }
    const QString revision = source_->data(selected_, revisionRole).toString();
    const QString epoch = source_->data(selected_, epochRole).toString();
    if (revision.isEmpty() || (revision == revision_ && epoch == epoch_)) { publish(typedFlags); return; }
    if (epoch != epoch_) gap_ = true;
    revision_ = revision; epoch_ = epoch;
    const QString raw = source_->data(selected_, textRole).toString();
    const QVariant sample = source_->data(selected_, valueRole);
    if (!source_->data(selected_, exactRole).toBool() || !sample.isValid()) {
        gap_ = true;
        publish(typedFlags | (setState(QStringLiteral("Not plotted: not a finite number a chart can show exactly: ") + raw) ? StateChanged : 0u));
        return;
    }
    const double value = sample.toDouble();
    if (points_.size() == capacity_) points_.removeFirst();
    points_.push_back(QVariantMap{{QStringLiteral("ms"), clock_.elapsed()},
                                  {QStringLiteral("value"), value},
                                  {QStringLiteral("text"), raw},
                                  {QStringLiteral("step"), step_},
                                  {QStringLiteral("breakBefore"), gap_}});
    gap_ = false;
    const bool changed = setState(QStringLiteral("Latest reading — host presentation time; ") + raw);
    publish(typedFlags | PointsChanged | (changed ? StateChanged : 0u));
}
}
