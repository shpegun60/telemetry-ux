// A real QQuickWindow exercises marking without changing editor selection or I/O. MIT.
#include "ScriptSteps.hpp"
#include <Telemetry/UX/QmlTypes.hpp>
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

class Fixture final : public QStandardItemModel {
    Q_OBJECT
public:
    QHash<int, QByteArray> roleNames() const override {
        return {{333, "name"}, {444, "detail"}, {487, "isEndpoint"}};
    }
    void populate() {
        for (int i = 0; i < 40; ++i) {
            auto* row = new QStandardItem;
            row->setData(QStringLiteral("Field %1").arg(i), 333);
            row->setData(QString(), 444);
            row->setData(true, 487);
            row->setData(true, 500);
            appendRow(row);
        }
        auto* command = new QStandardItem;
        command->setData(QStringLiteral("Command"), 333);
        command->setData(QString(), 444);
        command->setData(true, 487);
        command->setData(false, 500);
        appendRow(command);
    }
    Q_INVOKABLE bool candidate(const QModelIndex& index) const {
        return index.isValid() && index.model() == this && index.data(500).toBool();
    }
};

static QQuickItem* named(QQuickItem* root, const char* name, int row = -1) {
    if (root->objectName() == QLatin1String(name) && (row < 0 || root->property("row").toInt() == row)) return root;
    for (auto* child : root->childItems()) if (auto* found = named(child, name, row)) return found;
    return nullptr;
}
static void settle() { QCoreApplication::processEvents(); QTest::qWait(100); }
// Whether a row delegate is the one its view shows at its row, not one the
// view keeps in its pool for reuse (pooled delegates stay visible, culled).
static bool live(QQuickItem* row) {
    auto* view = row->parentItem() ? row->parentItem()->parentItem() : nullptr;
    QQuickItem* shown = nullptr;
    return view && QMetaObject::invokeMethod(view, "itemAtCell", Q_RETURN_ARG(QQuickItem*, shown),
                                             Q_ARG(QPoint, QPoint(0, row->property("row").toInt()))) && shown == row;
}
// The visible tree row of that name.
static QQuickItem* rowNamed(QQuickItem* root, const QString& name) {
    if (!root->isVisible()) return nullptr;
    if (root->objectName() == QLatin1String("endpointRow") && root->property("name").toString() == name && live(root)) return root;
    for (auto* child : root->childItems()) if (auto* found = rowNamed(child, name)) return found;
    return nullptr;
}
// The tree shows the model through its find filter: the tree's index of a row.
static QModelIndex shown(QObject* panel, const QModelIndex& source) {
    QVariant result;
    if (!QMetaObject::invokeMethod(panel, "shownOf", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant::fromValue(source))))
        throw std::runtime_error("shownOf");
    return result.value<QModelIndex>();
}
static void click(QQuickWindow* window, QQuickItem* target) {
    CHECK(target && target->isVisible());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      target->mapToScene(QPointF(target->width()/2, target->height()/2)).toPoint());
    settle();
}
static QVariant evaluate(QObject* object, const char* expression) {
    QQmlExpression e(qmlContext(object), object, QString::fromLatin1(expression));
    const auto value = e.evaluate();
    if (e.hasError()) throw std::runtime_error(e.error().toString().toStdString());
    return value;
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    telemetry::ux::registerQmlTypes();
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) ++warnings;
        std::fprintf(stderr, "%s\n", qPrintable(message));
    });
    try {
        Fixture model;
        model.populate();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("bulkFixtureModel"), &model);
        ScriptSteps steps;
        engine.rootContext()->setContextProperty(QStringLiteral("scriptSteps"), &steps);
        engine.load(QUrl(QStringLiteral("qrc:/Bulk.qml")));
        CHECK(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
        CHECK(window);
        settle();
        // Watch boxes (T-1) on the library's floor Qt: a box is watched at
        // once and shows what the provider says; a Command has none; while
        // polling the boxes are off and the line says why; a refusal at
        // capacity is said and changes nothing.
        {
            auto* boxesWindow = window->findChild<QQuickWindow*>(QStringLiteral("boxesWindow"));
            CHECK(boxesWindow);
            auto* boxTree = named(boxesWindow->contentItem(), "boxTree");
            CHECK(boxTree);
            auto* boxes = boxTree->property("provider").value<QObject*>();
            auto* firstRow = named(boxTree, "endpointRow", 0);
            auto* firstBox = firstRow ? named(firstRow, "endpointWatchCheck") : nullptr;
            auto* status = named(boxTree, "endpointWatchSelectionStatus");
            CHECK(boxes && firstRow && firstBox && status && evaluate(boxTree, "watchApi").toBool());
            CHECK(!named(boxTree, "endpointAddSelectedWatches")->isVisible());
            click(boxesWindow, firstBox);
            CHECK(boxes->property("calls").toInt() == 1 && firstRow->property("watchState").toInt() == Qt::Checked);
            click(boxesWindow, firstBox);
            CHECK(boxes->property("calls").toInt() == 2 && firstRow->property("watchState").toInt() == Qt::Unchecked);
            CHECK(evaluate(boxTree, "stateOf(searchSource.index(40, 0), watchRevision)").toInt() == -1);
            CHECK(boxes->setProperty("watchActive", true));
            settle();
            CHECK(!firstBox->isEnabled() && status->property("text").toString() == QStringLiteral("Stop polling to change the Watchlist."));
            // A provider that takes changes while polling (fix 8): the boxes stay on.
            CHECK(boxes->setProperty("watchChangesWhilePolling", true));
            settle();
            CHECK(firstBox->isEnabled() && status->property("text").toString() != QStringLiteral("Stop polling to change the Watchlist."));
            click(boxesWindow, firstBox);
            CHECK(boxes->property("calls").toInt() == 3 && firstRow->property("watchState").toInt() == Qt::Checked);
            click(boxesWindow, firstBox);
            CHECK(boxes->property("calls").toInt() == 4 && firstRow->property("watchState").toInt() == Qt::Unchecked);
            CHECK(boxes->setProperty("watchChangesWhilePolling", false));
            CHECK(boxes->setProperty("watchActive", false));
            CHECK(boxes->setProperty("full", true));
            settle();
            click(boxesWindow, firstBox);
            CHECK(status->property("text").toString().startsWith(QStringLiteral("No room")) &&
                  firstRow->property("watchState").toInt() == Qt::Unchecked);
            CHECK(boxes->setProperty("full", false));
            // The step a box made is offered on the status line while it is the
            // one Undo would take (UR-1); the link takes it back and says so.
            click(boxesWindow, firstBox);
            CHECK(firstRow->property("watchState").toInt() == Qt::Checked);
            auto* undo = named(boxTree, "undoNoticeLink");
            CHECK(undo && undo->isVisible() && status->property("text").toString().startsWith(QStringLiteral("Added ")) &&
                  undo->property("text").toString() == QStringLiteral("Undo") &&
                  evaluate(undo, "Accessible.name").toString() == QStringLiteral("Undo: Add 1 values"));
            click(boxesWindow, undo);
            // Taken, the line says so and offers it again.
            CHECK(firstRow->property("watchState").toInt() == Qt::Unchecked && undo->isVisible() &&
                  undo->property("text").toString() == QStringLiteral("Redo") &&
                  status->property("text").toString() == QStringLiteral("Undone: Add 1 values"));
            click(boxesWindow, undo);
            CHECK(firstRow->property("watchState").toInt() == Qt::Checked &&
                  status->property("text").toString() == QStringLiteral("Redone: Add 1 values") &&
                  undo->property("text").toString() == QStringLiteral("Undo"));
            // Ctrl+Z in the tree is the window's router's: the same step.
            boxesWindow->requestActivate();
            CHECK(QTest::qWaitForWindowActive(boxesWindow));
            QTest::keyClick(boxesWindow, Qt::Key_Z, Qt::ControlModifier);
            settle();
            CHECK(firstRow->property("watchState").toInt() == Qt::Unchecked &&
                  status->property("text").toString() == QStringLiteral("Undone: Add 1 values"));
            // A box changed by nothing that records leaves no link behind.
            CHECK(boxes->setProperty("history", QVariant::fromValue<QObject*>(nullptr)));
            click(boxesWindow, firstBox);
            CHECK(firstRow->property("watchState").toInt() == Qt::Checked && !undo->isVisible());
            click(boxesWindow, firstBox);
            // A second device's tree offers no watching: no boxes, no Add bar,
            // no Watch in its menu, and nothing reserved for them.
            auto* compareWindow = window->findChild<QQuickWindow*>(QStringLiteral("compareWindow"));
            CHECK(compareWindow);
            auto* compareTree = named(compareWindow->contentItem(), "compareTree");
            CHECK(compareTree && !evaluate(compareTree, "watchApi").toBool() &&
                  !evaluate(compareTree, "bulkWatchAvailable").toBool());
            auto* compareRow = named(compareTree, "endpointRow", 0);
            auto* compareBox = compareRow ? named(compareRow, "endpointWatchCheck") : nullptr;
            CHECK(compareRow && compareBox && !compareBox->isVisible() && !compareRow->property("watchable").toBool());
            compareWindow->close();
            // The checks below type into the main window: give it back the keyboard.
            boxesWindow->close();
            window->requestActivate();
            CHECK(QTest::qWaitForWindowActive(window));
            settle();
        }
        auto* panel = named(window->contentItem(), "bulkTree");
        auto* tree = named(panel, "endpointTree");
        auto* first = named(panel, "endpointRow", 0);
        auto* checkBox = named(first, "endpointWatchCheck");
        auto* add = named(panel, "endpointAddSelectedWatches");
        auto* provider = panel->property("provider").value<QObject*>();
        CHECK(panel && tree && first && checkBox && add && provider);
        auto* search = named(panel, "endpointSearch");
        auto* status = named(panel, "endpointWatchSelectionStatus");
        CHECK(search && status && !add->isVisible() && !named(panel, "endpointTreeTitle"));
        // The row is the type scale's row height; its box is the scale's even
        // check size, drawn in a row-square target (RowCheckBox).
        const qreal rowHeight = evaluate(panel, "Type.rowHeight").toReal();
        const qreal checkSize = evaluate(panel, "Type.checkSize").toReal();
        CHECK(qAbs(first->height() - rowHeight) < 0.5);
        auto* box = named(first, "endpointWatchBox");
        CHECK(box && checkSize >= 12 && int(checkSize) % 2 == 0 && box->width() == checkSize &&
              box->height() == checkSize && qAbs(checkBox->height() - rowHeight) < 0.5);
        click(window, checkBox);
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 1);
        // A narrow dock must not paint the bulk actions over the search.
        window->setWidth(280); settle();
        CHECK(add->isVisible());
        const auto searchRect = search->mapRectToScene(QRectF(0, 0, search->width(), search->height()));
        const auto addRect = add->mapRectToScene(QRectF(0, 0, add->width(), add->height()));
        CHECK(!searchRect.intersects(addRect));
        CHECK(searchRect.right() <= panel->mapRectToScene(QRectF(0, 0, panel->width(), panel->height())).right());
        // A short dock keeps the tree and the reserved status line.
        window->setHeight(280); settle();
        CHECK(tree->height() >= 60 && status->isVisible() && status->height() > 0);
        window->setHeight(700); settle();
        window->setWidth(600); settle();
        CHECK(provider->property("selections").toInt() == 0 && provider->property("submissions").toInt() == 0);
        CHECK(provider->property("draft").toString() == QStringLiteral("untouched"));
        // The same source index has one mark in the tree and in the tree
        // filtered by a find.
        auto* query = named(panel, "endpointSearch");
        CHECK(query && query->setProperty("text", QStringLiteral("Field 0")));
        QTest::qWait(250);
        CHECK(tree->property("rows").toInt() == 1);
        auto* searchRow = rowNamed(panel, QStringLiteral("Field 0"));
        auto* searchCheck = searchRow ? named(searchRow, "endpointWatchCheck") : nullptr;
        CHECK(searchCheck && searchCheck->property("checked").toBool());
        click(window, searchCheck);
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 0);
        click(window, searchCheck);
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 1);
        CHECK(provider->property("selections").toInt() == 0 && provider->property("submissions").toInt() == 0);
        // Down enters the tree at the match; Space marks it there, as its box.
        query->forceActiveFocus(); settle();
        QTest::keyClick(window, Qt::Key_Down); settle();
        CHECK(tree->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Space); settle();
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 0);
        QTest::keyClick(window, Qt::Key_Space); settle();
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 1);
        CHECK(provider->property("selections").toInt() == 0);
        CHECK(query->setProperty("text", QString()));
        QTest::qWait(250);
        settle();
        auto* selection = qobject_cast<QItemSelectionModel*>(tree->property("selectionModel").value<QObject*>());
        CHECK(selection);
        selection->setCurrentIndex(shown(panel, model.index(1, 0)), QItemSelectionModel::NoUpdate);
        tree->forceActiveFocus(); settle();
        QTest::keyClick(window, Qt::Key_Space); settle();
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 2);
        QTest::keyClick(window, Qt::Key_Return); settle();
        CHECK(provider->property("selections").toInt() == 1);
        CHECK(provider->property("selectedEndpointIndex").value<QModelIndex>() == model.index(1, 0));
        // A Command cannot be marked, even with the keyboard.
        selection->setCurrentIndex(shown(panel, model.index(40, 0)), QItemSelectionModel::NoUpdate);
        QTest::keyClick(window, Qt::Key_Space); settle();
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 2);
        CHECK(provider->setProperty("refuse", true));
        click(window, add);
        CHECK(provider->property("submissions").toInt() == 1);
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 2);
        CHECK(panel->property("watchMarkStatus").toString().contains(QStringLiteral("refused")));
        CHECK(provider->setProperty("refuse", false));
        CHECK(provider->setProperty("reportUnsupported", true));
        click(window, add);
        CHECK(provider->property("submissions").toInt() == 2);
        CHECK(provider->property("submitted").toList().size() == 2);
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 0);
        CHECK(panel->property("watchMarkStatus").toString().contains(QStringLiteral("unsupported 1")));
        CHECK(provider->setProperty("reportUnsupported", false));
        // The selection can exceed the wire's per-request batch size.
        evaluate(panel, "for (let i = 0; i < 40; ++i) toggleWatchMark(searchSource.index(i, 0))");
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 40);
        CHECK(provider->setProperty("watchActive", true)); settle();
        CHECK(!add->isEnabled());
        CHECK(provider->setProperty("watchActive", false)); settle();
        click(window, add);
        CHECK(provider->property("submitted").toList().size() == 40);
        CHECK(provider->property("selections").toInt() == 1);
        // A found row's mark and model removal: no stale index may survive.
        CHECK(query && query->setProperty("text", QStringLiteral("Field 3")));
        QTest::qWait(250);
        searchRow = rowNamed(panel, QStringLiteral("Field 3"));
        searchCheck = searchRow ? named(searchRow, "endpointWatchCheck") : nullptr;
        click(window, searchCheck);
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 1);
        model.removeRow(3);
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 0);
        QTest::qWait(200);
        CHECK(query->setProperty("text", QString()));
        evaluate(panel, "toggleWatchMark(searchSource.index(0, 0))");
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 1);
        CHECK(panel->setProperty("provider", window->property("legacyProvider")));
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 0);
        CHECK(!panel->property("bulkWatchAvailable").toBool());
        CHECK(panel->setProperty("provider", QVariant::fromValue(provider)));
        evaluate(panel, "toggleWatchMark(searchSource.index(0, 0))");
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 1);
        model.clear();
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 0);
        settle();
        CHECK(warnings == 0);
        std::printf("QML bulk-selection checks=%d warnings=0\n", checks);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "QML bulk-selection check %d failed: %s\n", checks, e.what());
        return 1;
    }
}
#include "Bulk.moc"
