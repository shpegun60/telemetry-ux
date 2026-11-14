// Real keyboard/pointer checks over the reusable typed Watchlist editor. MIT.
#include <QtCore/QDir>
#include <QtGui/QColor>
#include <QtGui/QGuiApplication>
#include <QtGui/QStandardItemModel>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlProperty>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QTest>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
int checks = 0, warnings = 0;
void check(bool condition, const char* expression) {
    ++checks;
    if (!condition) throw std::runtime_error(expression);
}
#define CHECK(x) check(bool(x), #x)
double luminance(const QColor& color) {
    const auto channel = [](double value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}
enum Role { Name = Qt::UserRole + 1, Status, Text, Writable, EditorKind, EditorText,
    Choices, ChoicesTruncated, SampleValue, SampleExact, TypeText };
class Model final : public QStandardItemModel {
public:
    QHash<int, QByteArray> roleNames() const override {
        return {{Name,"name"}, {Status,"status"}, {Text,"text"}, {Writable,"writable"},
            {EditorKind,"editorKind"}, {EditorText,"editorText"}, {Choices,"editorChoices"},
            {ChoicesTruncated,"editorChoicesTruncated"}, {SampleValue,"sampleValue"},
            {SampleExact,"sampleExact"}, {TypeText,"typeText"}};
    }
    Model() {
        const auto choice = [](const QString& label, const QString& text) {
            return QVariantMap{{QStringLiteral("label"),label}, {QStringLiteral("text"),text}};
        };
        const QVariantList boolean{choice(QStringLiteral("false (0)"),QStringLiteral("false")),
            choice(QStringLiteral("true (1)"),QStringLiteral("true"))};
        const QVariantList modes{choice(QStringLiteral("Idle (-2)"),QStringLiteral("-2")),
            choice(QStringLiteral("Active (7)"),QStringLiteral("7"))};
        const QStringList names{QStringLiteral("Enabled"),QStringLiteral("OperatingMode"),
            QStringLiteral("Counter"),QStringLiteral("LimitedMode")};
        for (int i = 0; i < names.size(); ++i) {
            auto* row = new QStandardItem;
            row->setData(names[i],Name); row->setData(QStringLiteral("Ok"),Status);
            row->setData(true,Writable); row->setData(QString::fromLatin1(i == 0 ? "bool" : i == 2 ? "text" : "enum"),EditorKind);
            row->setData(QString::fromLatin1(i == 0 ? "true" : i == 2 ? "18446744073709551615" : i == 3 ? "999" : "-123"),EditorText);
            row->setData(QString::fromLatin1(i == 0 ? "true (1)" : i == 2 ? "18446744073709551615" : i == 3 ? "999 (unknown)" : "-123 (unknown)"),Text);
            row->setData(i == 0 ? boolean : i == 2 ? QVariantList{} : modes,Choices);
            row->setData(i == 3,ChoicesTruncated); row->setData(false,SampleExact);
            row->setData(17.0,SampleValue); row->setData(QString::fromLatin1(i == 0 ? "bool" : i == 2 ? "u64" : "enum"),TypeText);
            appendRow(row);
        }
    }
};
class Writes final : public QObject {
    Q_OBJECT
public:
    int count = 0, row = -1;
    QString text;
    Q_INVOKABLE void write(int nextRow, const QString& value) { ++count; row = nextRow; text = value; }
};
QQuickItem* named(QQuickItem* root, const char* name) {
    if (root->objectName() == QLatin1String(name)) return root;
    for (auto* child : root->childItems()) if (auto* found = named(child,name)) return found;
    return nullptr;
}
QQuickItem* rowAt(QQuickItem* root, int row) {
    if (root->objectName() == QLatin1String("watchlistRow") && root->property("index").toInt() == row) return root;
    for (auto* child : root->childItems()) if (auto* found = rowAt(child,row)) return found;
    return nullptr;
}
void settle() { QCoreApplication::processEvents(); QTest::qWait(35); }
void type(QQuickWindow* window, const char* text) {
    for (const char* p = text; *p; ++p) QTest::keyClick(window,*p);
}
void click(QQuickWindow* window, QQuickItem* target) {
    CHECK(target && target->isVisible() && target->width() > 0 && target->height() > 0);
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,
        target->mapToScene(QPointF(target->width()/2,target->height()/2)).toPoint());
    settle();
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc,argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& text) {
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) ++warnings;
        std::fprintf(stderr,"%s\n",qPrintable(text));
    });
    try {
        {
            Model model;
            Writes writes;
            QQmlApplicationEngine engine;
            engine.rootContext()->setContextProperty(QStringLiteral("typedWatchModel"),&model);
            engine.rootContext()->setContextProperty(QStringLiteral("typedWatchWrites"),&writes);
            engine.load(QUrl(QStringLiteral("qrc:/TypedWatch.qml")));
            CHECK(!engine.rootObjects().isEmpty());
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            CHECK(window && QTest::qWaitForWindowExposed(window));
            settle();
            auto* list = named(window->contentItem(),"watchlistView");
            auto* outside = named(window->contentItem(),"outsideWatchEditor");
            CHECK(list && outside);
            const auto edit = [&](int row) {
                list->setProperty("currentIndex",row); list->forceActiveFocus();
                QTest::keyClick(window,Qt::Key_F2); settle();
                auto* item = rowAt(list,row);
                CHECK(item && item->property("editing").toBool());
                return item;
            };
            auto* row = edit(0);
            auto* choice = named(row,"watchValueChoice");
            CHECK(choice && choice->isVisible() && choice->property("currentText").toString() == "true (1)");
            QTest::keyClick(window,Qt::Key_Space); settle();
            QTest::keyClick(window,Qt::Key_Home); QTest::keyClick(window,Qt::Key_Return); settle();
            CHECK(writes.count == 0 && row->property("draftText").toString() == "false");
            CHECK(row->property("editing").toBool());
            click(window,outside);
            CHECK(!row->property("editing").toBool() && writes.count == 0 && outside->hasActiveFocus());
            row = edit(0);
            click(window,named(row,"watchValueWrite"));
            CHECK(writes.count == 1 && writes.row == 0 && writes.text == "true");
            row = edit(1); choice = named(row,"watchValueChoice");
            CHECK(choice->property("currentText").toString() == "Unknown (-123)");
            QTest::keyClick(window,Qt::Key_Space); settle();
            QTest::keyClick(window,Qt::Key_Down); QTest::keyClick(window,Qt::Key_Return); settle();
            CHECK(writes.count == 1 && row->property("draftText").toString() == "-2");
            model.setData(model.index(1,0),QStringLiteral("7"),EditorText); settle();
            CHECK(row->property("draftText").toString() == "-2");
            QTest::keyClick(window,Qt::Key_Return); settle();
            CHECK(writes.count == 2 && writes.row == 1 && writes.text == "-2");
            row = edit(1);
            QTest::keyClick(window,Qt::Key_Space); settle();
            QTest::keyClick(window,Qt::Key_End); QTest::keyClick(window,Qt::Key_Return); settle();
            auto* input = named(row,"watchValueEditor");
            CHECK(input && input->isVisible() && input->hasActiveFocus());
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); type(window,"-9223372036854775808");
            CHECK(writes.count == 2);
            QTest::keyClick(window,Qt::Key_Return); settle();
            CHECK(writes.count == 3 && writes.text == "-9223372036854775808");
            row = edit(2); input = named(row,"watchValueEditor");
            CHECK(input->property("text").toString() == "18446744073709551615");
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); type(window,"9007199254740993");
            model.setData(model.index(2,0),QStringLiteral("18446744073709551614"),EditorText); settle();
            CHECK(input->property("text").toString() == "9007199254740993");
            QTest::keyClick(window,Qt::Key_Return); settle();
            CHECK(writes.count == 4 && writes.text == "9007199254740993");
            row = edit(3); choice = named(row,"watchValueChoice");
            CHECK(choice->property("currentText").toString() == "Code (999)");
            QTest::keyClick(window,Qt::Key_Escape); settle();
            CHECK(!row->property("editing").toBool() && writes.count == 4);
            model.setData(model.index(0,0),QString{},EditorText);
            model.setData(model.index(0,0),QStringLiteral("Not read"),Status);
            model.setData(model.index(0,0),QStringLiteral("\u2014"),Text);
            for (const int width : {480,320}) {
                window->setWidth(width); settle(); row = edit(0); choice = named(row,"watchValueChoice");
                auto* write = named(row,"watchValueWrite");
                CHECK(choice->property("currentIndex").toInt() == -1 && !write->isEnabled());
                CHECK(row->property("draftText").toString().isEmpty());
                CHECK(choice->height() >= choice->implicitHeight());
                CHECK(write->height() >= write->implicitHeight());
                const QRectF bounds(QPointF(0,0),row->size());
                CHECK(bounds.contains(choice->mapRectToItem(row,QRectF(QPointF(0,0),choice->size()))));
                CHECK(bounds.contains(write->mapRectToItem(row,QRectF(QPointF(0,0),write->size()))));
                if (argc > 1) {
                    QDir().mkpath(QString::fromLocal8Bit(argv[1]));
                    CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(
                        QStringLiteral("watch-typed-%1.png").arg(width))));
                }
                QTest::keyClick(window,Qt::Key_Space); settle();
                QTest::keyClick(window,Qt::Key_Home); QTest::keyClick(window,Qt::Key_Return); settle();
                CHECK(writes.count == (width == 480 ? 4 : 5));
                click(window,named(row,"watchValueWrite"));
                CHECK(writes.count == (width == 480 ? 5 : 6) && writes.text == "false");
            }
            CHECK(writes.count == 6);
            for (const int width : {480,320}) {
                model.setData(model.index(1,0),QStringLiteral("-123"),EditorText);
                window->setWidth(width); settle(); row = edit(1); choice = named(row,"watchValueChoice");
                CHECK(choice->property("currentText").toString() == "Unknown (-123)");
                auto* choiceTip = row->findChild<QObject*>(QStringLiteral("watchValueChoiceToolTip"));
                CHECK(choiceTip && choiceTip->property("text").toString().contains(QStringLiteral("Unknown (-123)")));
                QTest::keyClick(window,Qt::Key_Space); settle();
                auto* popup = choice->property("popup").value<QObject*>();
                CHECK(popup && popup->property("visible").toBool() && writes.count == 6);
                const auto fill = QQmlProperty(choice,QStringLiteral("background.color")).read().value<QColor>();
                const auto ink = QQmlProperty(choice,QStringLiteral("contentItem.color")).read().value<QColor>();
                CHECK(fill.isValid() && ink.isValid());
                CHECK((std::max(luminance(fill),luminance(ink)) + 0.05) /
                      (std::min(luminance(fill),luminance(ink)) + 0.05) >= 4.5);
                if (argc > 1)
                    CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(
                        QStringLiteral("watch-enum-popup-%1.png").arg(width))));
                QTest::keyClick(window,Qt::Key_Escape); settle();
                CHECK(!popup->property("visible").toBool() && writes.count == 6);
                // Closing the list never commits a mutation; Escape also leaves
                // the row editor once the list has relinquished the key.
                if (row->property("editing").toBool()) { QTest::keyClick(window,Qt::Key_Escape); settle(); }
                CHECK(!row->property("editing").toBool());
                row = edit(1); choice = named(row,"watchValueChoice");
                QTest::keyClick(window,Qt::Key_Space); settle();
                QTest::keyClick(window,Qt::Key_End); QTest::keyClick(window,Qt::Key_Return); settle();
                input = named(row,"watchValueEditor");
                CHECK(input && input->isVisible() && input->hasActiveFocus());
                QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); type(window,"-9223372036854775808");
                CHECK(input->property("text").toString() == "-9223372036854775808" && writes.count == 6);
                auto* inputTip = row->findChild<QObject*>(QStringLiteral("watchValueEditorToolTip"));
                CHECK(inputTip && inputTip->property("text").toString().contains(QStringLiteral("-9223372036854775808")));
                if (argc > 1)
                    CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(
                        QStringLiteral("watch-enum-manual-%1.png").arg(width))));
                click(window,outside);
                CHECK(!row->property("editing").toBool() && writes.count == 6);
            }
            window->close();
        }
        settle();
        CHECK(warnings == 0);
        qInfo().noquote() << QStringLiteral("Typed Watch checks=%1 warnings=%2").arg(checks).arg(warnings);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"Typed Watch failure: %s\n",error.what());
        return 1;
    }
}
#include "TypedWatch.moc"
