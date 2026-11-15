// The Watchlist's list and tree views, by keyboard and pointer: groups and
// nodes from paths, folding, a node's summary, removing a node's values, the
// view's controls giving way, and an open editor that outlives rows added and
// removed around it. MIT.
#include "ScriptSteps.hpp"
#include <Telemetry/UX/QmlTypes.hpp>
#include <Telemetry/UX/WatchRows.hpp>
#include <QtCore/QDir>
#include <QtCore/QLocale>
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
	telemetry::ux::registerQmlTypes();
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
			ScriptSteps steps;
			QQmlApplicationEngine engine;
			engine.rootContext()->setContextProperty(QStringLiteral("scriptSteps"), &steps);
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
			// The list by name (WL-2c): the Name header sorts A-Z and back to the
			// order added; the rows move, nothing is reset.
			{
				auto* header = shown(content, "watchColumnName");
				QSignalSpy resets(&rows, &QAbstractItemModel::modelReset);
				click(window, header);
				CHECK(rows.sorted() && header->property("text").toString() == QStringLiteral("Name ▴"));
				for (int row = 1; row < rows.count(); ++row)
					CHECK(!telemetry::ux::WatchRows::pathLess(rows.index(row).data(telemetry::ux::WatchRows::NameRole).toString(),
					                                          rows.index(row - 1).data(telemetry::ux::WatchRows::NameRole).toString()));
				click(window, header);
				CHECK(!rows.sorted() && leaf(list, 1) == QStringLiteral("Enabled") && resets.isEmpty());
				// A value being edited keeps its editor and its text through a
				// sort: the row moves, it is not made again; the cursor follows.
				int writable = -1;
				for (int row = 0; row < rows.count() && writable < 0; ++row)
					if (rowShown(list, row)->property("writable").toBool())
						writable = row;
				CHECK(writable >= 0);
				list->setProperty("currentIndex", writable);
				list->forceActiveFocus();
				settle();
				key(window, Qt::Key_F2);
				auto* edited = rowShown(list, writable);
				CHECK(edited->property("editing").toBool());
				typeText(window, "7");
				const auto typed = textOf(edited, "watchValueEditor");
				click(window, header);
				CHECK(rows.sorted() && edited->property("editing").toBool() && textOf(edited, "watchValueEditor") == typed);
				CHECK(list->property("currentIndex").toInt() == edited->property("index").toInt());
				key(window, Qt::Key_Escape);
				// The menu's way to the same: checked as the list is sorted.
				click(window, named(content, "watchMoreButton"));
				auto* sortItem = shown(content, "watchSortByName");
				CHECK(sortItem->property("checked").toBool());
				click(window, sortItem);
				CHECK(!rows.sorted());
				// x says Delete does the same only on the cursor's row alone.
				list->setProperty("currentIndex", 0);
				list->forceActiveFocus();
				rows.select(0);
				settle();
				const std::function<QQuickItem*(QQuickItem*)> xOf = [&](QQuickItem* root) -> QQuickItem* {
					if (root->objectName() == QLatin1String("removeWatchButton"))
						return root;
					for (auto* child : root->childItems())
						if (auto* found = xOf(child))
							return found;
					return nullptr;
				};
				CHECK(xOf(rowShown(list, 0))->property("tip").toString().endsWith(QStringLiteral("(Delete)")));
				CHECK(!xOf(rowShown(list, 1))->property("tip").toString().endsWith(QStringLiteral("(Delete)")));
				rows.selectAll();
				settle();
				CHECK(!xOf(rowShown(list, 0))->property("tip").toString().endsWith(QStringLiteral("(Delete)")));
				rows.clearSelection();
			}

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
			// Every value back to Original (WL-3b): the Watchlist's form and every
			// row's own, from the "…" menu or a row's Number format menu, as one
			// change Undo and Ctrl+Z - the window's router - take back from
			// anywhere in the panel; the notice and its Undo go once a form is
			// chosen again.
			{
				auto* fixture = panel->property("provider").value<QObject*>();
				const auto ownFormats = [fixture] { return fixture->property("rowFormats").toMap(); };
				const QVariantMap chosen{{QStringLiteral("row3"), QStringLiteral("dec")},
				                         {QStringLiteral("row1"), QStringLiteral("dec")}};
				const auto formatIs = [panel](const char* name) {
					return panel->property("format").toString() == QLatin1String(name);
				};
				CHECK(ownFormats() == chosen);
				click(window, named(content, "watchFormat_hex"));
				auto* more = named(content, "watchMoreButton");
				click(window, more);
				auto* reset = shown(content, "watchResetFormats");
				CHECK(reset->property("enabled").toBool() &&
				      reset->property("text").toString() == QStringLiteral("Reset number formats to Original"));
				click(window, reset);
				CHECK(formatIs("original") && ownFormats().isEmpty());
				CHECK(textOf(content, "watchNotice") == QStringLiteral("Number formats reset to Original; 2 values had their own"));
				CHECK(named(content, "undoNoticeLink"));
				// Ctrl+Z with the keys on the ⋯ button, not in the list.
				more->forceActiveFocus(Qt::TabFocusReason);
				settle();
				QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
				settle();
				CHECK(formatIs("hex") && ownFormats() == chosen);
				// Taken, the step's link offers it again.
				CHECK(textOf(content, "watchNotice") == QStringLiteral("Undone: Reset number formats to Original") &&
				      textOf(content, "undoNoticeLink") == QStringLiteral("Redo"));
				// From the row's Number format menu, by keys: Down to it, Right
				// into it, past "Like the Watchlist", Original, Dec and Hex to
				// the reset under them.
				list->setProperty("currentIndex", 2);
				list->forceActiveFocus();
				key(window, Qt::Key_Menu);
				for (const auto k : {Qt::Key_Down, Qt::Key_Right, Qt::Key_Down, Qt::Key_Down, Qt::Key_Down, Qt::Key_Down})
					key(window, k);
				auto* rowReset = shown(content, "watchRowFormatReset");
				CHECK(rowReset->property("enabled").toBool() && rowReset->property("highlighted").toBool());
				key(window, Qt::Key_Return);
				CHECK(formatIs("original") && ownFormats().isEmpty() && textOf(content, "undoNoticeLink") == QStringLiteral("Undo"));
				// A form chosen: the notice no longer says how things are, and its
				// step is no longer the one Undo would take; both go.
				click(window, named(content, "watchFormat_dec"));
				CHECK(formatIs("dec") && !named(content, "watchNotice") && !named(content, "undoNoticeLink"));
				auto* history = fixture->property("history").value<QObject*>();
				CHECK(history && history->property("undoText").toString() == QStringLiteral("Show the Watchlist in dec"));
				// A value in a format of its own is counted by a mark before the
				// format control, and the mark shows every value as Original
				// (WL-3c). With nothing to count it keeps its room, clear and out
				// of reach, so its coming moves nothing in the row.
				auto* mark = named(content, "watchOwnFormats");
				const auto segmentX = [content] {
					return named(content, "watchFormat_original")->mapToItem(content, QPointF{}).x();
				};
				const auto flags = [panel] {
					QStringList set;
					for (const char* name : {"viewInBar", "readOnceInBar", "formatInBar", "everyShown"})
						set << QStringLiteral("%1=%2").arg(QLatin1String(name)).arg(panel->property(name).toBool());
					return set;
				};
				CHECK(mark && mark->isVisible() && mark->property("rows").toInt() == 0 && mark->opacity() == 0 &&
				      !mark->isEnabled());
				const auto clearAt = segmentX();
				const auto clearFlags = flags();
				CHECK(mark->mapToItem(content, QPointF{}).x() < clearAt);
				fixture->setProperty("rowFormats", QVariantMap{{QStringLiteral("row1"), QStringLiteral("hex")}});
				settle();
				CHECK(mark->property("rows").toInt() == 1 && mark->opacity() == 1 && mark->isEnabled());
				CHECK(mark->property("text").toString() == QStringLiteral("\u25CF 1"));
				CHECK(segmentX() == clearAt && flags() == clearFlags);
				// By keys: the keys it held go to the list, and Ctrl+Z brings the
				// value's own format, and the mark, back.
				mark->forceActiveFocus(Qt::TabFocusReason);
				settle();
				QTest::keyClick(window, Qt::Key_Space);
				settle();
				CHECK(formatIs("original") && ownFormats().isEmpty() && mark->opacity() == 0);
				CHECK(list->hasActiveFocus());
				QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
				settle();
				CHECK(formatIs("dec") && mark->property("rows").toInt() == 1);
				// A value in the Watchlist's own format is not in a format of
				// its own: the count follows the Watchlist's, set here from
				// elsewhere, and keys left on a mark with nothing to count go to
				// the list.
				mark->forceActiveFocus(Qt::TabFocusReason);
				settle();
				fixture->setProperty("watchFormat", QStringLiteral("hex"));
				settle();
				CHECK(mark->property("rows").toInt() == 0 && mark->opacity() == 0 && !mark->isEnabled());
				CHECK(list->hasActiveFocus());
				click(window, named(content, "watchFormat_dec"));
				CHECK(mark->property("rows").toInt() == 1);
				// Dec keeps the keys from its click; the reset chooses Original
				// under it without a warning (a focused segment keeps its stop).
				click(window, mark);
				CHECK(formatIs("original") && ownFormats().isEmpty() && mark->property("rows").toInt() == 0);
				// With everything Original there is nothing to reset.
				click(window, named(content, "watchFormat_original"));
				click(window, more);
				CHECK(!shown(content, "watchResetFormats")->property("enabled").toBool());
				auto* moreMenu = window->findChild<QObject*>(QStringLiteral("watchMoreMenu"));
				QTest::keyClick(window, Qt::Key_Escape);
				settle();
				CHECK(moreMenu && !moreMenu->property("visible").toBool());
			}
			// A window as narrow as a dock gets: nothing in the panel loops its
			// layout (a binding loop is warned about).
			window->setWidth(140);
			settle();
			window->setWidth(480);
			settle();

			// The pace (WL-6): from the first completed cycle a line stands in
			// for the status, shown where a host hides the status; behind, it
			// says the period polling achieves; its tooltip says how a cycle
			// read; times are in words; a stop says itself at once.
			{
				auto* fixture = panel->property("provider").value<QObject*>();
				const auto seconds = [](double value) { return QLocale().toString(value) + QStringLiteral(" s"); };
				const auto pace = [](bool behind, double cycle, double achieved) {
					return QVariantMap{{QStringLiteral("cycles"), 3},           {QStringLiteral("polling"), true},
					                   {QStringLiteral("routine"), true},        {QStringLiteral("values"), 65},
					                   {QStringLiteral("readMany"), 31},         {QStringLiteral("batch"), 1},
					                   {QStringLiteral("fieldRead"), 2},         {QStringLiteral("again"), 0},
					                   {QStringLiteral("lastCycleMs"), cycle},   {QStringLiteral("medianCycleMs"), cycle},
					                   {QStringLiteral("medianPeriodMs"), achieved}, {QStringLiteral("achievedPeriodMs"), achieved},
					                   {QStringLiteral("intervalMs"), 1000},     {QStringLiteral("skippedTicks"), 2},
					                   {QStringLiteral("behind"), behind}};
				};
				panel->setProperty("statusShown", false);
				fixture->setProperty("watchStatus", QStringLiteral("ReadMany complete - 65 values"));
				fixture->setProperty("watchActive", true);
				fixture->setProperty("watchPace", pace(true, 1200, 2000));
				settle();
				CHECK(textOf(content, "watchStatusLabel") ==
				      QStringLiteral("Every 2 s, not 1 s: a cycle of 65 values takes ") + seconds(1.2));
				const auto tip = panel->property("paceTip").toString().split(QLatin1Char('\n'));
				CHECK(tip.size() == 3 &&
				      tip[0] == QStringLiteral("Every 2 s, not 1 s: a cycle due while the last one still runs is skipped.") &&
				      tip[1] == QStringLiteral("65 values in 31 ReadMany, 1 Batch and 2 FieldRead requests; the last cycle took ") +
				                    seconds(1.2) + QLatin1Char('.') &&
				      tip[2].startsWith(QStringLiteral("Continuous reads as soon as a cycle ends")));
				// Within its interval, after the line's two seconds.
				fixture->setProperty("watchPace", pace(false, 412, 1000));
				QTest::qWait(2100);
				settle();
				CHECK(textOf(content, "watchStatusLabel") == QStringLiteral("A cycle of 65 values takes 410 ms"));
				CHECK(panel->property("paceTip").toString().startsWith(QStringLiteral("Every 1 s, as asked.\n")));
				// Times in words.
				const auto words = [panel](double ms) {
					QVariant out;
					QMetaObject::invokeMethod(panel, "durationText", Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, ms));
					return out.toString();
				};
				CHECK(words(9.4) == QStringLiteral("9 ms") && words(412) == QStringLiteral("410 ms") &&
				      words(1004) == seconds(1.004) && words(1240) == seconds(1.2) && words(14200) == QStringLiteral("14 s") &&
				      words(80000) == QStringLiteral("1 min 20 s"));
				// Stopped: the status says itself at once, and the interval's
				// tooltip keeps the last cycle.
				fixture->setProperty("watchActive", false);
				fixture->setProperty("watchStatus", QStringLiteral("Stopped"));
				auto stopped = pace(false, 412, 1000);
				stopped[QStringLiteral("routine")] = false;
				fixture->setProperty("watchPace", stopped);
				settle();
				CHECK(panel->property("paceText").toString().isEmpty() && !named(content, "watchStatusLabel"));
				panel->setProperty("statusShown", true);
				settle();
				CHECK(textOf(content, "watchStatusLabel") == QStringLiteral("Stopped"));
				// A Read once says what it read, and in how long.
				auto once = pace(false, 150, 0);
				once[QStringLiteral("polling")] = false;
				once[QStringLiteral("cycles")] = 1;
				fixture->setProperty("watchPace", once);
				fixture->setProperty("watchStatus", QStringLiteral("ReadMany complete - 65 values"));
				settle();
				CHECK(textOf(content, "watchStatusLabel") == QStringLiteral("65 values read in 150 ms"));
				fixture->setProperty("watchPace", QVariantMap{});
				fixture->setProperty("watchStatus", QString{});
				settle();
			}

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
			CHECK(!named(content, "undoNoticeLink"));
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
			// Edited while polling (WL-5): the live reading beside the name.
			auto* fixture = panel->property("provider").value<QObject*>();
			CHECK(!named(gain, "watchRowNow"));
			fixture->setProperty("watchActive", true);
			settle();
			CHECK(textOf(gain, "watchRowNow") == QStringLiteral("Now 1.25"));
			fixture->setProperty("watchActive", false);
			settle();
			CHECK(!named(gain, "watchRowNow") && gain->property("editing").toBool());
			CHECK(rowAt(list, 4) == gain);
			CHECK(textOf(gain, "watchValueEditor") == QStringLiteral("2"));
			key(window, Qt::Key_Return);
			CHECK(calls.writes == 1 && calls.writeRow == 3 && calls.writeText == QStringLiteral("2"));
			// WL-5: a write the device confirmed reads quietly; one it reads
			// back otherwise stands out.
			fixture->setProperty("watchWriteOutcome", QStringLiteral("Wrote device.Config.Gain = 2 · read back 2."));
			fixture->setProperty("watchWriteKind", QStringLiteral("confirmed"));
			settle();
			auto* outcome = shown(content, "watchWriteOutcome");
			const auto quiet = outcome->property("color").value<QColor>();
			fixture->setProperty("watchWriteKind", QStringLiteral("differs"));
			settle();
			CHECK(outcome->property("color").value<QColor>() != quiet);
			fixture->setProperty("watchWriteKind", QStringLiteral("unconfirmed"));
			settle();
			CHECK(outcome->property("color").value<QColor>() != quiet);
			fixture->setProperty("watchWriteOutcome", QString{});
			fixture->setProperty("watchWriteKind", QString{});
			settle();
			// F2 on a value that can't be written says why: read only; or
			// changes are off, with Allow changes beside it.
			const auto rowNamed = [list](const QString& name) {
				for (int at = 0; at < list->property("count").toInt(); ++at)
					if (auto* row = rowAt(list, at); row && row->property("name").toString() == name)
						return at;
				return -1;
			};
			const auto voltage = rowNamed(QStringLiteral("device.Voltage"));
			CHECK(voltage >= 0);
			list->setProperty("currentIndex", voltage);
			list->forceActiveFocus();
			key(window, Qt::Key_F2);
			CHECK(textOf(content, "watchWriteOutcome") == QStringLiteral("device.Voltage is read only.") &&
			      !named(content, "watchAllowChanges"));
			fixture->setProperty("canWriteWatch", false);
			fixture->setProperty("allowMutations", false);
			settle();
			const auto gainRow = rowNamed(QStringLiteral("device.Config.Gain"));
			CHECK(gainRow >= 0);
			list->setProperty("currentIndex", gainRow);
			list->forceActiveFocus();
			key(window, Qt::Key_F2);
			CHECK(textOf(content, "watchWriteOutcome") ==
			      QStringLiteral("Changes are off. Allow changes to write device.Config.Gain."));
			click(window, shown(content, "watchAllowChanges"));
			CHECK(fixture->property("allowMutations").toBool() && !named(content, "watchAllowChanges") &&
			      !named(content, "watchWriteOutcome"));
			fixture->setProperty("canWriteWatch", true);
			settle();

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
			click(window, named(content, "undoNoticeLink"));
			CHECK(calls.undos == 1 &&
			      removedLine->property("text").toString() == QStringLiteral("Undone: Remove 2 values from the Watchlist"));
			CHECK(textOf(content, "undoNoticeLink") == QStringLiteral("Redo"));
			list->setProperty("currentIndex", 0);
			list->forceActiveFocus();
			key(window, Qt::Key_Delete);
			CHECK(calls.removed == QList<int>({2, 0, 3, 1}));
			// Ctrl+Z undoes while the list has the keys: the router's, one step.
			QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
			settle();
			CHECK(calls.undos == 2);
			list->setProperty("currentIndex", 1);
			list->forceActiveFocus();
			key(window, Qt::Key_Delete);
			CHECK(calls.removed == QList<int>({2}));
			CHECK(removedLine->property("text").toString() == QStringLiteral("Removed device.Voltage"));

			// Several rows (WL-2c). Keys move and select; Shift extends from
			// the anchor, Ctrl moves the cursor alone, Ctrl+Space toggles.
			{
				const auto selectedRows = [&rows] {
					QList<int> out;
					for (const auto& row : rows.selectedRows())
						out << row.toInt();
					return out;
				};
				const auto current = [list] { return list->property("currentIndex").toInt(); };
				const int count = list->property("count").toInt();
				// Names as spec F says them: a member or an element by what it
				// belongs to; x on a node by the values it removes.
				{
					const std::function<QQuickItem*(QQuickItem*, const char*)> anyNamed =
					    [&](QQuickItem* root, const char* name) -> QQuickItem* {
						if (root->objectName() == QLatin1String(name))
							return root;
						for (auto* child : root->childItems())
							if (auto* found = anyNamed(child, name))
								return found;
						return nullptr;
					};
					int members = 0, nodes = 0;
					for (int row = 0; row < count; ++row) {
						auto* item = rowShown(list, row);
						const auto label = item->property("parentLabel").toString();
						if (leaf(list, row) == QStringLiteral("Gain") && ++members)
							CHECK(label == QStringLiteral("Config"));
						if (leaf(list, row) == QStringLiteral("[3]") && ++members)
							CHECK(label == QStringLiteral("Samples"));
						if (leaf(list, row) == QStringLiteral("Config") && ++nodes) {
							auto* remove = anyNamed(item, "removeWatchButton");
							CHECK(remove && remove->property("what").toString() ==
							                    QStringLiteral("Remove the %1 values of device.Config")
							                        .arg(item->property("leaves").toInt()));
						}
					}
					CHECK(members == 2 && nodes == 1);
				}
				list->setProperty("currentIndex", 0);
				list->forceActiveFocus();
				rows.select(0);
				settle();
				key(window, Qt::Key_Down);
				CHECK(current() == 1 && selectedRows() == QList<int>({1}));
				CHECK(rowShown(list, 1)->property("active").toBool() && named(rowShown(list, 1), "watchRowFocusFrame"));
				QTest::keyClick(window, Qt::Key_Down, Qt::ShiftModifier);
				settle();
				CHECK(current() == 2 && selectedRows() == QList<int>({1, 2}));
				QTest::keyClick(window, Qt::Key_Down, Qt::ControlModifier);
				settle();
				CHECK(current() == 3 && selectedRows() == QList<int>({1, 2}));
				CHECK(named(rowShown(list, 3), "watchRowFocusFrame") && !named(rowShown(list, 2), "watchRowFocusFrame"));
				QTest::keyClick(window, Qt::Key_Space, Qt::ControlModifier);
				settle();
				CHECK(selectedRows() == QList<int>({1, 2, 3}));
				// Ctrl+C copies every value of the selection once, in order.
				QGuiApplication::clipboard()->clear();
				QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
				settle();
				CHECK(QGuiApplication::clipboard()->text() == rows.selectedLines(false));
				const int copied = int(rows.selectedSourceRows().size());
				CHECK(textOf(content, "watchNotice") == QStringLiteral("Copied %1 values").arg(copied));
				// Escape: the current row alone.
				key(window, Qt::Key_Escape);
				CHECK(selectedRows() == QList<int>({3}));
				// Without the keys the selection stays, drawn quiet.
				named(content, "watchMoreButton")->forceActiveFocus(Qt::TabFocusReason);
				settle();
				CHECK(rowShown(list, 3)->property("marked").toBool() && !rowShown(list, 3)->property("active").toBool());
				list->forceActiveFocus();
				settle();
				// Ctrl+A, then Delete: one call with every value, said with the count.
				QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
				settle();
				CHECK(rows.selectedCount() == count);
				calls.removed.clear();
				key(window, Qt::Key_Delete);
				CHECK(calls.removed.size() == model.rowCount());
				CHECK(textOf(content, "watchNotice") == QStringLiteral("Removed %1 values").arg(model.rowCount()));
				// After a removal the cursor's row is the one selected.
				CHECK(selectedRows() == QList<int>({current()}));
				// Never asked first (UR-1): a history puts them back, device or
				// not; a host that gave none has no Undo, and is not asked either.
				auto* fixture = panel->property("provider").value<QObject*>();
				const auto history = fixture->property("history");
				fixture->setProperty("history", QVariant::fromValue<QObject*>(nullptr));
				QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
				settle();
				calls.removed.clear();
				key(window, Qt::Key_Delete);
				CHECK(calls.removed.size() == model.rowCount() && !named(content, "undoNoticeLink"));
				CHECK(!window->findChild<QObject*>(QStringLiteral("removeWatchDialog")));
				fixture->setProperty("history", history);
				list->forceActiveFocus();
				settle();
				// Home and End; Shift+Down on the last row moves nothing and
				// leaves the "+" row closed.
				key(window, Qt::Key_Home);
				CHECK(current() == 0 && selectedRows() == QList<int>({0}));
				key(window, Qt::Key_End);
				CHECK(current() == count - 1 && selectedRows() == QList<int>({count - 1}));
				QTest::keyClick(window, Qt::Key_Down, Qt::ShiftModifier);
				settle();
				CHECK(current() == count - 1 && list->hasActiveFocus());
				// The mouse: a click alone, Ctrl+click toggles, Shift+click
				// takes the rows from the anchor; a right-click inside keeps it.
				// On the name, clear of the chevron and the value's own handler.
				const auto at = [&](int row) {
					auto* name = shown(rowShown(list, row), "watchRowLeaf");
					return name->mapToScene(QPointF(name->width() / 2, name->height() / 2)).toPoint();
				};
				QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at(1));
				settle();
				CHECK(selectedRows() == QList<int>({1}) && current() == 1);
				QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, at(3));
				settle();
				CHECK(selectedRows() == QList<int>({1, 3}));
				QTest::mouseClick(window, Qt::LeftButton, Qt::ShiftModifier, at(5));
				settle();
				CHECK(selectedRows() == QList<int>({3, 4, 5}));
				QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, at(4));
				settle();
				CHECK(selectedRows() == QList<int>({3, 4, 5}));
				// The menu says it acts on the selection, and how much.
				auto* menu = window->findChild<QObject*>(QStringLiteral("watchRowMenu"));
				CHECK(menu && menu->property("visible").toBool());
				const int values = int(rows.selectedSourceRows().size());
				CHECK(shown(content, "watchRowCopy")->property("text").toString()
				          .startsWith(QStringLiteral("Copy %1 values").arg(values)));
				CHECK(shown(content, "watchRowRemove")->property("text").toString()
				          .startsWith(QStringLiteral("Remove %1 values").arg(values)));
				CHECK(!shown(content, "watchRowCopyValue")->property("enabled").toBool());
				CHECK(rowShown(list, 3)->property("active").toBool());  // still strong behind its menu
				QTest::keyClick(window, Qt::Key_Escape);
				settle();
				if (menu->property("visible").toBool())
					QTest::keyClick(window, Qt::Key_Escape);
				settle();
				// x is its own row's, whatever is selected.
				list->forceActiveFocus();
				list->setProperty("currentIndex", 4);
				settle();
				calls.removed.clear();
				click(window, shown(rowShown(list, 4), "removeWatchButton"));
				{
					QList<int> own;
					for (const auto& source : rows.sourceRows(4))
						own << source.toInt();
					// Only its row's values went; the cursor's row is the one selected after.
					CHECK(!own.isEmpty() && calls.removed == own && selectedRows() == QList<int>({4}));
				}
				// Escape with the cursor off the selection: the cursor's row alone.
				list->forceActiveFocus();
				rows.select(1);
				list->setProperty("currentIndex", 1);
				settle();
				QTest::keyClick(window, Qt::Key_Down, Qt::ControlModifier);
				settle();
				key(window, Qt::Key_Escape);
				CHECK(selectedRows() == QList<int>({2}));
				// While a value is edited, its keys are the editor's: the
				// selection and the cursor stay.
				int writable = -1;
				for (int row = 0; row < count && writable < 0; ++row)
					if (rowShown(list, row)->property("writable").toBool() && rowShown(list, row)->property("isValue").toBool())
						writable = row;
				CHECK(writable >= 0);
				list->setProperty("currentIndex", writable);
				list->forceActiveFocus();
				settle();
				key(window, Qt::Key_F2);
				CHECK(rowShown(list, writable)->property("editing").toBool() && selectedRows() == QList<int>({writable}));
				QTest::keyClick(window, Qt::Key_Down, Qt::ShiftModifier);
				QTest::keyClick(window, Qt::Key_PageDown);
				QTest::keyClick(window, Qt::Key_End, Qt::ControlModifier);
				settle();
				CHECK(current() == writable && selectedRows() == QList<int>({writable}));
				key(window, Qt::Key_Escape);
				CHECK(!rowShown(list, writable)->property("editing").toBool());
				list->forceActiveFocus();
				rows.select(1);
				settle();
			}

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
			CHECK(named(content, "undoNoticeLink"));
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
			CHECK(addField->property("text").toString().isEmpty() && named(content, "undoNoticeLink"));
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
			// A turn of the wheel, as a stream of real ones arrives: a Flickable
			// drops a turn stamped no later than the last event it saw (it
			// cannot tell a speed), and that may be a clock reading as well as
			// QTest's stamps, depending on how fast the run went - so the turn
			// comes again, stamped well after either, until it lands.
			for (int attempt = 0; attempt < 3 && list->property("contentY").toReal() <= top; ++attempt) {
				QWheelEvent wheel(middle, window->mapToGlobal(middle), QPoint(), QPoint(0, -360), Qt::NoButton,
				                  Qt::NoModifier, Qt::NoScrollPhase, false);
				QTest::lastMouseTimestamp += attempt == 0 ? 16 : 600000;
				wheel.setTimestamp(ulong(QTest::lastMouseTimestamp));
				QCoreApplication::sendEvent(window, &wheel);
				settle();
			}
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
