// Designer review driver: runs the unchanged examples/qt_docking Main.qml, drives it
// with synthetic in-process mouse/keyboard events (QTest), counts the actions of
// each task and grabs the real rendered windows. Layout goes to an explicit file,
// never to the user's AppConfigLocation. It starts and stops its own emulator.
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <Telemetry/UX/TrendHistory.hpp>
#include <integration/qt/ConnectionSession.hpp>

#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QPalette>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QTest>

#include <cstdio>
#include <functional>

class LayoutCloseFilter final : public QObject {
public:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Close) {
            QVariant saved;
            QMetaObject::invokeMethod(watched, "saveLayoutBeforeClose", Q_RETURN_ARG(QVariant, saved));
            std::printf("LAYOUT saved-on-close=%d\n", saved.toBool());
        }
        return false;
    }
};

struct Driver {
    QQmlApplicationEngine* engine = nullptr;
    QQuickWindow* main = nullptr;
    QObject* connection = nullptr;
    QObject* device = nullptr;
    QString out;
    QString emulatorExe;
    int port = 5691;
    QProcess* emulator = nullptr;
    int actions = 0;
    QElapsedTimer clock;

    void log(const QString& line) { std::printf("%s\n", qPrintable(line)); std::fflush(stdout); }
    QList<QQuickWindow*> windows() {
        QList<QQuickWindow*> r;
        for (auto* w : QGuiApplication::topLevelWindows())
            if (auto* q = qobject_cast<QQuickWindow*>(w); q && q->isVisible()) r << q;
        return r;
    }
    void collect(QQuickItem* it, QList<QQuickItem*>& r) { r << it; for (auto* c : it->childItems()) collect(c, r); }
    QList<QQuickItem*> all() { QList<QQuickItem*> r; for (auto* w : windows()) collect(w->contentItem(), r); return r; }
    QQuickItem* byName(const QString& n, int nth = 0) {
        int k = 0;
        for (auto* i : all()) if (i->objectName() == n && i->isVisible()) { if (k++ == nth) return i; }
        return nullptr;
    }
    QQuickItem* byText(const QString& t, const QString& cls = {}) {
        for (auto* i : all())
            if (i->isVisible() && i->property("text").toString() == t &&
                (cls.isEmpty() || QString(i->metaObject()->className()).contains(cls)))
                return i;
        return nullptr;
    }
    QQuickItem* titleBar(const QString& title) {
        for (auto* i : all())
            if (i->isVisible() && i->property("closeButton").isValid() && i->property("title").toString() == title)
                return i;
        return nullptr;
    }
    QObject* dock(const QString& unique) {
        for (auto* o : engine->rootObjects().first()->findChildren<QObject*>())
            if (o->property("uniqueName").toString() == unique && o->property("isOpen").isValid()) return o;
        return nullptr;
    }
    QString dockStates() {
        QStringList s;
        for (auto n : {"connection", "tree", "endpoint", "watch", "compare", "trend", "batch"}) {
            auto* d = dock(QStringLiteral("telemetry.") + n);
            if (d) s << QStringLiteral("%1=%2%3").arg(n, d->property("isOpen").toBool() ? "open" : "closed",
                                                     d->property("isFloating").toBool() ? "+float" : "");
        }
        return s.join(' ');
    }
    bool click(QQuickItem* i, const QString& what) {
        if (!i) { log("MISSING " + what); return false; }
        const QPointF p = i->mapToScene(QPointF(i->width() / 2, i->height() / 2));
        QTest::mouseClick(i->window(), Qt::LeftButton, Qt::NoModifier, p.toPoint());
        ++actions;
        QTest::qWait(250);
        return true;
    }
    bool clickObject(QObject* o, const QString& what) { return click(qobject_cast<QQuickItem*>(o), what); }
    void typeInto(QQuickItem* field, const QString& text, const QString& what) {
        if (!click(field, what)) return;
        QTest::keyClick(field->window(), Qt::Key_A, Qt::ControlModifier);
        if (text.isEmpty()) QTest::keyClick(field->window(), Qt::Key_Delete);
        for (const QChar c : text) QTest::keyClick(field->window(), c.toLatin1());
        QTest::qWait(500);
    }
    bool waitFor(const std::function<bool()>& f, int ms) {
        QElapsedTimer t; t.start();
        while (!f() && t.elapsed() < ms) QTest::qWait(50);
        return f();
    }
    void shot(const QString& name, QQuickWindow* w = nullptr) {
        w = w ? w : main;
        const QImage image = w->grabWindow();
        const QString path = QDir(out).filePath(name + ".png");
        image.save(path);
        log(QStringLiteral("SHOT %1 %2x%3 logical=%4x%5 actions=%6 t=%7ms docks[%8]")
                .arg(path).arg(image.width()).arg(image.height()).arg(w->width()).arg(w->height())
                .arg(actions).arg(clock.elapsed()).arg(dockStates()));
    }
    void step(const QString& name) { log(QStringLiteral("STEP %1 (cumulative actions %2)").arg(name).arg(actions)); }
    void texts(const QString& name, int limit = 12) {
        QStringList s;
        for (int n = 0; n < limit; ++n) {
            auto* i = byName(name, n);
            if (!i) break;
            s << i->property("text").toString().replace('\n', ' ');
        }
        log("TEXT " + name + ": " + s.join(" | "));
    }
    void startEmulator() {
        emulator = new QProcess;
        emulator->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        emulator->start(emulatorExe, {QStringLiteral("--port"), QString::number(port)});
        emulator->waitForStarted(5000);
        QTest::qWait(1500);
        log(QStringLiteral("EMULATOR started pid=%1").arg(emulator->processId()));
    }
    void stopEmulator() {
        if (!emulator) return;
        emulator->kill();
        emulator->waitForFinished(3000);
        delete emulator;
        emulator = nullptr;
        log("EMULATOR killed (peer loss)");
    }
    void openPanel(const QString& entry) {
        click(byText(QStringLiteral("Panels")), "Panels menu");
        click(byText(entry, QStringLiteral("MenuItem")), "menu " + entry);
    }
    QQuickWindow* floatingWindow() {
        for (auto* w : windows()) if (w != main) return w;
        return nullptr;
    }
    void focusAudit(int count) {
        main->requestActivate();
        log(QStringLiteral("ACTIVE %1").arg(QTest::qWaitForWindowActive(main, 3000)));
        QStringList seen;
        for (int n = 0; n < count; ++n) {
            QTest::keyClick(main, Qt::Key_Tab);
            QTest::qWait(60);
            auto* f = main->activeFocusItem();
            QString d = f ? QString(f->metaObject()->className()).section("_QML", 0, 0) : "none";
            if (f && !f->objectName().isEmpty()) d += "#" + f->objectName();
            if (f && f->property("text").isValid() && !f->property("text").toString().isEmpty())
                d += "'" + f->property("text").toString().left(24) + "'";
            seen << d;
        }
        log("FOCUS " + seen.join(" -> "));
    }
    void countControls(const QString& label) {
        QMap<QString, int> counts;
        int total = 0;
        int disabled = 0;
        for (auto* i : all()) {
            if (!i->isVisible() || i->width() < 2) continue;
            const QString c = i->metaObject()->className();
            for (auto kind : {"QQuickCheckBox", "QQuickTabButton", "QQuickMenuBarItem", "QQuickAbstractButton",
                              "QQuickTextField", "QQuickSpinBox", "QQuickComboBox"})
                if (i->inherits(kind) || (QString(kind) == "QQuickAbstractButton" && c.contains("TitleBarButton"))) {
                    if (!i->isEnabled()) { ++disabled; break; }
                    ++counts[kind]; ++total; break;
                }
        }
        counts["(disabled but visible)"] = disabled;
        QStringList s;
        for (auto it = counts.begin(); it != counts.end(); ++it) s << it.key() + "=" + QString::number(it.value());
        log(QStringLiteral("CONTROLS %1 enabled-visible=%2 (%3)").arg(label).arg(total).arg(s.join(", ")));
    }

    void runMain() {
        clock.start();
        main->resize(1600, 900);
        QTest::qWait(800);
        step("S0 first sight, disconnected");
        countControls("first-sight");
        shot("d01-start-1600x900");
        focusAudit(18);
        QTest::keyClick(main, Qt::Key_Escape);

        step("S1 connect");
        startEmulator();
        actions = 0;
        click(byName("connectionAction"), "Connection… button");
        shot("d02-connection-open");
        auto* portBox = byName("connectionPort");
        typeInto(portBox ? portBox->property("contentItem").value<QQuickItem*>() : nullptr, QString::number(port), "port");
        QTest::keyClick(main, Qt::Key_Return);
        log(QStringLiteral("PORT value=%1").arg(portBox ? portBox->property("value").toInt() : -1));
        click(byText("Connect", "Button"), "Connect");
        const bool ready = waitFor([&] { return connection->property("ready").toBool(); }, 10000);
        log(QStringLiteral("CONNECTED ready=%1 after actions=%2").arg(ready).arg(actions));
        QTest::qWait(500);
        countControls("connected");
        shot("d03-connected-1600x900");

        step("S1b hide Connection with its close button");
        auto* bar = titleBar("Connection");
        clickObject(bar ? bar->property("closeButton").value<QObject*>() : nullptr, "Connection close button");
        log(QStringLiteral("AFTER-CLOSE ready=%1").arg(connection->property("ready").toBool()));
        shot("d04-connection-closed");

        step("S2 search by name");
        actions = 0;
        typeInto(byName("endpointSearch"), "Voltage", "search");
        texts("endpointSearchStatus", 1); texts("endpointSearchResult");
        shot("d05-search-name");
        typeInto(byName("endpointSearch"), "measure/volt", "search path");
        texts("endpointSearchStatus", 1); texts("endpointSearchResult");
        shot("d06-search-path");
        typeInto(byName("endpointSearch"), "Signal0", "search prefix");
        texts("endpointSearchStatus", 1);
        typeInto(byName("endpointSearch"), "no-such-field", "search none");
        texts("endpointSearchStatus", 1);
        shot("d07-search-none");
        // Select a result with the keyboard: Tab from the field, Enter.
        typeInto(byName("endpointSearch"), "Current", "search");
        QTest::keyClick(main, Qt::Key_Tab);
        QTest::keyClick(main, Qt::Key_Tab);
        QTest::keyClick(main, Qt::Key_Return);
        QTest::qWait(300);
        auto* f = main->activeFocusItem();
        log(QStringLiteral("KEYBOARD-SELECT focus=%1 selection=%2").arg(f ? QString(f->metaObject()->className()) + "#" + f->objectName() : "none",
                                                                       device->property("selection").toString()));

        step("S3 add several fields at once");
        actions = 0;
        typeInto(byName("endpointSearch"), "measure", "search group");
        texts("endpointSearchResult");
        for (int n = 0; n < 4; ++n) click(byName("endpointSearchWatchCheck", n), "result check " + QString::number(n));
        texts("endpointAddSelectedWatches", 1);
        shot("d08-marked");
        click(byName("endpointAddSelectedWatches"), "Add N");
        texts("endpointWatchSelectionStatus", 1);
        log(QStringLiteral("WATCH count=%1").arg(device->property("watchCount").toInt()));
        shot("d09-added-not-expanded");
        click(byName("watchlistExpanded"), "Watchlist expand");
        shot("d10-watch-expanded");
        click(byName("startWatchButton"), "Start polling");
        QTest::qWait(2500);
        texts("watchStatusLabel", 1); texts("watchValueText"); texts("watchRowStatus");
        log(QStringLiteral("POLLING active=%1 after actions=%2").arg(device->property("watchActive").toBool()).arg(actions));
        shot("d11-polling");

        step("S4 trend");
        actions = 0;
        openPanel("Trend");
        shot("d12-trend-open");
        click(byName("trendField"), "trend field combo");
        QTest::qWait(300);
        QQuickItem* option = nullptr;
        for (auto* i : all())
            if (i->isVisible() && QString(i->metaObject()->className()).contains("ItemDelegate") &&
                i->property("text").toString().contains("Voltage")) { option = i; break; }
        click(option, "trend option Voltage");
        QTest::qWait(4000);
        texts("trendState", 1);
        shot("d13-trend-plot");
        main->resize(1280, 720);
        QTest::qWait(1500);
        shot("d14-polling-trend-1280x720");

        step("S5 peer lost while polling");
        stopEmulator();
        QTest::qWait(3500);
        texts("watchStatusLabel", 1); texts("watchRowStatus"); texts("trendState", 1);
        log(QStringLiteral("LOST ready=%1 watchActive=%2 lastError=%3").arg(connection->property("ready").toBool())
                .arg(device->property("watchActive").toBool()).arg(connection->property("lastError").toString()));
        shot("d15-peer-lost");
        actions = 0;
        click(byName("connectionErrorAction"), "header error action");
        shot("d16-error-opens-connection");
        startEmulator();
        click(byText("Connect", "Button"), "Connect again");
        waitFor([&] { return connection->property("ready").toBool(); }, 10000);
        QTest::qWait(1500);
        texts("watchStatusLabel", 1); texts("watchRowStatus");
        log(QStringLiteral("RECONNECTED watchActive=%1 watchCount=%2").arg(device->property("watchActive").toBool())
                .arg(device->property("watchCount").toInt()));
        shot("d17-reconnected");
        if (device->property("watchActive").toBool()) click(byName("startWatchButton"), "Stop");

        step("S6 workspace: float, narrow, close, reopen, reset");
        main->resize(1600, 900);
        QTest::qWait(600);
        actions = 0;
        auto* tree = titleBar("Device tree");
        clickObject(tree ? tree->property("floatButton").value<QObject*>() : nullptr, "tree float button");
        QTest::qWait(600);
        if (auto* fw = floatingWindow()) {
            fw->resize(280, 640);
            QTest::qWait(800);
            typeInto(byName("endpointSearch"), "measure / volt", "narrow search");
            shot("d18-tree-floating-280", fw);
            typeInto(byName("endpointSearch"), "", "clear search");
            shot("d19-tree-floating-280-tree", fw);
        }
        shot("d20-main-with-tree-floating");
        tree = titleBar("Device tree");
        clickObject(tree ? tree->property("floatButton").value<QObject*>() : nullptr, "tree redock button");
        QTest::qWait(600);
        auto* watchBar = titleBar("Watchlist");
        clickObject(watchBar ? watchBar->property("floatButton").value<QObject*>() : nullptr, "watch float button");
        QTest::qWait(600);
        if (auto* fw = floatingWindow()) {
            fw->resize(280, 520);
            QTest::qWait(800);
            shot("d21-watch-floating-280", fw);
        }
        watchBar = titleBar("Watchlist");
        clickObject(watchBar ? watchBar->property("floatButton").value<QObject*>() : nullptr, "watch redock button");
        QTest::qWait(600);
        shot("d22-after-redock");
        auto* endpointBar = titleBar("Selected endpoint");
        clickObject(endpointBar ? endpointBar->property("closeButton").value<QObject*>() : nullptr, "close Selected endpoint");
        shot("d23-endpoint-closed");
        openPanel("Selected endpoint");
        shot("d24-endpoint-reopened");
        openPanel("Reset layout");
        QTest::qWait(600);
        shot("d25-reset");

        step("S7 arrange a layout to survive restart");
        openPanel("Trend");
        openPanel("Connection");
        tree = titleBar("Device tree");
        clickObject(tree ? tree->property("floatButton").value<QObject*>() : nullptr, "tree float for restart");
        QTest::qWait(600);
        if (auto* fw = floatingWindow()) fw->resize(320, 600);
        QTest::qWait(400);
        shot("d26-before-restart");
        log("BEFORE-RESTART docks[" + dockStates() + "]");
        stopEmulator();
        main->close();
        QTimer::singleShot(500, qApp, &QCoreApplication::quit);
    }

    // Second pass: tree checkboxes (no search), Trend with a plotted Field,
    // a narrow floating Watchlist group and Compare, at 1600 x 900.
    void runTrend() {
        clock.start();
        main->resize(1600, 900);
        QTest::qWait(600);
        countControls("first-sight");
        startEmulator();
        actions = 0;
        click(byName("connectionAction"), "Connection…");
        auto* portBox = byName("connectionPort");
        typeInto(portBox ? portBox->property("contentItem").value<QQuickItem*>() : nullptr, QString::number(port), "port");
        QTest::keyClick(main, Qt::Key_Return);
        click(byText("Connect", "Button"), "Connect");
        waitFor([&] { return connection->property("ready").toBool(); }, 10000);
        QTest::qWait(600);
        countControls("connected");
        step("T1 first value: select Voltage in the tree, Refresh");
        actions = 0;
        for (auto* i : all())
            if (i->isVisible() && i->objectName() == "endpointRow" && i->property("text").toString() == "Voltage") { click(i, "tree Voltage"); break; }
        click(byName("refreshButton"), "Refresh");
        QTest::qWait(800);
        log(QStringLiteral("FIRST-VALUE selection=%1 actions-after-connect=%2").arg(device->property("selection").toString()).arg(actions));
        texts("valueRow", 4);
        shot("t01-first-value");
        step("T2 add from tree checkboxes");
        actions = 0;
        for (auto n : {"Voltage", "Current", "Power"})
            for (auto* row : all())
                if (row->isVisible() && row->objectName() == "endpointRow" && row->property("text").toString() == n) {
                    for (auto* c : row->findChildren<QQuickItem*>())
                        if (c->objectName() == "endpointWatchCheck") { click(c, QString("check ") + n); break; }
                    break;
                }
        texts("endpointAddSelectedWatches", 1);
        shot("t02-tree-marked");
        click(byName("endpointAddSelectedWatches"), "Add N");
        click(byName("watchlistExpanded"), "expand");
        click(byName("startWatchButton"), "Start polling");
        log(QStringLiteral("WATCH count=%1 active=%2 actions=%3").arg(device->property("watchCount").toInt())
                .arg(device->property("watchActive").toBool()).arg(actions));
        step("T3 trend");
        actions = 0;
        openPanel("Trend");
        auto* combo = byName("trendField");
        click(combo, "trend combo");
        QTest::qWait(700);
        QStringList seen;
        QQuickItem* option = nullptr;
        for (auto* i : all())
            if (i->isVisible() && i->property("highlighted").isValid() && i->property("text").toString().contains("Power")) option = i;
        for (auto* i : all())
            if (i->isVisible() && i->property("highlighted").isValid()) seen << QString(i->metaObject()->className()).section("_QML", 0, 0) + ":" + i->property("text").toString();
        log("POPUP " + seen.join(" | "));
        click(option, "trend option Power");
        QTest::qWait(6000);
        texts("trendState", 1);
        log(QStringLiteral("TREND choice=%1 actions=%2").arg(combo ? combo->property("currentText").toString() : "?").arg(actions));
        shot("t03-trend-plot-1600x900");
        countControls("polling-with-trend");
        step("T4 narrow floating Watchlist group");
        click(byText("Watchlist", "TabButton"), "Watchlist tab");
        auto* bar = titleBar("Watchlist");
        clickObject(bar ? bar->property("floatButton").value<QObject*>() : nullptr, "watch group float");
        QTest::qWait(700);
        if (auto* fw = floatingWindow()) {
            fw->resize(280, 640);
            QTest::qWait(900);
            shot("t04-watch-floating-280", fw);
        }
        shot("t05-main-after-watch-float");
        openPanel("Reset layout");
        QTest::qWait(500);
        step("T5 compare");
        openPanel("Compare endpoint");
        QTest::qWait(500);
        shot("t06-compare");
        if (device->property("watchActive").toBool()) click(byName("startWatchButton"), "Stop");
        stopEmulator();
        QCoreApplication::quit();
    }

    void runRestore() {
        clock.start();
        main->resize(1600, 900);
        QTest::qWait(1200);
        log(QStringLiteral("RESTORED restartLayoutRestored=%1 docks[%2]")
                .arg(main->property("restartLayoutRestored").toBool()).arg(dockStates()));
        shot("r01-restored-main");
        if (auto* fw = floatingWindow()) shot("r02-restored-floating", fw);
        // Leave the user's next start clean: reset, then quit without saving.
        QMetaObject::invokeMethod(main, "resetLayout");
        QTest::qWait(400);
        shot("r03-after-reset");
        QCoreApplication::quit();
    }
};

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("TelemetryReview"));
    app.setApplicationName(QStringLiteral("DockingReviewDriver"));
    QCommandLineParser parser;
    parser.addOptions({{"out", "Screenshot directory", "dir"}, {"layout", "Layout file", "file"},
                       {"emulator", "Emulator executable", "file"}, {"port", "Emulator port", "port", "5691"},
                       {"restore", "Second process: verify the restored layout"},
                       {"trend", "Tree checkboxes, Trend plot, narrow Watchlist, Compare"}});
    parser.process(app);

    QQuickStyle::setStyle(QStringLiteral("TelemetryDark"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Basic"));
    QPalette palette;  // Same application palette as examples/qt_docking/main.cpp.
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#263b4c")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#f0f5f9")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#17202b")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#e8edf5")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#233349")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e8edf5")));
    palette.setColor(QPalette::Dark, QColor(QStringLiteral("#172331")));
    palette.setColor(QPalette::Mid, QColor(QStringLiteral("#3b5267")));
    palette.setColor(QPalette::BrightText, QColor(QStringLiteral("#bdcddd")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#215a68")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    QGuiApplication::setPalette(palette);

    QTemporaryDir temporary;
    const QString savedLayout = parser.value("layout");
    telemetry::integration::qt::ConnectionSession connection;
    telemetry::ux::QtConnectionBinding binding(&connection);
    binding.setMutationsAllowed(false);
    auto* primary = binding.createProvider();
    auto* compare = binding.createProvider();
    telemetry::ux::TrendHistory history;

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app, [](const QList<QQmlError>& errors) {
        for (const auto& e : errors) std::printf("QMLWARN %s\n", qPrintable(e.toString()));
    });
    engine.rootContext()->setContextProperty(QStringLiteral("device"), primary);
    engine.rootContext()->setContextProperty(QStringLiteral("compareDevice"), compare);
    engine.rootContext()->setContextProperty(QStringLiteral("connection"), &connection);
    engine.rootContext()->setContextProperty(QStringLiteral("trendHistory"), &history);
    engine.rootContext()->setContextProperty(QStringLiteral("defaultFile"), temporary.filePath(QStringLiteral("default.json")));
    engine.rootContext()->setContextProperty(QStringLiteral("savedFile"), savedLayout);
    engine.rootContext()->setContextProperty(QStringLiteral("savedLayoutExists"), QFile::exists(savedLayout));
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().size() != 1) return 1;

    Driver driver;
    driver.engine = &engine;
    driver.main = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    driver.connection = &connection;
    driver.device = primary;
    driver.out = parser.value("out");
    driver.emulatorExe = parser.value("emulator");
    driver.port = parser.value("port").toInt();
    QDir().mkpath(driver.out);
    LayoutCloseFilter closeFilter;
    driver.main->installEventFilter(&closeFilter);
    const bool restore = parser.isSet("restore");
    QTimer::singleShot(0, &app, [&] {
        driver.waitFor([&] { return driver.main->property("layoutReady").toBool(); }, 10000);
        if (restore) driver.runRestore();
        else if (parser.isSet("trend")) driver.runTrend();
        else driver.runMain();
    });
    const int code = app.exec();
    driver.stopEmulator();
    return code;
}
