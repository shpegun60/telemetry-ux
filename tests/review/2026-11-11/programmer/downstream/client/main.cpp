// Programmer-review downstream client: one ConnectionSession, one provider, four panels.
// `--check` drives the same objects the buttons use and prints a verdict. MIT.
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <Telemetry/UX/TrendHistory.hpp>
#include <integration/qt/ConnectionSession.hpp>
#include <QtCore/QCommandLineParser>
#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonDocument>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <cstdio>
#include <functional>

namespace {
QString roleText(const QAbstractItemModel* model, int row, const QByteArray& role)
{
    const auto names = model->roleNames();
    return model->data(model->index(row, 0), names.key(role)).toString();
}
// Find an endpoint by dotted path in the provider's endpoint tree.
QModelIndex findEndpoint(QAbstractItemModel* model, const QString& path, const QModelIndex& parent = {},
                         const QString& prefix = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const auto index = model->index(row, 0, parent);
        const auto full = prefix.isEmpty() ? index.data().toString() : prefix + '.' + index.data().toString();
        if (full.endsWith(path))
            return index;
        if (auto child = findEndpoint(model, path, index, full); child.isValid())
            return child;
    }
    return {};
}
void waitUntil(const std::function<bool()>& done, int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}
} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QCommandLineParser args;
    args.addOption({QStringLiteral("port"), QStringLiteral("Server port"), QStringLiteral("port"), QStringLiteral("5591")});
    args.addOption({QStringLiteral("check"), QStringLiteral("Scripted check, then exit")});
    args.addOption({QStringLiteral("screenshot"), QStringLiteral("PNG path"), QStringLiteral("file")});
    args.process(app);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    telemetry::integration::qt::ConnectionSession connection;
    telemetry::ux::QtConnectionBinding presentation{&connection};
    presentation.setMutationsAllowed(true);
    auto* device = presentation.createProvider();
    telemetry::ux::TrendHistory trendHistory;

    QQmlApplicationEngine engine;
    unsigned warnings = 0;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app, [&](const QList<QQmlError>& list) {
        for (const auto& e : list) std::fprintf(stderr, "QML: %s\n", qPrintable(e.toString()));
        warnings += unsigned(list.size());
    });
    engine.rootContext()->setContextProperty(QStringLiteral("connection"), &connection);
    engine.rootContext()->setContextProperty(QStringLiteral("device"), device);
    engine.rootContext()->setContextProperty(QStringLiteral("trendHistory"), &trendHistory);
    engine.rootContext()->setContextProperty(QStringLiteral("startupPort"), args.value(QStringLiteral("port")).toInt());
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().size() != 1)
        return 1;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!args.isSet(QStringLiteral("check")))
        return app.exec();

    // ---- scripted check -------------------------------------------------
    bool ok = true;
    const auto expect = [&](bool condition, const char* what) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", what);
        ok = ok && condition;
    };
    connection.connectTcpReference(QStringLiteral("127.0.0.1"), quint16(args.value(QStringLiteral("port")).toUInt()));
    waitUntil([&] { return connection.ready() || !connection.lastError().isEmpty(); }, 5000);
    expect(connection.ready(), "connected (Resource bootstrap + Encoded)");
    if (!connection.ready()) std::printf("  lastError: %s\n", qPrintable(connection.lastError()));

    // Group add through the same QML function the toolbar button uses.
    QVariant result;
    QMetaObject::invokeMethod(window, "watchGroup", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QStringLiteral("measure")));
    const auto map = result.toMap();
    std::printf("  addWatchMany -> %s\n", QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact).constData());
    expect(map.value(QStringLiteral("status")).toString() == QStringLiteral("ok") && map.value(QStringLiteral("added")).toInt() == 4,
           "addWatchMany added the 4 measure Fields in one call");

    auto* watch = device->watchlist();
    int temperatureRow = -1, uptimeRow = -1;
    for (int r = 0; r < watch->rowCount(); ++r) {
        const auto name = roleText(watch, r, "name");
        if (name.contains(QStringLiteral("Temperature"))) temperatureRow = r;
        if (name.contains(QStringLiteral("Uptime"))) uptimeRow = r;
    }
    trendHistory.setRow(temperatureRow);
    device->setProperty("watchIntervalMs", 100);
    QMetaObject::invokeMethod(device, "startWatch");
    waitUntil([] { return false; }, 2500);
    QMetaObject::invokeMethod(device, "stopWatch");
    waitUntil([&] { return !device->property("watchPending").toBool(); }, 2000);
    std::printf("  watchStatus: %s\n", qPrintable(device->property("watchStatus").toString()));
    for (int r = 0; r < watch->rowCount(); ++r)
        std::printf("  watch[%d] %s = %s (%s)\n", r, qPrintable(roleText(watch, r, "name")),
                    qPrintable(roleText(watch, r, "text")), qPrintable(roleText(watch, r, "status")));
    const auto uptimeText = uptimeRow >= 0 ? roleText(watch, uptimeRow, "text") : QString();
    bool parsed = false;
    const auto uptime = uptimeText.toULongLong(&parsed);
    expect(parsed && uptime >= 0x1234'5678'9ABC'DEF0ull && uptime % 256 != 0,
           "U64 Uptime shown exactly (not rounded through double)");
    std::printf("  trend points=%lld state=%s\n", (long long)trendHistory.points().size(), qPrintable(trendHistory.state()));
    expect(trendHistory.points().size() >= 5, "TrendPanel history filled from Watchlist polling");

    // Command through the provider: select control.ResetUptime, execute.
    const auto command = findEndpoint(device->endpoints(), QStringLiteral("control.ResetUptime"));
    expect(command.isValid(), "found control.ResetUptime in endpoint tree");
    device->setProperty("allowMutations", true);
    bool selected = false;
    QMetaObject::invokeMethod(device, "selectEndpoint", Q_RETURN_ARG(bool, selected), Q_ARG(QModelIndex, command));
    waitUntil([&] { return device->property("canExecute").toBool(); }, 2000);
    std::printf("  after selecting Command: selected=%d editing=%d canExecute=%d\n", selected,
                device->property("editing").toBool(), device->property("canExecute").toBool());
    QMetaObject::invokeMethod(device, "executeCommand");
    waitUntil([&] { return !device->property("commandOutcome").toString().isEmpty() && !device->property("busy").toBool(); }, 3000);
    std::printf("  commandOutcome: %s\n", qPrintable(device->property("commandOutcome").toString()));
    std::printf("  before refreshWatch: editing=%d busy=%d watchPending=%d status=%s\n",
                device->property("editing").toBool(), device->property("busy").toBool(),
                device->property("watchPending").toBool(), qPrintable(device->property("statusText").toString()));
    const auto readOnce = [&](const char* label) {
        const auto before = roleText(watch, uptimeRow, "text");
        QMetaObject::invokeMethod(device, "refreshWatch");
        const bool started = device->property("watchPending").toBool();
        waitUntil([&] { return !device->property("watchPending").toBool(); }, 2000);
        waitUntil([] { return false; }, 200);
        const auto now = roleText(watch, uptimeRow, "text");
        std::printf("  %s: refreshWatch started=%d changed=%d watchStatus=%s\n", label, started, before != now,
                    qPrintable(device->property("watchStatus").toString()));
        return before != now;
    };
    const bool whileCommandSelected = readOnce("Command selected");
    expect(whileCommandSelected, "Watchlist read once works while a Command is selected in the same provider");
    if (!whileCommandSelected) {
        QMetaObject::invokeMethod(device, "cancelEdit");
        readOnce("after cancelEdit");
    }
    // Polling already running, then the operator clicks a Command in the tree.
    QMetaObject::invokeMethod(device, "startWatch");
    waitUntil([] { return false; }, 600);
    QMetaObject::invokeMethod(device, "selectEndpoint", Q_RETURN_ARG(bool, selected), Q_ARG(QModelIndex, command));
    waitUntil([] { return false; }, 300);
    const auto pollBefore = roleText(watch, uptimeRow, "text");
    waitUntil([] { return false; }, 1200);
    const auto pollAfter = roleText(watch, uptimeRow, "text");
    std::printf("  polling with Command selected: editing=%d %s -> %s watchStatus=%s\n",
                device->property("editing").toBool(), qPrintable(pollBefore), qPrintable(pollAfter),
                qPrintable(device->property("watchStatus").toString()));
    expect(pollBefore != pollAfter, "running polling continues after selecting a Command");
    QMetaObject::invokeMethod(device, "cancelEdit");
    QMetaObject::invokeMethod(device, "stopWatch");
    waitUntil([&] { return !device->property("watchPending").toBool(); }, 2000);
    const auto after = roleText(watch, uptimeRow, "text").toULongLong(&parsed);
    std::printf("  Uptime after ResetUptime: %llu\n", after);
    expect(parsed && after < 1000, "Command executed and readback shows reset Uptime");

    if (args.isSet(QStringLiteral("screenshot")))
        window->grabWindow().save(args.value(QStringLiteral("screenshot")));
    connection.disconnect();
    std::printf("QML warnings: %u\n%s\n", warnings, ok && warnings == 0 ? "VERDICT PASS" : "VERDICT FAIL");
    return ok && warnings == 0 ? 0 : 1;
}
