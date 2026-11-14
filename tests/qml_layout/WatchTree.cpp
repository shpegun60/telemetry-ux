// The Watchlist's list and tree views, by keyboard and pointer: groups and
// nodes from paths, folding, a node's summary, removing a node's values, the
// view's controls giving way, and an open editor that outlives rows added and
// removed around it. MIT.
#include <Telemetry/UX/WatchRows.hpp>
#include <QtCore/QDir>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QStandardItemModel>
#include <QtGui/QWheelEvent>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <cstdio>
#include <functional>
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

enum Role { Name = Qt::UserRole + 1, Status, Text, Writable, EditorKind, EditorText, TypeText, Key, PathTypes, Access };

class Model final : public QStandardItemModel {
public:
	QHash<int, QByteArray> roleNames() const override
	{
		return {{Name, "name"},         {Status, "status"},     {Text, "text"},
		        {Writable, "writable"}, {EditorKind, "editorKind"}, {EditorText, "editorText"},
		        {TypeText, "typeText"}, {Key, "watchKey"},      {PathTypes, "pathTypes"},
		        {Access, "access"}};
	}
	void put(int at, const QString& path, quint32 field, quint32 leaf, const QString& text, const QStringList& types,
	         bool writable)
	{
		auto* row = new QStandardItem;
		row->setData(path, Name);
		row->setData(QStringLiteral("Ok"), Status);
		row->setData(text, Text);
		row->setData(writable, Writable);
		row->setData(QStringLiteral("text"), EditorKind);
		row->setData(text, EditorText);
		row->setData(types.last(), TypeText);
		row->setData(qulonglong((quint64{leaf} << 32) | field), Key);
		row->setData(types, PathTypes);
		row->setData(writable ? QStringLiteral("rw") : QStringLiteral("r-"), Access);
		insertRow(at, row);
	}
	Model()
	{
		const QStringList samples{QStringLiteral("u16[64]"), QStringLiteral("u16")};
		put(0, QStringLiteral("data.Samples[7]"), 7, 8, QStringLiteral("498"), samples, false);
		put(1, QStringLiteral("device.Config.Enabled"), 5, 2, QStringLiteral("true"),
		    {QStringLiteral("struct (3)"), QStringLiteral("bool")}, true);
		put(2, QStringLiteral("device.Voltage"), 3, 0, QStringLiteral("230"), {QStringLiteral("u16")}, false);
		put(3, QStringLiteral("device.Config.Gain"), 5, 1, QStringLiteral("1.5"),
		    {QStringLiteral("struct (3)"), QStringLiteral("f32")}, true);
		put(4, QStringLiteral("data.Samples[3]"), 7, 4, QStringLiteral("512"), samples, false);
	}
};

class Calls final : public QObject {
	Q_OBJECT
public:
	int writes = 0, writeRow = -1, undos = 0;
	QString writeText;
	QList<int> removed;
	Q_INVOKABLE void write(int row, const QString& text)
	{
		++writes;
		writeRow = row;
		writeText = text;
	}
	Q_INVOKABLE void undo() { ++undos; }
	QStringList addedLines;
	Q_INVOKABLE QVariantMap check(const QString& text) const
	{
		const auto typed = text.trimmed();
		if (typed == QStringLiteral("device.Config.Gain"))
			return {{"status", "adds"}, {"message", "Adds device.Config.Gain"}, {"adds", 1}, {"already", 0}, {"row", -1}};
		if (typed == QStringLiteral("control.Execute"))
			return {{"status", "error"}, {"message", "control.Execute is a Command. Only Fields can be watched."}};
		return {{"status", typed.isEmpty() ? "empty" : "missing"}, {"message", typed.isEmpty() ? "" : typed + " is not on this device."}};
	}
	Q_INVOKABLE QVariantList candidates(const QString& text) const
	{
		const auto entry = [](const char* path, const char* type, int values, bool container, bool watched) {
			return QVariantMap{{"path", QString::fromUtf8(path)}, {"typeText", QString::fromUtf8(type)},
			                   {"values", values}, {"container", container}, {"watched", watched}};
		};
		// The top-level groups, as the provider gives them: the path ends in
		// "." already and there is no "next".
		if (text == QStringLiteral("d") || text == QStringLiteral("de"))
			return {entry("device.", "", 3, true, false)};
		if (text == QStringLiteral("s"))
			return {entry("device.", "", 3, true, false), entry("signals.", "", 2, true, false)};
		if (text == QStringLiteral("device."))
		{
			auto config = entry("device.Config", "struct (3)", 3, true, false);
			config.insert("next", ".");
			return {entry("device.Voltage", "u16", 1, false, true), config};
		}
		if (text == QStringLiteral("device.Config.") || text == QStringLiteral("device.Config.G"))
			return {entry("device.Config.Gain", "f32", 1, false, false)};
		return {};
	}
	Q_INVOKABLE QVariantMap addPaths(const QStringList& lines)
	{
		addedLines = lines;
		// A path the device lacks is kept as a row that says so (WL-4c).
		if (lines == QStringList{QStringLiteral("device.Nope")})
			return {{"status", "ok"}, {"added", 0}, {"already", 0},
			        {"missing", QVariantList{QStringLiteral("device.Nope")}}, {"refused", QVariantList{}}};
		return {{"status", "ok"}, {"added", int(lines.size())}, {"already", 0},
		        {"missing", QVariantList{}}, {"refused", QVariantList{}}};
	}
	QList<int> formatted;
	QString formatName = QStringLiteral("unset");
	Q_INVOKABLE void format(const QVariantList& rows, const QString& name)
	{
		formatted.clear();
		for (const auto& row : rows)
			formatted.append(row.toInt());
		formatName = name;
	}
	Q_INVOKABLE void remove(const QVariantList& rows)
	{
		removed.clear();
		for (const auto& row : rows)
			removed.append(row.toInt());
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
// An item that must be shown: a failure names it, never a null dereference.
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
QQuickItem* rowAt(QQuickItem* root, int row)
{
	if (root->objectName() == QLatin1String("watchlistRow") && root->property("index").toInt() == row)
		return root;
	for (auto* child : root->childItems())
		if (auto* found = rowAt(child, row))
			return found;
	return nullptr;
}
QQuickItem* rowShown(QQuickItem* root, int row)
{
	auto* found = rowAt(root, row);
	if (!found)
		throw std::runtime_error("row not shown: " + std::to_string(row));
	return found;
}
QString leaf(QQuickItem* list, int row)
{
	auto* item = rowAt(list, row);
	auto* text = item ? named(item, "watchRowLeaf") : nullptr;
	return text ? text->property("text").toString() : QString{};
}
QString summary(QQuickItem* list, int row)
{
	auto* item = rowAt(list, row);
	auto* text = item ? named(item, "watchRowSummary") : nullptr;
	return text ? text->property("text").toString() : QString{};
}
QString value(QQuickItem* list, int row)
{
	auto* item = rowAt(list, row);
	auto* text = item ? named(item, "watchValueText") : nullptr;
	return text ? text->property("text").toString() : QString{};
}
void settle()
{
	QCoreApplication::processEvents();
	QTest::qWait(35);
}
void click(QQuickWindow* window, QQuickItem* target)
{
	CHECK(target && target->isVisible() && target->width() > 0 && target->height() > 0);
	QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
	                  target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
	settle();
}
void typeText(QQuickWindow* window, const char* text)
{
	for (const char* at = text; *at; ++at)
		QTest::keyClick(window, *at);
	QTest::qWait(150);  // past the row's 80 ms settle
}
void key(QQuickWindow* window, Qt::Key k)
{
	QTest::keyClick(window, k);
	settle();
}
void shot(QQuickWindow* window, int argc, char** argv, const QString& name)
{
	if (argc < 2)
		return;
	QDir().mkpath(QString::fromLocal8Bit(argv[1]));
	CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(name + QStringLiteral(".png"))));
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
			Model model;
			telemetry::ux::WatchRows rows;
			rows.setSource(&model);
			Calls calls;
			QQmlApplicationEngine engine;
			engine.rootContext()->setContextProperty(QStringLiteral("watchTreeModel"), &model);
			engine.rootContext()->setContextProperty(QStringLiteral("watchTreeRows"), &rows);
			engine.rootContext()->setContextProperty(QStringLiteral("watchTreeCalls"), &calls);
			engine.load(QUrl(QStringLiteral("qrc:/WatchTree.qml")));
			CHECK(!engine.rootObjects().isEmpty());
			auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
			CHECK(window && QTest::qWaitForWindowExposed(window));
			settle();
			auto* content = window->contentItem();
			auto* panel = named(content, "watchTreePanel");
			auto* list = named(content, "watchlistView");
			CHECK(panel && list);

			// The list, as added: whole paths, the prefix muted.
			CHECK(list->property("count").toInt() == 5);
			CHECK(named(content, "watchViewList") && named(content, "watchViewTree"));
			CHECK(shown(content, "watchViewList")->property("checked").toBool());
			CHECK(leaf(list, 1) == QStringLiteral("Enabled"));
			CHECK(!named(content, "watchRowChevron"));
			shot(window, argc, argv, QStringLiteral("watch-list-480"));

			// The tree: groups and nodes from the paths, descriptor order.
			click(window, named(content, "watchViewTree"));
			CHECK(rows.tree() && panel->property("treeView").toBool());
			CHECK(list->property("count").toInt() == 9);
			const QStringList leaves{QStringLiteral("device"), QStringLiteral("Voltage"), QStringLiteral("Config"),
			                         QStringLiteral("Gain"),   QStringLiteral("Enabled"), QStringLiteral("data"),
			                         QStringLiteral("Samples"), QStringLiteral("[3]"),    QStringLiteral("[7]")};
			for (int row = 0; row < leaves.size(); ++row)
				CHECK(leaf(list, row) == leaves[row]);
			auto* group = rowAt(list, 0);
			CHECK(group && named(group, "watchGroupCount") &&
			      textOf(group, "watchGroupCount") == QStringLiteral("3 watched"));
			CHECK(!named(group, "watchValueText") && named(group, "watchRowChevron"));
			CHECK(!named(rowAt(list, 1), "watchRowChevron") && named(rowAt(list, 2), "watchRowChevron"));
			// A member sits one indent deeper than its Field.
			const auto x = [&](int row) {
				auto* text = named(rowAt(list, row), "watchRowLeaf");
				return text->mapToScene(QPointF(0, 0)).x();
			};
			CHECK(x(3) > x(2) && x(2) > x(0) && x(1) == x(2));
			// Inside a Field: no status or rw of its own; the Field has its type.
			CHECK(rowShown(list, 3)->property("accessText").toString().isEmpty());
			CHECK(rowShown(list, 2)->property("accessText").toString() == QStringLiteral("rw"));
			CHECK(rowShown(list, 2)->property("typeText").toString() == QStringLiteral("struct (3)"));
			shot(window, argc, argv, QStringLiteral("watch-tree-480"));

			// The number format (WL-3): the Watchlist's from its segments, a
			// row's own from its menu, which a node gives every value under it.
			window->setWidth(720);  // room for every control, whatever the fonts
			settle();
			auto* formats = named(content, "watchFormatSegments");
			CHECK(formats && shown(content, "watchFormat_original")->property("checked").toBool());
			click(window, named(content, "watchFormat_hex"));
			CHECK(panel->property("format").toString() == QStringLiteral("hex"));
			CHECK(shown(content, "watchFormat_hex")->property("checked").toBool());
			click(window, named(content, "watchFormat_original"));
			list->setProperty("currentIndex", 2);  // Config
			list->forceActiveFocus();
			key(window, Qt::Key_Menu);
			auto* rowMenu = window->findChild<QObject*>(QStringLiteral("watchRowMenu"));
			CHECK(rowMenu && rowMenu->property("visible").toBool() && panel->property("menuRow").toInt() == 2);
			// By keys: Down to "Number format", Right into it, Down twice past
			// "Like the Watchlist" and Original to Dec, Enter.
			for (const auto k : {Qt::Key_Down, Qt::Key_Right, Qt::Key_Down, Qt::Key_Down, Qt::Key_Return})
				key(window, k);
			CHECK(calls.formatted == QList<int>({3, 1}) && calls.formatName == QStringLiteral("dec"));
			auto* inherit = window->findChild<QObject*>(QStringLiteral("watchRowFormat_inherit"));
			CHECK(inherit && inherit->property("text").toString() == QStringLiteral("Like the Watchlist (Original)"));
			QTest::keyClick(window, Qt::Key_Escape);
			settle();
			if (rowMenu->property("visible").toBool()) {
				QTest::keyClick(window, Qt::Key_Escape);
				settle();
			}
			CHECK(!rowMenu->property("visible").toBool());
			window->setWidth(480);
			settle();

			// Copies (WL-4a): Ctrl+C takes "path = value" lines for what the
			// row shows, a node every value under it; Alt+Shift+C the paths.
			list->setProperty("currentIndex", 2);  // Config
			list->forceActiveFocus();
			QGuiApplication::clipboard()->clear();
			QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
			settle();
			CHECK(QGuiApplication::clipboard()->text() ==
			      QStringLiteral("device.Config.Gain = 1.5\ndevice.Config.Enabled = true"));
			CHECK(textOf(content, "watchNotice") == QStringLiteral("Copied 2 values"));
			CHECK(!named(content, "watchUndo"));
			QTest::keyClick(window, Qt::Key_C, Qt::AltModifier | Qt::ShiftModifier);
			settle();
			CHECK(QGuiApplication::clipboard()->text() == QStringLiteral("device.Config.Gain\ndevice.Config.Enabled"));
			list->setProperty("currentIndex", 1);  // Voltage
			QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
			settle();
			CHECK(QGuiApplication::clipboard()->text() == QStringLiteral("device.Voltage = 230"));
			CHECK(textOf(content, "watchNotice") == QStringLiteral("Copied device.Voltage"));

			// The Device tree's keys: Left folds, then steps out; Right opens,
			// then steps in. Folded, a node shows its values in brief.
			list->setProperty("currentIndex", 2);
			list->forceActiveFocus();
			const auto column = panel->property("valueColumn").toDouble();
			key(window, Qt::Key_Left);
			CHECK(list->property("count").toInt() == 7 && !rowShown(list, 2)->property("expanded").toBool());
			// The summary in the name's spare room, ending on the numbers'
			// edge; the Value column does not move.
			CHECK(summary(list, 2) == QStringLiteral("{Gain 1.5, Enabled true}") && value(list, 2).isEmpty());
			CHECK(panel->property("valueColumn").toDouble() == column);
			{
				auto* brief = named(rowAt(list, 2), "watchRowSummary");
				auto* number = named(rowAt(list, 1), "watchValueText");
				CHECK(brief && number);
				CHECK(qAbs(brief->mapToScene(QPointF(brief->width(), 0)).x() -
				           number->mapToScene(QPointF(number->width(), 0)).x()) < 1);
			}
			shot(window, argc, argv, QStringLiteral("watch-tree-folded-480"));
			key(window, Qt::Key_Right);
			CHECK(list->property("count").toInt() == 9 && summary(list, 2).isEmpty());
			CHECK(panel->property("valueColumn").toDouble() == column);
			key(window, Qt::Key_Right);
			CHECK(list->property("currentIndex").toInt() == 3);
			key(window, Qt::Key_Left);
			CHECK(list->property("currentIndex").toInt() == 2);
			key(window, Qt::Key_Return);
			CHECK(list->property("count").toInt() == 7);
			key(window, Qt::Key_Return);
			CHECK(list->property("count").toInt() == 9);

			// An open editor outlives rows added and removed around it, and
			// writes to its own value's row.
			list->setProperty("currentIndex", 3);
			list->forceActiveFocus();
			key(window, Qt::Key_F2);
			auto* gain = rowAt(list, 3);
			CHECK(gain && gain->property("editing").toBool());
			QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
			QTest::keyClick(window, 'x');
			settle();
			// Why not, on its own line; Enter keeps the text in the editor.
			CHECK(textOf(gain, "watchValueErrorText") == QStringLiteral("x is not a number a f32 holds."));
			CHECK(!named(gain, "watchValuePreview"));
			key(window, Qt::Key_Return);
			CHECK(gain->property("editing").toBool() && calls.writes == 0);
			shot(window, argc, argv, QStringLiteral("watch-edit-refused-480"));
			// What Enter would write, in the other form, beside the field.
			QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
			QTest::keyClick(window, '2');
			settle();
			CHECK(textOf(gain, "watchValuePreview") == QStringLiteral("= 0x40000000"));
			CHECK(!named(gain, "watchValueErrorText"));
			shot(window, argc, argv, QStringLiteral("watch-edit-preview-480"));
			// A row in above it: the list hands its current row the focus
			// again, and the row keeps it in its editor.
			QSignalSpy resets(&rows, &QAbstractItemModel::modelReset);
			model.put(0, QStringLiteral("device.Count"), 4, 0, QStringLiteral("9"), {QStringLiteral("u32")}, false);
			settle();
			CHECK(resets.isEmpty());
			CHECK(gain->property("editing").toBool());
			CHECK(gain->property("index").toInt() == 4);
			model.removeRows(1, 1);  // data.Samples[7]
			settle();
			CHECK(gain->property("editing").toBool());
			model.setData(model.index(3, 0), QStringLiteral("1.25"), Text);  // Gain read meanwhile
			settle();
			CHECK(gain->property("editing").toBool());
			CHECK(rowAt(list, 4) == gain);
			CHECK(textOf(gain, "watchValueEditor") == QStringLiteral("2"));
			key(window, Qt::Key_Return);
			CHECK(calls.writes == 1 && calls.writeRow == 3 && calls.writeText == QStringLiteral("2"));

			// × on a node and Delete on a group remove all their values at once;
			// Delete on a value removes it alone.
			list->setProperty("currentIndex", 3);  // Config
			list->forceActiveFocus();
			settle();
			click(window, named(rowAt(list, 3), "removeWatchButton"));
			CHECK(calls.removed == QList<int>({3, 1}));
			// Said with Undo, as the Device tree says it.
			auto* removedLine = named(content, "watchNotice");
			CHECK(removedLine && removedLine->property("text").toString() ==
			                         QStringLiteral("Removed 2 values of device.Config"));
			click(window, named(content, "watchUndo"));
			CHECK(calls.undos == 1 && removedLine->property("text").toString() == QStringLiteral("Undone"));
			CHECK(!named(content, "watchUndo"));
			list->setProperty("currentIndex", 0);
			list->forceActiveFocus();
			key(window, Qt::Key_Delete);
			CHECK(calls.removed == QList<int>({2, 0, 3, 1}));
			// Ctrl+Z undoes while the list has the keys.
			QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
			settle();
			CHECK(calls.undos == 2);
			list->setProperty("currentIndex", 1);
			list->forceActiveFocus();
			key(window, Qt::Key_Delete);
			CHECK(calls.removed == QList<int>({2}));
			CHECK(removedLine->property("text").toString() == QStringLiteral("Removed device.Voltage"));

			// Narrow: List | Tree moves into ⋯ last of all, after Read once.
			auto* treeItem = window->findChild<QObject*>(QStringLiteral("watchViewTreeItem"));
			CHECK(treeItem);
			for (const int width : {320, 240}) {
				window->setWidth(width);
				settle();
				const bool inBar = panel->property("viewInBar").toBool();
				CHECK(inBar == bool(named(content, "watchViewTree")));
				CHECK(inBar != treeItem->property("shown").toBool());
				// Read once goes before List | Tree does.
				CHECK(inBar || !panel->property("readOnceInBar").toBool());
				if (!inBar)
					CHECK(treeItem->property("checked").toBool());
				// The format's segments give way to one button before Read once goes.
				CHECK(panel->property("formatInBar").toBool() == bool(named(content, "watchFormatSegments")));
				CHECK(panel->property("formatInBar").toBool() || named(content, "watchFormatButton"));
				CHECK(!panel->property("readOnceInBar").toBool() || panel->property("formatInBar").toBool() ||
				      !panel->property("everyShown").toBool());
				if (width == 320)
					shot(window, argc, argv, QStringLiteral("watch-tree-320"));
				else
					CHECK(!inBar);
			}
			window->setWidth(480);
			settle();
			CHECK(panel->property("viewInBar").toBool() && !treeItem->property("shown").toBool());

			// The "+" row (WL-4b): Down past the last row edits it; typing
			// completes from the provider; Enter on a candidate fills the
			// field, on the path adds it; pasted lines go in at once.
			window->setWidth(720);
			settle();
			CHECK(named(content, "watchAddRow") && named(content, "watchAddRest"));
			list->setProperty("currentIndex", list->property("count").toInt() - 1);
			list->forceActiveFocus();
			key(window, Qt::Key_Down);
			auto* addField = named(content, "watchAddField");
			CHECK(addField && addField->hasActiveFocus() && !named(content, "watchAddRest"));
			// A top-level group, whose path ends in "." already, goes in with
			// one dot: by Enter, by Tab, and by a click.
			typeText(window, "d");
			settle();
			key(window, Qt::Key_Down);
			key(window, Qt::Key_Return);
			CHECK(addField->property("text").toString() == QStringLiteral("device."));
			addField->setProperty("text", QString());
			typeText(window, "de");
			settle();
			key(window, Qt::Key_Tab);
			CHECK(addField->property("text").toString() == QStringLiteral("device."));
			addField->setProperty("text", QString());
			typeText(window, "s");
			settle();
			{
				// The popup's rows live in the window's overlay, under its content item.
				QQuickItem* second = nullptr;
				const std::function<void(QQuickItem*)> find = [&](QQuickItem* item) {
					if (item->objectName() == QLatin1String("watchAddCandidate") && item->isVisible() &&
					    item->property("index").toInt() == 1)
						second = item;
					for (auto* child : item->childItems())
						find(child);
				};
				find(window->contentItem());
				CHECK(second != nullptr);
				if (second) {
					const auto at = second->mapToScene(QPointF(second->width() / 2, second->height() / 2)).toPoint();
					QTest::mouseClick(window, Qt::LeftButton, {}, at);
					settle();
				}
				CHECK(addField->property("text").toString() == QStringLiteral("signals."));
			}
			addField->setProperty("text", QString());
			settle();
			typeText(window, "device.");
			settle();
			auto* addPopup = window->findChild<QObject*>(QStringLiteral("watchAddPopup"));
			CHECK(addPopup && addPopup->property("visible").toBool());
			shot(window, argc, argv, QStringLiteral("watch-add-720"));
			key(window, Qt::Key_Down);
			key(window, Qt::Key_Down);
			key(window, Qt::Key_Return);  // device.Config, a struct: its members next
			CHECK(addField->property("text").toString() == QStringLiteral("device.Config."));
			typeText(window, "Gain");
			settle();
			CHECK(textOf(content, "watchAddCheck") == QStringLiteral("Adds device.Config.Gain"));
			key(window, Qt::Key_Return);
			CHECK(calls.addedLines == QStringList({QStringLiteral("device.Config.Gain")}));
			CHECK(addField->property("text").toString().isEmpty() && addField->hasActiveFocus());
			CHECK(textOf(content, "watchNotice") == QStringLiteral("Added device.Config.Gain"));
			CHECK(named(content, "watchUndo"));
			// A Command is refused in words, and Enter adds nothing.
			typeText(window, "control.Execute");
			settle();
			CHECK(textOf(content, "watchAddCheck") ==
			      QStringLiteral("control.Execute is a Command. Only Fields can be watched."));
			calls.addedLines.clear();
			key(window, Qt::Key_Return);
			CHECK(calls.addedLines.isEmpty());
			QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
			key(window, Qt::Key_Delete);
			// Several lines pasted, as copied from the Watchlist: added at once.
			QGuiApplication::clipboard()->setText(QStringLiteral("device.Config.Gain = 1.5\ndevice.Voltage = 230\n"));
			QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
			settle();
			CHECK(calls.addedLines == QStringList({QStringLiteral("device.Config.Gain = 1.5"), QStringLiteral("device.Voltage = 230")}));
			CHECK(textOf(content, "watchNotice") == QStringLiteral("Added 2"));
			// A path this device lacks: said quietly, and Enter keeps it as a row.
			typeText(window, "device.Nope");
			settle();
			auto* nopeCheck = named(content, "watchAddCheck");
			CHECK(nopeCheck->property("text").toString() == QStringLiteral("device.Nope is not on this device."));
			key(window, Qt::Key_Return);
			CHECK(calls.addedLines == QStringList({QStringLiteral("device.Nope")}));
			CHECK(textOf(content, "watchNotice") == QStringLiteral("Added device.Nope, not on this device"));
			CHECK(addField->property("text").toString().isEmpty() && named(content, "watchUndo"));
			// Half typed, the suggestions going on from it: Enter takes the first,
			// never keeps the half as a row the device lacks.
			// Enter at once, before the suggestions settle: made again first.
			calls.addedLines.clear();
			for (const char* at = "device.Config.G"; *at; ++at)
				QTest::keyClick(window, *at);
			key(window, Qt::Key_Return);
			CHECK(calls.addedLines.isEmpty() && addField->property("text").toString() == QStringLiteral("device.Config.Gain"));
			key(window, Qt::Key_Return);
			CHECK(calls.addedLines == QStringList({QStringLiteral("device.Config.Gain")}));
			key(window, Qt::Key_Escape);
			CHECK(named(content, "watchAddRest"));
			window->setWidth(480);
			settle();

			// Back to the list: the current value stays current.
			list->setProperty("currentIndex", 4);  // Gain
			CHECK(QMetaObject::invokeMethod(panel, "showTree", Q_ARG(QVariant, false)));
			settle();
			CHECK(!rows.tree() && list->property("count").toInt() == 5);
			const int current = list->property("currentIndex").toInt();
			CHECK(current >= 0 && rowShown(list, current)->property("name").toString() == QStringLiteral("device.Config.Gain"));
			// A row for a path this device lacks: a quiet word, no value, no access.
			model.put(5, QStringLiteral("device.Nope"), 0, 0, QString{}, {QString{}}, false);
			model.item(5)->setData(QStringLiteral("Missing"), Status);
			model.item(5)->setData(QString{}, Access);
			model.item(5)->setData(QString{}, TypeText);
			model.item(5)->setData(qulonglong((quint64{1} << 52) | 1), Key);
			settle();
			auto* nope = rowAt(list, 5);
			CHECK(nope && nope->property("missing").toBool() && !nope->property("problem").toBool());
			// The word, whether the status shows as a word or (narrow) as a mark.
			CHECK(nope->property("statusText").toString() == QStringLiteral("Not on device"));
			CHECK(value(list, 5) == QStringLiteral("–"));
			shot(window, argc, argv, QStringLiteral("watch-missing-480"));
			model.removeRow(5);
			settle();
			// Many rows in a short panel: the list takes the room and scrolls,
			// the "+" row stays in view under it.
			for (int at = 0; at < 40; ++at)
				model.put(model.rowCount(), QStringLiteral("data.Samples[%1]").arg(100 + at), 7, quint32(101 + at),
				          QStringLiteral("1"), {QStringLiteral("u16[64]"), QStringLiteral("u16")}, false);
			settle();
			CHECK(list->height() + 1 < list->property("contentHeight").toReal());
			auto* plusRow = shown(content, "watchAddRow");
			CHECK(plusRow->mapToItem(content, QPointF(0, plusRow->height())).y() <= content->height() + 0.5);
			const auto top = list->property("contentY").toReal();
			const QPointF middle = list->mapToScene(QPointF(list->width() / 2, list->height() / 2));
			QWheelEvent wheel(middle, window->mapToGlobal(middle), QPoint(), QPoint(0, -360), Qt::NoButton,
			                  Qt::NoModifier, Qt::NoScrollPhase, false);
			QCoreApplication::sendEvent(window, &wheel);
			settle();
			CHECK(list->property("contentY").toReal() > top);
			// Back to the top and settled before the rows go, as a user would be.
			list->setProperty("contentY", top);
			QTest::qWait(200);
			settle();
			model.removeRows(5, 40);
			settle();
			window->close();
		}
		settle();
		CHECK(warnings == 0);
		qInfo().noquote() << QStringLiteral("Watch tree checks=%1 warnings=%2").arg(checks).arg(warnings);
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Watch tree failure: %s\n", error.what());
		return 1;
	}
}
#include "WatchTree.moc"
