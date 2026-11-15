// The shared value cell alone (BA-4·0a), in a real QQuickWindow: a value until
// it is clicked or opened, one row tall with its editor; live text; the words
// for text that is no value, and only then; Escape puts back the value it had;
// leaving with no value puts back the last; Tab asks for the next cell; a
// choice is a combo as wide as its items; a locked or read-only cell never
// opens. No device, no provider. MIT.
#include <QtCore/QDir>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QTest>
#include <cstdio>
#include <stdexcept>

namespace {
int checks = 0, warnings = 0;
void check(bool condition, const char* expression)
{
	++checks;
	if (!condition)
		throw std::runtime_error(expression);
}
#define CHECK(x) check(bool(x), #x)

QQuickItem* named(QQuickItem* root, const QString& name)
{
	if (root->objectName() == name)
		return root;
	for (auto* child : root->childItems())
		if (auto* found = named(child, name))
			return found;
	return nullptr;
}

void settle()
{
	QCoreApplication::processEvents();
	QTest::qWait(60);
}

QPoint centre(QQuickItem* item)
{
	return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

// QTest has keyClicks for widgets only.
void typeText(QWindow* window, const QString& typed)
{
	for (const auto letter : typed.toLatin1())
		QTest::keyClick(window, letter);
}

QString text(QObject* object, const char* property = "text")
{
	return object->property(property).toString();
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
			++warnings;
		std::fprintf(stderr, "%s\n", qPrintable(message));
	});
	try {
		CHECK(argc > 1);
		QQmlApplicationEngine engine;
		engine.load(QUrl(QStringLiteral("qrc:/ValueCellStates.qml")));
		CHECK(!engine.rootObjects().isEmpty());
		auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
		CHECK(window && QTest::qWaitForWindowExposed(window));
		settle();
		auto* content = window->contentItem();
		auto* gain = named(content, QStringLiteral("gainCell"));
		auto* enabled = named(content, QStringLiteral("enabledCell"));
		auto* mode = named(content, QStringLiteral("modeCell"));
		auto* factor = named(content, QStringLiteral("factorCell"));
		auto* locked = named(content, QStringLiteral("lockedCell"));
		auto* readOnly = named(content, QStringLiteral("readOnlyCell"));
		auto* gainRow = window->findChild<QObject*>(QStringLiteral("gainRow"));
		auto* factorRow = window->findChild<QObject*>(QStringLiteral("factorRow"));
		auto* enabledRow = window->findChild<QObject*>(QStringLiteral("enabledRow"));
		auto* modeRow = window->findChild<QObject*>(QStringLiteral("modeRow"));
		CHECK(gain && enabled && mode && factor && locked && readOnly && gainRow && factorRow && enabledRow && modeRow);
		const int rowHeight = qRound(gain->height());
		// Every cell is one row tall, value or editor (spec C: 22 px at 100 %).
		for (auto* cell : {gain, enabled, mode, factor, locked, readOnly})
			CHECK(qAbs(cell->height() - rowHeight) < 1);
		CHECK(rowHeight >= 20 && rowHeight <= 24);
		// A value until opened: the text shows, no editor.
		auto* gainText = named(gain, QStringLiteral("valueText"));
		auto* gainEditor = named(gain, QStringLiteral("scalarEditor"));
		auto* gainCaption = named(gain, QStringLiteral("valueCaption"));
		CHECK(gainText && gainEditor && gainCaption);
		CHECK(gainText->isVisible() && text(gainText) == QStringLiteral("3") && !gainEditor->isVisible());
		// Numbers stand right; their glyph-free cell keeps the digits in place.
		CHECK(gainText->property("horizontalAlignment").toInt() == Qt::AlignRight);
		// Hover shows the pencil, in the gap after the value.
		auto* pencil = named(gain, QStringLiteral("valueEditGlyph"));
		QTest::mouseMove(window, QPoint(window->width() - 4, window->height() - 4));
		settle();
		CHECK(pencil && !pencil->isVisible());
		QTest::mouseMove(window, centre(gainText));
		settle();
		CHECK(pencil->isVisible());
		CHECK(pencil->mapToScene(QPointF(0, 0)).x() >= gain->mapToScene(QPointF(gain->width(), 0)).x() - 0.5);
		// A click opens it, one row tall, the text selected.
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre(gainText));
		settle();
		CHECK(gain->property("open").toBool() && gainEditor->isVisible() && gainEditor->hasActiveFocus());
		CHECK(qAbs(gain->height() - rowHeight) < 1 && qAbs(gainEditor->height() - rowHeight) < 1);
		CHECK(text(gainEditor) == QStringLiteral("3"));
		CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QStringLiteral("value-cell-open.png"))));
		// Typing is live: a value goes into the row at once, no words.
		QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
		typeText(window, QStringLiteral("12"));
		settle();
		CHECK(text(gainRow) == QStringLiteral("12") && !gainCaption->isVisible());
		// Text that is no value: the row keeps its last value; the caption says
		// what the cell takes, in the error colour, and the row grows for it.
		typeText(window, QStringLiteral("x"));
		settle();
		CHECK(text(gainRow) == QStringLiteral("12"));
		CHECK(gain->property("invalid").toBool() && gainCaption->isVisible());
		CHECK(text(gainCaption) == QStringLiteral("Enter a number from 0 to 255."));
		CHECK(gain->height() > rowHeight + 4);
		CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QStringLiteral("value-cell-invalid.png"))));
		// Enter with no value: the last value comes back, the cell closes, and
		// the caption says it was not changed and why.
		QTest::keyClick(window, Qt::Key_Return);
		settle();
		CHECK(!gain->property("open").toBool() && !gain->property("invalid").toBool());
		CHECK(text(gainRow) == QStringLiteral("12") && text(gainText) == QStringLiteral("12"));
		CHECK(gainRow->property("finishedByKey").toInt() == 1);
		CHECK(gainCaption->isVisible() && text(gainCaption) == QStringLiteral("Not changed: Enter a number from 0 to 255."));
		// Opened again, the words go.
		CHECK(QMetaObject::invokeMethod(gain, "begin"));
		settle();
		CHECK(!gainCaption->isVisible());
		QTest::keyClick(window, Qt::Key_Escape);
		settle();
		CHECK(qAbs(gain->height() - rowHeight) < 1);
		// Escape puts back the value the cell had when it was opened.
		CHECK(QMetaObject::invokeMethod(gain, "begin"));
		settle();
		QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
		typeText(window, QStringLiteral("200"));
		settle();
		CHECK(text(gainRow) == QStringLiteral("200"));
		QTest::keyClick(window, Qt::Key_Escape);
		settle();
		CHECK(!gain->property("open").toBool() && text(gainRow) == QStringLiteral("12"));
		// Tab keeps the value and asks for the next cell; Shift+Tab the previous.
		CHECK(QMetaObject::invokeMethod(gain, "begin"));
		settle();
		QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
		typeText(window, QStringLiteral("9"));
		QTest::keyClick(window, Qt::Key_Tab);
		settle();
		CHECK(!gain->property("open").toBool() && text(gainRow) == QStringLiteral("9"));
		CHECK(gainRow->property("tabbedForward").toInt() == 1);
		CHECK(QMetaObject::invokeMethod(gain, "begin"));
		settle();
		QTest::keyClick(window, Qt::Key_Backtab, Qt::ShiftModifier);
		settle();
		CHECK(gainRow->property("tabbedBack").toInt() == 1 && text(gainRow) == QStringLiteral("9"));
		// Leaving with text that is no value also puts the last value back.
		CHECK(QMetaObject::invokeMethod(gain, "begin"));
		settle();
		typeText(window, QStringLiteral("-"));
		settle();
		CHECK(gain->property("invalid").toBool());
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre(named(content, QStringLiteral("elsewhere"))));
		settle();
		CHECK(!gain->property("open").toBool() && text(gainRow) == QStringLiteral("9"));
		// Never in silence: the caption says why the value stayed.
		CHECK(gainCaption->isVisible() && text(gainCaption) == QStringLiteral("Not changed: Enter a number from 0 to 255."));
		// Enter and Escape gave the keys back; Tab and a click elsewhere did not.
		CHECK(gainRow->property("finishedByKey").toInt() == 3);
		// A click elsewhere keeps a valid value, as Enter does.
		CHECK(QMetaObject::invokeMethod(gain, "begin"));
		settle();
		QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
		typeText(window, QStringLiteral("33"));
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre(named(content, QStringLiteral("elsewhere"))));
		settle();
		CHECK(!gain->property("open").toBool() && text(gainRow) == QStringLiteral("33") && !gainCaption->isVisible());
		// Unchanged text writes nothing: opened and left by Enter or a click away.
		const int gainWrites = gainRow->property("written").toList().size();
		CHECK(QMetaObject::invokeMethod(gain, "begin"));
		settle();
		QTest::keyClick(window, Qt::Key_Return);
		settle();
		CHECK(QMetaObject::invokeMethod(gain, "begin"));
		settle();
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre(named(content, QStringLiteral("elsewhere"))));
		settle();
		CHECK(!gain->property("open").toBool() && text(gainRow) == QStringLiteral("33"));
		CHECK(gainRow->property("written").toList().size() == gainWrites && !gainCaption->isVisible());
		// A long value shows whole while edited: the editor grows leftwards
		// over the owner's room, its right edge on the cell's, its start in sight.
		auto* pi = named(content, QStringLiteral("piCell"));
		auto* piRow = window->findChild<QObject*>(QStringLiteral("piRow"));
		auto* piEditor = named(pi, QStringLiteral("scalarEditor"));
		auto* piText = named(pi, QStringLiteral("valueText"));
		CHECK(pi && piRow && piEditor && piText);
		CHECK(piText->property("truncated").toBool());
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre(piText));
		settle();
		CHECK(pi->property("open").toBool() && piEditor->isVisible() && piEditor->hasActiveFocus());
		CHECK(text(piEditor) == QStringLiteral("3.14159274"));
		const auto rightEdge = [](QQuickItem* item) { return item->mapToScene(QPointF(item->width(), 0)).x(); };
		const auto shownWhole = [&]() {
			QRectF first, last;
			const int length = text(piEditor).size();
			const bool asked = QMetaObject::invokeMethod(piEditor, "positionToRectangle", Q_RETURN_ARG(QRectF, first), Q_ARG(int, 0)) &&
			                   QMetaObject::invokeMethod(piEditor, "positionToRectangle", Q_RETURN_ARG(QRectF, last), Q_ARG(int, length));
			return asked && first.x() >= 0 && last.x() + 1 <= piEditor->width();
		};
		CHECK(piEditor->width() > pi->width() && qAbs(rightEdge(piEditor) - rightEdge(pi)) < 0.5);
		CHECK(piEditor->property("contentWidth").toReal() + piEditor->property("leftPadding").toReal() +
		      piEditor->property("rightPadding").toReal() <= piEditor->width());
		CHECK(shownWhole());
		CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QStringLiteral("value-cell-long.png"))));
		// Typed longer, it grows with the text up to the owner's room, and no further.
		QTest::keyClick(window, Qt::Key_End);
		typeText(window, QStringLiteral("12"));
		settle();
		CHECK(shownWhole() && qAbs(rightEdge(piEditor) - rightEdge(pi)) < 0.5);
		typeText(window, QStringLiteral("123456789012345678901234567890"));
		settle();
		const qreal room = pi->property("editorRoom").toReal();
		CHECK(room > 0 && qAbs(piEditor->width() - (pi->width() + room)) < 0.5);
		// The name's first characters stay in sight.
		auto* piName = named(content, QStringLiteral("piName"));
		CHECK(piName && piEditor->mapToScene(QPointF(0, 0)).x() >= piName->mapToScene(QPointF(0, 0)).x() + 8);
		QTest::keyClick(window, Qt::Key_Escape);
		settle();
		CHECK(!pi->property("open").toBool() && text(piRow) == QStringLiteral("3.14159274"));
		// A float says what it encodes while the text differs from it.
		auto* factorText = named(factor, QStringLiteral("valueText"));
		auto* factorCaption = named(factor, QStringLiteral("valueCaption"));
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre(factorText));
		settle();
		CHECK(factor->property("open").toBool() && factorCaption->isVisible());
		CHECK(text(factorCaption) == QStringLiteral("Encoded: 0.100000001 (f32)"));
		QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
		typeText(window, QStringLiteral("abc"));
		settle();
		CHECK(text(factorCaption) == QStringLiteral("Enter a number.") && text(factorRow) == QStringLiteral("0.1"));
		QTest::keyClick(window, Qt::Key_Escape);
		settle();
		CHECK(!factor->property("open").toBool() && !factorCaption->isVisible());
		// A bool is a choice: false and true, as wide as its items, no words.
		auto* enabledText = named(enabled, QStringLiteral("valueText"));
		auto* enabledChoice = named(enabled, QStringLiteral("valueChoice"));
		CHECK(enabledText && enabledChoice && !enabledChoice->isVisible());
		CHECK(QMetaObject::invokeMethod(enabled, "begin"));
		settle();
		CHECK(enabledChoice->isVisible() && enabledChoice->width() < enabled->width());
		CHECK(qAbs(enabledChoice->height() - rowHeight) < 1);
		CHECK(enabledChoice->property("count").toInt() == 2);
		// Each name with its code, in the list and in the closed box.
		QString first, second;
		CHECK(QMetaObject::invokeMethod(enabledChoice, "textAt", Q_RETURN_ARG(QString, first), Q_ARG(int, 0)));
		CHECK(QMetaObject::invokeMethod(enabledChoice, "textAt", Q_RETURN_ARG(QString, second), Q_ARG(int, 1)));
		CHECK(first == QStringLiteral("false (0)") && second == QStringLiteral("true (1)"));
		CHECK(enabledChoice->property("displayText").toString().endsWith(QStringLiteral(" (0)")) ||
		      enabledChoice->property("displayText").toString().endsWith(QStringLiteral(" (1)")));
		CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QStringLiteral("value-cell-choice.png"))));
		CHECK(QMetaObject::invokeMethod(enabledChoice, "activated", Q_ARG(int, 1)));
		settle();
		CHECK(text(enabledRow) == QStringLiteral("true") && !enabled->property("open").toBool());
		CHECK(!named(enabled, QStringLiteral("valueCaption"))->isVisible());
		// An enum: its names, and a code typed when chosen so.
		CHECK(QMetaObject::invokeMethod(mode, "begin"));
		settle();
		auto* modeChoice = named(mode, QStringLiteral("valueChoice"));
		CHECK(modeChoice->isVisible() && modeChoice->property("count").toInt() == 4);
		QString idle;
		CHECK(QMetaObject::invokeMethod(modeChoice, "textAt", Q_RETURN_ARG(QString, idle), Q_ARG(int, 0)));
		CHECK(idle == QStringLiteral("Idle (0)") && modeChoice->property("displayText").toString().endsWith(QLatin1Char(')')));
		// In hex, as the Watchlist's hex form: "Idle (0x00)".
		CHECK(mode->setProperty("hexCodes", true));
		settle();
		CHECK(QMetaObject::invokeMethod(modeChoice, "textAt", Q_RETURN_ARG(QString, idle), Q_ARG(int, 0)));
		CHECK(idle == QStringLiteral("Idle (0x00)"));
		CHECK(mode->setProperty("hexCodes", false));
		settle();
		CHECK(QMetaObject::invokeMethod(modeChoice, "activated", Q_ARG(int, 3)));
		settle();
		auto* modeEditor = named(mode, QStringLiteral("scalarEditor"));
		CHECK(mode->property("manualCode").toBool() && modeEditor->isVisible() && modeEditor->hasActiveFocus());
		typeText(window, QStringLiteral("7"));
		QTest::keyClick(window, Qt::Key_Return);
		settle();
		CHECK(text(modeRow) == QStringLiteral("7") && !mode->property("open").toBool());
		// Locked (changes off, or busy) and read-only cells never open.
		CHECK(!locked->property("canOpen").toBool() && !readOnly->property("canOpen").toBool());
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre(named(locked, QStringLiteral("valueText"))));
		settle();
		QVariant opened = true;
		CHECK(QMetaObject::invokeMethod(locked, "begin", Q_RETURN_ARG(QVariant, opened)) && !opened.toBool());
		CHECK(!locked->property("open").toBool() && !named(locked, QStringLiteral("scalarEditor"))->isVisible());
		CHECK(!named(locked, QStringLiteral("valueCaption"))->isVisible());
		std::printf("Value cell checks=%d warnings=%d\n", checks, warnings);
		return warnings == 0 ? 0 : 1;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Value cell check failed after %d checks: %s\n", checks, error.what());
		return 1;
	}
}
