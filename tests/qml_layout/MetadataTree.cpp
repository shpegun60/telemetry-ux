// The definition table Details and the Batch item pane show before a Field is
// read: a real selection driven three times (Config, another Field, Config) and
// the rows counted each time. The model is the twin of the provider's own filter.
// MIT.
#include <QtCore/QPersistentModelIndex>
#include <QtCore/QSortFilterProxyModel>
#include <QtGui/QGuiApplication>
#include <QtGui/QStandardItemModel>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QTest>
#include <cstdio>
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

class Source final : public QStandardItemModel {
public:
	enum Role { Name = Qt::UserRole + 1, Detail, IsEndpoint, TypeText };
	Source()
	{
		setItemRoleNames({{Name, "name"}, {Detail, "detail"}, {IsEndpoint, "isEndpoint"}, {TypeText, "typeText"}});
	}
	QStandardItem* add(QStandardItem* parent, const QString& name, const QString& type, bool endpoint)
	{
		auto* item = new QStandardItem;
		item->setData(name, Name);
		item->setData(type, Detail);
		item->setData(endpoint, IsEndpoint);
		item->setData(type, TypeText);
		(parent ? parent->appendRow(item) : appendRow(item));
		return item;
	}
};

// The provider's EndpointDefinitionModel: the selection, its ancestors and its descendants.
class Definitions final : public QSortFilterProxyModel {
public:
	QPersistentModelIndex selected;
	void select(const QModelIndex& index)
	{
		if (selected == index)
			return;
		beginFilterChange();
		selected = index;
		endFilterChange(Direction::Rows);
	}
	QModelIndex rootIndex() const { return selected.isValid() ? mapFromSource(selected.parent()) : QModelIndex{}; }
protected:
	bool filterAcceptsRow(int row, const QModelIndex& parent) const override
	{
		if (!selected.isValid() || !sourceModel())
			return false;
		const auto index = sourceModel()->index(row, 0, parent);
		for (QModelIndex p = selected; p.isValid(); p = p.parent())
			if (p == index)
				return true;
		for (QModelIndex p = index.parent(); p.isValid(); p = p.parent())
			if (p == selected)
				return true;
		return false;
	}
};

class Control final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString selection READ selection NOTIFY changed)
	Q_PROPERTY(QModelIndex rootIndex READ rootIndex NOTIFY changed)
	Q_PROPERTY(QAbstractItemModel* definitions READ definitions CONSTANT)
	Q_PROPERTY(QAbstractItemModel* values READ values CONSTANT)
public:
	Control()
	{
		auto* group = source.add(nullptr, "device", "1 Field", false);
		config = source.add(group, "Config", "struct (3)", true);
		source.add(config, "Gain", "f32", false);
		source.add(config, "Enabled", "bool", false);
		source.add(config, "SamplePeriodMs", "u32", false);
		other = source.add(group, "Counter", "u32", true);
		proxy.setSourceModel(&source);
	}
	QString selection() const { return path; }
	QModelIndex rootIndex() const { return proxy.rootIndex(); }
	QAbstractItemModel* definitions() { return &proxy; }
	QAbstractItemModel* values() { return &none; }
	void pick(const QString& name)
	{
		path = QStringLiteral("device.") + name;
		proxy.select((name == QLatin1String("Config") ? config : other)->index());
		emit changed();
	}
signals:
	void changed();
private:
	Source source;
	Definitions proxy;
	QStandardItemModel none;
	QStandardItem* config = nullptr;
	QStandardItem* other = nullptr;
	QString path;
};

void settle()
{
	for (int i = 0; i < 12; ++i) {
		QCoreApplication::processEvents();
		QTest::qWait(60);
	}
}
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
		Control control;
		QQmlApplicationEngine engine;
		engine.rootContext()->setContextProperty(QStringLiteral("control"), &control);
		engine.load(QUrl(QStringLiteral("qrc:/MetadataTree.qml")));
		CHECK(!engine.rootObjects().isEmpty());
		auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
		CHECK(window && QTest::qWaitForWindowExposed(window));
		settle();
		const char* panels[] = {"detailsPanel", "batchItemPanel"};
		const auto rows = [&](const char* panel) {
			QVariant result;
			CHECK(QMetaObject::invokeMethod(window, "table", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QString::fromLatin1(panel))));
			return result.toList();
		};
		const QString dash = QString::fromUtf8("\xE2\x80\x93");
		const auto expectConfig = [&](const char* panel, const char* round) {
			const auto shown = rows(panel);
			std::fprintf(stderr, "%s %s: %d rows\n", panel, round, static_cast<int>(shown.size()));
			// One root and its three members, never more.
			CHECK(shown.size() == 4);
			const auto root = shown.at(0).toMap();
			CHECK(root.value("name").toString() == QLatin1String("Config"));
			CHECK(root.value("type").toString() == QLatin1String("struct (3)"));
			CHECK(root.value("depth").toInt() == 0 && root.value("expanded").toBool());
			const QStringList members{"Gain", "Enabled", "SamplePeriodMs"};
			for (int i = 0; i < 3; ++i) {
				const auto row = shown.at(i + 1).toMap();
				CHECK(row.value("name").toString() == members.at(i));
				CHECK(row.value("depth").toInt() == 1);
			}
			for (const auto& item : shown) {
				const auto row = item.toMap();
				// The Type column is shown, and "-" stands where a value would be;
				// the open root row shows nothing there (Details layout §9).
				CHECK(row.value("typeShown").toBool());
				CHECK(!row.value("type").toString().isEmpty());
				CHECK(row.value("value").toString() == (row.value("expanded").toBool() ? QString{} : dash));
			}
		};
		control.pick(QStringLiteral("Config"));
		settle();
		for (const char* panel : panels)
			expectConfig(panel, "first");
		control.pick(QStringLiteral("Counter"));
		settle();
		for (const char* panel : panels) {
			const auto shown = rows(panel);
			CHECK(shown.size() == 1 && shown.at(0).toMap().value("name").toString() == QLatin1String("Counter"));
			CHECK(shown.at(0).toMap().value("typeShown").toBool());
		}
		control.pick(QStringLiteral("Config"));
		settle();
		for (const char* panel : panels)
			expectConfig(panel, "again");
		control.pick(QStringLiteral("Counter"));
		settle();
		control.pick(QStringLiteral("Config"));
		settle();
		for (const char* panel : panels)
			expectConfig(panel, "third");
		std::printf("Metadata tree checks=%d warnings=%d\n", checks, warnings);
		return warnings == 0 ? 0 : 1;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Metadata tree check failed after %d checks: %s\n", checks, error.what());
		return 1;
	}
}

#include "MetadataTree.moc"
