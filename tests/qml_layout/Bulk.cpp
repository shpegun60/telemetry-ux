// A real QQuickWindow exercises marking without changing editor selection or I/O. MIT.
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
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) ++warnings;
        std::fprintf(stderr, "%s\n", qPrintable(message));
    });
    try {
        Fixture model;
        model.populate();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("bulkFixtureModel"), &model);
        engine.load(QUrl(QStringLiteral("qrc:/Bulk.qml")));
        CHECK(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
        CHECK(window);
        settle();
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
        // The row is the type scale's row height, its box 16 px in a 24 px target.
        CHECK(qAbs(first->height() - evaluate(panel, "Type.rowHeight").toReal()) < 0.5);
        auto* box = named(first, "endpointWatchBox");
        CHECK(box && box->width() == 16 && box->height() == 16 && checkBox->height() >= 24);
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
        // The same source index has one mark across tree and filtered search.
        auto* query = named(panel, "endpointSearch");
        CHECK(query && query->setProperty("text", QStringLiteral("Field 0")));
        QTest::qWait(250);
        auto* searchCheck = named(panel, "endpointSearchWatchCheck");
        CHECK(searchCheck && searchCheck->property("checked").toBool());
        click(window, searchCheck);
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 0);
        click(window, searchCheck);
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 1);
        CHECK(provider->property("selections").toInt() == 0 && provider->property("submissions").toInt() == 0);
        auto* searchRow = named(panel, "endpointSearchResult");
        CHECK(searchRow);
        searchRow->forceActiveFocus(); settle();
        QTest::keyClick(window, Qt::Key_Space); settle();
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 0);
        QTest::keyClick(window, Qt::Key_Space); settle();
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 1);
        CHECK(provider->property("selections").toInt() == 0);
        CHECK(query->setProperty("text", QString()));
        settle();
        auto* selection = qobject_cast<QItemSelectionModel*>(tree->property("selectionModel").value<QObject*>());
        CHECK(selection);
        selection->setCurrentIndex(model.index(1, 0), QItemSelectionModel::NoUpdate);
        tree->forceActiveFocus(); settle();
        QTest::keyClick(window, Qt::Key_Space); settle();
        CHECK(evaluate(panel, "watchMarks.length").toInt() == 2);
        QTest::keyClick(window, Qt::Key_Return); settle();
        CHECK(provider->property("selections").toInt() == 1);
        CHECK(provider->property("selectedEndpointIndex").value<QModelIndex>() == model.index(1, 0));
        // A Command cannot be marked, even with the keyboard.
        selection->setCurrentIndex(model.index(40, 0), QItemSelectionModel::NoUpdate);
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
        // Search result and model removal: no stale index may survive.
        CHECK(query && query->setProperty("text", QStringLiteral("Field 3")));
        QTest::qWait(250);
        searchCheck = named(panel, "endpointSearchWatchCheck");
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
