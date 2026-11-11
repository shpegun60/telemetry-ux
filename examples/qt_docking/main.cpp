// Standalone, read-only Qt Quick docking example using installed Telemetry packages.
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <Telemetry/UX/TrendHistory.hpp>
#include <integration/qt/ConnectionSession.hpp>

#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QPalette>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <cstdio>
#include <functional>

namespace {
// Save before KDDock closes child docks; QML onClosing may be too late.
class LayoutCloseFilter final : public QObject {
public:
    explicit LayoutCloseFilter(bool enabled) : enabled_(enabled) {}
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (enabled_ && event->type() == QEvent::Close) {
            QVariant saved;
            if (!QMetaObject::invokeMethod(watched, "saveLayoutBeforeClose", Q_RETURN_ARG(QVariant, saved)) || !saved.toBool())
                qWarning("Could not save the Telemetry docking layout");
        }
        return false;
    }
private:
    bool enabled_;
};

void wait(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

// A real press and release delivered to the item's window, as a mouse would.
bool click(QQuickItem* item)
{
    if (!item || !item->window() || !item->isVisible())
        return false;
    auto* window = item->window();
    const QPointF scene = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    const QPointF global = window->mapToGlobal(scene);
    QMouseEvent press(QEvent::MouseButtonPress, scene, scene, global, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QGuiApplication::sendEvent(window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, scene, scene, global, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QGuiApplication::sendEvent(window, &release);
    wait(400);
    return true;
}

// KDDockWidgets title bars are visual children whose QObject parent differs,
// so the search follows childItems() rather than findChildren().
QQuickItem* findTitleBar(QQuickItem* item, const QString& title)
{
    if (!item || !item->isVisible())
        return nullptr;
    if (item->property("floatButton").isValid() && item->property("title").toString() == title)
        return item;
    for (auto* child : item->childItems())
        if (auto* found = findTitleBar(child, title))
            return found;
    return nullptr;
}

// The visible KDDockWidgets title bar showing this title, in any window.
QQuickItem* titleBar(const QString& title)
{
    for (auto* window : QGuiApplication::allWindows()) {
        auto* quick = qobject_cast<QQuickWindow*>(window);
        if (quick && quick->isVisible())
            if (auto* found = findTitleBar(quick->contentItem(), title))
                return found;
    }
    return nullptr;
}

QObject* dock(QObject* root, const QString& uniqueName)
{
    for (auto* object : root->findChildren<QObject*>())
        if (object->property("uniqueName").toString() == uniqueName && object->property("isOpen").isValid())
            return object;
    return nullptr;
}
} // namespace

int main(int argc, char** argv)
{
#if defined(Q_OS_WIN) && defined(TELEMETRY_KDDW_BIN_DIR)
    // The KDDockWidgets QML plugin loads its library from this directory.
    qputenv("PATH", QByteArray(TELEMETRY_KDDW_BIN_DIR) + ';' + qgetenv("PATH"));
#endif
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Telemetry"));
    app.setApplicationName(QStringLiteral("QtDockingExample"));
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption smokeOption(QStringLiteral("smoke-test"),
        QStringLiteral("Exercise layout in isolated temporary files without connecting"));
    const QCommandLineOption fileOption(QStringLiteral("layout-file"),
        QStringLiteral("Save and restore the layout in this file instead of the user's settings"),
        QStringLiteral("path"));
    const QCommandLineOption prepareOption(QStringLiteral("prepare-layout"),
        QStringLiteral("Check: open and float docks away from the default, save, exit"));
    const QCommandLineOption expectOption(QStringLiteral("expect-layout"),
        QStringLiteral("Check: the restored layout must be 'restored' or 'default'"), QStringLiteral("which"));
    const QCommandLineOption titleBarOption(QStringLiteral("title-bar-check"),
        QStringLiteral("Check: float the Device tree with its title-bar button and dock it back"));
    parser.addOptions({smokeOption, fileOption, prepareOption, expectOption, titleBarOption});
    parser.process(app);
    const bool smoke = parser.isSet(smokeOption);
    const QString expected = parser.value(expectOption);
    const bool checking = smoke || parser.isSet(prepareOption) || !expected.isEmpty() || parser.isSet(titleBarOption);
    if (!expected.isEmpty() && expected != QStringLiteral("restored") && expected != QStringLiteral("default")) {
        std::fprintf(stderr, "--expect-layout takes 'restored' or 'default'\n");
        return 1;
    }

    QQuickStyle::setStyle(QStringLiteral("TelemetryDark"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Basic"));
    QPalette palette;
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
    if (!temporary.isValid()) return 1;
    QString savedLayout = parser.value(fileOption);
    if (savedLayout.isEmpty()) {
        const auto directory = smoke ? temporary.path() : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        if (directory.isEmpty() || !QDir().mkpath(directory)) return 1;
        savedLayout = QDir(directory).filePath(QStringLiteral("layout.json"));
    }

    // One connection owns the FIFO; presentations have independent selections.
    telemetry::integration::qt::ConnectionSession connection;
    telemetry::ux::QtConnectionBinding binding(&connection);
    binding.setMutationsAllowed(false);
    auto* primary = binding.createProvider();
    auto* compare = binding.createProvider();
    if (!primary || !compare || primary == compare) return 1;
    telemetry::ux::TrendHistory history;

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
#ifdef TELEMETRY_KDDW_QML_IMPORT_DIR
    engine.addImportPath(QStringLiteral(TELEMETRY_KDDW_QML_IMPORT_DIR));
#endif
    unsigned warnings = 0;
    QStringList errors;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app, [&](const QList<QQmlError>& list) {
        warnings += unsigned(list.size());
        for (const auto& error : list)
            errors.append(error.toString());
    });
    engine.rootContext()->setContextProperty(QStringLiteral("device"), primary);
    engine.rootContext()->setContextProperty(QStringLiteral("compareDevice"), compare);
    engine.rootContext()->setContextProperty(QStringLiteral("connection"), &connection);
    engine.rootContext()->setContextProperty(QStringLiteral("trendHistory"), &history);
    engine.rootContext()->setContextProperty(QStringLiteral("defaultFile"), temporary.filePath(QStringLiteral("default.json")));
    engine.rootContext()->setContextProperty(QStringLiteral("savedFile"), savedLayout);
    engine.rootContext()->setContextProperty(QStringLiteral("savedLayoutExists"), QFile::exists(savedLayout));
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().size() != 1) {
        if (errors.join(QLatin1Char('\n')).contains(QStringLiteral("com.kdab.dockwidgets"))) {
            std::fprintf(stderr,
                "This example needs the KDDockWidgets 2.5 Qt Quick QML module (com.kdab.dockwidgets),\n"
                "built for this exact Qt version with KDDockWidgets_FRONTENDS=qtquick and\n"
                "KDDockWidgets_QML_MODULE=ON. Configure this example with\n"
                "-DKDDOCKWIDGETS_QML_IMPORT_DIR=<dir containing com/kdab/dockwidgets> and, on Windows,\n"
                "-DKDDOCKWIDGETS_BIN_DIR=<dir of the KDDockWidgets DLL>, or set QML_IMPORT_PATH and PATH.\n"
                "See README.md in this directory.\n");
            return 2;
        }
        for (const auto& error : errors)
            std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    auto* root = engine.rootObjects().first();

    LayoutCloseFilter closeFilter(!checking);
    root->installEventFilter(&closeFilter);
    if (!checking)
        return app.exec();

    // Checks run once the layout has been restored (or its default established).
    const std::function<bool()> check = [&]() -> bool {
        if (smoke) {
            QVariant result;
            return QMetaObject::invokeMethod(root, "smokeCheck", Q_RETURN_ARG(QVariant, result)) &&
                   result.toBool() && !connection.ready() && !connection.busy();
        }
        if (parser.isSet(prepareOption)) {
            QVariant prepared, saved;
            QMetaObject::invokeMethod(root, "prepareNonDefaultLayout", Q_RETURN_ARG(QVariant, prepared));
            wait(300);
            QMetaObject::invokeMethod(root, "saveLayoutBeforeClose", Q_RETURN_ARG(QVariant, saved));
            std::printf("prepared=%d saved=%d\n", prepared.toBool(), saved.toBool());
            return prepared.toBool() && saved.toBool();
        }
        if (!expected.isEmpty()) {
            QVariant state;
            QMetaObject::invokeMethod(root, "layoutState", Q_RETURN_ARG(QVariant, state));
            const auto s = state.toMap();
            std::printf("layout restored=%d connection=%d compare=%d tree=%d treeFloating=%d "
                        "trend=%d trendFloating=%d endpoint=%d watch=%d\n",
                        s["restored"].toBool(), s["connection"].toBool(), s["compare"].toBool(),
                        s["tree"].toBool(), s["treeFloating"].toBool(), s["trend"].toBool(),
                        s["trendFloating"].toBool(), s["endpoint"].toBool(), s["watch"].toBool());
            if (expected == QStringLiteral("restored"))
                return s["restored"].toBool() && s["connection"].toBool() && s["compare"].toBool() &&
                       s["tree"].toBool() && s["treeFloating"].toBool() && s["trend"].toBool() &&
                       s["trendFloating"].toBool();
            return !s["restored"].toBool() && !s["connection"].toBool() && !s["compare"].toBool() &&
                   s["tree"].toBool() && !s["treeFloating"].toBool() && !s["trend"].toBool() &&
                   s["endpoint"].toBool() && s["watch"].toBool();
        }
        // The title-bar float button floats the dock and, from its floating
        // window, docks it back: actual mouse events, not property writes.
        auto* tree = dock(root, QStringLiteral("telemetry.tree"));
        auto* bar = titleBar(QStringLiteral("Device tree"));
        auto* floatButton = bar ? qobject_cast<QQuickItem*>(bar->property("floatButton").value<QObject*>()) : nullptr;
        std::printf("title bar: dock=%d bar=%d button=%d visible=%d\n", tree != nullptr, bar != nullptr,
                    floatButton != nullptr, floatButton && floatButton->isVisible());
        const bool floated = tree && click(floatButton) &&
            tree->property("isFloating").toBool() && tree->property("isOpen").toBool();
        auto* floatingBar = floated ? titleBar(QStringLiteral("Device tree")) : nullptr;
        const bool docked = floatingBar &&
            click(qobject_cast<QQuickItem*>(floatingBar->property("floatButton").value<QObject*>())) &&
            !tree->property("isFloating").toBool() && tree->property("isOpen").toBool();
        std::printf("title bar: floated=%d docked=%d\n", floated, docked);
        return floated && docked;
    };

    QTimer timer;
    int attempts = 0;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (!root->property("layoutReady").toBool() && ++attempts < 200) return;
        timer.stop();
        const bool ok = check() && warnings == 0;
        std::printf("Qt docking check: %s; QML warnings=%u\n", ok ? "PASS" : "FAIL", warnings);
        std::fflush(stdout);
        app.exit(ok ? 0 : 1);
    });
    timer.start(25);
    return app.exec();
}
