// A real QQuickWindow drives the endpoint tree's row actions: the context menu
// (Watch, Watch this group, Show details, Copy path) by mouse and keyboard,
// Enter and the arrows in the search, and the find() entry for a host's Ctrl+K.
// No network or device I/O. MIT.
#include <QClipboard>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardItemModel>
#include <QTest>
#include <cstdio>
#include <stdexcept>

static int checks = 0, warnings = 0;
static void check(bool condition, const char* expression) {
    ++checks;
    if (!condition) throw std::runtime_error(expression);
}
#define CHECK(x) check(bool(x), #x)

enum Role { NameRole = 333, DetailRole = 444, EndpointRole = 487, CandidateRole = 500 };

class Fixture final : public QStandardItemModel {
    Q_OBJECT
public:
    QHash<int, QByteArray> roleNames() const override {
        return {{NameRole, "name"}, {DetailRole, "detail"}, {EndpointRole, "isEndpoint"}};
    }
    static QStandardItem* node(const QString& name, bool endpoint, bool candidate) {
        auto* row = new QStandardItem;
        row->setData(name, NameRole);
        row->setData(QString(), DetailRole);
        row->setData(endpoint, EndpointRole);
        row->setData(candidate, CandidateRole);
        return row;
    }
    // measure {Voltage, Current, Reset()}, device {config {Gain, Offset}, Name}, control {Start()}
    void populate() {
        auto* measure = node(QStringLiteral("measure"), false, false);
        measure->appendRow(node(QStringLiteral("Voltage"), true, true));
        measure->appendRow(node(QStringLiteral("Current"), true, true));
        measure->appendRow(node(QStringLiteral("Reset"), true, false));
        auto* device = node(QStringLiteral("device"), false, false);
        auto* config = node(QStringLiteral("config"), false, false);
        config->appendRow(node(QStringLiteral("Gain"), true, true));
        config->appendRow(node(QStringLiteral("Offset"), true, true));
        device->appendRow(config);
        device->appendRow(node(QStringLiteral("Name"), true, true));
        auto* control = node(QStringLiteral("control"), false, false);
        control->appendRow(node(QStringLiteral("Start"), true, false));
        appendRow(measure);
        appendRow(device);
        appendRow(control);
    }
    Q_INVOKABLE bool candidate(const QModelIndex& index) const {
        return index.isValid() && index.model() == this && index.data(CandidateRole).toBool();
    }
    Q_INVOKABLE QString pathOf(const QModelIndex& index) const {
        QStringList parts;
        for (auto i = index; i.isValid(); i = i.parent())
            parts.prepend(i.data(NameRole).toString());
        return parts.join(QLatin1Char('.'));
    }
};

static QQuickItem* named(QQuickItem* root, const char* name) {
    if (root->objectName() == QLatin1String(name)) return root;
    for (auto* child : root->childItems()) if (auto* found = named(child, name)) return found;
    return nullptr;
}
static QQuickItem* rowNamed(QQuickItem* root, const QString& name) {
    if (root->objectName() == QLatin1String("endpointRow") && root->property("name").toString() == name && root->isVisible())
        return root;
    for (auto* child : root->childItems()) if (auto* found = rowNamed(child, name)) return found;
    return nullptr;
}
static void settle() { QCoreApplication::processEvents(); QTest::qWait(100); }
static QPoint centre(QQuickItem* target) {
    return target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
}
static void click(QQuickWindow* window, QQuickItem* target, Qt::MouseButton button = Qt::LeftButton) {
    CHECK(target && target->isVisible());
    QTest::mouseClick(window, button, Qt::NoModifier, centre(target));
    settle();
}
static QVariant evaluate(QObject* object, const QString& expression) {
    QQmlExpression e(qmlContext(object), object, expression);
    const auto value = e.evaluate();
    if (e.hasError()) throw std::runtime_error(e.error().toString().toStdString());
    return value;
}
static QStringList watched(QObject* provider) {
    return provider->property("watched").toStringList();
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) ++warnings;
        std::fprintf(stderr, "%s\n", qPrintable(message));
    });
    try {
        Fixture model;
        model.populate();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("treeActionsModel"), &model);
        engine.load(QUrl(QStringLiteral("qrc:/TreeActions.qml")));
        CHECK(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
        CHECK(window);
        settle();
        auto* panel = named(window->contentItem(), "actionsTree");
        auto* tree = named(panel, "endpointTree");
        auto* provider = panel->property("provider").value<QObject*>();
        auto* menu = panel->findChild<QObject*>(QStringLiteral("endpointContextMenu"));
        auto* status = named(panel, "endpointWatchSelectionStatus");
        CHECK(panel && tree && provider && menu && status);
        CHECK(QMetaObject::invokeMethod(tree, "expandRecursively", Q_ARG(int, -1), Q_ARG(int, -1)));
        settle();
        const auto voltage = model.index(0, 0, model.index(0, 0));
        const auto reset = model.index(2, 0, model.index(0, 0));
        const auto offset = model.index(1, 0, model.index(0, 0, model.index(1, 0)));

        // The path is read through the named role, root first, dotted.
        CHECK(evaluate(panel, QStringLiteral("pathOf(searchSource.index(1, 0, searchSource.index(0, 0, searchSource.index(1, 0))))")).toString()
              == QStringLiteral("device.config.Offset"));
        CHECK(model.pathOf(offset) == QStringLiteral("device.config.Offset"));

        const auto openOn = [&](const QString& name) {
            auto* row = rowNamed(window->contentItem(), name);
            CHECK(row);
            click(window, row, Qt::RightButton);
            CHECK(menu->property("opened").toBool() || menu->property("visible").toBool());
        };
        const auto menuItem = [&](const char* name) {
            auto* entry = named(window->contentItem(), name);
            CHECK(entry);
            return entry;
        };

        // A Field: Watch and Show details, not Watch this group.
        openOn(QStringLiteral("Voltage"));
        CHECK(menuItem("endpointMenuWatch")->isEnabled());
        CHECK(!menuItem("endpointMenuWatchGroup")->isEnabled());
        CHECK(menuItem("endpointMenuDetails")->isEnabled());
        click(window, menuItem("endpointMenuWatch"));
        CHECK(!menu->property("visible").toBool());
        CHECK(watched(provider) == QStringList{QStringLiteral("measure.Voltage")});
        CHECK(status->property("text").toString() == QStringLiteral("Watching measure.Voltage."));
        CHECK(provider->property("selections").toInt() == 0);
        // The ticked marks are a separate selection and stay untouched.
        CHECK(evaluate(panel, QStringLiteral("watchMarks.length")).toInt() == 0);
        openOn(QStringLiteral("Voltage"));
        click(window, menuItem("endpointMenuWatch"));
        CHECK(watched(provider).size() == 1);
        CHECK(status->property("text").toString() == QStringLiteral("measure.Voltage is already watched."));

        // A group: Watch this group adds every Field under it, nested, in tree order.
        openOn(QStringLiteral("device"));
        CHECK(!menuItem("endpointMenuWatch")->isEnabled());
        CHECK(menuItem("endpointMenuWatchGroup")->isEnabled());
        CHECK(!menuItem("endpointMenuDetails")->isEnabled());
        click(window, menuItem("endpointMenuWatchGroup"));
        CHECK(watched(provider) == (QStringList{QStringLiteral("measure.Voltage"), QStringLiteral("device.config.Gain"),
                                                QStringLiteral("device.config.Offset"), QStringLiteral("device.Name")}));
        CHECK(status->property("text").toString().startsWith(QStringLiteral("Watching 3 from device;")));
        // A group with only a Command offers the action and says why nothing happened.
        const int before = provider->property("submissions").toInt();
        openOn(QStringLiteral("control"));
        click(window, menuItem("endpointMenuWatchGroup"));
        CHECK(provider->property("submissions").toInt() == before);
        CHECK(status->property("text").toString() == QStringLiteral("Nothing in control can be watched."));

        // A Command: no Watch; Show details selects it.
        openOn(QStringLiteral("Reset"));
        CHECK(!menuItem("endpointMenuWatch")->isEnabled());
        click(window, menuItem("endpointMenuDetails"));
        CHECK(provider->property("selections").toInt() == 1);
        CHECK(provider->property("selectedEndpointIndex").value<QModelIndex>() == reset);

        // Copy path puts the dotted path on the clipboard.
        QGuiApplication::clipboard()->clear();
        openOn(QStringLiteral("Current"));
        click(window, menuItem("endpointMenuCopyPath"));
        CHECK(QGuiApplication::clipboard()->text() == QStringLiteral("measure.Current"));
        CHECK(status->property("text").toString() == QStringLiteral("Copied measure.Current."));

        // The Menu key and Shift+F10 open the menu on the keyboard's current row.
        auto* selection = qobject_cast<QItemSelectionModel*>(tree->property("selectionModel").value<QObject*>());
        CHECK(selection);
        selection->setCurrentIndex(voltage, QItemSelectionModel::NoUpdate);
        tree->forceActiveFocus();
        settle();
        QTest::keyClick(window, Qt::Key_Menu);
        settle();
        CHECK(menu->property("visible").toBool());
        CHECK(evaluate(menu, QStringLiteral("target === treeActionsModel.index(0, 0, treeActionsModel.index(0, 0))")).toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        settle();
        CHECK(!menu->property("visible").toBool());
        tree->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_F10, Qt::ShiftModifier);
        settle();
        CHECK(menu->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        settle();

        // While polling the Watchlist cannot change: the actions say so.
        CHECK(provider->setProperty("watchActive", true));
        openOn(QStringLiteral("Current"));
        CHECK(!menuItem("endpointMenuWatch")->isEnabled());
        QTest::keyClick(window, Qt::Key_Escape);
        settle();
        CHECK(!evaluate(panel, QStringLiteral("watchIndex(searchSource.index(1, 0, searchSource.index(0, 0)))")).toBool());
        CHECK(status->property("text").toString() == QStringLiteral("Stop polling to change the Watchlist."));
        CHECK(provider->setProperty("watchActive", false));

        // find(): the host's Ctrl+K focuses the search with a query; Enter
        // watches the first result, and the status says Enter does that.
        auto* search = named(panel, "endpointSearch");
        auto* searchStatus = named(panel, "endpointSearchStatus");
        CHECK(search && searchStatus);
        CHECK(QMetaObject::invokeMethod(panel, "find", Q_ARG(QVariant, QStringLiteral("Current"))));
        QTest::qWait(250);
        CHECK(search->hasActiveFocus() && search->property("text").toString() == QStringLiteral("Current"));
        CHECK(searchStatus->property("text").toString() == QStringLiteral("1 matching endpoints; Enter watches the first"));
        QTest::keyClick(window, Qt::Key_Return);
        settle();
        CHECK(watched(provider).contains(QStringLiteral("measure.Current")));
        CHECK(status->property("text").toString() == QStringLiteral("Watching measure.Current."));
        // A Command's first result opens its details instead.
        const int selections = provider->property("selections").toInt();
        CHECK(QMetaObject::invokeMethod(panel, "find", Q_ARG(QVariant, QStringLiteral("Start"))));
        QTest::qWait(250);
        QTest::keyClick(window, Qt::Key_Enter);
        settle();
        CHECK(provider->property("selections").toInt() == selections + 1);
        CHECK(provider->property("selectedEndpointIndex").value<QModelIndex>() == model.index(0, 0, model.index(2, 0)));
        // Down and Up walk from the field into the results and back.
        CHECK(QMetaObject::invokeMethod(panel, "find", Q_ARG(QVariant, QStringLiteral("measure"))));
        QTest::qWait(250);
        auto* results = named(panel, "endpointSearchResults");
        CHECK(results && results->property("count").toInt() == 3);
        QTest::keyClick(window, Qt::Key_Down);
        settle();
        auto* focused = window->activeFocusItem();
        CHECK(focused && focused->objectName() == QStringLiteral("endpointSearchResult") &&
              focused->property("index").toInt() == 0);
        QTest::keyClick(window, Qt::Key_Down);
        settle();
        focused = window->activeFocusItem();
        CHECK(focused && focused->property("index").toInt() == 1);
        // The menu opens on a result as on a tree row.
        QTest::keyClick(window, Qt::Key_Menu);
        settle();
        CHECK(menu->property("visible").toBool());
        CHECK(evaluate(menu, QStringLiteral("target === treeActionsModel.index(1, 0, treeActionsModel.index(0, 0))")).toBool());
        CHECK(menuItem("endpointMenuDetails")->isEnabled());
        QTest::keyClick(window, Qt::Key_Escape);
        settle();
        QTest::keyClick(window, Qt::Key_Up);
        settle();
        QTest::keyClick(window, Qt::Key_Up);
        settle();
        CHECK(search->hasActiveFocus());
        // Without a query, find() only focuses the field and keeps its text.
        CHECK(QMetaObject::invokeMethod(panel, "find", Q_ARG(QVariant, QVariant())));
        CHECK(search->property("text").toString() == QStringLiteral("measure"));
        CHECK(QMetaObject::invokeMethod(panel, "find", Q_ARG(QVariant, QString())));
        settle();
        // A structural change closes an open menu and drops its index: no
        // action can reach the provider with an index from before the change.
        openOn(QStringLiteral("Name"));
        CHECK(evaluate(menu, QStringLiteral("target !== undefined")).toBool());
        model.removeRow(2);
        settle();
        CHECK(!menu->property("visible").toBool());
        CHECK(evaluate(menu, QStringLiteral("target === undefined")).toBool());
        const int submissionsBefore = provider->property("submissions").toInt();
        // A closed menu's items are out of the scene; reach them through the menu.
        auto* staleWatch = menu->findChild<QQuickItem*>(QStringLiteral("endpointMenuWatch"));
        CHECK(staleWatch && !staleWatch->isEnabled());
        CHECK(QMetaObject::invokeMethod(staleWatch, "triggered"));
        settle();
        CHECK(provider->property("submissions").toInt() == submissionsBefore);
        CHECK(warnings == 0);
        std::printf("QML tree-actions checks=%d warnings=0\n", checks);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "QML tree-actions check %d failed: %s\n", checks, e.what());
        return 1;
    }
}
#include "TreeActions.moc"
