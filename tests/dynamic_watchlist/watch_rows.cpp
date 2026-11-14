// The Watchlist's rows as shown, list and tree, over a plain item model. MIT.
#include <Telemetry/UX/WatchRows.hpp>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
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

// What a view would show if it read a row only when told: every insert,
// removal, move, change and reset applied as it is announced.
struct Mirror {
	QStringList lines;
	explicit Mirror(Rows& rows)
	{
		const auto all = [this, &rows] {
			lines.clear();
			for (int row = 0; row < rows.count(); ++row)
				lines << line(rows, row);
		};
		all();
		QObject::connect(&rows, &QAbstractItemModel::modelReset, &rows, all);
		QObject::connect(&rows, &QAbstractItemModel::rowsInserted, &rows,
		                 [this, &rows](const QModelIndex&, int first, int last) {
			                 for (int row = first; row <= last; ++row)
				                 lines.insert(row, line(rows, row));
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
				                 lines[row] = line(rows, row);
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
	rows.setSorted(true);
	CHECK(resets.count() == 1);
	CHECK(names(rows) == QStringList({QStringLiteral("data.Samples[9]"), QStringLiteral("data.Samples[10]"),
	                                  QStringLiteral("device.Voltage")}));
	CHECK(role(rows, 0, Rows::SourceRowRole).toInt() == 2);
	// Added while sorted: in its place, without a reset.
	QSignalSpy inserted(&rows, &QAbstractItemModel::rowsInserted);
	add(*source, 3, QStringLiteral("data.Count"), key(6, 0));
	CHECK(resets.count() == 1 && inserted.count() == 1);
	CHECK(name(rows, 0) == QStringLiteral("data.Count"));
	// A value read: its row, nothing else.
	QSignalSpy changed(&rows, &QAbstractItemModel::dataChanged);
	source->item(0)->setData(QStringLiteral("231"), Text);
	CHECK(changed.count() == 1);
	CHECK(changed[0][0].toModelIndex().row() == 3 && changed[0][1].toModelIndex().row() == 3);
	CHECK(changed[0][2].value<QList<int>>() == QList<int>({Rows::TextRole}));
	rows.setSorted(false);
	CHECK(names(rows).first() == QStringLiteral("device.Voltage"));
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
			QCoreApplication::processEvents();
			Rows fresh;
			fresh.setFolded(rows.folded());
			fresh.setTree(rows.tree());
			fresh.setSorted(rows.sorted());
			fresh.setSource(source.get());
			CHECK(shown(rows) == shown(fresh));
			CHECK(mirror.lines == shown(rows));
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
	staticNames();
	randomSequences();
	std::printf("Watch rows checks=%u failures=0\n", checks);
}
