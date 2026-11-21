// The designer's review of 20 November 2026 (section 14, part A) in a real
// QQuickWindow: the path grammar's helpers (15.1), and the Device tree over a
// third-party model shaped as a group tree will be - groups inside groups,
// a group's subgroups first, then its Fields, Commands and Services - for
// what a flat (v3.0) descriptor cannot show: counts with "in N groups", one
// indent step chosen for the deepest group, the context line with a path of
// groups, find by path segments and by named type, the opening rule, * and
// "Collapse all", the kinds filter, Copy path, and the path label a
// breadcrumb or the context line shows. No network or device I/O. MIT.
#include "PathLabelChecks.hpp"
#include <Telemetry/UX/PathGrammar.hpp>
#include <Telemetry/UX/QmlTypes.hpp>
#include <QtCore/QDir>
#include <QtCore/QItemSelectionModel>
#include <QtGui/QClipboard>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QStandardItemModel>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlExpression>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
int checks = 0, warnings = 0;
void check(bool condition, const char* expression, int line)
{
	++checks;
	if (!condition)
		throw std::runtime_error(std::string(expression) + " (line " + std::to_string(line) + ")");
}
#define CHECK(x) check(bool(x), #x, __LINE__)

namespace pg = telemetry::ux::path_grammar;

enum Role {
	Name = Qt::UserRole + 1, Detail, IsEndpoint, Category, Kind, TypeText, TypeName, Path, FieldCount, CommandCount,
	ServiceCount, GroupCount, Count, Readable, Writable, PreviewChild
};

// A group tree as part B's descriptor will give one: every row's roles as
// the Device tree's own model names them.
class Outline final : public QStandardItemModel {
	Q_OBJECT
	Q_PROPERTY(int deepestGroup MEMBER deepest NOTIFY shapeChanged)
	Q_PROPERTY(int fieldTotal MEMBER fields NOTIFY shapeChanged)
	Q_PROPERTY(int commandTotal MEMBER commands NOTIFY shapeChanged)
	Q_PROPERTY(int serviceTotal MEMBER services NOTIFY shapeChanged)
	Q_PROPERTY(QString widestFieldType MEMBER widest NOTIFY shapeChanged)
public:
	int deepest = -1, fields = 0, commands = 0, services = 0;
	QString widest;

	QHash<int, QByteArray> roleNames() const override
	{
		return {{Name, "name"},           {Detail, "detail"},         {IsEndpoint, "isEndpoint"},
		        {Category, "category"},   {Kind, "kind"},             {TypeText, "typeText"},
		        {TypeName, "typeName"},   {Path, "path"},             {FieldCount, "fieldCount"},
		        {CommandCount, "commandCount"}, {ServiceCount, "serviceCount"}, {GroupCount, "groupCount"},
		        {Count, "count"},         {Readable, "readable"},     {Writable, "writable"},
		        {PreviewChild, "previewChild"}};
	}
	QStandardItem* add(QStandardItem* parent, const QString& name, const QString& kind, bool endpoint, int category)
	{
		auto* row = new QStandardItem;
		row->setData(name, Name);
		row->setData(kind, Kind);
		row->setData(endpoint, IsEndpoint);
		row->setData(category, Category);
		row->setData(QString{}, Detail);
		row->setData(QString{}, TypeText);
		row->setData(QString{}, TypeName);
		row->setData(false, PreviewChild);
		row->setData(true, Readable);
		row->setData(category == 1, Writable);
		(parent ? parent : invisibleRootItem())->appendRow(row);
		return row;
	}
	QStandardItem* group(QStandardItem* parent, const QString& name) { return add(parent, name, QStringLiteral("group"), false, 0); }
	QStandardItem* field(QStandardItem* parent, const QString& name, const QString& type, const QString& kind = QStringLiteral("value"),
	                     const QString& typeName = {})
	{
		auto* row = add(parent, name, kind, true, 1);
		row->setData(type, TypeText);
		row->setData(typeName, TypeName);
		row->setData(QStringLiteral("Field · %1").arg(type), Detail);
		return row;
	}
	QStandardItem* member(QStandardItem* field, const QString& name, const QString& type, const QString& kind = QStringLiteral("value"))
	{
		auto* row = add(field, name, kind, false, 1);
		row->setData(type, TypeText);
		row->setData(true, PreviewChild);
		return row;
	}
	QStandardItem* command(QStandardItem* parent, const QString& name, const QString& signature, bool service = false)
	{
		auto* row = add(parent, name, service ? QStringLiteral("service") : QStringLiteral("command"), true, service ? 3 : 2);
		row->setData(signature, TypeText);
		row->setData(false, Readable);
		return row;
	}
	struct Tally {
		int fields = 0, commands = 0, services = 0, groups = 0;
	};
	// Paths in the one grammar and every group's counts, from the leaves up.
	Tally finish(QStandardItem* item, const QString& base, int depth)
	{
		Tally total;
		for (int row = 0; row < item->rowCount(); ++row) {
			auto* child = item->child(row);
			const auto kind = child->data(Kind).toString();
			const auto name = child->data(Name).toString();
			const bool inField = item != invisibleRootItem() && item->data(Kind).toString() != QLatin1String("group");
			const auto path = inField ? base + QLatin1Char('.') + name : base.isEmpty() ? pg::escaped(name) : pg::endpoint(base, name);
			child->setData(path, Path);
			if (kind == QLatin1String("group")) {
				deepest = std::max(deepest, depth);
				const auto under = finish(child, path, depth + 1);
				child->setData(under.fields, FieldCount);
				child->setData(under.commands, CommandCount);
				child->setData(under.services, ServiceCount);
				child->setData(under.groups, GroupCount);
				child->setData(under.fields + under.commands + under.services, Count);
				total.fields += under.fields;
				total.commands += under.commands;
				total.services += under.services;
				total.groups += under.groups + 1;
				continue;
			}
			if (child->data(IsEndpoint).toBool()) {
				const int category = child->data(Category).toInt();
				(category == 1 ? total.fields : category == 2 ? total.commands : total.services) += 1;
			}
			finish(child, path, depth + 1);
		}
		return total;
	}
	void build(int extraTops)
	{
		clear();
		deepest = -1;
		auto* adc = group(nullptr, QStringLiteral("adc"));
		field(group(adc, QStringLiteral("channel1")), QStringLiteral("Gain"), QStringLiteral("f32"));
		field(group(adc, QStringLiteral("channel2")), QStringLiteral("Gain"), QStringLiteral("f32"));
		field(adc, QStringLiteral("Config"), QStringLiteral("adc::Config"), QStringLiteral("struct"), QStringLiteral("adc::Config"));
		field(adc, QStringLiteral("Status"), QStringLiteral("adc::Status"), QStringLiteral("struct"), QStringLiteral("adc::Status"));
		command(adc, QStringLiteral("Reset"), QStringLiteral("()"));
		command(adc, QStringLiteral("Calibrate"), QStringLiteral("(f32) -> f32"), true);
		auto* power = group(nullptr, QStringLiteral("power"));
		auto* metrology = group(power, QStringLiteral("metrology"));
		auto* harmonics = group(metrology, QStringLiteral("harmonics"));
		for (const char* phase : {"phaseA", "phaseB", "phaseC"}) {
			auto* at = group(harmonics, QString::fromLatin1(phase));
			field(at, QStringLiteral("H3"), QStringLiteral("f32"));
			auto* spectrum = field(at, QStringLiteral("Spectrum"), QStringLiteral("metrology::Spectrum"), QStringLiteral("struct"),
			                       QStringLiteral("metrology::Spectrum"));
			member(spectrum, QStringLiteral("Bins"), QStringLiteral("f32[64]"), QStringLiteral("array"));
			member(spectrum, QStringLiteral("Window"), QStringLiteral("metrology::Window"));
		}
		field(metrology, QStringLiteral("Config"), QStringLiteral("metrology::Config"), QStringLiteral("struct"),
		      QStringLiteral("metrology::Config"));
		command(metrology, QStringLiteral("Reset"), QStringLiteral("()"));
		auto* meter = group(power, QStringLiteral("meter"));
		for (int n = 0; n < 16; ++n)
			field(meter, QStringLiteral("M%1").arg(n), QStringLiteral("f32"));
		for (int n = 0; n < extraTops; ++n)
			field(group(nullptr, QStringLiteral("extra%1").arg(n)), QStringLiteral("X"), QStringLiteral("u8"));
		const auto total = finish(invisibleRootItem(), {}, 0);
		fields = total.fields;
		commands = total.commands;
		services = total.services;
		widest = QStringLiteral("metrology::Spectrum");
		Q_EMIT shapeChanged();
	}
	Q_INVOKABLE QString pathOf(const QModelIndex& index) const { return index.data(Path).toString(); }
	Q_INVOKABLE bool isGroup(const QModelIndex& index) const { return index.data(Kind).toString() == QLatin1String("group"); }
	Q_INVOKABLE QModelIndex indexOfGroupPath(const QString& path) const
	{
		const auto found = at(path);
		return isGroup(found) ? found : QModelIndex{};
	}
	QModelIndex at(const QString& path, const QModelIndex& parent = {}) const
	{
		for (int row = 0; row < rowCount(parent); ++row) {
			const auto child = index(row, 0, parent);
			if (child.data(Path).toString() == path)
				return child;
			if (const auto found = at(path, child); found.isValid())
				return found;
		}
		return {};
	}
Q_SIGNALS:
	void shapeChanged();
};

class Provider final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel* endpoints READ endpoints CONSTANT)
	Q_PROPERTY(QModelIndex selectedEndpointIndex READ selected NOTIFY selectionChanged)
	Q_PROPERTY(bool connected READ connected CONSTANT)
public:
	explicit Provider(Outline* shown) : model(shown) {}
	QAbstractItemModel* endpoints() const { return model; }
	QModelIndex selected() const { return selection; }
	bool connected() const { return true; }
	Q_INVOKABLE bool selectEndpoint(const QModelIndex& index)
	{
		selection = index;
		Q_EMIT selectionChanged();
		return true;
	}
	Q_INVOKABLE void revealGroup(const QString& path) { Q_EMIT revealGroupRequested(path); }
	Outline* model;
	QPersistentModelIndex selection;
Q_SIGNALS:
	void selectionChanged();
	void revealGroupRequested(const QString& path);
};

void settle(int ms = 60)
{
	QCoreApplication::processEvents();
	QTest::qWait(ms);
}
QQuickItem* named(QQuickItem* root, const QString& name, bool visibleOnly = true)
{
	if (!root || (visibleOnly && !root->isVisible()))
		return nullptr;
	if (root->objectName() == name)
		return root;
	for (auto* child : root->childItems())
		if (auto* found = named(child, name, visibleOnly))
			return found;
	return nullptr;
}
void collect(QQuickItem* root, const QString& name, QList<QQuickItem*>& out)
{
	if (!root || !root->isVisible())
		return;
	if (root->objectName() == name)
		out.append(root);
	for (auto* child : root->childItems())
		collect(child, name, out);
}
QVariant eval(QObject* scope, const QString& expression)
{
	QQmlExpression e(qmlContext(scope), scope, expression);
	const auto value = e.evaluate();
	if (e.hasError())
		throw std::runtime_error(e.error().toString().toStdString());
	return value;
}
bool near(double a, double b, double within = 0.6) { return std::abs(a - b) <= within; }
QColor color(QObject* item) { return item->property("color").value<QColor>(); }

struct Tree {
	QQuickWindow* window = nullptr;
	QQuickItem* panel = nullptr;
	QQuickItem* view = nullptr;
	Outline* model = nullptr;

	QModelIndex at(const QString& path) const
	{
		const auto index = model->at(path);
		CHECK(index.isValid());
		return index;
	}
	QModelIndex shown(const QModelIndex& source) const
	{
		QVariant result;
		CHECK(QMetaObject::invokeMethod(panel, "shownOf", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant::fromValue(source))));
		return result.value<QModelIndex>();
	}
	int row(const QModelIndex& source) const
	{
		int result = -1;
		CHECK(QMetaObject::invokeMethod(view, "rowAtIndex", Q_RETURN_ARG(int, result), Q_ARG(QModelIndex, shown(source))));
		return result;
	}
	bool expanded(const QModelIndex& source) const
	{
		const int at = row(source);
		bool result = false;
		if (at >= 0)
			CHECK(QMetaObject::invokeMethod(view, "isExpanded", Q_RETURN_ARG(bool, result), Q_ARG(int, at)));
		return result;
	}
	void reveal(const QModelIndex& source, bool current = false) const
	{
		QVariant result;
		CHECK(QMetaObject::invokeMethod(panel, "reveal", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant::fromValue(source)),
		                                Q_ARG(QVariant, current)));
		CHECK(result.toBool());
		settle();
	}
	QModelIndex current() const
	{
		auto* selection = qobject_cast<QItemSelectionModel*>(view->property("selectionModel").value<QObject*>());
		CHECK(selection);
		QVariant source;
		CHECK(QMetaObject::invokeMethod(panel, "sourceOf", Q_RETURN_ARG(QVariant, source),
		                                Q_ARG(QVariant, QVariant::fromValue(selection->currentIndex()))));
		return source.value<QModelIndex>();
	}
	QList<QQuickItem*> rows() const
	{
		QList<QQuickItem*> all, out;
		collect(view, QStringLiteral("endpointRow"), all);
		for (auto* row : all) {
			QQuickItem* shownItem = nullptr;
			if (QMetaObject::invokeMethod(view, "itemAtCell", Q_RETURN_ARG(QQuickItem*, shownItem),
			                              Q_ARG(QPoint, QPoint(0, row->property("row").toInt()))) && shownItem == row)
				out.append(row);
		}
		return out;
	}
	QQuickItem* item(const QModelIndex& source) const
	{
		reveal(source);
		for (auto* candidate : rows())
			if (candidate->property("sourceIndex").value<QModelIndex>() == source)
				return candidate;
		return nullptr;
	}
	QQuickItem* part(const QModelIndex& source, const char* name) const
	{
		auto* found = item(source);
		CHECK(found);
		return named(found, QString::fromLatin1(name));
	}
	void width(int pixels) const
	{
		window->resize(pixels, window->height());
		settle(150);
	}
	void key(int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
	{
		QTest::keyClick(window, Qt::Key(code), modifiers);
		settle();
	}
	void shot(const QString& directory, const QString& name) const
	{
		settle(150);
		CHECK(window->grabWindow().save(QDir(directory).filePath(name)));
	}
};

// The path grammar's helpers (PathGrammar.hpp), as the providers use them.
void grammar()
{
	CHECK(pg::escaped(QStringLiteral("power.meter")) == QStringLiteral("power\\.meter"));
	CHECK(pg::escaped(QStringLiteral("a/b[1]\\")) == QStringLiteral("a\\/b\\[1\\]\\\\"));
	CHECK(pg::unescaped(QStringLiteral("power\\.meter")) == QStringLiteral("power.meter"));
	CHECK(pg::groupPath({QStringLiteral("metrology"), QStringLiteral("harmonics")}) == QStringLiteral("metrology/harmonics"));
	CHECK(pg::endpoint(QStringLiteral("metrology/harmonics"), QStringLiteral("H3")) == QStringLiteral("metrology/harmonics/H3"));
	CHECK(pg::compose(QStringLiteral("device"), QStringLiteral("Config"), QStringLiteral(".Gain")) == QStringLiteral("device/Config.Gain"));
	CHECK(pg::compose(QStringLiteral("power.meter"), QStringLiteral("Voltage"), {}) == QStringLiteral("power\\.meter/Voltage"));
	{
		const auto parts = pg::parse(QStringLiteral("metrology/harmonics/phaseA/Spectrum.Window"));
		CHECK(parts && parts->segments == (QStringList{QStringLiteral("metrology"), QStringLiteral("harmonics"), QStringLiteral("phaseA")}));
		CHECK(parts->group == QStringLiteral("metrology/harmonics/phaseA") && parts->name == QStringLiteral("Spectrum") &&
		      parts->member == QStringLiteral(".Window"));
	}
	{
		const auto parts = pg::parse(QStringLiteral("data/Samples[3]"));
		CHECK(parts && parts->group == QStringLiteral("data") && parts->name == QStringLiteral("Samples") &&
		      parts->member == QStringLiteral("[3]"));
	}
	{
		const auto parts = pg::parse(QStringLiteral("power\\.meter/Voltage"));
		CHECK(parts && parts->group == QStringLiteral("power.meter") && parts->name == QStringLiteral("Voltage") && parts->member.isEmpty());
	}
	CHECK(!pg::parse(QStringLiteral("device.Config.Gain")));
	CHECK(!pg::parse(QStringLiteral("a//b")) && !pg::parse(QStringLiteral("/b")) && !pg::parse(QStringLiteral("a/")));
	// An older project's dotted key, read against the groups: the longest one
	// it starts with, then the endpoint, then the members (15.1).
	const QSet<QString> groups{QStringLiteral("power"), QStringLiteral("power.meter"), QStringLiteral("device"),
	                           QStringLiteral("adc/channel1")};
	{
		const auto parts = pg::parseDotted(QStringLiteral("power.meter.Voltage"), groups);
		CHECK(parts && parts->group == QStringLiteral("power.meter") && parts->name == QStringLiteral("Voltage"));
	}
	{
		const auto parts = pg::parseAny(QStringLiteral("device.Config.Gain"), groups);
		CHECK(parts && parts->group == QStringLiteral("device") && parts->name == QStringLiteral("Config") &&
		      parts->member == QStringLiteral(".Gain"));
	}
	{
		// A flat group that holds a "/" keeps its key resolvable.
		const auto parts = pg::parseAny(QStringLiteral("adc/channel1.Gain"), groups);
		CHECK(parts && parts->group == QStringLiteral("adc/channel1") && parts->name == QStringLiteral("Gain"));
	}
	{
		const auto parts = pg::parseAny(QStringLiteral("device/Config.Gain"), groups);
		CHECK(parts && parts->group == QStringLiteral("device") && parts->name == QStringLiteral("Config"));
	}
	CHECK(pg::canonical(QStringLiteral("device.Config.Gain"), groups) == QStringLiteral("device/Config.Gain"));
	CHECK(pg::canonical(QStringLiteral("power.meter.Voltage"), groups) == QStringLiteral("power\\.meter/Voltage"));
	CHECK(pg::canonical(QStringLiteral("device/Config.Gain"), groups) == QStringLiteral("device/Config.Gain"));
	CHECK(pg::canonical(QStringLiteral("nothing.Here"), groups) == QStringLiteral("nothing.Here"));
	CHECK(pg::canonicalPrefix(QStringLiteral("device.Con"), groups) == QStringLiteral("device/Con"));
	CHECK(pg::canonicalPrefix(QStringLiteral("dev"), groups) == QStringLiteral("dev"));
	CHECK(pg::leafStart(QStringLiteral("device/Config.Gain")) == 14);
	CHECK(pg::leafStart(QStringLiteral("data/Samples[3]")) == 12);
	CHECK(pg::leafStart(QStringLiteral("power\\.meter/Voltage")) == 13);
	CHECK(pg::slashed(QStringLiteral("a/b")) && !pg::slashed(QStringLiteral("a\\/b")));
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	telemetry::ux::registerQmlTypes();
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
			++warnings;
		std::fprintf(stderr, "%s\n", qPrintable(message));
	});
	try {
		const auto output = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
		grammar();
		Outline model;
		model.build(0);
		Provider provider(&model);
		QQmlApplicationEngine engine;
		engine.rootContext()->setContextProperty(QStringLiteral("groupsProvider"), &provider);
		engine.load(QUrl(QStringLiteral("qrc:/DeviceTreeGroups.qml")));
		CHECK(!engine.rootObjects().isEmpty());
		Tree t;
		t.window = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
		CHECK(t.window);
		settle(250);
		t.panel = named(t.window->contentItem(), QStringLiteral("groupsTree"));
		t.view = named(t.panel, QStringLiteral("endpointTree"));
		t.model = &model;
		CHECK(t.panel && t.view);
		const auto type = [&](const char* name) { return eval(t.panel, QStringLiteral("Type.") + QLatin1String(name)); };
		const auto token = [&](const char* name) { return eval(t.panel, QStringLiteral("Theme.") + QLatin1String(name)).value<QColor>(); };
		const double rh = type("rowHeight").toDouble();
		const double nameFloor = type("nameFloor").toDouble();
		const double padding = type("padding").toDouble();

		const auto adc = t.at(QStringLiteral("adc"));
		const auto power = t.at(QStringLiteral("power"));
		const auto metrology = t.at(QStringLiteral("power/metrology"));
		const auto harmonics = t.at(QStringLiteral("power/metrology/harmonics"));
		const auto phaseA = t.at(QStringLiteral("power/metrology/harmonics/phaseA"));
		const auto h3 = t.at(QStringLiteral("power/metrology/harmonics/phaseA/H3"));
		const auto spectrum = t.at(QStringLiteral("power/metrology/harmonics/phaseA/Spectrum"));
		const auto meter = t.at(QStringLiteral("power/meter"));

		// The opening rule: 7 top-level groups or fewer open one level.
		CHECK(t.expanded(adc) && t.expanded(power) && !t.expanded(metrology) && !t.expanded(t.at(QStringLiteral("adc/channel1"))));
		// A group's subgroups first, then its Fields, Commands and Services.
		{
			QStringList order;
			for (int row = t.row(adc) + 1; row < t.row(power); ++row) {
				QModelIndex index;
				CHECK(QMetaObject::invokeMethod(t.view, "index", Q_RETURN_ARG(QModelIndex, index), Q_ARG(int, row), Q_ARG(int, 0)));
				order << index.data(Qt::UserRole + 1).toString();
			}
			CHECK(order == (QStringList{QStringLiteral("channel1"), QStringLiteral("channel2"), QStringLiteral("Config"),
			                            QStringLiteral("Status"), QStringLiteral("Reset"), QStringLiteral("Calibrate")}));
		}

		// Counts cover everything under a group, in words, Fields first; the
		// tooltip and the accessible name carry the whole, with its groups.
		auto* powerCount = t.part(power, "endpointRowCount");
		CHECK(powerCount && powerCount->property("text").toString() == QStringLiteral("23 Fields · 1 Command"));
		CHECK(t.item(power)->property("countFull").toString() == QStringLiteral("23 Fields · 1 Command in 6 groups"));
		CHECK(eval(t.item(power), QStringLiteral("Accessible.name")).toString().contains(QStringLiteral("in 6 groups")));
		CHECK(t.part(adc, "endpointRowCount")->property("text").toString() ==
		      QStringLiteral("4 Fields · 1 Command · 1 Service"));
		CHECK(color(powerCount) == token("textMuted"));

		// One indent step per descriptor: D is the deepest group (3) plus 2.
		CHECK(t.panel->property("indentDepth").toInt() == 5);
		const auto stepOf = [&] { return t.panel->property("indentStep").toDouble(); };
		CHECK(near(stepOf(), 16, 0.01));
		// * on power: every group under it opens, its Fields folded.
		t.reveal(power, true);
		t.view->forceActiveFocus();
		t.key(Qt::Key_Asterisk, Qt::ShiftModifier);
		for (const auto& index : {power, metrology, harmonics, phaseA, meter, t.at(QStringLiteral("power/metrology/harmonics/phaseC"))})
			CHECK(t.expanded(index));
		CHECK(!t.expanded(spectrum));
		// Opening never moves a row sideways: the step is the descriptor's.
		CHECK(near(stepOf(), 16, 0.01));
		const auto guidesStraight = [&](double step) {
			for (auto* row : t.rows()) {
				CHECK(near(row->property("indentation").toDouble(), step, 0.01));
				QList<QQuickItem*> guides;
				collect(row, QStringLiteral("indentGuide"), guides);
				CHECK(guides.size() == row->property("depth").toInt());
				for (int a = 0; a < guides.size(); ++a)
					CHECK(near(guides[a]->mapToItem(t.panel, QPointF(0, 0)).x(), std::round(padding + a * step + 8), 0.01));
			}
		};
		guidesStraight(16);
		const double nameDeep = t.part(h3, "endpointRowName")->mapToItem(t.panel, QPointF(0, 0)).x();
		const double nameUp = t.part(phaseA, "endpointRowName")->mapToItem(t.panel, QPointF(0, 0)).x();
		CHECK(near(nameDeep - nameUp, 16, 0.6));
		t.shot(output, QStringLiteral("device-tree-groups-420.png"));

		// 280 px: the step gives way to 8 px (after rw and before the names),
		// still one for every row; a group's count drops whole parts.
		t.width(280);
		CHECK(near(stepOf(), 8, 0.01));
		t.reveal(h3);
		guidesStraight(8);
		CHECK(near(t.part(h3, "endpointRowName")->mapToItem(t.panel, QPointF(0, 0)).x() -
		           t.part(phaseA, "endpointRowName")->mapToItem(t.panel, QPointF(0, 0)).x(), 8, 0.6));
		{
			auto* count = t.part(adc, "endpointRowCount");
			const auto words = count->property("text").toString();
			CHECK(words.startsWith(QStringLiteral("4 Fields")));
			CHECK(words == QStringLiteral("4 Fields · 1 Command · 1 Service") || words.endsWith(QStringLiteral(" · …")));
			auto* name = t.part(adc, "endpointRowName");
			CHECK(name->width() >= std::min(name->implicitWidth(), nameFloor) - 1);
		}
		// A depth-4 name at 8 px keeps its first characters before its type
		// elides (give-way steps 5 and 6).
		{
			auto* name = t.part(spectrum, "endpointRowName");
			CHECK(!t.part(spectrum, "endpointRowType")->property("truncated").toBool() ||
			      name->width() >= std::min(name->implicitWidth(), nameFloor) - 1);
		}

		// The context line: scrolled into phaseA, the line names the group the
		// top row belongs to (power/metrology/harmonics), laid over the tree
		// under its header, a 1 px rule below.
		auto* context = named(t.panel, QStringLiteral("endpointTreeContext"), false);
		CHECK(context && !context->isVisible());
		t.view->setProperty("contentY", t.row(harmonics) * rh);
		settle(150);
		CHECK(context->isVisible() && t.panel->property("contextPath").toString() == QStringLiteral("power/metrology/harmonics"));
		CHECK(named(context, QStringLiteral("endpointTreeContextRule")));
		auto* contextLabel = named(context, QStringLiteral("endpointTreeContextPath"));
		CHECK(contextLabel && contextLabel->property("spokenName").toString() == QStringLiteral("harmonics, in power, metrology"));
		t.shot(output, QStringLiteral("device-tree-groups-context-280.png"));
		t.width(420);
		t.view->setProperty("contentY", t.row(harmonics) * rh);
		settle(150);
		CHECK(context->isVisible());
		t.shot(output, QStringLiteral("device-tree-groups-context-420.png"));
		// Deeper: H3's row under the line names phaseA's path.
		t.view->setProperty("contentY", t.row(h3) * rh);
		settle(120);
		CHECK(t.panel->property("contextPath").toString() == QStringLiteral("power/metrology/harmonics/phaseA"));
		// PL-1: the line's path takes the room it has - whole whenever it
		// fits, else only whole segments drop - at every width, after a
		// resize both ways, the pointer on it and off.
		for (const int width : {420, 240, 280, 320, 560, 260, 420}) {
			t.width(width);
			t.view->setProperty("contentY", t.row(h3) * rh);
			QTest::mouseMove(t.window, QPoint(t.window->width() - 2, t.window->height() - 2));
			settle(120);
			CHECK(context->isVisible());
			auto fault = path_label_checks::fault(contextLabel);
			if (!fault.isEmpty())
				throw std::runtime_error("PL-1 context line: " + fault.toStdString());
			CHECK(fault.isEmpty());
			QTest::mouseMove(t.window, contextLabel->mapToScene(QPointF(contextLabel->width() / 2, contextLabel->height() / 2)).toPoint());
			settle(60);
			fault = path_label_checks::fault(contextLabel);
			if (!fault.isEmpty())
				throw std::runtime_error("PL-1 context line, hovered: " + fault.toStdString());
			CHECK(fault.isEmpty());
		}
		QTest::mouseMove(t.window, QPoint(t.window->width() - 2, t.window->height() - 2));
		t.view->setProperty("contentY", t.row(h3) * rh);
		settle(120);
		// A click on a segment makes that group current and shows it.
		{
			QQuickItem* link = nullptr;
			QList<QQuickItem*> links;
			collect(contextLabel, QStringLiteral("pathLabelLink"), links);
			for (auto* candidate : links)
				if (candidate->property("text").toString().endsWith(QStringLiteral("metrology")))
					link = candidate;
			CHECK(link);
			QSignalSpy clicked(contextLabel, SIGNAL(segmentActivated(QString)));
			QTest::mouseClick(t.window, Qt::LeftButton, Qt::NoModifier,
			                  link->mapToScene(QPointF(link->width() - 4, link->height() / 2)).toPoint());
			settle(150);
			CHECK(clicked.count() == 1 && clicked.at(0).at(0).toString() == QStringLiteral("power/metrology"));
			CHECK(t.current() == metrology);
		}
		// A provider's request reveals a group too (Details' breadcrumb).
		provider.revealGroup(QStringLiteral("power/metrology/harmonics/phaseB"));
		settle(150);
		CHECK(t.current() == t.at(QStringLiteral("power/metrology/harmonics/phaseB")));

		// Find: the segments of a path in order with gaps, the last in the
		// row's own name; "/" and "." one separator.
		auto* search = named(t.panel, QStringLiteral("endpointSearch"));
		auto* status = named(t.panel, QStringLiteral("endpointSearchStatus"), false);
		auto* filter = t.panel->findChild<QObject*>(QStringLiteral("endpointTreeFilter"));
		CHECK(search && status && filter);
		const auto find = [&](const QString& text) {
			CHECK(search->setProperty("text", text));
			settle(250);
			return filter->property("count").toInt();
		};
		CHECK(find(QStringLiteral("harm/H3")) == 3);
		CHECK(status->property("text").toString() == QStringLiteral("3 matches"));
		for (const char* phase : {"phaseA", "phaseB", "phaseC"}) {
			const auto match = t.at(QStringLiteral("power/metrology/harmonics/%1/H3").arg(QLatin1String(phase)));
			CHECK(t.row(match) >= 0);
			auto* name = t.part(match, "endpointRowName");
			CHECK(name->property("text").toString().contains(QStringLiteral("<u>")));
		}
		CHECK(find(QStringLiteral("power.harm.phaseB/H3")) == 1);
		CHECK(find(QStringLiteral("H3/harm")) == 0);
		// By a named type, as a whole: the run is underlined in the Type cell.
		CHECK(find(QStringLiteral("adc::")) == 2);
		{
			auto* typeCell = t.part(t.at(QStringLiteral("adc/Config")), "endpointRowType");
			CHECK(typeCell->property("text").toString().contains(QStringLiteral("<u>")) &&
			      typeCell->property("textFormat").toInt() == 4);
			CHECK(!t.part(t.at(QStringLiteral("adc/Config")), "endpointRowName")->property("text").toString().contains(QStringLiteral("<u>")));
		}
		CHECK(find(QStringLiteral("metrology::Spectrum")) == 3);
		CHECK(find(QStringLiteral("f32")) == 0);
		CHECK(find(QStringLiteral("Config")) == 2);
		// The kinds filter: Services only.
		CHECK(find(QString{}) == 0);
		t.panel->setProperty("kindsShown", 4);
		settle(250);
		CHECK(status->property("text").toString() == QStringLiteral("Services only · 1 match"));
		CHECK(t.row(t.at(QStringLiteral("adc/Calibrate"))) >= 0 && t.row(h3) < 0);
		t.shot(output, QStringLiteral("device-tree-groups-kinds-420.png"));
		t.width(280);
		t.shot(output, QStringLiteral("device-tree-groups-kinds-280.png"));
		t.width(420);
		t.panel->setProperty("kindsShown", 0);
		settle(250);

		// Copy path takes the one grammar, which device code takes; a group's
		// path has no trailing "/".
		{
			const auto copied = [&](const QModelIndex& index) {
				QVariant path;
				CHECK(QMetaObject::invokeMethod(t.panel, "rowPath", Q_RETURN_ARG(QVariant, path), Q_ARG(QVariant, QVariant::fromValue(index))));
				return path.toString();
			};
			CHECK(copied(h3) == QStringLiteral("power/metrology/harmonics/phaseA/H3"));
			CHECK(copied(harmonics) == QStringLiteral("power/metrology/harmonics"));
			CHECK(copied(t.at(QStringLiteral("power/metrology/harmonics/phaseA/Spectrum.Window"))) ==
			      QStringLiteral("power/metrology/harmonics/phaseA/Spectrum.Window"));
		}

		// Collapse all: the top-level groups alone.
		CHECK(QMetaObject::invokeMethod(t.panel, "collapseAll"));
		settle();
		CHECK(t.view->property("rows").toInt() == 2);

		// PL-1's cause: a label exactly as wide as it says it needs shows its
		// whole path. The leaf's room was the label's width less the prefix
		// Text's own width, which counts the "/" glyph's overhang past its
		// advance, so the Batch's label, sized to its implicit width, cut
		// "measure/P…r" by a fraction of a pixel.
		for (const bool interactive : {false, true}) {
			for (const char* path : {"measure/Power", "measure/Current", "control/SetEnabled", "power/metrology/harmonics/phaseA/H3"}) {
				QQmlComponent fitted(&engine);
				fitted.setData(QByteArray("import QtQuick\nimport Telemetry.UX\nPathLabel { interactive: ") +
				                   (interactive ? "true" : "false") + "; path: \"" + path + "\" }",
				               QUrl());
				std::unique_ptr<QObject> made(fitted.create(qmlContext(t.panel)));
				auto* label = qobject_cast<QQuickItem*>(made.get());
				CHECK(label);
				label->setParentItem(t.window->contentItem());
				label->setProperty("width", label->property("implicitWidth").toDouble());
				settle();
				const auto fault = path_label_checks::fault(label);
				if (!fault.isEmpty())
					throw std::runtime_error("PL-1 label at its implicit width: " + fault.toStdString());
				CHECK(fault.isEmpty() && !label->property("elided").toBool());
				CHECK(!named(label, QStringLiteral("pathLabelLeaf"))->property("truncated").toBool());
			}
		}

		// The path label alone: segments drop under one "…" in the design's
		// order, the leaf elides last; a breadcrumb is one Tab stop whose
		// arrows move between its groups and whose Enter shows one.
		{
			QQmlComponent component(&engine);
			component.setData("import QtQuick\nimport Telemetry.UX\nPathLabel { interactive: true; width: 2000;"
			                  " path: \"power/metrology/harmonics/phaseA/Spectrum\" }", QUrl());
			auto* label = qobject_cast<QQuickItem*>(component.create(qmlContext(t.panel)));
			CHECK(label);
			label->setParentItem(t.window->contentItem());
			settle();
			CHECK(!label->property("elided").toBool());
			CHECK(eval(label, QStringLiteral("shape(1).prefix")).toString() == QStringLiteral("power/…/harmonics/phaseA/"));
			CHECK(eval(label, QStringLiteral("shape(2).prefix")).toString() == QStringLiteral("power/…/phaseA/"));
			CHECK(eval(label, QStringLiteral("shape(3).prefix")).toString() == QStringLiteral("…/phaseA/"));
			CHECK(eval(label, QStringLiteral("shape(4).prefix")).toString() == QStringLiteral("…/"));
			CHECK(label->property("spokenName").toString() == QStringLiteral("Spectrum, in power, metrology, harmonics, phaseA"));
			// Within the measure's 1 px tolerance it stays whole (PL-1); past
			// it, one segment drops.
			label->setProperty("width", label->property("implicitWidth").toDouble() - 1);
			settle();
			CHECK(!label->property("elided").toBool());
			label->setProperty("width", label->property("implicitWidth").toDouble() - 2);
			settle();
			CHECK(label->property("elided").toBool() && label->property("drops").toInt() == 1);
			label->setProperty("width", 40);
			settle();
			auto* leaf = named(label, QStringLiteral("pathLabelLeaf"));
			CHECK(leaf && leaf->property("elide").toInt() == Qt::ElideMiddle && leaf->property("truncated").toBool());
			// Keys: one Tab stop, Left and Right among the groups, Enter shows one.
			label->setProperty("width", 2000);
			settle();
			QSignalSpy activated(label, SIGNAL(segmentActivated(QString)));
			CHECK(activated.isValid());
			label->forceActiveFocus(Qt::TabFocusReason);
			settle();
			CHECK(label->hasActiveFocus() && label->property("activeFocusOnTab").toBool());
			CHECK(label->property("currentLink").toInt() == 3);
			t.key(Qt::Key_Left);
			t.key(Qt::Key_Left);
			CHECK(label->property("currentLink").toInt() == 1);
			CHECK(named(label, QStringLiteral("pathLabelLinkFocus")));
			t.key(Qt::Key_Return);
			t.key(Qt::Key_Right);
			t.key(Qt::Key_Right);
			t.key(Qt::Key_Right);
			CHECK(label->property("currentLink").toInt() == 3);
			t.key(Qt::Key_Return);
			CHECK(activated.count() == 2);
			CHECK(activated.at(0).at(0).toString() == QStringLiteral("power/metrology"));
			CHECK(activated.at(1).at(0).toString() == QStringLiteral("power/metrology/harmonics/phaseA"));
			CHECK(eval(label, QStringLiteral("links.length")).toInt() == 4);
			CHECK(eval(label, QStringLiteral("links[1].groupPath")).toString() == QStringLiteral("power/metrology"));
			// Left to right in a mirrored layout: a path is code.
			CHECK(!eval(label, QStringLiteral("LayoutMirroring.enabled")).toBool());
			delete label;
		}

		// More than 7 top-level groups: a new descriptor opens nothing.
		model.build(6);
		settle(300);
		const int tops = model.rowCount();
		CHECK(tops == 8 && t.view->property("rows").toInt() == tops);

		CHECK(warnings == 0);
		std::printf("Device tree groups checks=%d warnings=0\n", checks);
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Device tree groups check %d failed: %s\n", checks, error.what());
		return 1;
	}
}
#include "DeviceTreeGroups.moc"
