// The Watchlist's list and tree views, by keyboard and pointer: groups and
// nodes from paths, folding, a node's summary, removing a node's values, the
// view's controls giving way, and an open editor that outlives rows added and
// removed around it. MIT.
#include <Telemetry/UX/WatchRows.hpp>
#include <QtCore/QDir>
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
QQuickItem* rowAt(QQuickItem* root, int row)
{
	if (root->objectName() == QLatin1String("watchlistRow") && root->property("index").toInt() == row)
		return root;
	for (auto* child : root->childItems())
		if (auto* found = rowAt(child, row))
			return found;
	return nullptr;
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
			CHECK(named(content, "watchViewList")->property("checked").toBool());
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
			      named(group, "watchGroupCount")->property("text").toString() == QStringLiteral("3 watched"));
			CHECK(!named(group, "watchValueText") && named(group, "watchRowChevron"));
			CHECK(!named(rowAt(list, 1), "watchRowChevron") && named(rowAt(list, 2), "watchRowChevron"));
			// A member sits one indent deeper than its Field.
			const auto x = [&](int row) {
				auto* text = named(rowAt(list, row), "watchRowLeaf");
				return text->mapToScene(QPointF(0, 0)).x();
			};
			CHECK(x(3) > x(2) && x(2) > x(0) && x(1) == x(2));
			// Inside a Field: no status or rw of its own; the Field has its type.
			CHECK(rowAt(list, 3)->property("accessText").toString().isEmpty());
			CHECK(rowAt(list, 2)->property("accessText").toString() == QStringLiteral("rw"));
			CHECK(rowAt(list, 2)->property("typeText").toString() == QStringLiteral("struct (3)"));
			shot(window, argc, argv, QStringLiteral("watch-tree-480"));

			// The Device tree's keys: Left folds, then steps out; Right opens,
			// then steps in. Folded, a node shows its values in brief.
			list->setProperty("currentIndex", 2);
			list->forceActiveFocus();
			const auto column = panel->property("valueColumn").toDouble();
			key(window, Qt::Key_Left);
			CHECK(list->property("count").toInt() == 7 && !rowAt(list, 2)->property("expanded").toBool());
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
			QTest::keyClick(window, '2');
			settle();
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
			CHECK(named(gain, "watchValueEditor")->property("text").toString() == QStringLiteral("2"));
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
			auto* removedLine = named(content, "watchRemoved");
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
				if (width == 320)
					shot(window, argc, argv, QStringLiteral("watch-tree-320"));
				else
					CHECK(!inBar);
			}
			window->setWidth(480);
			settle();
			CHECK(panel->property("viewInBar").toBool() && !treeItem->property("shown").toBool());

			// Back to the list: the current value stays current.
			list->setProperty("currentIndex", 4);  // Gain
			CHECK(QMetaObject::invokeMethod(panel, "showTree", Q_ARG(QVariant, false)));
			settle();
			CHECK(!rows.tree() && list->property("count").toInt() == 5);
			const int current = list->property("currentIndex").toInt();
			CHECK(current >= 0 && rowAt(list, current)->property("name").toString() == QStringLiteral("device.Config.Gain"));
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
