#include <Telemetry/UX/TrendHistory.hpp>
#include <Telemetry/UX/QmlTypes.hpp>
#include <Telemetry/UX/UndoHistory.hpp>
#include <Telemetry/UX/UndoRouter.hpp>
#include <Telemetry/UX/UndoScope.hpp>
#include <QQuickItem>
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
    // The installed module's C++ types, the central Undo's history among them.
    telemetry::ux::registerQmlTypes();
    Provider p;
    p.model.setItemRoleNames({{Qt::UserRole + 1, "name"}, {Qt::UserRole + 2, "status"},
                              {Qt::UserRole + 3, "sampleRevision"}, {Qt::UserRole + 4, "sampleText"},
                              {Qt::UserRole + 5, "sampleEpoch"}, {Qt::UserRole + 6, "sampleValue"},
                              {Qt::UserRole + 7, "sampleExact"}, {Qt::UserRole + 8, "statusCode"}});
    telemetry::ux::TrendHistory history;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("testProvider", &p);
    engine.rootContext()->setContextProperty("testHistory", &history);
    if (argc != 2) return 1;
    engine.load(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if (engine.rootObjects().isEmpty() || history.source() != &p.model ||
        !engine.rootObjects().first()->findChild<QObject*>("trendPanel")) return 2;
    std::puts("Installed Trend source and window passed");
    // A host makes its history in QML (UR-1): empty, 100 steps, nothing to undo.
    auto* undo = engine.rootObjects().first()->findChild<telemetry::ux::UndoHistory*>("workspaceHistory");
    if (!undo || undo->limit() != 100 || undo->canUndo() || undo->undo() || undo->undoSerial() != 0) return 3;
    std::puts("Installed UndoHistory made in QML passed");
    // The router, its buttons, a notice and a scope, from the installed module.
    auto* root = engine.rootObjects().first();
    auto* router = root->findChild<telemetry::ux::UndoRouter*>("undoRouter");
    auto* inert = root->findChild<QQuickItem*>("inertArea");
    const auto* scope = telemetry::ux::UndoScope::of(inert);
    if (!router || router->workspace() != undo || router->active() != undo || router->canUndo() ||
        router->undoReason() != QStringLiteral("Nothing to undo.") || !root->findChild<QObject*>("undoButtons") ||
        !root->findChild<QObject*>("undoNotice") || !scope || !scope->inert())
        return 4;
    std::puts("Installed UndoRouter, UndoButtons, UndoNotice and UndoScope made in QML passed");
    return 0;
}
#include "TrendInstalled.moc"
