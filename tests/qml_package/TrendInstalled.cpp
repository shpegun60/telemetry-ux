#include <Telemetry/UX/TrendHistory.hpp>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardItemModel>
#include <cstdio>
class Provider : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* watchlist READ watchlist CONSTANT)
public:
    QStandardItemModel model;
    QAbstractItemModel* watchlist() { return &model; }
};
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    Provider p;
    p.model.setItemRoleNames({{Qt::UserRole + 1, "name"}, {Qt::UserRole + 2, "status"},
                              {Qt::UserRole + 3, "sampleRevision"}, {Qt::UserRole + 4, "sampleText"},
                              {Qt::UserRole + 5, "sampleEpoch"}});
    telemetry::ux::TrendHistory history;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("testProvider", &p);
    engine.rootContext()->setContextProperty("testHistory", &history);
    if (argc != 2) return 1;
    engine.load(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if (engine.rootObjects().isEmpty() || history.source() != &p.model ||
        !engine.rootObjects().first()->findChild<QObject*>("trendPanel")) return 2;
    std::puts("Installed Trend source and window passed");
    return 0;
}
#include "TrendInstalled.moc"
