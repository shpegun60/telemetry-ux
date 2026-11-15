// The Watchlist's rows as shown, list and tree, over a plain item model. MIT.
#include <Telemetry/UX/WatchRows.hpp>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QElapsedTimer>
#include <QtCore/QRandomGenerator>
#include <QtGui/QStandardItemModel>
#include <QtTest/QAbstractItemModelTester>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace ux = telemetry::ux;
using Rows = ux::WatchRows;

namespace {
unsigned checks = 0;
#define CHECK(...)                                                                                 \
	do {                                                                                           \
		++checks;                                                                                  \
		if (!(__VA_ARGS__)) {                                                                      \
			std::fprintf(stderr, "Watch rows check %u line %d: %s\n", checks, __LINE__, #__VA_ARGS__); \
			std::abort();                                                                          \
		}                                                                                          \
	} while (false)

enum SourceRole { Name = Qt::UserRole + 1, Status, Text, Key, PathTypes, Access, LeafStart, Format };

// A Watchlist model as a host might have one: roles by name, no watch keys
// unless asked for.
std::unique_ptr<QStandardItemModel> sourceModel(bool keys = true)
{
	auto model = std::make_unique<QStandardItemModel>();
	QHash<int, QByteArray> names{{Name, "name"}, {Status, "status"}, {Text, "text"},
	                             {PathTypes, "pathTypes"}, {Access, "access"}, {LeafStart, "leafStart"},
	                             {Format, "format"}};
	if (keys)
		names.insert(Key, "watchKey");
	model->setItemRoleNames(names);
	return model;
}

quint64 key(quint32 field, quint32 leaf) { return (quint64{leaf} << 32) | field; }

QStandardItem* item(const QString& path, quint64 watchKey, const QString& text = {},
                    const QStringList& types = {}, const QString& access = QStringLiteral("rw"))
{
	auto* row = new QStandardItem;
	row->setData(path, Name);
	row->setData(QStringLiteral("Ok"), Status);
	row->setData(text, Text);
	row->setData(qulonglong(watchKey), Key);
	row->setData(types, PathTypes);
	row->setData(access, Access);
	return row;
}

void add(QStandardItemModel& model, int at, const QString& path, quint64 watchKey, const QString& text = {},
         const QStringList& types = {})
{
	model.insertRow(at, item(path, watchKey, text, types));
}

QString name(const Rows& rows, int row) { return rows.index(row).data(Rows::NameRole).toString(); }
QVariant role(const Rows& rows, int row, int r) { return rows.index(row).data(r); }
QStringList names(const Rows& rows)
{
	QStringList out;
	for (int row = 0; row < rows.count(); ++row)
		out << name(rows, row);
	return out;
}
QString line(const Rows& rows, int row);
// What each row shows: its path and depth, and for a node its summary.
QStringList shown(const Rows& rows)
{
	QStringList out;
	for (int row = 0; row < rows.count(); ++row)
		out << line(rows, row);
	return out;
}

QString line(const Rows& rows, int row)
{
	return QStringLiteral("%1:%2:%3:%4")
	    .arg(name(rows, row))
	    .arg(role(rows, row, Rows::DepthRole).toInt())
	    .arg(role(rows, row, Rows::SourceRowRole).toInt())
	    .arg(role(rows, row, Rows::TextRole).toString());
}
// A row as a view draws it, its selection too: what the Mirror must keep.
QString told(const Rows& rows, int row)
{
	return line(rows, row) + (role(rows, row, Rows::SelectedRole).toBool() ? QStringLiteral(" *") : QString{});
}
QStringList toldAll(const Rows& rows)
{
	QStringList out;
	for (int row = 0; row < rows.count(); ++row)
		out << told(rows, row);
	return out;
}

// What a view would show if it read a row only when told: every insert,
// removal, move, change and reset applied as it is announced.
struct Mirror {
	QStringList lines;
	explicit Mirror(Rows& rows)
	{
		const auto all = [this, &rows] {
			lines.clear();
			for (int row = 0; row < rows.count(); ++row)
				lines << told(rows, row);
		};
		all();
		QObject::connect(&rows, &QAbstractItemModel::modelReset, &rows, all);
		QObject::connect(&rows, &QAbstractItemModel::rowsInserted, &rows,
		                 [this, &rows](const QModelIndex&, int first, int last) {
			                 for (int row = first; row <= last; ++row)
				                 lines.insert(row, told(rows, row));
		                 });
		QObject::connect(&rows, &QAbstractItemModel::rowsRemoved, &rows,
		                 [this](const QModelIndex&, int first, int last) { lines.remove(first, last - first + 1); });
		QObject::connect(&rows, &QAbstractItemModel::rowsMoved, &rows,
		                 [this](const QModelIndex&, int first, int last, const QModelIndex&, int dest) {
			                 const auto block = lines.mid(first, last - first + 1);
			                 lines.remove(first, block.size());
			                 const auto at = dest > first ? dest - block.size() : dest;
			                 for (qsizetype i = 0; i < block.size(); ++i)
				                 lines.insert(at + i, block.at(i));
		                 });
		QObject::connect(&rows, &QAbstractItemModel::dataChanged, &rows,
		                 [this, &rows](const QModelIndex& top, const QModelIndex& bottom) {
			                 for (int row = top.row(); row <= bottom.row(); ++row)
				                 lines[row] = told(rows, row);
		                 });
	}
};

void steps()
{
	auto at = Rows::steps(QStringLiteral("device.Calib.Phase[2].Offset"));
	CHECK(at.size() == 5);
	CHECK(at[2].start == 13 && at[2].end == 18);
	CHECK(at[3].start == 18 && at[3].end == 21);
	CHECK(at[4].start == 22 && at[4].end == 28);
	at = Rows::steps(QStringLiteral("data.Samples[3]"));
	CHECK(at.size() == 3 && at[2].start == 12 && at[2].end == 15);
	at = Rows::steps(QStringLiteral("Catalog / Name"));
	CHECK(at.size() == 2 && at[0].end == 7 && at[1].start == 10);
	CHECK(Rows::steps(QStringLiteral("Field10")).size() == 1);
	CHECK(Rows::steps(QString{}).isEmpty());
	CHECK(Rows::steps(QStringLiteral("data.Samples[0 … 255]")).size() == 3);

	CHECK(Rows::pathLess(QStringLiteral("a.S[9]"), QStringLiteral("a.S[10]")));
	CHECK(!Rows::pathLess(QStringLiteral("a.S[10]"), QStringLiteral("a.S[9]")));
	CHECK(Rows::pathLess(QStringLiteral("a.S[09]"), QStringLiteral("a.S[10]")));
	CHECK(Rows::pathLess(QStringLiteral("Device.a"), QStringLiteral("device.b")));
	CHECK(Rows::pathLess(QStringLiteral("device.a"), QStringLiteral("device.ab")));
	CHECK(!Rows::pathLess(QStringLiteral("x"), QStringLiteral("x")));
}

void listView()
{
	auto source = sourceModel();
	add(*source, 0, QStringLiteral("device.Voltage"), key(3, 0), QStringLiteral("230"));
	add(*source, 1, QStringLiteral("data.Samples[10]"), key(7, 11));
	add(*source, 2, QStringLiteral("data.Samples[9]"), key(7, 10));
	source->item(0)->setData(7, LeafStart);
	Rows rows;
	QAbstractItemModelTester tester(&rows, QAbstractItemModelTester::FailureReportingMode::Fatal);
	rows.setSource(source.get());
	CHECK(rows.count() == 3);
	CHECK(names(rows) == QStringList({QStringLiteral("device.Voltage"), QStringLiteral("data.Samples[10]"),
	                                  QStringLiteral("data.Samples[9]")}));
	for (int row = 0; row < 3; ++row) {
		CHECK(role(rows, row, Rows::SourceRowRole).toInt() == row);
		CHECK(role(rows, row, Rows::DepthRole).toInt() == 0);
		CHECK(role(rows, row, Rows::NodeRole).toString() == QStringLiteral("value"));
		CHECK(!role(rows, row, Rows::NestedRole).toBool());
	}
	CHECK(role(rows, 0, Rows::TextRole).toString() == QStringLiteral("230"));
	CHECK(role(rows, 0, Rows::AccessRole).toString() == QStringLiteral("rw"));
	// The source's own leafStart, where it has one; else the last step.
	CHECK(role(rows, 0, Rows::LeafStartRole).toInt() == 7);
	CHECK(role(rows, 1, Rows::LeafStartRole).toInt() == 12);
	// A role the source lacks reads as nothing; a text role as "".
	CHECK(!role(rows, 0, Rows::WriteScopeRole).isValid());
	CHECK(role(rows, 0, Rows::NameRole).typeId() == QMetaType::QString);

	QSignalSpy resets(&rows, &QAbstractItemModel::modelReset);
	QSignalSpy moved(&rows, &QAbstractItemModel::rowsMoved);
	rows.setSorted(true);
	// The rows move into place, without a reset: a row being edited keeps
	// its delegate.
	CHECK(resets.isEmpty() && !moved.isEmpty());
	CHECK(names(rows) == QStringList({QStringLiteral("data.Samples[9]"), QStringLiteral("data.Samples[10]"),
	                                  QStringLiteral("device.Voltage")}));
	CHECK(role(rows, 0, Rows::SourceRowRole).toInt() == 2);
	// Added while sorted: in its place, without a reset.
	QSignalSpy inserted(&rows, &QAbstractItemModel::rowsInserted);
	add(*source, 3, QStringLiteral("data.Count"), key(6, 0));
	CHECK(resets.isEmpty() && inserted.count() == 1);
	CHECK(name(rows, 0) == QStringLiteral("data.Count"));
	// A value read: its row, nothing else.
	QSignalSpy changed(&rows, &QAbstractItemModel::dataChanged);
	source->item(0)->setData(QStringLiteral("231"), Text);
	CHECK(changed.count() == 1);
	CHECK(changed[0][0].toModelIndex().row() == 3 && changed[0][1].toModelIndex().row() == 3);
	CHECK(changed[0][2].value<QList<int>>() == QList<int>({Rows::TextRole}));
	rows.setSorted(false);
	// Back to the order added, by moves too.
	CHECK(resets.isEmpty() && names(rows) == QStringList({QStringLiteral("device.Voltage"),
	                                                      QStringLiteral("data.Samples[10]"),
	                                                      QStringLiteral("data.Samples[9]"),
	                                                      QStringLiteral("data.Count")}));
}

// What a nested row belongs to, for a screen reader (WL-2c): its parent's
// own step, an element named with its array.
void parentLabels()
{
	auto source = sourceModel();
	add(*source, 0, QStringLiteral("device.Calib.Phase[2].Offset"), key(1, 1));
	add(*source, 1, QStringLiteral("data.M[3][2]"), key(2, 7));
	add(*source, 2, QStringLiteral("device.Config.Gain"), key(3, 1));
	Rows rows;
	rows.setTree(true);
	rows.setSource(source.get());
	const auto labelOf = [&rows](const char* path) {
		for (int row = 0; row < rows.count(); ++row)
			if (rows.index(row).data(Qt::DisplayRole).toString() == QLatin1String(path))
				return role(rows, row, Rows::ParentLabelRole).toString();
		return QStringLiteral("?");
	};
	CHECK(labelOf("device.Calib.Phase[2].Offset") == QStringLiteral("Phase[2]"));
	CHECK(labelOf("device.Calib.Phase[2]") == QStringLiteral("Phase"));
	CHECK(labelOf("device.Calib.Phase") == QStringLiteral("Calib"));
	CHECK(labelOf("data.M[3][2]") == QStringLiteral("M[3]"));
	CHECK(labelOf("device.Config.Gain") == QStringLiteral("Config"));
	CHECK(labelOf("device.Calib").isEmpty() && labelOf("device").isEmpty());
	rows.setTree(false);
	for (int row = 0; row < rows.count(); ++row)
		CHECK(role(rows, row, Rows::ParentLabelRole).toString().isEmpty());
}

QStringList configTypesFor() { return {QStringLiteral("struct (3)"), QStringLiteral("f32")}; }

// device.Voltage (Field 3), device.Config.Gain and .Enabled (Field 5),
// data.Samples[3] and [7] (Field 7), added out of order.
std::unique_ptr<QStandardItemModel> treeSource()
{
	auto source = sourceModel();
	const QStringList configTypes{QStringLiteral("struct (3)"), QStringLiteral("f32")};
	add(*source, 0, QStringLiteral("data.Samples[7]"), key(7, 8), QStringLiteral("498"),
	    {QStringLiteral("u16[64]"), QStringLiteral("u16")});
	add(*source, 1, QStringLiteral("device.Config.Enabled"), key(5, 2), QStringLiteral("true"),
	    {QStringLiteral("struct (3)"), QStringLiteral("bool")});
	add(*source, 2, QStringLiteral("device.Voltage"), key(3, 0), QStringLiteral("230"), {QStringLiteral("u16")});
	add(*source, 3, QStringLiteral("device.Config.Gain"), key(5, 1), QStringLiteral("1.5"), configTypes);
	add(*source, 4, QStringLiteral("data.Samples[3]"), key(7, 4), QStringLiteral("512"),
	    {QStringLiteral("u16[64]"), QStringLiteral("u16")});
	return source;
}

void treeView()
{
	auto source = treeSource();
	Rows rows;
	QAbstractItemModelTester tester(&rows, QAbstractItemModelTester::FailureReportingMode::Fatal);
	rows.setSource(source.get());
	rows.setTree(true);
	// Descriptor order: by Field, then the value's place in it.
	CHECK(names(rows) == QStringList({QStringLiteral("device"), QStringLiteral("device.Voltage"),
	                                  QStringLiteral("device.Config"), QStringLiteral("device.Config.Gain"),
	                                  QStringLiteral("device.Config.Enabled"), QStringLiteral("data"),
	                                  QStringLiteral("data.Samples"), QStringLiteral("data.Samples[3]"),
	                                  QStringLiteral("data.Samples[7]")}));
	const QStringList kinds{QStringLiteral("group"), QStringLiteral("value"), QStringLiteral("struct"),
	                        QStringLiteral("value"), QStringLiteral("value"), QStringLiteral("group"),
	                        QStringLiteral("array"), QStringLiteral("value"), QStringLiteral("value")};
	const QList<int> depths{0, 1, 1, 2, 2, 0, 1, 2, 2};
	const QList<int> leaves{3, 1, 2, 1, 1, 2, 2, 1, 1};
	for (int row = 0; row < rows.count(); ++row) {
		CHECK(role(rows, row, Rows::NodeRole).toString() == kinds[row]);
		CHECK(role(rows, row, Rows::DepthRole).toInt() == depths[row]);
		CHECK(role(rows, row, Rows::LeavesRole).toInt() == leaves[row]);
		CHECK(role(rows, row, Rows::NestedRole).toBool() == (depths[row] >= 2));
		CHECK(role(rows, row, Rows::BranchRole).toBool() == (kinds[row] != QStringLiteral("value")));
		CHECK(role(rows, row, Rows::ExpandedRole).toBool() == (kinds[row] != QStringLiteral("value")));
	}
	// The tree shows each row's own step.
	CHECK(role(rows, 3, Rows::LeafStartRole).toInt() == 14);
	CHECK(role(rows, 7, Rows::LeafStartRole).toInt() == 12);
	CHECK(role(rows, 0, Rows::LeafStartRole).toInt() == 0);
	CHECK(role(rows, 3, Rows::SourceRowRole).toInt() == 3);
	CHECK(role(rows, 2, Rows::SourceRowRole).toInt() == -1);
	// A Field above values: its type, access and the shared read's status.
	CHECK(role(rows, 2, Rows::TypeTextRole).toString() == QStringLiteral("struct (3)"));
	CHECK(role(rows, 6, Rows::TypeTextRole).toString() == QStringLiteral("u16[64]"));
	CHECK(role(rows, 2, Rows::AccessRole).toString() == QStringLiteral("rw"));
	CHECK(role(rows, 2, Rows::StatusRole).toString() == QStringLiteral("Ok"));
	CHECK(role(rows, 0, Rows::StatusRole).toString().isEmpty());
	CHECK(role(rows, 0, Rows::TypeTextRole).toString().isEmpty());
	CHECK(role(rows, 2, Rows::WritableRole).toBool() == false);
	// Open, a node shows no value; its values do.
	CHECK(role(rows, 2, Rows::TextRole).toString().isEmpty());
	CHECK(rows.parentRow(3) == 2 && rows.parentRow(2) == 0 && rows.parentRow(0) == -1);
	CHECK(rows.sourceRows(0) == QVariantList({2, 3, 1}));
	CHECK(rows.sourceRows(5) == QVariantList({4, 0}));
	// A node's format is the one its values share, "mixed" when they differ;
	// the node is told when one of them changes.
	{
		CHECK(role(rows, 2, Rows::FormatRole).toString().isEmpty());
		QSignalSpy told(&rows, &QAbstractItemModel::dataChanged);
		source->item(3)->setData(QStringLiteral("hex"), Format);  // Gain
		QTest::qWait(1);
		QCoreApplication::processEvents();
		CHECK(role(rows, 2, Rows::FormatRole).toString() == QStringLiteral("mixed"));
		CHECK(role(rows, 0, Rows::FormatRole).toString() == QStringLiteral("mixed"));
		bool configTold = false;
		for (const auto& signal : told)
			configTold = configTold || (signal[0].toModelIndex().row() <= 2 && signal[1].toModelIndex().row() >= 2 &&
			                            signal[2].value<QList<int>>().contains(Rows::FormatRole));
		CHECK(configTold);
		source->item(1)->setData(QStringLiteral("hex"), Format);  // Enabled
		CHECK(role(rows, 2, Rows::FormatRole).toString() == QStringLiteral("hex"));
		CHECK(role(rows, 3, Rows::FormatRole).toString() == QStringLiteral("hex"));
		source->item(3)->setData(QString{}, Format);
		source->item(1)->setData(QString{}, Format);
		CHECK(role(rows, 2, Rows::FormatRole).toString().isEmpty());
	}

	// Folding removes the rows under it, row by row, and the folded node
	// shows its values in brief.
	QSignalSpy resets(&rows, &QAbstractItemModel::modelReset);
	QSignalSpy removed(&rows, &QAbstractItemModel::rowsRemoved);
	QSignalSpy inserted(&rows, &QAbstractItemModel::rowsInserted);
	QSignalSpy folds(&rows, &Rows::foldedChanged);
	CHECK(rows.setExpanded(2, false));
	CHECK(!rows.setExpanded(1, false));
	CHECK(removed.count() == 1 && resets.isEmpty() && folds.count() == 1);
	CHECK(removed[0][1].toInt() == 3 && removed[0][2].toInt() == 4);
	CHECK(rows.count() == 7);
	CHECK(role(rows, 2, Rows::TextRole).toString() == QStringLiteral("{Gain 1.5, Enabled true}"));
	CHECK(!role(rows, 2, Rows::ExpandedRole).toBool());
	CHECK(rows.folded() == QStringList({QStringLiteral("device.Config")}));
	CHECK(rows.setExpanded(3, false));
	CHECK(role(rows, 3, Rows::TextRole).toString().isEmpty());  // a group gives its count instead
	CHECK(rows.count() == 4);

	// A value read under a folded node: the node is told once, after.
	QSignalSpy changed(&rows, &QAbstractItemModel::dataChanged);
	source->item(3)->setData(QStringLiteral("2"), Text);
	source->item(1)->setData(QStringLiteral("false"), Text);
	CHECK(changed.isEmpty());
	QTest::qWait(1);
	QCoreApplication::processEvents();
	CHECK(changed.count() == 1);
	CHECK(changed[0][0].toModelIndex().row() == 2);
	CHECK(role(rows, 2, Rows::TextRole).toString() == QStringLiteral("{Gain 2, Enabled false}"));

	// Added under a folded node: the node opens, only the new rows come in.
	inserted.clear();
	removed.clear();
	add(*source, 5, QStringLiteral("device.Config.Mode"), key(5, 3), QStringLiteral("High"),
	    {QStringLiteral("struct (3)"), QStringLiteral("enum High, Low")});
	CHECK(resets.isEmpty() && removed.isEmpty());
	CHECK(rows.folded() == QStringList({QStringLiteral("data")}));
	CHECK(names(rows) == QStringList({QStringLiteral("device"), QStringLiteral("device.Voltage"),
	                                  QStringLiteral("device.Config"), QStringLiteral("device.Config.Gain"),
	                                  QStringLiteral("device.Config.Enabled"), QStringLiteral("device.Config.Mode"),
	                                  QStringLiteral("data")}));
	CHECK(inserted.count() == 1 && inserted[0][1].toInt() == 3 && inserted[0][2].toInt() == 5);

	// Every value of a node removed: the node goes with them.
	removed.clear();
	source->removeRows(1, 1);  // Enabled
	source->removeRows(2, 1);  // Gain (now source row 2)
	source->removeRows(3, 1);  // Mode (now source row 3)
	CHECK(resets.isEmpty());
	CHECK(names(rows) == QStringList({QStringLiteral("device"), QStringLiteral("device.Voltage"),
	                                  QStringLiteral("data")}));
	CHECK(role(rows, 0, Rows::LeavesRole).toInt() == 1);
	// The fold of a node that went stays for when it comes back.
	rows.setFolded({QStringLiteral("device.Config")});
	add(*source, 0, QStringLiteral("device.Config.Gain"), key(5, 1), QStringLiteral("1"), configTypesFor());
	CHECK(rows.folded().isEmpty());  // added: shown
	rows.collapseAll();
	CHECK(names(rows) == QStringList({QStringLiteral("device"), QStringLiteral("data")}));
	CHECK(rows.revealPath(QStringLiteral("device.Config.Gain")) == 3);
	CHECK(rows.folded() == QStringList({QStringLiteral("data"), QStringLiteral("data.Samples")}));
	CHECK(rows.reveal(3) == 6);  // data.Samples[3], source row 3
	CHECK(rows.folded().isEmpty());
	rows.collapseAll();
	rows.expandAll();
	CHECK(rows.count() == 8);
	CHECK(rows.revealPath(QStringLiteral("nothing.here")) == -1);
	CHECK(rows.reveal(99) == -1);
}

// The Static provider's names and no watch keys: order as added.
void staticNames()
{
	auto source = sourceModel(false);
	add(*source, 0, QStringLiteral("measure / Voltage"), 0);
	add(*source, 1, QStringLiteral("config / Gain"), 0);
	add(*source, 2, QStringLiteral("measure / Current"), 0);
	Rows rows;
	QAbstractItemModelTester tester(&rows, QAbstractItemModelTester::FailureReportingMode::Fatal);
	rows.setTree(true);
	rows.setSource(source.get());
	CHECK(names(rows) == QStringList({QStringLiteral("measure"), QStringLiteral("measure / Voltage"),
	                                  QStringLiteral("measure / Current"), QStringLiteral("config"),
	                                  QStringLiteral("config / Gain")}));
	CHECK(role(rows, 1, Rows::LeafStartRole).toInt() == 10);
	CHECK(!role(rows, 1, Rows::NestedRole).toBool());
	QSignalSpy resets(&rows, &QAbstractItemModel::modelReset);
	QSignalSpy removed(&rows, &QAbstractItemModel::rowsRemoved);
	source->removeRows(1, 1);
	// The value and its group, in one removal.
	CHECK(resets.isEmpty() && removed.count() == 1);
	CHECK(removed[0][1].toInt() == 3 && removed[0][2].toInt() == 4);
	CHECK(names(rows) == QStringList({QStringLiteral("measure"), QStringLiteral("measure / Voltage"),
	                                  QStringLiteral("measure / Current")}));
	// A group's place follows its first value: removing it moves the group,
	// row by row, never resetting.
	add(*source, 1, QStringLiteral("config / Gain"), 0);
	CHECK(names(rows).at(3) == QStringLiteral("config"));
	QSignalSpy moved(&rows, &QAbstractItemModel::rowsMoved);
	source->removeRows(0, 1);  // measure / Voltage
	CHECK(resets.isEmpty() && moved.count() == 1);
	CHECK(names(rows) == QStringList({QStringLiteral("config"), QStringLiteral("config / Gain"),
	                                  QStringLiteral("measure"), QStringLiteral("measure / Current")}));
	{
		Rows fresh;
		fresh.setTree(true);
		fresh.setSource(source.get());
		CHECK(shown(rows) == shown(fresh));
	}
	CHECK(rows.index(0).data(Qt::DisplayRole).toString() == name(rows, 0));
	// The source gone: nothing shown.
	source.reset();
	CHECK(rows.count() == 0 && rows.source() == nullptr);
}

QList<int> ints(const QVariantList& list)
{
	QList<int> out;
	for (const auto& value : list)
		out << value.toInt();
	return out;
}

// The selection (WL-2c): kept by path through readings, rows added and
// removed, folds and a switch of view; a node is every value under it.
void selection()
{
	auto source = sourceModel();
	add(*source, 0, QStringLiteral("device.Voltage"), key(1, 0), QStringLiteral("230"));
	add(*source, 1, QStringLiteral("device.Config.Gain"), key(2, 1), QStringLiteral("1.5"));
	add(*source, 2, QStringLiteral("device.Config.Enabled"), key(2, 2), QStringLiteral("true"));
	add(*source, 3, QStringLiteral("data.Count"), key(3, 0), QStringLiteral("7"));
	Rows rows;
	QAbstractItemModelTester tester(&rows, QAbstractItemModelTester::FailureReportingMode::Fatal);
	rows.setSource(source.get());
	QSignalSpy changed(&rows, &Rows::selectionChanged);
	QSignalSpy data(&rows, &QAbstractItemModel::dataChanged);
	// A click: the row alone, the anchor; Ctrl+click toggles.
	rows.select(1);
	CHECK(rows.selectedCount() == 1 && rows.isSelected(1) && role(rows, 1, Rows::SelectedRole).toBool());
	CHECK(rows.anchorRow() == 1 && changed.count() == 1);
	CHECK(data.count() == 1 && data[0][0].toModelIndex().row() == 1 && data[0][1].toModelIndex().row() == 1 &&
	      data[0][2].value<QList<int>>() == QList<int>({Rows::SelectedRole}));
	rows.select(3, true);
	CHECK(ints(rows.selectedRows()) == QList<int>({1, 3}) && rows.anchorRow() == 3);
	rows.select(3, true);
	CHECK(ints(rows.selectedRows()) == QList<int>({1}));
	// Shift+click: from the anchor, alone or added to what is selected.
	rows.select(1);
	rows.selectRange(3);
	CHECK(ints(rows.selectedRows()) == QList<int>({1, 2, 3}) && rows.anchorRow() == 1);
	rows.selectRange(0);
	CHECK(ints(rows.selectedRows()) == QList<int>({0, 1}));
	rows.select(3, true);
	rows.selectRange(2, true);
	CHECK(ints(rows.selectedRows()) == QList<int>({0, 1, 2, 3}));
	CHECK(rows.selectedLines(true) ==
	      QStringLiteral("device.Voltage\ndevice.Config.Gain\ndevice.Config.Enabled\ndata.Count"));
	rows.clearSelection();
	CHECK(rows.selectedCount() == 0 && rows.selectedSourceRows().isEmpty());
	// A reading moves nothing; a row added before keeps the selection on its row.
	rows.select(2);
	source->item(2)->setData(QStringLiteral("false"), Text);
	add(*source, 0, QStringLiteral("data.First"), key(4, 0));
	CHECK(ints(rows.selectedRows()) == QList<int>({3}) && name(rows, 3) == QStringLiteral("device.Config.Enabled"));
	// The row removed leaves the selection.
	changed.clear();
	source->removeRow(3);
	CHECK(rows.selectedCount() == 0 && changed.count() == 1);
	add(*source, 3, QStringLiteral("device.Config.Enabled"), key(2, 2), QStringLiteral("true"));
	// In the tree a node is every value under it, each once, in the order shown.
	rows.setTree(true);
	CHECK(names(rows) == QStringList({QStringLiteral("device"), QStringLiteral("device.Voltage"),
	                                  QStringLiteral("device.Config"), QStringLiteral("device.Config.Gain"),
	                                  QStringLiteral("device.Config.Enabled"), QStringLiteral("data"),
	                                  QStringLiteral("data.Count"), QStringLiteral("data.First")}));
	rows.select(2);             // device.Config
	rows.select(3, true);       // and its Gain: still once
	rows.select(6, true);       // data.Count
	CHECK(ints(rows.selectedSourceRows()) == QList<int>({2, 3, 4}));
	CHECK(rows.selectedLines(false) ==
	      QStringLiteral("device.Config.Gain = 1.5\ndevice.Config.Enabled = true\ndata.Count = 7"));
	// A fold hides what it covers: those rows leave; folding everything
	// selected away selects the node folded.
	rows.setExpanded(2, false);
	CHECK(ints(rows.selectedRows()) == QList<int>({2, 4}) && name(rows, 4) == QStringLiteral("data.Count"));
	rows.select(4);  // device.Config folded: row 3 is data, row 4 its Count
	rows.setExpanded(3, false);
	CHECK(ints(rows.selectedRows()) == QList<int>({3}) && name(rows, 3) == QStringLiteral("data"));
	rows.setExpanded(3, true);
	rows.setExpanded(2, true);
	// To the list: a node is its values; back to the tree: the values, the
	// nodes above them opened.
	rows.select(2);  // device.Config
	rows.setTree(false);
	CHECK(rows.selectedLines(true) == QStringLiteral("device.Config.Gain\ndevice.Config.Enabled"));
	rows.setTree(true);
	rows.setExpanded(2, false);  // device.Config folded: its values leave
	CHECK(rows.selectedCount() == 1 && name(rows, 2) == QStringLiteral("device.Config") && rows.isSelected(2));
	rows.setTree(false);
	rows.selectRange(0);   // no anchor in the list: data.First alone
	rows.select(3, true);  // and device.Config.Enabled
	rows.setTree(true);    // device.Config opened again to show it
	CHECK(rows.folded().isEmpty() &&
	      rows.selectedLines(true) == QStringLiteral("device.Config.Enabled\ndata.First"));
	// Folding everything over what is selected selects the nodes folded,
	// whichever way the fold comes (here a project's folds).
	rows.select(3);  // device.Config.Gain
	rows.select(7, true);  // data.First
	rows.setFolded({QStringLiteral("device.Config"), QStringLiteral("data")});
	CHECK(rows.selectedLines(true) == QStringLiteral("device.Config.Gain\ndevice.Config.Enabled\ndata.Count\ndata.First"));
	CHECK(rows.selectedCount() == 2);
	rows.expandAll();
	// A selection of every value kept to the list and back keeps the folds
	// rather than opening every node; a few values open theirs.
	rows.collapseAll();
	rows.setTree(false);
	rows.selectAll();
	rows.setTree(true);
	CHECK(!rows.folded().isEmpty() || rows.selectedCount() <= 64);
	rows.expandAll();
	// Everything shown; the selection survives a sort switch.
	rows.selectAll();
	CHECK(rows.selectedCount() == rows.count() && rows.selectedSourceRows().size() == source->rowCount());
	rows.setTree(false);
	rows.setSorted(true);
	CHECK(rows.selectedCount() == rows.count());
}

// Many source changes in one call (WL-2c): a scattered removal comes as a
// signal for every run of rows; held, the rows follow once, to the same end.
void heldChanges()
{
	for (const bool tree : {false, true}) {
		qint64 took[2] = {0, 0};
		for (const bool held : {false, true}) {
			auto source = sourceModel();
			for (int at = 0; at < 1024; ++at)
				add(*source, at, QStringLiteral("g%1.F%2.V%3").arg(at % 8).arg(at / 64).arg(at), key(at / 64 + 1, at),
				    QString::number(at));
			Rows rows;
			rows.setTree(tree);
			rows.setSource(source.get());
			Mirror mirror(rows);
			QSignalSpy data(&rows, &QAbstractItemModel::dataChanged);
			QSignalSpy resets(&rows, &QAbstractItemModel::modelReset);
			rows.selectAll();
			data.clear();
			QElapsedTimer timer;
			timer.start();
			{
				std::unique_ptr<Rows::Changes> hold;
				if (held)
					hold = std::make_unique<Rows::Changes>(rows);
				// Every other row, last first: 512 removals, as removeIds sends them.
				for (int at = 1023; at > 0; at -= 2)
					source->removeRow(at);
				// A held format comes out as every role of every row.
				source->item(0)->setData(QStringLiteral("hex"), Format);
				if (held)
					CHECK(data.isEmpty() && rows.count() > source->rowCount());
			}
			took[held] = timer.elapsed();
			QCoreApplication::processEvents();
			Rows fresh;
			fresh.setTree(tree);
			fresh.setSource(source.get());
			CHECK(shown(rows) == shown(fresh) && mirror.lines == toldAll(rows) && resets.isEmpty());
			CHECK(rows.selectedSourceRows().size() == 512);
			if (held) {
				// One refresh: one change of the rows that stayed, then the held format.
				int whole = 0;
				for (const auto& signal : data)
					whole += signal[0].toModelIndex().row() == 0 && signal[1].toModelIndex().row() == rows.count() - 1;
				CHECK(whole == 2 && data.last()[2].value<QList<int>>().isEmpty());
			}
		}
		std::printf("Watch rows: 512 scattered removals of 1024 rows (%s): %lld ms one by one, %lld ms held\n",
		            tree ? "tree" : "list", static_cast<long long>(took[0]), static_cast<long long>(took[1]));
		// Held is one rebuild for 512: far cheaper whenever the cost shows at all.
		if (took[0] >= 40)
			CHECK(took[1] * 4 < took[0]);
	}
	// A held reading that renames a row (a reconnect rebinds them): the rows
	// are rebuilt at the end, not only told.
	{
		auto source = sourceModel();
		add(*source, 0, QStringLiteral("a.X"), key(1, 0));
		add(*source, 1, QStringLiteral("b.Y"), key(2, 0));
		Rows rows;
		rows.setTree(true);
		rows.setSource(source.get());
		rows.select(1);  // a.X
		{
			Rows::Changes hold{rows};
			source->item(0)->setData(QStringLiteral("c.X"), Name);
			CHECK(rows.selectedRows().isEmpty() && rows.sourceRows(0).isEmpty());  // held: answers nothing
		}
		Rows fresh;
		fresh.setTree(true);
		fresh.setSource(source.get());
		CHECK(shown(rows) == shown(fresh));
	}
	// Nested pairs: the rows follow at the outer end only.
	auto source = sourceModel();
	add(*source, 0, QStringLiteral("a.X"), key(1, 0));
	add(*source, 1, QStringLiteral("a.Y"), key(2, 0));
	Rows rows;
	rows.setSource(source.get());
	rows.beginChanges();
	rows.beginChanges();
	source->removeRow(0);
	rows.endChanges();
	CHECK(rows.count() == 2);
	rows.endChanges();
	CHECK(rows.count() == 1 && name(rows, 0) == QStringLiteral("a.Y"));
}

// Any sequence of adds, removals, reads, folds and switches leaves the rows
// as a model made afresh shows them, without a reset unless the switch asks.
void randomSequences()
{
	QStringList paths;
	QList<quint64> keys;
	for (quint32 field = 1; field <= 4; ++field) {
		// Interleaved: a group's place moves with what it holds.
		paths << QStringLiteral("g%1.Whole%2").arg(field % 2).arg(field);
		keys << key(field, 0);
	}
	for (quint32 leaf = 1; leaf <= 6; ++leaf) {
		paths << QStringLiteral("g1.Table.Row[%1].Value").arg(leaf);
		keys << key(9, leaf);
		paths << QStringLiteral("g2.Config.M%1").arg(leaf);
		keys << key(11, leaf);
	}
	const auto seed = static_cast<quint32>(QDateTime::currentMSecsSinceEpoch());
	std::printf("Watch rows random seed=%u\n", seed);
	QRandomGenerator generator(seed);
	auto* random = &generator;
	for (int run = 0; run < 40; ++run) {
		auto source = sourceModel();
		Rows rows;
		QAbstractItemModelTester tester(&rows, QAbstractItemModelTester::FailureReportingMode::Fatal);
		rows.setSource(source.get());
		rows.setTree(run % 2 == 0);
		rows.setSorted(run % 3 == 0);
		QSignalSpy resets(&rows, &QAbstractItemModel::modelReset);
		Mirror mirror(rows);
		for (int step = 0; step < 60; ++step) {
			const auto action = random->bounded(10);
			if (action < 4) {
				const auto pick = random->bounded(paths.size());
				if (source->match(source->index(0, 0), Name, paths[pick], 1, Qt::MatchExactly).isEmpty())
					add(*source, random->bounded(source->rowCount() + 1), paths[pick], keys[pick],
					    QString::number(step));
			} else if (action < 6 && source->rowCount() > 0) {
				const auto at = random->bounded(source->rowCount());
				source->removeRows(at, 1 + random->bounded(std::min(3, source->rowCount() - at)));
			} else if (action < 7 && source->rowCount() > 0) {
				source->item(random->bounded(source->rowCount()))->setData(QString::number(step), Text);
			} else if (action < 9 && rows.count() > 0) {
				const auto row = random->bounded(rows.count());
				rows.setExpanded(row, !role(rows, row, Rows::ExpandedRole).toBool());
			} else if (rows.count() > 0) {
				rows.collapseAll();
			}
			// Now and then the list's sort switched: the rows move, what is
			// selected and the anchor stay.
			if (random->bounded(8) == 0) {
				const auto asSet = [](const QVariantList& list) {
					QSet<int> out;
					for (const auto& value : list)
						out.insert(value.toInt());
					return out;
				};
				const auto before = asSet(rows.selectedSourceRows());
				const auto anchor = rows.anchorRow() >= 0 ? name(rows, rows.anchorRow()) : QString{};
				rows.setSorted(!rows.sorted());
				CHECK(asSet(rows.selectedSourceRows()) == before);
				CHECK(anchor.isEmpty() || (rows.anchorRow() >= 0 && name(rows, rows.anchorRow()) == anchor));
			}
			// A click, Ctrl+click, Shift+click, Ctrl+Shift+click or Ctrl+A.
			if (rows.count() > 0) {
				const auto row = random->bounded(rows.count());
				switch (random->bounded(7)) {
					case 0: rows.select(row); break;
					case 1: rows.select(row, true); break;
					case 2: rows.selectRange(row); break;
					case 3: rows.selectRange(row, true); break;
					case 4: rows.selectAll(); break;
					case 5: if (step % 7 == 0) rows.clearSelection(); break;
					default: break;
				}
			}
			QCoreApplication::processEvents();
			// The selection is of rows shown, each says so, and what it
			// acts on is every value at and under them, once, in order.
			{
				QList<int> brute;
				QVector<bool> seen(source->rowCount(), false);
				int selectedShown = 0;
				for (int row = 0; row < rows.count(); ++row) {
					CHECK(rows.isSelected(row) == role(rows, row, Rows::SelectedRole).toBool());
					if (!rows.isSelected(row))
						continue;
					++selectedShown;
					for (const auto& at : rows.sourceRows(row))
						if (!seen[at.toInt()]) {
							seen[at.toInt()] = true;
							brute << at.toInt();
						}
				}
				CHECK(selectedShown == rows.selectedCount() && rows.selectedRows().size() == selectedShown);
				CHECK(ints(rows.selectedSourceRows()) == brute);
			}
			Rows fresh;
			fresh.setFolded(rows.folded());
			fresh.setTree(rows.tree());
			fresh.setSorted(rows.sorted());
			fresh.setSource(source.get());
			CHECK(shown(rows) == shown(fresh));
			CHECK(mirror.lines == toldAll(rows));
		}
		CHECK(resets.isEmpty());
	}
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	steps();
	listView();
	treeView();
	parentLabels();
	staticNames();
	selection();
	heldChanges();
	randomSequences();
	std::printf("Watch rows checks=%u failures=0\n", checks);
}
