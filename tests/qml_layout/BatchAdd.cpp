// The Batch's "+" row (BA-1), by keyboard: a half-typed path takes its first
// suggestion, a Field is added as a read, a name two endpoints share opens the
// suggestions, one that takes input is selected instead, a refusal is said in
// the error colour, several pasted lines go in one call, and the row is off
// while the Batch runs. A fixture provider stands in for the device. MIT.
#include <QtCore/QDir>
#include <QtCore/QVariantMap>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QStandardItemModel>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QTest>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
int checks = 0, warnings = 0;
void check(bool condition, const char* expression)
{
	++checks;
	if (!condition)
		throw std::runtime_error(expression);
}
#define CHECK(x) check(bool(x), #x)

class Calls final : public QObject {
	Q_OBJECT
public:
	QList<QStringList> added;
	QStringList selected;
	int count = 0;
	Q_INVOKABLE QVariantMap check(const QString& text) const
	{
		const auto typed = text.trimmed();
		if (typed == QStringLiteral("device.Voltage"))
			return {{"status", "adds"}, {"message", "Adds a read of device.Voltage as item 1."},
			        {"path", "device.Voltage"}, {"operation", "read"}};
		if (typed.compare(QStringLiteral("Ping"), Qt::CaseInsensitive) == 0)
			return {{"status", "choose"}, {"message", "2 endpoints are named Ping. Down to choose."}, {"path", "Ping"}};
		if (typed == QStringLiteral("command control.Ping"))
			return {{"status", "adds"}, {"message", "Adds Command control.Ping as item 2; it runs when the Batch is executed."},
			        {"path", "control.Ping"}, {"operation", "command"}};
		if (typed == QStringLiteral("control.Execute"))
			return {{"status", "input"},
			        {"message", "control.Execute takes input. Enter selects it: fill it in Details, then Add selected draft."},
			        {"path", "control.Execute"}, {"operation", "command"}};
		if (typed == QStringLiteral("Volt"))
			return {{"status", "partial"}, {"message", "Enter takes measure.Voltage."}, {"path", "measure.Voltage"}};
		// A Service's values typed after its path (BA-2), as the provider answers.
		if (typed == QStringLiteral("control.Scale") || typed == QStringLiteral("control.Scale value=2"))
			return {{"status", "needs"},
			        {"message", typed.endsWith('2') ? "control.Scale needs factor (f32) too. Enter writes factor=."
			                                        : "control.Scale needs value (f32) and factor (f32). Enter writes value=; "
			                                          "Ctrl+Enter fills it in Details."},
			        {"next", typed.endsWith('2') ? "factor" : "value"}, {"path", "control.Scale"}, {"operation", "service"}};
		if (typed == QStringLiteral("control.Scale value="))
			return {{"status", "needs"}, {"message", "value (f32) takes a number."}, {"next", "value"},
			        {"path", "control.Scale"}, {"operation", "service"}};
		if (typed == QStringLiteral("control.Scale 2 3"))
			return {{"status", "unnamed"},
			        {"message", "control.Scale takes named values: value=2 factor=3. Enter writes the names in."},
			        {"named", "control.Scale value=2 factor=3"}, {"path", "control.Scale"}, {"operation", "service"}};
		if (typed == QStringLiteral("control.Scale value=2 factor=3"))
			return {{"status", "adds"},
			        {"message", "Adds a call of Service control.Scale (value 2, factor 3) as item 4; it is called when the Batch is executed."},
			        {"path", "control.Scale"}, {"operation", "service"}};
		if (typed.isEmpty())
			return {{"status", "empty"}, {"message", ""}};
		return {{"status", "missing"}, {"message", typed.contains('.') ? typed + " is not on this device."
		                                                               : "Nothing on this device is named " + typed + "."},
		        {"path", typed}};
	}
	Q_INVOKABLE QVariantList candidates(const QString& text) const
	{
		const auto entry = [](const char* path, const char* completion, const char* kind, const char* type) {
			return QVariantMap{{"path", QString::fromUtf8(path)}, {"completion", QString::fromUtf8(completion)},
			                   {"kind", QString::fromUtf8(kind)}, {"typeText", QString::fromUtf8(type)},
			                   {"container", false}};
		};
		const auto typed = text.trimmed();
		if (typed == QStringLiteral("device.Vo"))
			return {entry("device.Voltage", "device.Voltage", "Field", "f32 r-")};
		if (typed == QStringLiteral("Volt"))
			return {entry("measure.Voltage", "measure.Voltage", "Field", "f32 r-")};
		if (typed.compare(QStringLiteral("Ping"), Qt::CaseInsensitive) == 0 || typed == QStringLiteral("Pi"))
			return {entry("control.Ping", "command control.Ping", "Command", "()"),
			        entry("control.Ping", "service control.Ping", "Service", "() -> ()")};
		return {};
	}
	Q_INVOKABLE QVariantMap addPaths(const QStringList& lines)
	{
		added.append(lines);
		for (int at = 0; at < lines.size(); ++at)
			if (lines[at].trimmed() == QStringLiteral("Nope"))
				return {{"status", "refused"}, {"added", 0},
				        {"message", QStringLiteral("Nothing added: line %1, Nope: Nothing on this device is named Nope.").arg(at + 1)}};
		QVariantList items;
		for (const auto& line : lines)
			items.append(QVariantMap{{"path", line}, {"what", "a read of " + line}});
		const int first = count + 1;
		count += int(lines.size());
		return {{"status", "ok"}, {"added", int(lines.size())}, {"items", items}, {"first", first}};
	}
	Q_INVOKABLE bool selectPath(const QString& text)
	{
		selected.append(text.trimmed());
		return true;
	}
};

QQuickItem* named(QQuickItem* root, const char* name)
{
	if (root->objectName() == QLatin1String(name) && root->isVisible())
		return root;
	for (auto* child : root->childItems())
		if (auto* found = named(child, name))
			return found;
	return nullptr;
}
QQuickItem* shown(QQuickItem* root, const char* name)
{
	auto* found = root ? named(root, name) : nullptr;
	if (!found)
		throw std::runtime_error(std::string("not shown: ") + name);
	return found;
}
QString textOf(QQuickItem* root, const char* name)
{
	return shown(root, name)->property("text").toString();
}
void shot(QQuickWindow* window, int argc, char** argv, const QString& name)
{
	if (argc < 2)
		return;
	QDir().mkpath(QString::fromLocal8Bit(argv[1]));
	CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(name + QStringLiteral(".png"))));
}
void settle()
{
	QCoreApplication::processEvents();
	QTest::qWait(30);
	QCoreApplication::processEvents();
}
void typeText(QQuickWindow* window, const char* text, bool wait = true)
{
	for (const char* at = text; *at; ++at)
		QTest::keyClick(window, *at);
	if (wait)
		QTest::qWait(150);  // past the row's 80 ms settle
}
void key(QQuickWindow* window, Qt::Key k, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	QTest::keyClick(window, k, modifiers);
	settle();
}
void clearField(QQuickWindow* window)
{
	key(window, Qt::Key_A, Qt::ControlModifier);
	key(window, Qt::Key_Delete);
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& text) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
			++warnings;
		std::fprintf(stderr, "%s\n", qPrintable(text));
	});
	try {
		{
			QStandardItemModel items, responses;
			Calls calls;
			QQmlApplicationEngine engine;
			engine.rootContext()->setContextProperty(QStringLiteral("batchAddItems"), &items);
			engine.rootContext()->setContextProperty(QStringLiteral("batchAddResponses"), &responses);
			engine.rootContext()->setContextProperty(QStringLiteral("batchAddCalls"), &calls);
			engine.load(QUrl(QStringLiteral("qrc:/BatchAdd.qml")));
			CHECK(!engine.rootObjects().isEmpty());
			auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
			CHECK(window && QTest::qWaitForWindowExposed(window));
			settle();
			auto* content = window->contentItem();
			// Folded, the row is not there; unfolded, it sits under the list.
			CHECK(!named(content, "batchAddRow"));
			CHECK(shown(content, "batchToggle")->setProperty("checked", true));
			settle();
			auto* row = shown(content, "batchAddRow");
			// Its own names, never the Watchlist's.
			CHECK(textOf(content, "batchAddRest") == QStringLiteral("Add by name or path"));
			CHECK(!named(content, "watchAddRow") && !named(content, "watchAddRest"));
			CHECK(QMetaObject::invokeMethod(row, "begin"));
			settle();
			auto* field = shown(content, "batchAddField");
			CHECK(field->hasActiveFocus());

			// Half typed, Enter at once: the first suggestion, nothing added.
			typeText(window, "device.Vo", false);
			key(window, Qt::Key_Return);
			CHECK(calls.added.isEmpty() && field->property("text").toString() == QStringLiteral("device.Voltage"));
			// Said in the quiet colour; Enter adds it as the next item.
			settle();
			auto* line = shown(content, "batchAddCheck");
			CHECK(line->property("text").toString() == QStringLiteral("Adds a read of device.Voltage as item 1."));
			const auto quiet = line->property("color");
			key(window, Qt::Key_Return);
			CHECK(calls.added.size() == 1 && calls.added.first() == QStringList({QStringLiteral("device.Voltage")}));
			CHECK(textOf(content, "batchAddNotice") == QStringLiteral("Added a read of device.Voltage as item 1."));
			CHECK(field->property("text").toString().isEmpty() && field->hasActiveFocus());
			const auto noticeQuiet = shown(content, "batchAddNotice")->property("color");

			// A name half typed: said quietly, Enter takes the first suggestion.
			typeText(window, "Volt");
			CHECK(textOf(content, "batchAddCheck") == QStringLiteral("Enter takes measure.Voltage."));
			CHECK(shown(content, "batchAddCheck")->property("color") == quiet);
			key(window, Qt::Key_Return);
			CHECK(field->property("text").toString() == QStringLiteral("measure.Voltage") && calls.added.size() == 1);
			clearField(window);
			shot(window, argc, argv, QStringLiteral("batch-add-added-760"));

			// A name two endpoints share: the suggestions, kinds in words, Enter takes one.
			typeText(window, "Ping");
			CHECK(textOf(content, "batchAddCheck") == QStringLiteral("2 endpoints are named Ping. Down to choose."));
			CHECK(shown(content, "batchAddCheck")->property("color") == quiet);
			key(window, Qt::Key_Escape);  // the popup the suggestions opened
			key(window, Qt::Key_Return);
			auto* popup = window->findChild<QObject*>(QStringLiteral("batchAddPopup"));
			CHECK(popup && popup->property("visible").toBool());
			CHECK(row->property("highlighted").toInt() == 0);
			shot(window, argc, argv, QStringLiteral("batch-add-choose-760"));
			key(window, Qt::Key_Return);
			CHECK(field->property("text").toString() == QStringLiteral("command control.Ping") && calls.added.size() == 1);
			clearField(window);

			// One that takes input is selected for Details, not added.
			typeText(window, "control.Execute");
			key(window, Qt::Key_Return);
			CHECK(calls.selected == QStringList({QStringLiteral("control.Execute")}) && calls.added.size() == 1);
			CHECK(textOf(content, "batchAddNotice").startsWith(QStringLiteral("Selected control.Execute")));
			// Ready to add it once filled: the operation box says what it is.
			CHECK(shown(content, "batchOperation")->property("currentText") == QStringLiteral("command"));
			clearField(window);

			// BA-2: a Service's values typed after it. Enter writes the next
			// name; on "name=" it waits for the value; values with no names get
			// them written in; once all are valid Enter adds.
			typeText(window, "control.Scale");
			CHECK(shown(content, "batchAddCheck")->property("color") == quiet);
			key(window, Qt::Key_Return);
			CHECK(field->property("text").toString() == QStringLiteral("control.Scale value=") && calls.added.size() == 1);
			key(window, Qt::Key_Return);
			CHECK(field->property("text").toString() == QStringLiteral("control.Scale value="));
			clearField(window);
			typeText(window, "control.Scale 2 3");
			key(window, Qt::Key_Return);
			CHECK(field->property("text").toString() == QStringLiteral("control.Scale value=2 factor=3") &&
			      calls.added.size() == 1);
			settle();
			key(window, Qt::Key_Return);
			CHECK(calls.added.size() == 2 && calls.added.last() == QStringList({QStringLiteral("control.Scale value=2 factor=3")}));
			// Ctrl+Enter: to Details with it, whatever is typed.
			typeText(window, "control.Scale value=2");
			QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier);
			settle();
			CHECK(calls.selected.last() == QStringLiteral("control.Scale value=2") && calls.added.size() == 2);
			clearField(window);

			// A refusal in the error colour; Enter adds nothing.
			typeText(window, "Nope");
			line = shown(content, "batchAddCheck");
			CHECK(line->property("text").toString() == QStringLiteral("Nothing on this device is named Nope."));
			CHECK(line->property("color") != quiet);
			key(window, Qt::Key_Return);
			CHECK(calls.added.size() == 2);
			clearField(window);

			// Pasted lines: one call, in order.
			QGuiApplication::clipboard()->setText(QStringLiteral("device.Voltage\ncommand control.Ping\n"));
			key(window, Qt::Key_V, Qt::ControlModifier);
			CHECK(calls.added.size() == 3 &&
			      calls.added.last() == QStringList({QStringLiteral("device.Voltage"), QStringLiteral("command control.Ping")}));
			CHECK(textOf(content, "batchAddNotice") == QStringLiteral("Added 2 items as items 3 to 4."));
			// A refused paste: nothing added, the line named, in the error colour.
			QGuiApplication::clipboard()->setText(QStringLiteral("device.Voltage\nNope\n"));
			key(window, Qt::Key_V, Qt::ControlModifier);
			CHECK(textOf(content, "batchAddNotice") ==
			      QStringLiteral("Nothing added: line 2, Nope: Nothing on this device is named Nope."));
			CHECK(shown(content, "batchAddNotice")->property("color") != noticeQuiet);
			// Escape on an empty field: back to the list, never a hidden field.
			key(window, Qt::Key_Escape);
			CHECK(!field->hasActiveFocus() && shown(content, "batchList")->hasActiveFocus());
			// Down from the last item comes back to the row.
			key(window, Qt::Key_Down);
			CHECK(field->hasActiveFocus());

			// While the Batch runs the row is off.
			auto* fixture = window->findChild<QObject*>(QStringLiteral("batchAddFixture"));
			CHECK(fixture && fixture->setProperty("batchPending", true));
			settle();
			CHECK(!row->isEnabled());
			// The reply in: the field takes the keys back.
			CHECK(fixture->setProperty("batchPending", false));
			settle();
			CHECK(row->isEnabled() && field->hasActiveFocus());
			window->close();
		}
		settle();
		CHECK(warnings == 0);
		qInfo().noquote() << QStringLiteral("Batch add checks=%1 warnings=%2").arg(checks).arg(warnings);
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Batch add failure: %s\n", error.what());
		return 1;
	}
}
#include "BatchAdd.moc"
