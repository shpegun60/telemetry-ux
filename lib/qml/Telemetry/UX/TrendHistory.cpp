// Bounded host-presentation history over existing Watchlist results. MIT.
#include "TrendHistory.hpp"
#include <QtCore/QRegularExpression>
#include <QtCore/QThread>
#include <cmath>

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
        connect(source_, &QAbstractItemModel::modelAboutToBeReset, this, &TrendHistory::detachSelection);
        connect(source_, &QAbstractItemModel::rowsInserted, this, [this] { publish(RowChanged); });
        connect(source_, &QAbstractItemModel::rowsRemoved, this, [this] { publish(RowChanged); });
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
            if (setState(QStringLiteral("Field unavailable; select a Field again"))) flags |= StateChanged;
            publish(flags);
        });
    }
    unsigned flags = PointsChanged | RowChanged | SourceChanged;
    if (setState(wrongThread ? QStringLiteral("Watchlist must share the Trend owner thread")
                             : QStringLiteral("Select a watched Field"))) flags |= StateChanged;
    publish(flags);
}
void TrendHistory::detachSelection()
{
    selected_ = QModelIndex{}; revision_.clear(); epoch_.clear(); gap_ = true;
    unsigned flags = RowChanged;
    if (setState(QStringLiteral("Field unavailable; select a Field again"))) flags |= StateChanged;
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
        changed = setState(QStringLiteral("Waiting for a new reading"));
    } else changed = setState(QStringLiteral("Select a watched Field"));
    publish(PointsChanged | RowChanged | (changed ? StateChanged : 0u));
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
    const int statusRole = role("status"), revisionRole = role("sampleRevision"),
              epochRole = role("sampleEpoch"), sampleRole = role("sampleText");
    if (statusRole < 0 || revisionRole < 0 || epochRole < 0 || sampleRole < 0) {
        gap_ = true;
        if (setState(QStringLiteral("This provider has no sample history support"))) publish(StateChanged);
        return;
    }
    const QString status = source_->data(selected_, statusRole).toString();
    if (status != QStringLiteral("Ok")) {
        if (status != QStringLiteral("Pending") && status != QStringLiteral("NotRead") &&
            status != QStringLiteral("Not read")) gap_ = true;
        if (setState(status.isEmpty() ? QStringLiteral("No reading") : status)) publish(StateChanged);
        return;
    }
    const QString revision = source_->data(selected_, revisionRole).toString();
    const QString epoch = source_->data(selected_, epochRole).toString();
    if (revision.isEmpty() || (revision == revision_ && epoch == epoch_)) return;
    if (epoch != epoch_) gap_ = true;
    revision_ = revision; epoch_ = epoch;
    const QString raw = source_->data(selected_, sampleRole).toString();
    QString numeric = raw;
    static const QRegularExpression zeroBits(QStringLiteral("^(-?0) \\(0x[0-9a-fA-F]+\\)$"));
    const auto zeroMatch = zeroBits.match(numeric);
    if (zeroMatch.hasMatch()) numeric = zeroMatch.captured(1);
    static const QRegularExpression decimal(QStringLiteral("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"));
    static const QRegularExpression integer(QStringLiteral("^[+-]?[0-9]+$"));
    bool ok = false;
    const double value = numeric.toDouble(&ok);
    if (!decimal.match(numeric).hasMatch() || !ok || !std::isfinite(value) ||
        (integer.match(numeric).hasMatch() && std::abs(value) > 9007199254740991.0)) {
        gap_ = true;
        if (setState(QStringLiteral("Value cannot be plotted without losing information: ") + raw)) publish(StateChanged);
        return;
    }
    if (points_.size() == capacity_) points_.removeFirst();
    points_.push_back(QVariantMap{{QStringLiteral("ms"), clock_.elapsed()},
                                  {QStringLiteral("value"), value},
                                  {QStringLiteral("text"), raw},
                                  {QStringLiteral("breakBefore"), gap_}});
    gap_ = false;
    const bool changed = setState(QStringLiteral("Latest reading — host presentation time; ") + raw);
    publish(PointsChanged | (changed ? StateChanged : 0u));
}
}
