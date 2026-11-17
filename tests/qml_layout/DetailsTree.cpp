// Details' value tree (the house tree, indent guides, folded summaries, keys,
// fold memory, edit marks) in a real QQuickWindow over a fake provider whose
// models are QStandardItemModels with the roles of the real ValueModel. MIT.
#include <QtCore/QDir>
#include <QtGui/QGuiApplication>
#include <QtGui/QStandardItemModel>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QTest>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>

namespace {
int checks = 0, warnings = 0;
void check(bool condition, const char* expression, int line)
{
	++checks;
	if (!condition)
		throw std::runtime_error(std::string(expression) + " (line " + std::to_string(line) + ")");
}
#define CHECK(x) check(bool(x), #x, __LINE__)

// A tree with the roles of ValueModel, built by hand.
class TreeModel final : public QStandardItemModel {
	Q_OBJECT
	Q_PROPERTY(int deepest READ deepest CONSTANT)
public:
	enum Role {
		Name = Qt::UserRole + 1, Detail, Value, Type, Aggregate, ChildCount, Loaded, Editable, EditorText, Takes,
		Kind, Choices, Path, Parts, SummaryKind, ItemCount, Unsent, UnsentCount, Stale, StaleNote, Baseline,
		SummaryText
	};
	explicit TreeModel(int deepestLevel, QObject* parent = nullptr) : QStandardItemModel(parent), deepest_(deepestLevel)
	{
		setItemRoleNames({{Name, "name"}, {Detail, "detail"}, {Value, "value"}, {Type, "typeText"},
		                  {Aggregate, "isAggregate"}, {ChildCount, "childCount"}, {Loaded, "loadedCount"},
		                  {Editable, "editable"}, {EditorText, "editorText"}, {Takes, "takes"},
		                  {Kind, "editorKind"}, {Choices, "editorChoices"}, {Path, "path"},
		                  {Parts, "summaryParts"}, {SummaryKind, "summaryKind"}, {ItemCount, "itemCount"},
		                  {Unsent, "unsent"}, {UnsentCount, "unsentCount"}, {Stale, "stale"},
		                  {StaleNote, "staleNote"}});
	}
	int deepest() const { return deepest_; }

	QStandardItem* leaf(QStandardItem* parent, const QString& name, const QString& type, const QString& value,
	                    const QString& path, const QString& kind = QStringLiteral("text"), const QString& summary = {})
	{
		auto* item = new QStandardItem;
		item->setData(name, Name);
		item->setData(type, Detail);
		item->setData(type, Type);
		item->setData(value, Value);
		item->setData(value, EditorText);
		item->setData(value, Baseline);
		item->setData(summary.isEmpty() ? value : summary, SummaryText);
		item->setData(false, Aggregate);
		item->setData(true, Editable);
		item->setData(QStringLiteral("Enter a number."), Takes);
		item->setData(kind, Kind);
		if (kind == QLatin1String("bool"))
			item->setData(QVariantList{QVariantMap{{"label", "false (0)"}, {"text", "false"}},
			                           QVariantMap{{"label", "true (1)"}, {"text", "true"}}}, Choices);
		if (kind == QLatin1String("enum"))
			item->setData(QVariantList{QVariantMap{{"label", "Idle (0)"}, {"text", "0"}},
			                           QVariantMap{{"label", "Run (1)"}, {"text", "1"}}}, Choices);
		item->setData(path, Path);
		item->setData(false, Unsent);
		item->setData(0, UnsentCount);
		item->setData(0, ChildCount);
		item->setData(0, Loaded);
		(parent ? parent->appendRow(item) : appendRow(item));
		return item;
	}
	QStandardItem* branch(QStandardItem* parent, const QString& name, const QString& type, const QString& kind,
	                      int count, const QString& path, int items = -1)
	{
		auto* item = leaf(parent, name, type, QString{}, path);
		item->setData(true, Aggregate);
		item->setData(false, Editable);
		item->setData(kind, SummaryKind);
		item->setData(count, ChildCount);
		item->setData(items < 0 ? count : items, ItemCount);
		return item;
	}
	void settle(QStandardItem* item)
	{
		// Loaded counts and summaries from the children, as the real model reads them.
		for (int row = 0; row < item->rowCount(); ++row)
			settle(item->child(row));
		if (!item->data(Aggregate).toBool())
			return;
		item->setData(item->rowCount(), Loaded);
		refresh(item);
	}
	void refresh(QStandardItem* item)
	{
		const auto kind = item->data(SummaryKind).toString();
		QStringList parts;
		int unsent = 0;
		for (int row = 0; row < item->rowCount() && row < 8; ++row) {
			auto* child = item->child(row);
			const bool aggregate = child->data(Aggregate).toBool();
			const auto shown = aggregate ? (child->data(SummaryKind).toString() == QLatin1String("struct")
			                                    ? QStringLiteral("{…}") : QStringLiteral("[…]"))
			                             : child->data(SummaryText).toString();
			parts << (kind == QLatin1String("struct") ? child->data(Name).toString() + QLatin1Char(' ') + shown : shown);
		}
		for (int row = 0; row < item->rowCount(); ++row)
			unsent += item->child(row)->data(UnsentCount).toInt();
		if (kind != QLatin1String("arrayOfAggregates"))
			item->setData(parts, Parts);
		item->setData(unsent, UnsentCount);
		item->setData(unsent > 0, Unsent);
	}
	// A read: the same rows again, the view starts over.
	Q_INVOKABLE void reread()
	{
		beginResetModel();
		endResetModel();
	}
	Q_INVOKABLE QString pathOf(const QModelIndex& index) const { return data(index, Path).toString(); }
	Q_INVOKABLE QModelIndex indexOfPath(const QString& path) const
	{
		std::function<QModelIndex(const QModelIndex&)> walk = [&](const QModelIndex& parent) -> QModelIndex {
			for (int row = 0; row < rowCount(parent); ++row) {
				const auto child = index(row, 0, parent);
				if (data(child, Path).toString() == path)
					return child;
				if (const auto found = walk(child); found.isValid())
					return found;
			}
			return {};
		};
		return walk({});
	}
	Q_INVOKABLE bool setScalar(const QModelIndex& index, const QString& text)
	{
		auto* item = itemFromIndex(index);
		if (!item)
			return false;
		bool number = false;
		const auto kind = item->data(Kind).toString();
		if (kind == QLatin1String("bool")) {
			if (text != QLatin1String("true") && text != QLatin1String("false"))
				return false;
		} else {
			text.toDouble(&number);
			if (!number)
				return false;
		}
		item->setData(text, Value);
		item->setData(text, EditorText);
		item->setData(text, SummaryText);
		const bool sent = text != item->data(Baseline).toString();
		item->setData(sent ? 1 : 0, UnsentCount);
		item->setData(sent, Unsent);
		for (auto* up = item->parent(); up; up = up->parent())
			refresh(up);
		return true;
	}

private:
	int deepest_;
};

QQuickItem* named(QQuickItem* root, const QString& name)
{
	if (root->objectName() == name)
		return root;
	for (auto* child : root->childItems())
		if (auto* found = named(child, name))
			return found;
	return nullptr;
}

void settle()
{
	QCoreApplication::processEvents();
	QTest::qWait(90);
}

struct Rig {
	QQuickWindow* window = nullptr;
	QObject* root = nullptr;
	QVariant call(const char* method, QVariant a = {}, QVariant b = {})
	{
		QVariant result;
		bool ok;
		if (b.isValid())
			ok = QMetaObject::invokeMethod(root, method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, a), Q_ARG(QVariant, b));
		else if (a.isValid())
			ok = QMetaObject::invokeMethod(root, method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, a));
		else
			ok = QMetaObject::invokeMethod(root, method, Q_RETURN_ARG(QVariant, result));
		if (!ok)
			throw std::runtime_error(std::string("no method ") + method);
		settle();
		return result;
	}
	QVariantMap row(int r) { return call("row", r).toMap(); }
	QVariantList rows() { return call("rows").toList(); }
	QStringList names()
	{
		QStringList out;
		for (const auto& item : rows())
			out << item.toMap().value("name").toString();
		return out;
	}
	int rowOf(const QString& name) { return call("rowByName", name).toInt(); }
	int rowOfPath(const QString& path) { return call("rowByPath", path).toInt(); }
	void key(Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier)
	{
		QTest::keyClick(window, key, mods);
		settle();
	}
	void select(int r) { call("select", r); }
	QString current() { return call("currentName").toString(); }
	void resizeTo(int width)
	{
		window->resize(width, window->height());
		settle();
		settle();
	}
	void shot(const QString& dir, const QString& name)
	{
		settle();
		CHECK(window->grabWindow().save(QDir(dir).filePath(name)));
	}
};

// Rows at 7 px per character is the design's grid; here the real fonts, so
// the numbers come from the same functions the panel uses.
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
			++warnings;
		std::fprintf(stderr, "%s\n", qPrintable(message));
	});
	try {
		CHECK(argc > 1);
		const QString out = QString::fromLocal8Bit(argv[1]);
		QQmlApplicationEngine engine;
		// The struct of the design's section I: Config 0, Calib 1, Phase 2, [0] 3, Offset 4.
		TreeModel field(4);
		auto* config = field.branch(nullptr, "Config", "struct (4)", "struct", 4, "");
		field.leaf(config, "Gain", "f32", "1.5", "Gain");
		field.leaf(config, "Enabled", "bool", "true", "Enabled", "bool");
		field.leaf(config, "SamplePeriodMs", "u32", "100", "SamplePeriodMs");
		auto* calib = field.branch(config, "Calib", "struct (3)", "struct", 3, "Calib");
		field.leaf(calib, "Mode", "enum (u8)", "1", "Calib.Mode", "enum", "Run");
		auto* taps = field.branch(calib, "Taps", "u16[64]", "array", 8, "Calib.Taps", 64);
		const int tapValues[] = {0, 0, 1, 2, 3, 5, 8, 13};
		for (int i = 0; i < 8; ++i)
			field.leaf(taps, QStringLiteral("[%1]").arg(i), "u16", QString::number(tapValues[i]), QStringLiteral("Calib.Taps[%1]").arg(i));
		auto* phase = field.branch(calib, "Phase", "struct[3]", "arrayOfAggregates", 3, "Calib.Phase");
		const char* offsets[] = {"0.25", "0.5", "0"};
		const char* scales[] = {"1.002", "1", "0.998"};
		for (int i = 0; i < 3; ++i) {
			auto* element = field.branch(phase, QStringLiteral("[%1]").arg(i), "struct (2)", "struct", 2, QStringLiteral("Calib.Phase[%1]").arg(i));
			field.leaf(element, "Offset", "f32", offsets[i], QStringLiteral("Calib.Phase[%1].Offset").arg(i));
			field.leaf(element, "Scale", "f32", scales[i], QStringLiteral("Calib.Phase[%1].Scale").arg(i));
		}
		field.settle(config);

		// A chain seven levels deep: Deep 0 ... F 6.
		TreeModel deep(6);
		auto* node = deep.branch(nullptr, "Deep", "struct (1)", "struct", 1, "");
		QString path;
		for (const char* step : {"A", "B", "C", "D", "E"}) {
			path += (path.isEmpty() ? "" : ".") + QString::fromLatin1(step);
			node = deep.branch(node, step, "struct (1)", "struct", 1, path);
		}
		deep.leaf(node, "F", "f32", "0.5", path + ".F");
		deep.settle(deep.item(0));

		// A Service's request: no root row, the heading is the root.
		TreeModel service(2);
		service.leaf(nullptr, "value", "f32", "2", "value");
		auto* limits = service.branch(nullptr, "limits", "struct (2)", "struct", 2, "limits");
		service.leaf(limits, "Lo", "f32", "0", "limits.Lo");
		service.leaf(limits, "Hi", "f32", "100", "limits.Hi");
		service.settle(limits);
		// A section whose only row is a struct.
		TreeModel single(2);
		auto* cfg = single.branch(nullptr, "cfg", "struct (2)", "struct", 2, "cfg");
		single.leaf(cfg, "Enable", "bool", "true", "cfg.Enable", "bool");
		single.leaf(cfg, "Gain", "f32", "1.5", "cfg.Gain");
		single.settle(cfg);
		QStandardItemModel empty;
		engine.rootContext()->setContextProperty(QStringLiteral("fieldModel"), &field);
		engine.rootContext()->setContextProperty(QStringLiteral("deepModel"), &deep);
		engine.rootContext()->setContextProperty(QStringLiteral("serviceModel"), &service);
		engine.rootContext()->setContextProperty(QStringLiteral("singleModel"), &single);
		engine.rootContext()->setContextProperty(QStringLiteral("emptyModel"), &empty);
		engine.load(QUrl(QStringLiteral("qrc:/DetailsTree.qml")));
		CHECK(!engine.rootObjects().isEmpty());
		auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
		CHECK(window && QTest::qWaitForWindowExposed(window));
		settle();
		Rig rig{window, window};
		const int body = qRound(QGuiApplication::font().pointSizeF() * QGuiApplication::primaryScreen()->logicalDotsPerInch() / 72.0);
		Q_UNUSED(body)

#include "DetailsTreeChecks.inc"

		std::printf("Details tree checks=%d warnings=%d\n", checks, warnings);
		return warnings == 0 ? 0 : 1;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Details tree check failed after %d checks: %s\n", checks, error.what());
		return 1;
	}
}

#include "DetailsTree.moc"
