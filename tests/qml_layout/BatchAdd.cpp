// The Batch's "+" row (BA-1, BA-4b), by keyboard: a half-typed path takes its
// first suggestion, a Field is added as a read, a name two endpoints share
// opens the suggestions, one that takes input is added at its initial values,
// the suggestions sit under the headings Fields, Commands and Services that
// the keys skip and a click narrows to, values typed after a path are refused,
// a refusal is said in the error colour, several pasted lines go in one call,
// and the row is off while the Batch runs. A fixture provider stands in for
// the device. MIT.
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
#include <QtTest/QSignalSpy>
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
	int count = 0;
	// The Batch's items as the panel lists them, when given.
	QStandardItemModel* items = nullptr;
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
		// One that takes input: added at its initial values (BA-4b).
		if (typed == QStringLiteral("control.Execute"))
			return {{"status", "adds"},
			        {"message", "Adds Command control.Execute as item 2, with taps [0 0 0 … (16)]; it runs when the Batch is executed."},
			        {"path", "control.Execute"}, {"operation", "command"}};
		if (typed == QStringLiteral("Volt"))
			return {{"status", "partial"}, {"message", "Enter takes measure.Voltage."}, {"path", "measure.Voltage"}};
		// Values typed after a path: set on the item instead.
		if (typed == QStringLiteral("control.Scale value=2"))
			return {{"status", "error"}, {"message", "Set values on the item, not here: remove \"value=2\"."},
			        {"path", "control.Scale"}};
		if (typed == QStringLiteral("Pi") || typed == QStringLiteral("service Pi"))
			return {{"status", "partial"}, {"message", "Enter takes command control.Ping."}, {"path", "control.Ping"}};
		if (typed.isEmpty())
			return {{"status", "empty"}, {"message", ""}};
		return {{"status", "missing"}, {"message", typed.contains('.') ? typed + " is not on this device."
		                                                               : "Nothing on this device is named " + typed + "."},
		        {"path", typed}};
	}
	Q_INVOKABLE QVariantList candidates(const QString& text) const
	{
		const auto entry = [](const char* path, const char* completion, const char* section, const char* type) {
			return QVariantMap{{"path", QString::fromUtf8(path)}, {"completion", QString::fromUtf8(completion)},
			                   {"kind", QString::fromUtf8(section).chopped(1)}, {"section", QString::fromUtf8(section)},
			                   {"typeText", QString::fromUtf8(type)}, {"container", false}};
		};
		// A heading, as the provider writes one: its kind's word before the text.
		const auto heading = [&text](const char* path, const char* word, int count, int shown) {
			return QVariantMap{{"heading", true}, {"path", QString::fromUtf8(path)}, {"word", QString::fromUtf8(word)},
			                   {"completion", QString::fromUtf8(word) + ' ' + text.trimmed()}, {"count", count},
			                   {"shown", shown}, {"container", false}};
		};
		const auto typed = text.trimmed();
		if (typed == QStringLiteral("device.Vo"))
			return {heading("Fields", "field", 1, 1), entry("device.Voltage", "device.Voltage", "Fields", "f32")};
		if (typed == QStringLiteral("Volt"))
			return {heading("Fields", "field", 1, 1), entry("measure.Voltage", "measure.Voltage", "Fields", "f32")};
		if (typed.compare(QStringLiteral("Ping"), Qt::CaseInsensitive) == 0 || typed == QStringLiteral("Pi"))
			return {heading("Commands", "command", 1, 1), entry("control.Ping", "command control.Ping", "Commands", "no input"),
			        heading("Services", "service", 1, 1), entry("control.Ping", "service control.Ping", "Services", "() -> ()")};
		if (typed == QStringLiteral("service Pi"))
			return {heading("Services", "service", 1, 1), entry("control.Ping", "service control.Ping", "Services", "() -> ()")};
		// Empty: the first of each kind.
		if (typed.isEmpty())
			return {heading("Fields", "field", 120, 1), entry("device.Voltage", "device.Voltage", "Fields", "f32"),
			        heading("Commands", "command", 2, 1), entry("control.Ping", "command control.Ping", "Commands", "no input")};
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
		for (const auto& line : lines) {
			items.append(QVariantMap{{"path", line}, {"what", "a read of " + line}});
			if (this->items) {
				auto* row = new QStandardItem;
				row->setData(line, Qt::UserRole + 1);
				row->setData(QStringLiteral("read"), Qt::UserRole + 2);
				row->setData(QStringLiteral("Ready"), Qt::UserRole + 3);
				row->setData(false, Qt::UserRole + 4);
				row->setData(false, Qt::UserRole + 5);
				this->items->appendRow(row);
			}
		}
		const int first = count + 1;
		count += int(lines.size());
		// The provider's step (UR-1): one serial per addition.
		return {{"status", "ok"}, {"added", int(lines.size())}, {"items", items}, {"first", first},
		        {"serial", 100 + int(added.size())}};
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
			items.setItemRoleNames({{Qt::UserRole + 1, "name"}, {Qt::UserRole + 2, "operation"}, {Qt::UserRole + 3, "status"},
			                        {Qt::UserRole + 4, "hasValue"}, {Qt::UserRole + 5, "historical"}});
			calls.items = &items;
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
			CHECK(textOf(content, "batchAddRest") == QStringLiteral("Add a Field, Command or Service"));
			// No operation box, no "Add selected draft" (BA-4b).
			CHECK(!named(content, "batchOperation") && !named(content, "addBatchButton"));
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
			// The row says what it added, and the step it recorded (UR-1).
			QSignalSpy said(named(content, "batchAddRow"), SIGNAL(added(QString, bool, int)));
			key(window, Qt::Key_Return);
			CHECK(calls.added.size() == 1 && calls.added.first() == QStringList({QStringLiteral("device.Voltage")}));
			CHECK(said.count() == 1 && said.first().at(1).toBool() && said.first().at(2).toInt() == 101);
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

			// A name two endpoints share: the suggestions under their kinds'
			// headings, Enter takes the first one that is not a heading.
			typeText(window, "Ping");
			CHECK(textOf(content, "batchAddCheck") == QStringLiteral("2 endpoints are named Ping. Down to choose."));
			CHECK(shown(content, "batchAddCheck")->property("color") == quiet);
			key(window, Qt::Key_Escape);  // the popup the suggestions opened
			key(window, Qt::Key_Return);
			auto* popup = window->findChild<QObject*>(QStringLiteral("batchAddPopup"));
			CHECK(popup && popup->property("visible").toBool());
			CHECK(row->property("highlighted").toInt() == 1);
			// The keys skip the headings: Down to the Service, Up back.
			key(window, Qt::Key_Down);
			CHECK(row->property("highlighted").toInt() == 3);
			key(window, Qt::Key_Down);
			CHECK(row->property("highlighted").toInt() == 3);
			key(window, Qt::Key_Up);
			CHECK(row->property("highlighted").toInt() == 1);
			shot(window, argc, argv, QStringLiteral("batch-add-choose-760"));
			key(window, Qt::Key_Return);
			CHECK(field->property("text").toString() == QStringLiteral("command control.Ping") && calls.added.size() == 1);
			clearField(window);

			// One that takes input is added at its initial values (BA-4b).
			typeText(window, "control.Execute");
			CHECK(textOf(content, "batchAddCheck").startsWith(QStringLiteral("Adds Command control.Execute as item 2, with ")));
			key(window, Qt::Key_Return);
			CHECK(calls.added.size() == 2 && calls.added.last() == QStringList({QStringLiteral("control.Execute")}));
			CHECK(field->property("text").toString().isEmpty() && field->hasActiveFocus());

			// Values typed after a path: refused in words, in the error colour.
			typeText(window, "control.Scale value=2");
			line = shown(content, "batchAddCheck");
			CHECK(line->property("text").toString() == QStringLiteral("Set values on the item, not here: remove \"value=2\"."));
			CHECK(line->property("color") != quiet);
			key(window, Qt::Key_Return);
			CHECK(calls.added.size() == 2);
			// Ctrl+Enter is nothing more: no Details to fill.
			QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier);
			settle();
			CHECK(calls.added.size() == 2);
			clearField(window);

			// A heading's click writes its kind word before the text: that
			// kind only.
			typeText(window, "Pi");
			CHECK(popup->property("visible").toBool());
			const auto candidates = row->property("candidates").toList();
			CHECK(candidates.size() == 4 && candidates.at(2).toMap().value("heading").toBool());
			CHECK(QMetaObject::invokeMethod(row, "choose", Q_ARG(QVariant, QVariant(2))));
			settle();
			CHECK(field->property("text").toString() == QStringLiteral("service Pi") && popup->property("visible").toBool());
			CHECK(row->property("candidates").toList().size() == 2);
			shot(window, argc, argv, QStringLiteral("batch-add-headings-760"));
			key(window, Qt::Key_Escape);
			clearField(window);
			// On an empty field, Down lists the first of each kind.
			key(window, Qt::Key_Down);
			CHECK(popup->property("visible").toBool() && row->property("highlighted").toInt() == 1);
			key(window, Qt::Key_Escape);

			// The item just added is the current one, its row saying it.
			auto* list = shown(content, "batchList");
			CHECK(list->property("count").toInt() == calls.count &&
			      list->property("currentIndex").toInt() == list->property("count").toInt() - 1);

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
