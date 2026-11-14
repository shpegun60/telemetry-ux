// Bounded host-arrival history over existing Watchlist results. MIT.
#ifndef TELEMETRY_UX_TREND_HISTORY_HPP
#define TELEMETRY_UX_TREND_HISTORY_HPP
#include <QtCore/QAbstractItemModel>
#include <QtCore/QElapsedTimer>
#include <QtCore/QPersistentModelIndex>
#include <QtCore/QPointer>
#include <QtCore/QVariantList>

namespace telemetry::ux {
class TrendHistory : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int row READ row WRITE setRow NOTIFY rowChanged)
    Q_PROPERTY(QVariantList points READ points NOTIFY pointsChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool step READ step NOTIFY stepChanged)
public:
    explicit TrendHistory(QObject* parent = nullptr);
    QAbstractItemModel* source() const { return source_; }
    void setSource(QAbstractItemModel* source);
    int row() const { return selected_.isValid() ? selected_.row() : -1; }
    void setRow(int row);
    QVariantList points() const { return points_; }
    QString state() const { return state_; }
    bool step() const { return step_; }
    Q_INVOKABLE void clear();
Q_SIGNALS:
    void sourceChanged();
    void rowChanged();
    void pointsChanged();
    void stateChanged();
    void stepChanged();
private:
    void inspect();
    void detachSelection();
    // With nothing chosen: what to do next, by whether anything is watched.
    QString idleText() const;
    bool setState(const QString& state);
    bool updateStep();
    void publish(unsigned flags);
    int role(const QByteArray& name) const;
    QPointer<QAbstractItemModel> source_;
    QPersistentModelIndex selected_;
    QElapsedTimer clock_;
    QVariantList points_;
    QString revision_, epoch_, state_ = QStringLiteral("Watch a Field to plot it.");
    bool gap_ = true, step_ = false;
    unsigned pendingSignals_ = 0;
    bool publishing_ = false;
    enum Signal : unsigned { StateChanged = 1, PointsChanged = 2, RowChanged = 4, SourceChanged = 8, StepChanged = 16 };
    static constexpr int capacity_ = 512;
};
}
#endif
