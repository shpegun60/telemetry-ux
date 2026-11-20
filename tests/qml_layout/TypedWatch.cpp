// Real keyboard/pointer checks over the reusable typed Watchlist editor. MIT.
#include <Telemetry/UX/QmlTypes.hpp>
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
    Choices, ChoicesTruncated, SampleValue, SampleExact, TypeText, WriteScope };
class Model final : public QStandardItemModel {
public:
    QHash<int, QByteArray> roleNames() const override {
        return {{Name,"name"}, {Status,"status"}, {Text,"text"}, {Writable,"writable"},
            {EditorKind,"editorKind"}, {EditorText,"editorText"}, {Choices,"editorChoices"},
            {ChoicesTruncated,"editorChoicesTruncated"}, {SampleValue,"sampleValue"},
            {SampleExact,"sampleExact"}, {TypeText,"typeText"}, {WriteScope,"writeScope"}};
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
            if (i == 0) row->setData(QVariantMap{{QStringLiteral("field"),QStringLiteral("device.Config")},{QStringLiteral("others"),2},{QStringLiteral("kind"),QStringLiteral("members")}},WriteScope);
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
    // The Watchlist's tree uses the module's KindIcon (ToolIcon).
    telemetry::ux::registerQmlTypes();
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
            const auto cellOf = [&](QQuickItem* item) { auto* cell = named(item,"valueCell"); CHECK(cell); return cell; };
            const double rowHeight = rowAt(list,3)->height();
            // The editor is Details' own cell: a choice for a bool, as wide as its
            // items, the right edge on the value column's.
            auto* row = edit(0);
            auto* cell = cellOf(row);
            auto* choice = named(row,"valueChoice");
            CHECK(choice && choice->isVisible() && choice->property("currentText").toString() == "true (1)");
            CHECK(!named(row,"watchValueWrite") && !row->findChild<QObject*>(QStringLiteral("watchValueEditorToolTip")));
            CHECK(choice->width() <= 150 && qAbs(choice->height() - rowHeight) < 1.5);
            CHECK(qAbs(choice->mapToItem(row,QPointF(choice->width(),0)).x() - cell->mapToItem(row,QPointF(cell->width(),0)).x()) < 1.5);
            // A member edit says what Enter writes, in one quiet line under the cell.
            auto* caption = named(row,"valueCaption");
            CHECK(caption && caption->isVisible() && caption->property("text").toString() == "Writes all 3 members of Config.");
            CHECK(caption->property("lineCount").toInt() == 1);
            CHECK(row->height() > rowHeight + 8 && row->height() <= rowHeight + 24);
            // Columns stay while editing, × goes.
            CHECK(named(row,"watchRowType")->isVisible() && !named(row,"removeWatchButton")->isVisible());
            CHECK(named(row,"watchRowType")->isVisible());
            // No tooltip is open on focus; the line's own opens only on hover.
            for (auto* tip : row->findChildren<QObject*>(QStringLiteral("valueCaptionToolTip")))
                CHECK(!tip->property("visible").toBool());
            if (argc > 1) {
                QDir().mkpath(QString::fromLocal8Bit(argv[1]));
                CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QStringLiteral("watch-edit-480-member.png"))));
            }
            // Choosing from the list writes once; Escape writes nothing.
            QTest::keyClick(window,Qt::Key_Space); settle();
            QTest::keyClick(window,Qt::Key_Home); QTest::keyClick(window,Qt::Key_Return); settle();
            CHECK(writes.count == 1 && writes.row == 0 && writes.text == "false");
            CHECK(!row->property("editing").toBool());
            row = edit(0);
            QTest::keyClick(window,Qt::Key_Escape); settle();
            CHECK(!row->property("editing").toBool() && writes.count == 1);
            // A whole Field (no scope) stays one row high.
            row = edit(1); choice = named(row,"valueChoice");
            CHECK(choice->property("currentText").toString() == "Unknown (-123)");
            CHECK(qAbs(row->height() - rowHeight) < 1.5);
            QTest::keyClick(window,Qt::Key_Space); settle();
            QTest::keyClick(window,Qt::Key_Down); QTest::keyClick(window,Qt::Key_Return); settle();
            CHECK(writes.count == 2 && writes.row == 1 && writes.text == "-2");
            // A code typed: Space, End, Enter; the text editor grows leftwards, Enter writes once.
            row = edit(1);
            choice = named(row,"valueChoice");
            QTest::keyClick(window,Qt::Key_Space); settle();
            QTest::keyClick(window,Qt::Key_End); QTest::keyClick(window,Qt::Key_Return); settle();
            auto* input = named(row,"scalarEditor");
            CHECK(input && input->isVisible() && input->hasActiveFocus());
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); type(window,"-9223372036854775808");
            CHECK(writes.count == 2);
            QTest::keyClick(window,Qt::Key_Return); settle();
            CHECK(writes.count == 3 && writes.text == "-9223372036854775808" && !row->property("editing").toBool());
            // Polling goes on under the editor: the typed text stays.
            row = edit(2); input = named(row,"scalarEditor");
            CHECK(input->property("text").toString() == "18446744073709551615");
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); type(window,"9007199254740993");
            model.setData(model.index(2,0),QStringLiteral("18446744073709551614"),EditorText); settle();
            CHECK(row->property("editing").toBool() && input->property("text").toString() == "9007199254740993");
            CHECK(input->width() >= cellOf(row)->width() && input->mapToItem(row,QPointF(0,0)).x() >= 0);
            QTest::keyClick(window,Qt::Key_Return); settle();
            CHECK(writes.count == 4 && writes.text == "9007199254740993");
            row = edit(3); choice = named(row,"valueChoice");
            CHECK(choice->property("currentText").toString() == "Code (999)");
            QTest::keyClick(window,Qt::Key_Escape); settle();
            CHECK(!row->property("editing").toBool() && writes.count == 4);
            // A value not read yet: the choice has none selected; nothing is written for it.
            model.setData(model.index(0,0),QString{},EditorText);
            model.setData(model.index(0,0),QStringLiteral("Not read"),Status);
            model.setData(model.index(0,0),QStringLiteral("—"),Text);
            for (const int width : {480,320}) {
                window->setWidth(width); settle(); row = edit(0); choice = named(row,"valueChoice");
                CHECK(choice->property("currentIndex").toInt() == -1);
                const QRectF bounds(QPointF(0,0),row->size());
                CHECK(bounds.contains(choice->mapRectToItem(row,QRectF(QPointF(0,0),choice->size()))));
                QTest::keyClick(window,Qt::Key_Escape); settle();
                CHECK(!row->property("editing").toBool());
            }
            CHECK(writes.count == 4);
            model.setData(model.index(0,0),QStringLiteral("true"),EditorText);
            model.setData(model.index(0,0),QStringLiteral("Ok"),Status);
            model.setData(model.index(0,0),QStringLiteral("true (1)"),Text);
            // Closing the list never writes; the contrast of the open list holds.
            for (const int width : {480,320}) {
                const int base = writes.count;
                model.setData(model.index(1,0),QStringLiteral("-123"),EditorText);
                window->setWidth(width); settle(); row = edit(1); choice = named(row,"valueChoice");
                CHECK(choice->property("currentText").toString() == "Unknown (-123)");
                    QTest::keyClick(window,Qt::Key_Space); settle();
                auto* popup = choice->property("popup").value<QObject*>();
                CHECK(popup && popup->property("visible").toBool() && writes.count == base);
                const auto fill = QQmlProperty(choice,QStringLiteral("background.color")).read().value<QColor>();
                const auto ink = QQmlProperty(choice,QStringLiteral("contentItem.color")).read().value<QColor>();
                CHECK(fill.isValid() && ink.isValid());
                CHECK((std::max(luminance(fill),luminance(ink)) + 0.05) /
                      (std::min(luminance(fill),luminance(ink)) + 0.05) >= 4.5);
                if (argc > 1)
                    CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(
                        QStringLiteral("watch-enum-popup-%1.png").arg(width))));
                QTest::keyClick(window,Qt::Key_Escape); settle();
                CHECK(!popup->property("visible").toBool() && writes.count == base);
                if (row->property("editing").toBool()) { QTest::keyClick(window,Qt::Key_Escape); settle(); }
                CHECK(!row->property("editing").toBool());
            }
            // One commit rule: Enter, a click on another row, Tab or a click away writes;
            // Escape cancels; the same text writes nothing; text that is no value
            // writes nothing and the cell says why in Enter's words.
            window->setWidth(480); settle();
            const auto otherRow = [&]() { return named(rowAt(list,0),"watchRowName"); };
            int base = writes.count;
            row = edit(2);
            QTest::keyClick(window,Qt::Key_Return); settle();
            CHECK(!row->property("editing").toBool() && writes.count == base);
            row = edit(2);
            click(window,otherRow());
            CHECK(!row->property("editing").toBool() && writes.count == base && list->property("currentIndex").toInt() == 0);
            row = edit(2);
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); type(window,"42");
            click(window,otherRow());
            CHECK(!row->property("editing").toBool() && writes.count == base + 1 && writes.row == 2 && writes.text == "42");
            // Tab writes, then opens the next writable row.
            row = edit(2);
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); type(window,"43");
            QTest::keyClick(window,Qt::Key_Tab); settle();
            CHECK(writes.count == base + 2 && writes.row == 2 && writes.text == "43" && !row->property("editing").toBool());
            CHECK(rowAt(list,3)->property("editing").toBool());
            QTest::keyClick(window,Qt::Key_Escape); settle();
            row = edit(2);
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); type(window,"44");
            QTest::keyClick(window,Qt::Key_Return); settle();
            CHECK(!row->property("editing").toBool() && writes.count == base + 3 && writes.text == "44");
            row = edit(2);
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); type(window,"45");
            QTest::keyClick(window,Qt::Key_Escape); settle();
            CHECK(!row->property("editing").toBool() && writes.count == base + 3);
            // Text that is no value: nothing written, the words under the closed cell.
            row = edit(2);
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); type(window,"4x"); settle();
            caption = named(row,"valueCaption");
            CHECK(caption->isVisible() && caption->property("text").toString() == "Enter a whole number." && caption->property("lineCount").toInt() <= 2);
            if (argc > 1)
                CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QStringLiteral("watch-edit-480-error.png"))));
            // Enter is refused (WL-9 C2): the editor stays open, its border in
            // the error colour, the refusal said; Tab leaves and holds the text
            // in the row, struck, with why under it.
            QTest::keyClick(window,Qt::Key_Return); settle();
            CHECK(row->property("editing").toBool() && writes.count == base + 3);
            CHECK(cellOf(row)->property("announced").toInt() == 1 &&
                  cellOf(row)->property("lastAnnounced").toString() == "Enter a whole number.");
            QTest::keyClick(window,Qt::Key_Tab); settle();
            CHECK(!row->property("editing").toBool() && writes.count == base + 3);
            CHECK(rowAt(list,3)->property("editing").toBool());
            QTest::keyClick(window,Qt::Key_Escape); settle();
            CHECK(named(row,"valueHeldText") && named(row,"valueHeldText")->property("text").toString() == "4x");
            caption = named(row,"valueCaption");
            CHECK(caption->isVisible() && caption->property("text").toString()
                  == "Not written: Enter a whole number · device holds 18446744073709551615");
            // Esc on the row lets go of it.
            list->setProperty("currentIndex",2); list->forceActiveFocus(); settle();
            QTest::keyClick(window,Qt::Key_Escape); settle();
            CHECK(!named(row,"valueHeldText") || !named(row,"valueHeldText")->isVisible());
            for (const int width : {1900,560,360}) {
                window->setWidth(width); settle();
                row = edit(0);
                CHECK(qAbs(named(row,"valueChoice")->mapToItem(row,QPointF(named(row,"valueChoice")->width(),0)).x()
                           - cellOf(row)->mapToItem(row,QPointF(cellOf(row)->width(),0)).x()) < 1.5);
                CHECK(named(row,"valueChoice")->width() <= 150);
                if (argc > 1)
                    CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(
                        QStringLiteral("watch-edit-%1-member.png").arg(width))));
                QTest::keyClick(window,Qt::Key_Escape); settle();
                row = edit(1);
                CHECK(qAbs(row->height() - rowHeight) < 1.5);
                if (argc > 1)
                    CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(
                        QStringLiteral("watch-edit-%1-field.png").arg(width))));
                QTest::keyClick(window,Qt::Key_Escape); settle();
                row = edit(2);
                QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); type(window,"4x"); settle();
                CHECK(named(row,"valueCaption")->property("lineCount").toInt() <= 2);
                if (argc > 1)
                    CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(
                        QStringLiteral("watch-edit-%1-error.png").arg(width))));
                QTest::keyClick(window,Qt::Key_Escape); settle();
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
