// One press, one step (UR-1): Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z pressed with
// the keys on every place of a window - the real Configurator over a
// disconnected Session, a file in a Viewer, plain controls, text fields, an
// inert area, a menu, a modal dialog - each say one thing and take one step,
// or none where none is to be taken. MIT.
#include "ScriptSteps.hpp"
#include <Telemetry/UX/FileContent.hpp>
#include <Telemetry/UX/QmlTypes.hpp>
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <Telemetry/UX/UndoHistory.hpp>
#include <Telemetry/UX/UndoRouter.hpp>
#include <Telemetry/UX/UndoScope.hpp>
#include <integration/qt/ConnectionSession.hpp>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlExpression>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <cstdio>
#include <stdexcept>
#include <functional>
#include <string>

namespace {
unsigned checks = 0;
unsigned warnings = 0;
#define CHECK(condition)                                                                                               \
	do {                                                                                                               \
		++checks;                                                                                                      \
		if (!(condition))                                                                                              \
			throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #condition);                \
	} while (false)

namespace ux = telemetry::ux;

void settle()
{
	QCoreApplication::processEvents();
	QTest::qWait(30);
}

QQuickItem* named(QQuickItem* root, const QString& name)
{
	if (root->objectName() == name && root->isVisible())
		return root;
	for (auto* child : root->childItems())
		if (auto* found = named(child, name))
			return found;
	return nullptr;
}

QQuickItem* shown(QQuickItem* root, const char* name)
{
	auto* found = named(root, QString::fromLatin1(name));
	if (!found)
		throw std::runtime_error(std::string("not shown: ") + name);
	return found;
}

QVariant evaluate(QObject* object, const char* expression)
{
	QQmlExpression e(qmlContext(object), object, QString::fromLatin1(expression));
	const auto value = e.evaluate();
	if (e.hasError())
		throw std::runtime_error(e.error().toString().toStdString());
	return value;
}

// Typing goes to a field that can be typed into: its own Undo, not the router's.
bool typing(const QQuickItem* item)
{
	return item && (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit")) &&
	       !item->property("readOnly").toBool();
}

// Every place the keys can be: the Tab chain, and the lists and tables a
// click gives the keys to.
QList<QQuickItem*> places(QQuickItem* root)
{
	QList<QQuickItem*> found;
	const std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
		if (!item->isVisible() || !item->isEnabled() || item->width() <= 0 || item->height() <= 0)
			return;
		if (item->activeFocusOnTab() || item->inherits("QQuickItemView") || item->inherits("QQuickTableView"))
			found << item;
		for (auto* child : item->childItems())
			walk(child);
	};
	walk(root);
	return found;
}

QString describe(const QQuickItem* item)
{
	return QStringLiteral("%1 %2").arg(QString::fromLatin1(item->metaObject()->className()), item->objectName());
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	ux::registerQmlTypes();
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& text) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
			++warnings;
		std::fprintf(stderr, "%s\n", qPrintable(text));
	});
	if (argc != 2)
		return 2;
	try {
		const auto folder = QString::fromLocal8Bit(argv[1]);
		QDir().mkpath(folder);
		const auto path = QDir(folder).filePath(QStringLiteral("keys.bin"));
		{
			QFile file(path);
			CHECK(file.open(QIODevice::WriteOnly) && file.write("ABCDEFGH") == 8);
		}
		ux::FileContent content;
		content.setLocalFile(path);
		CHECK(content.loaded());
		content.setEditing(true);
		telemetry::integration::qt::ConnectionSession session;
		ux::QtConnectionBinding binding(&session);
		auto* provider = binding.createProvider(&app);
		ScriptSteps steps;
		QQmlApplicationEngine engine;
		engine.rootContext()->setContextProperty(QStringLiteral("scriptSteps"), &steps);
		engine.setInitialProperties({{QStringLiteral("ownerProvider"), QVariant::fromValue<QObject*>(provider)},
		                             {QStringLiteral("fileContent"), QVariant::fromValue<QObject*>(&content)}});
		engine.load(QUrl(QStringLiteral("qrc:/UndoKeys.qml")));
		CHECK(!engine.rootObjects().isEmpty());
		auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
		CHECK(window && QTest::qWaitForWindowExposed(window));
		window->requestActivate();
		CHECK(QTest::qWaitForWindowActive(window));
		settle();
		auto* root = window->contentItem();
		auto* workspace = window->findChild<ux::UndoHistory*>(QStringLiteral("workspace"));
		auto* router = window->findChild<ux::UndoRouter*>(QStringLiteral("router"));
		CHECK(workspace && router && router->workspace() == workspace && router->active() == workspace);
		CHECK(provider->property("history").value<QObject*>() == workspace);
		auto* notice = shown(root, "plainButton")->parentItem()->findChild<QQuickItem*>(QStringLiteral("keysNotice"));
		CHECK(notice);
		QSignalSpy announced(router, &ux::UndoRouter::announced);
		int undone = 0, redone = 0;
		// Two steps of the workspace, the last one Ctrl+Z would take.
		const auto twoSteps = [&] {
			workspace->clear();
			steps.record(workspace, QStringLiteral("Add device.First to the Watchlist"), [&] { ++undone; },
			             [&] { ++redone; });
			steps.record(workspace, QStringLiteral("Add device.Second to the Watchlist"), [&] { ++undone; },
			             [&] { ++redone; });
		};
		const auto press = [&](Qt::KeyboardModifiers modifiers = Qt::ControlModifier, Qt::Key key = Qt::Key_Z) {
			QTest::keyClick(window, key, modifiers);
			settle();
		};

		// No step yet: the menu's words, and Ctrl+Z says there is nothing.
		CHECK(!router->canUndo() && router->undoText().isEmpty() &&
		      router->undoReason() == QStringLiteral("Nothing to undo.") &&
		      router->redoReason() == QStringLiteral("Nothing to redo."));
		shown(root, "plainButton")->forceActiveFocus(Qt::TabFocusReason);
		settle();
		press();
		CHECK(announced.size() == 1 && announced.last().at(0).toString() == QStringLiteral("Nothing to undo."));

		// The walk: the keys on each place in turn, two steps recorded after
		// they got there, one Ctrl+Z. Details, whose controls are all off with
		// no device, are a place too: a click in them gives them the keys.
		auto all = places(root);
		all << shown(root, "details");
		int walked = 0, typed = 0, refused = 0, inert = 0, taken = 0;
		for (auto* place : all) {
			place->forceActiveFocus(Qt::TabFocusReason);
			settle();
			auto* focus = window->activeFocusItem();
			if (!focus || !(place == focus || place->isAncestorOf(focus)))
				throw std::runtime_error("the keys did not go to " + describe(place).toStdString());
			twoSteps();
			const int heard = int(announced.size());
			undone = 0;
			press();
			const auto* scope = ux::UndoScope::of(window->activeFocusItem());
			const auto where = describe(place).toStdString();
			if (typing(window->activeFocusItem()) || (scope && scope->inert())) {
				if (int(announced.size()) != heard || undone != 0)
					throw std::runtime_error("a press did something at " + where);
				typing(window->activeFocusItem()) ? ++typed : ++inert;
			} else if (scope && !scope->refusal().isEmpty()) {
				if (int(announced.size()) != heard + 1 || undone != 0 ||
				    announced.last().at(0).toString() != scope->refusal())
					throw std::runtime_error("a refusal did not say only its words at " + where);
				++refused;
			} else {
				if (int(announced.size()) != heard + 1 || undone != 1 || !workspace->canUndo() ||
				    workspace->undoText() != QStringLiteral("Add device.First to the Watchlist"))
					throw std::runtime_error("a press did not take exactly one step at " + where + ": heard " +
					                         std::to_string(int(announced.size()) - heard) + ", undone " +
					                         std::to_string(undone));
				++taken;
			}
			++checks;
			++walked;
		}
		std::printf("walked %d places: %d took a step, %d text, %d refused, %d inert\n", walked, taken, typed,
		            refused, inert);
		// Every kind of place was reached.
		CHECK(walked == int(all.size()) && walked >= 18 && typed >= 2 && refused >= 1 && inert >= 1 && taken >= 12);

		// Details outside a field say why, and the Edit menu still acts there:
		// it names its step.
		auto* details = shown(root, "details");
		twoSteps();
		details->forceActiveFocus(Qt::TabFocusReason);
		settle();
		undone = 0;
		press();
		CHECK(undone == 0 && announced.last().at(0).toString() ==
		                         QStringLiteral("Undo does not take back a write. To go back, apply the old value."));
		CHECK(router->undo() && undone == 1 &&
		      announced.last().at(0).toString() == QStringLiteral("Undone: Add device.Second to the Watchlist"));

		// After an action on the device the first Ctrl+Z only says so; the
		// next goes on.
		auto* plain = shown(root, "plainButton");
		plain->forceActiveFocus(Qt::TabFocusReason);
		settle();
		twoSteps();
		undone = 0;
		workspace->noteIrreversible(QStringLiteral("The write to device.Gain is not undone; write 1.5 to go back."));
		press();
		CHECK(undone == 0 && workspace->irreversibleNote().isEmpty() &&
		      announced.last().at(0).toString() == QStringLiteral("The write to device.Gain is not undone; write 1.5 to go back."));
		press();
		CHECK(undone == 1);
		// A step taken from the menu ends the note too.
		workspace->noteIrreversible(QStringLiteral("The Command device.Reset was sent; Undo does not take it back."));
		CHECK(router->undo() && undone == 2 && workspace->irreversibleNote().isEmpty());

		// Ctrl+Y and Ctrl+Shift+Z redo, one step each.
		redone = 0;
		press(Qt::ControlModifier, Qt::Key_Y);
		CHECK(redone == 1 && announced.last().at(0).toString() == QStringLiteral("Redone: Add device.First to the Watchlist"));
		press(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_Z);
		CHECK(redone == 2 && !router->canRedo() && router->undoText() == QStringLiteral("Add device.Second to the Watchlist"));

		// A panel's notice that speaks for the step says it: the status line
		// need not (noticed). Hidden, it leaves it to the status line.
		press();
		CHECK(notice->property("text").toString() == QStringLiteral("Undone: Add device.Second to the Watchlist") &&
		      announced.last().at(1).toBool());
		auto* link = notice->findChild<QQuickItem*>(QStringLiteral("undoNoticeLink"));
		CHECK(link && link->isVisible() && link->property("text").toString() == QStringLiteral("Redo") &&
		      evaluate(link, "Accessible.name").toString() == QStringLiteral("Redo: Add device.Second to the Watchlist"));
		CHECK(notice->setProperty("active", false));
		press();
		CHECK(!announced.last().at(1).toBool() && announced.last().at(0).toString().startsWith(QStringLiteral("Undone: ")));
		CHECK(notice->setProperty("active", true));

		// A step that waits keeps its words, greyed, with why; Ctrl+Z says it.
		twoSteps();
		steps.setBlocked(QStringLiteral("Stop polling to change the Watchlist."));
		workspace->refresh();
		CHECK(!router->canUndo() && router->undoText() == QStringLiteral("Add device.Second to the Watchlist") &&
		      router->undoReason() == QStringLiteral("Stop polling to change the Watchlist."));
		auto* undoButton = shown(root, "undoButton");
		CHECK(!undoButton->isEnabled() &&
		      undoButton->property("text").toString() == QStringLiteral("Undo: Add device.Second to the Watchlist") &&
		      undoButton->property("disabledReason").toString() == QStringLiteral("Stop polling to change the Watchlist."));
		undone = 0;
		press();
		CHECK(undone == 0 && announced.last().at(0).toString() == QStringLiteral("Stop polling to change the Watchlist."));
		steps.setBlocked({});
		workspace->refresh();
		// The toolbar's Undo acts and never takes the keys.
		CHECK(undoButton->isEnabled() && undoButton->property("focusPolicy").toInt() == Qt::NoFocus);
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
		                  undoButton->mapToScene(QPointF(undoButton->width() / 2, undoButton->height() / 2)).toPoint());
		settle();
		CHECK(undone == 1 && plain->hasActiveFocus());
		CHECK(shown(root, "redoButton")->property("text").toString() ==
		      QStringLiteral("Redo: Add device.Second to the Watchlist"));

		// A long path is cut in the middle for a menu; the history keeps it whole.
		workspace->clear();
		const auto longPath = QStringLiteral("device.Measurements.Channels.Analog.Input7.Calibration.Offset");
		steps.record(workspace, QStringLiteral("Add %1 to the Watchlist").arg(longPath), [] {});
		CHECK(workspace->undoText().contains(longPath) && router->undoText().contains(QChar(0x2026)) &&
		      router->undoText().startsWith(QStringLiteral("Add device.Measurements")) &&
		      router->undoText().endsWith(QStringLiteral("Offset to the Watchlist")) &&
		      ux::UndoRouter::shortened(longPath).size() == 50);

		// The file in the Viewer: once the keys are there its bytes are what
		// Ctrl+Z takes; a step of the workspace recorded after that is taken
		// first (last recorded into), and the keys coming back to the file
		// make it the one again.
		workspace->clear();
		steps.record(workspace, QStringLiteral("Add device.First to the Watchlist"), [&] { ++undone; });
		auto* viewer = shown(root, "viewer");
		auto* grid = viewer->findChild<QQuickItem*>(QStringLiteral("fileViewerHex"));
		CHECK(grid);
		grid->forceActiveFocus(Qt::TabFocusReason);
		settle();
		CHECK(router->active() == content.history());
		CHECK(content.overwrite(0, QByteArray("Z")) && content.bytes().startsWith("ZB"));
		CHECK(router->undoText() == QStringLiteral("Overwrite 1 byte in keys.bin"));
		undone = 0;
		press();
		CHECK(content.bytes().startsWith("AB") && undone == 0 && workspace->canUndo());
		CHECK(content.overwrite(0, QByteArray("Y")));
		steps.record(workspace, QStringLiteral("Add device.Second to the Watchlist"), [&] { ++undone; });
		CHECK(router->active() == workspace);
		press();
		CHECK(undone == 1 && content.bytes().startsWith("YB"));
		plain->forceActiveFocus(Qt::TabFocusReason);
		settle();
		CHECK(router->active() == workspace);
		grid->forceActiveFocus(Qt::TabFocusReason);
		settle();
		CHECK(router->active() == content.history());
		// While the host writes the copy to the device, its steps wait.
		content.setWriting(true);
		CHECK(!router->canUndo() &&
		      router->undoReason() == QStringLiteral("Wait for keys.bin to finish writing to the device."));
		press();
		CHECK(content.bytes().startsWith("YB") &&
		      announced.last().at(0).toString() == QStringLiteral("Wait for keys.bin to finish writing to the device."));
		content.setWriting(false);
		press();
		CHECK(content.bytes().startsWith("AB"));

		// A menu and the palette act on what came before them: opening one
		// leaves the file the one acted on; its keys take no step.
		auto* menu = window->findChild<QObject*>(QStringLiteral("testMenu"));
		CHECK(menu && QMetaObject::invokeMethod(menu, "open"));
		settle();
		CHECK(menu->property("visible").toBool() && router->active() == content.history());
		const int heard = int(announced.size());
		CHECK(content.overwrite(1, QByteArray("Q")));
		press();
		CHECK(int(announced.size()) == heard && content.bytes().startsWith("AQ"));
		CHECK(router->undo() && content.bytes().startsWith("AB"));
		QTest::keyClick(window, Qt::Key_Escape);
		settle();
		CHECK(!menu->property("visible").toBool());
		// A modal dialog: nothing.
		auto* dialog = window->findChild<QObject*>(QStringLiteral("modalDialog"));
		CHECK(dialog && QMetaObject::invokeMethod(dialog, "open"));
		settle();
		CHECK(content.overwrite(2, QByteArray("R")));
		const int before = int(announced.size());
		press();
		CHECK(int(announced.size()) == before && content.bytes().startsWith("ABR"));
		QTest::keyClick(window, Qt::Key_Escape);
		settle();
		CHECK(!dialog->property("visible").toBool());

		window->close();
		settle();
		CHECK(warnings == 0);
		std::printf("Undo keys checks=%u warnings=%u\n", checks, warnings);
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Undo keys failure: %s\n", error.what());
		return 1;
	}
}
