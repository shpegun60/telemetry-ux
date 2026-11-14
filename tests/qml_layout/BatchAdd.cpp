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
	Q_INVOKABLE QVariantMap check(const QString& text) const
	{
		const auto typed = text.trimmed();
		if (typed == QStringLiteral("device.Voltage"))
			return {{"status", "adds"}, {"message", "Adds a read of device.Voltage as item 1."},
			        {"path", "device.Voltage"}, {"operation", "read"}};
		if (typed.compare(QStringLiteral("Ping"), Qt::CaseInsensitive) == 0)
			return {{"status", "choose"}, {"message", "2 endpoints are named Ping: choose one below."}, {"path", "Ping"}};
		if (typed == QStringLiteral("command control.Ping"))
			return {{"status", "adds"}, {"message", "Adds Command control.Ping as item 2; it runs when the Batch is executed."},
			        {"path", "control.Ping"}, {"operation", "command"}};
		if (typed == QStringLiteral("control.Execute"))
			return {{"status", "input"},
			        {"message", "control.Execute takes input. Enter selects it: fill it in Details, then Add selected."},
			        {"path", "control.Execute"}};
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
		if (typed.compare(QStringLiteral("Ping"), Qt::CaseInsensitive) == 0 || typed == QStringLiteral("Pi"))
			return {entry("control.Ping", "command control.Ping", "Command", "()"),
			        entry("control.Ping", "service control.Ping", "Service", "() -> ()")};
		return {};
	}
	Q_INVOKABLE QVariantMap addPaths(const QStringList& lines)
	{
		added.append(lines);
		for (const auto& line : lines)
			if (line.trimmed() == QStringLiteral("Nope"))
				return {{"status", "refused"}, {"added", 0}, {"message", "Nothing on this device is named Nope."}};
		return {{"status", "ok"}, {"added", int(lines.size())}};
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
			CHECK(textOf(content, "batchAddNotice") == QStringLiteral("Added device.Voltage as item 1"));
			CHECK(field->property("text").toString().isEmpty() && field->hasActiveFocus());
			CHECK(shown(content, "batchToggle")->property("checked").toBool());
			shot(window, argc, argv, QStringLiteral("batch-add-added-760"));

			// A name two endpoints share: the suggestions, kinds in words, Enter takes one.
			typeText(window, "Ping");
			CHECK(textOf(content, "batchAddCheck") == QStringLiteral("2 endpoints are named Ping: choose one below."));
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
			clearField(window);

			// A refusal in the error colour; Enter adds nothing.
			typeText(window, "Nope");
			line = shown(content, "batchAddCheck");
			CHECK(line->property("text").toString() == QStringLiteral("Nothing on this device is named Nope."));
			CHECK(line->property("color") != quiet);
			key(window, Qt::Key_Return);
			CHECK(calls.added.size() == 1);
			clearField(window);

			// Pasted lines: one call, in order.
			QGuiApplication::clipboard()->setText(QStringLiteral("device.Voltage\ncommand control.Ping\n"));
			key(window, Qt::Key_V, Qt::ControlModifier);
			CHECK(calls.added.size() == 2 &&
			      calls.added.last() == QStringList({QStringLiteral("device.Voltage"), QStringLiteral("command control.Ping")}));
			CHECK(textOf(content, "batchAddNotice") == QStringLiteral("Added 2 items, 2 to 3"));

			// While the Batch runs the row is off.
			auto* fixture = window->findChild<QObject*>(QStringLiteral("batchAddFixture"));
			CHECK(fixture && fixture->setProperty("batchPending", true));
			settle();
			CHECK(!row->isEnabled());
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
