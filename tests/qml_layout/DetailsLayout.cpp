// The Details layout spec's checks (§11, 1-16) and the action feedback's,
// in a real QQuickWindow: Details and the Batch over host-owned fake
// providers whose tables are real ValueModels made from a real descriptor -
// a structure-only state (resetShape) for a Field's values and a Service's
// response, a request editor's draft for a Command's arguments and a
// Service's request - and a Field's definition as Details' provider gives
// it. MIT.
#include <Telemetry/UX/DynamicModels.hpp>
#include <integration/inspector/SnapshotStore.hpp>
#include <integration/workbench/RequestEditor.hpp>
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QItemSelectionModel>
#include <QtCore/QPersistentModelIndex>
#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QTranslator>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QStandardItemModel>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlExpression>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
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

namespace ux = telemetry::ux;
namespace rt = telemetry::runtime;
namespace ins = telemetry::integration::inspector;
namespace wb = telemetry::integration::workbench;
using Category = telemetry::descriptor::Category;

// The demo device of the spec's mocks: device.Config, control.Reset,
// control.SetEnabled and control.Scale; and device.Table, a Field of about
// 2,000 rows for the cost of "*".
namespace demo {
struct Config {
	float Gain = 1.0f;
	bool Enabled = false;
	std::uint32_t SamplePeriodMs = 100;
};
struct SetEnabledArguments {
	bool enabled = false;
};
struct ScaleRequest {
	float value = 0;
	float factor = 0;
};
struct ScaleResult {
	float result = 0;
};
struct Device {
	Config config{};
	std::array<Config, 500> table{};
	Config read() const noexcept { return config; }
	telemetry::WriteResult write(const Config& value) noexcept
	{
		config = value;
		return telemetry::WriteResult::Applied;
	}
	std::array<Config, 500> readTable() const noexcept { return table; }
	telemetry::CommandResult reset() noexcept { return telemetry::CommandResult::Executed; }
	telemetry::CommandResult setEnabled(const SetEnabledArguments&) noexcept { return telemetry::CommandResult::Executed; }
	telemetry::ServiceResult<ScaleResult> scale(const ScaleRequest& request) noexcept
	{
		return telemetry::ServiceResult<ScaleResult>::success({request.value * request.factor});
	}
};
inline Device device;
inline constexpr telemetry::FieldTable deviceFields{
    telemetry::field<&Device::read, &Device::write>("Config", device),
    telemetry::field<&Device::readTable>("Table", device)};
inline constexpr telemetry::CommandTable controlCommands{
    telemetry::command<&Device::reset>("Reset", device),
    telemetry::command<&Device::setEnabled>("SetEnabled", device)};
inline constexpr telemetry::ServiceTable controlServices{telemetry::service<&Device::scale>("Scale", device)};
inline constexpr telemetry::FieldCatalogTable fields{telemetry::group("device", deviceFields)};
inline constexpr telemetry::CommandCatalogTable commands{telemetry::group("control", controlCommands)};
inline constexpr telemetry::ServiceCatalogTable services{telemetry::group("control", controlServices)};
inline constexpr telemetry::Model model{fields, commands, services};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();
} // namespace demo

// The descriptor parsed once: the schema the tables are made from.
struct Schema {
	std::array<std::byte, demo::descriptorBytes.size()> bytes = demo::descriptorBytes;
	std::array<rt::RuntimeType, 32> types{};
	std::array<rt::RuntimeMember, 32> members{};
	std::array<rt::RuntimeEnumEntry, 4> enums{};
	std::array<rt::RuntimeCatalog, 8> catalogs{};
	std::array<rt::RuntimeEndpoint, 8> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
	rt::RuntimeSchema schema;
	Schema()
	{
		const auto parsed = rt::RuntimeSchema::parse(bytes, workspace);
		CHECK(parsed);
		schema = parsed.schema;
	}
	const rt::RuntimeEndpoint& endpoint(Category category, const char* name) const
	{
		for (const auto& item : schema.endpoints(category))
			if (item.name == std::string_view{name})
				return item;
		throw std::runtime_error(std::string("no endpoint ") + name);
	}
};

// A Field's definition as Details' provider has it: the endpoint tree's
// rows with their types, filtered to the selected Field (the twin of
// EndpointDefinitionModel, as MetadataTree.cpp's).
class Definitions final : public QSortFilterProxyModel {
public:
	QPersistentModelIndex selected;
	void select(const QModelIndex& index)
	{
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
class EndpointTree final : public QStandardItemModel {
public:
	enum Role { Name = Qt::UserRole + 1, Detail, IsEndpoint, TypeText };
	QStandardItem* config = nullptr;
	EndpointTree()
	{
		setItemRoleNames({{Name, "name"}, {Detail, "detail"}, {IsEndpoint, "isEndpoint"}, {TypeText, "typeText"}});
		auto* group = add(nullptr, QStringLiteral("device"), QStringLiteral("1 Field"), false);
		config = add(group, QStringLiteral("Config"), QStringLiteral("struct (3)"), true);
		add(config, QStringLiteral("Gain"), QStringLiteral("f32"), false);
		add(config, QStringLiteral("Enabled"), QStringLiteral("bool"), false);
		add(config, QStringLiteral("SamplePeriodMs"), QStringLiteral("u32"), false);
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

// Details' provider as the panel reads it; every value a fixture sets.
class Provider final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString selection MEMBER selection NOTIFY stateChanged)
	Q_PROPERTY(QVariantMap selectionInfo MEMBER selectionInfo NOTIFY stateChanged)
	Q_PROPERTY(QVariantMap detailsStatus MEMBER detailsStatus NOTIFY stateChanged)
	Q_PROPERTY(QAbstractItemModel* values READ valueModel CONSTANT)
	Q_PROPERTY(QAbstractItemModel* responses READ responseModel CONSTANT)
	Q_PROPERTY(QAbstractItemModel* definitions READ definitionModel CONSTANT)
	Q_PROPERTY(QModelIndex definitionRootIndex READ definitionRootIndex NOTIFY stateChanged)
	Q_PROPERTY(bool connected MEMBER connected NOTIFY stateChanged)
	Q_PROPERTY(bool busy MEMBER busy NOTIFY stateChanged)
	Q_PROPERTY(bool editing MEMBER editing NOTIFY stateChanged)
	Q_PROPERTY(bool commandSelected MEMBER commandSelected NOTIFY stateChanged)
	Q_PROPERTY(bool serviceSelected MEMBER serviceSelected NOTIFY stateChanged)
	Q_PROPERTY(bool allowMutations MEMBER allowMutations NOTIFY stateChanged)
	Q_PROPERTY(bool canRefresh MEMBER canRefresh NOTIFY stateChanged)
	Q_PROPERTY(bool canApply MEMBER canApply NOTIFY stateChanged)
	Q_PROPERTY(bool canExecute MEMBER canExecute NOTIFY stateChanged)
	Q_PROPERTY(bool canCallService MEMBER canCallService NOTIFY stateChanged)
	Q_PROPERTY(bool canEdit MEMBER canEdit NOTIFY stateChanged)
	Q_PROPERTY(QString serviceResponseEndpoint MEMBER responseEndpoint NOTIFY stateChanged)
	Q_PROPERTY(QString valueNote MEMBER valueNote NOTIFY stateChanged)
	// The Batch item's own.
	Q_PROPERTY(int item MEMBER item NOTIFY stateChanged)
	Q_PROPERTY(QString operation MEMBER operation NOTIFY stateChanged)
	Q_PROPERTY(bool historical MEMBER historical NOTIFY stateChanged)
	Q_PROPERTY(bool switchable MEMBER switchable NOTIFY stateChanged)
public:
	explicit Provider(Definitions* shared) : definitions(shared) {}
	QAbstractItemModel* valueModel() { return &values; }
	QAbstractItemModel* responseModel() { return &responses; }
	QAbstractItemModel* definitionModel() { return definitions; }
	QModelIndex definitionRootIndex() const { return definitions ? definitions->rootIndex() : QModelIndex{}; }
	ux::ValueModel values, responses;
	Definitions* definitions = nullptr;
	QString selection, responseEndpoint, valueNote, operation;
	QVariantMap selectionInfo, detailsStatus;
	bool connected = true, busy = false, editing = false, commandSelected = false, serviceSelected = false;
	bool allowMutations = true, canRefresh = true, canApply = false, canExecute = true, canCallService = true, canEdit = true;
	bool historical = false, switchable = false;
	int item = 0, refreshes = 0, executes = 0, calls = 0;
	Q_INVOKABLE bool refresh() { ++refreshes; return true; }
	Q_INVOKABLE bool executeCommand() { ++executes; return true; }
	Q_INVOKABLE bool callService() { ++calls; return true; }
	Q_INVOKABLE bool apply() { return true; }
	Q_INVOKABLE void cancelEdit() {}
	Q_INVOKABLE bool setScalarText(const QModelIndex&, const QString&) { return true; }
	Q_INVOKABLE bool setOperation(const QString&) { return true; }
	Q_INVOKABLE void closeValue() {}
	void changed() { Q_EMIT stateChanged(); }
Q_SIGNALS:
	void stateChanged();
	void readFinished(bool published);
};

// The Batch's provider: a list of items and the selected item as Details
// shows it (another Provider).
class Batch final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel* batchItems READ batchItems CONSTANT)
	Q_PROPERTY(QObject* batchItem READ batchItem CONSTANT)
	Q_PROPERTY(int batchCount MEMBER count NOTIFY stateChanged)
	Q_PROPERTY(bool batchPending MEMBER pending NOTIFY stateChanged)
	Q_PROPERTY(QString batchStatus MEMBER status NOTIFY stateChanged)
	Q_PROPERTY(bool batchAttention MEMBER attention NOTIFY stateChanged)
	Q_PROPERTY(bool canExecuteBatch MEMBER canExecute NOTIFY stateChanged)
	Q_PROPERTY(int batchSelection MEMBER selected NOTIFY stateChanged)
	Q_PROPERTY(bool batchSupported MEMBER supported CONSTANT)
	Q_PROPERTY(bool busy MEMBER busy NOTIFY stateChanged)
	Q_PROPERTY(bool allowMutations MEMBER allowed NOTIFY stateChanged)
public:
	enum Role { Name = Qt::UserRole + 1, Operation, Status, HasValue, Historical, Line, Changed, Failed, Uid, Outcome };
	explicit Batch(Provider* shown) : item(shown)
	{
		items.setItemRoleNames({{Name, "name"}, {Operation, "operation"}, {Status, "status"}, {HasValue, "hasValue"},
		                        {Historical, "historical"}, {Line, "line"}, {Changed, "changed"}, {Failed, "failed"},
		                        {Uid, "uid"}, {Outcome, "outcome"}});
	}
	QAbstractItemModel* batchItems() { return &items; }
	QObject* batchItem() { return item; }
	QStandardItemModel items;
	Provider* item = nullptr;
	QString status;
	int count = 0, selected = -1;
	bool pending = false, attention = false, canExecute = true, supported = true, busy = false, allowed = true;
	std::function<void(int)> onSelect;
	void add(const QString& name, const QString& operation, const QString& line, const QString& outcome, bool failed = false)
	{
		auto* row = new QStandardItem;
		row->setData(name, Name);
		row->setData(operation, Operation);
		row->setData(outcome == QStringLiteral("–") ? QStringLiteral("Draft") : QStringLiteral("Completed"), Status);
		row->setData(false, HasValue);
		row->setData(false, Historical);
		row->setData(line, Line);
		row->setData(false, Changed);
		row->setData(failed, Failed);
		row->setData(QString::number(items.rowCount() + 1), Uid);
		row->setData(outcome, Outcome);
		items.appendRow(row);
		count = items.rowCount();
	}
	Q_INVOKABLE bool selectBatch(int row)
	{
		selected = row;
		if (onSelect)
			onSelect(row);
		Q_EMIT stateChanged();
		return true;
	}
	Q_INVOKABLE bool executeBatch() { return true; }
	Q_INVOKABLE bool clearBatch() { return true; }
	Q_INVOKABLE bool removeBatch(int) { return true; }
	Q_INVOKABLE bool moveBatch(int, int) { return true; }
	Q_INVOKABLE QVariantMap checkBatchPath(const QString&) const
	{
		return {{QStringLiteral("status"), QStringLiteral("empty")}, {QStringLiteral("message"), QString{}}};
	}
	Q_INVOKABLE QVariantList batchPathCandidates(const QString&) const { return {}; }
	Q_INVOKABLE QVariantMap addBatchPaths(const QStringList&)
	{
		return {{QStringLiteral("status"), QStringLiteral("refused")}, {QStringLiteral("message"), QString{}}};
	}
	void changed() { Q_EMIT stateChanged(); }
Q_SIGNALS:
	void stateChanged();
};

// The fixtures: what each kind of endpoint shows in each state, with the
// mocks' words and values.
struct Fixtures {
	Schema schema;
	EndpointTree tree;
	Definitions definitions;
	std::array<std::byte, 4096> requestMemory{}, draftMemory{}, commitMemory{};
	ins::SnapshotStore requestStore{requestMemory};
	wb::RequestEditor editor{draftMemory, commitMemory};
	Fixtures() { definitions.setSourceModel(&tree); }

	static QVariantMap status(const QString& text, bool failed = false)
	{
		return {{QStringLiteral("text"), text}, {QStringLiteral("failed"), failed}, {QStringLiteral("tip"), QString{}}};
	}
	telemetry::TypeId fieldType(const char* name) const { return schema.endpoint(Category::Field, name).typeId; }
	// A request's draft: the bytes given, editable as Details' request is.
	void draft(ux::ValueModel& values, Category category, const char* name, const QByteArray& bytes)
	{
		const auto& endpoint = schema.endpoint(category, name);
		CHECK(requestStore.admit(schema.schema, 17) == ins::ModelStatus::Ok);
		CHECK(requestStore.selectEndpoint(endpoint.id, endpoint.typeId, category) == ins::ModelStatus::Ok);
		const auto identity = requestStore.snapshotInfo();
		CHECK(requestStore.publish(rt::Input{reinterpret_cast<const std::byte*>(bytes.constData()), std::size_t(bytes.size())},
		                           identity.admission, identity.revision) == ins::ModelStatus::Ok);
		CHECK(editor.begin(requestStore) == rt::Status::Ok);
		values.setSchemaMetadata(schema.schema);
		values.setSection(ux::ValueModel::Section::Request);
		values.resetDraft(editor);
	}
	static QByteArray floats(float a, float b)
	{
		QByteArray out(8, '\0');
		std::memcpy(out.data(), &a, 4);
		std::memcpy(out.data() + 4, &b, 4);
		return out;
	}
	void reset(Provider& p)
	{
		p.busy = p.editing = p.commandSelected = p.serviceSelected = false;
		p.allowMutations = true;
		p.valueNote.clear();
		p.responseEndpoint.clear();
		p.responses.clear();
		p.definitions->select({});
	}
	void field(Provider& p, bool read, const QString& status = {})
	{
		reset(p);
		p.selection = QStringLiteral("device.Config");
		p.selectionInfo = {{QStringLiteral("detail"), QStringLiteral("Field · struct (3) · 9 bytes · read and write")},
		                   {QStringLiteral("typeText"), QStringLiteral("struct (3)")}, {QStringLiteral("wireBytes"), 9},
		                   {QStringLiteral("readable"), true}, {QStringLiteral("writable"), true}};
		p.detailsStatus = Fixtures::status(status.isEmpty() ? (read ? QStringLiteral("Read 22:35:47") : QStringLiteral("Not read yet"))
		                                                      : status);
		if (read) {
			p.values.resetShape(schema.schema, fieldType("Config"), ux::ValueModel::Section::Value,
			                    {{QStringLiteral("Gain"), QStringLiteral("1")}, {QStringLiteral("Enabled"), QStringLiteral("false")},
			                     {QStringLiteral("SamplePeriodMs"), QStringLiteral("100")}});
		} else {
			p.values.clear();
			p.definitions->select(tree.config->index());
		}
		p.changed();
	}
	void table(Provider& p)
	{
		reset(p);
		p.selection = QStringLiteral("device.Table");
		p.selectionInfo = {{QStringLiteral("detail"), QStringLiteral("Field · Config[500] · 4,500 bytes · read only")},
		                   {QStringLiteral("typeText"), QStringLiteral("struct (3)[500]")}, {QStringLiteral("wireBytes"), 4500},
		                   {QStringLiteral("readable"), true}, {QStringLiteral("writable"), false}};
		p.detailsStatus = Fixtures::status(QStringLiteral("Read 22:35:47"));
		QHash<QString, QString> shown;
		for (int at = 0; at < 500; ++at)
			shown.insert(QStringLiteral("[%1].Gain").arg(at), QString::number(at));
		p.values.resetShape(schema.schema, fieldType("Table"), ux::ValueModel::Section::Value, shown);
		p.changed();
	}
	void commandNone(Provider& p)
	{
		reset(p);
		p.selection = QStringLiteral("control.Reset");
		p.commandSelected = true;
		p.selectionInfo = {{QStringLiteral("detail"), QStringLiteral("Command · ()")}, {QStringLiteral("typeText"), QStringLiteral("()")},
		                   {QStringLiteral("wireBytes"), 0}};
		p.detailsStatus = Fixtures::status(QStringLiteral("Not executed yet"));
		p.values.clear();
		p.changed();
	}
	void commandArguments(Provider& p)
	{
		reset(p);
		p.selection = QStringLiteral("control.SetEnabled");
		p.commandSelected = true;
		p.selectionInfo = {{QStringLiteral("detail"), QStringLiteral("Command · (bool) · 1-byte argument")},
		                   {QStringLiteral("typeText"), QStringLiteral("(bool)")}, {QStringLiteral("wireBytes"), 1}};
		p.detailsStatus = Fixtures::status(QStringLiteral("Executed 22:40:01 · accepted"));
		draft(p.values, Category::Command, "SetEnabled", QByteArray(1, '\1'));
		p.changed();
	}
	// before, after or failed (an earlier response kept, stale).
	void service(Provider& p, const QString& state)
	{
		reset(p);
		p.selection = QStringLiteral("control.Scale");
		p.serviceSelected = true;
		p.selectionInfo = {{QStringLiteral("detail"), QStringLiteral("Service · (f32, f32) -> (f32) · 8-byte request")},
		                   {QStringLiteral("typeText"), QStringLiteral("(f32, f32) -> (f32)")}, {QStringLiteral("wireBytes"), 8},
		                   {QStringLiteral("responseTypeText"), QStringLiteral("(f32)")}, {QStringLiteral("responseWireBytes"), 4}};
		const bool before = state == QLatin1String("before");
		p.detailsStatus = before ? Fixtures::status(QStringLiteral("Not called yet"))
		                  : state == QLatin1String("after") ? Fixtures::status(QStringLiteral("Called 22:36:19 · took 19 ms"))
		                                                    : Fixtures::status(QStringLiteral("Call failed 22:41:07 · no reply in 1 s"), true);
		draft(p.values, Category::Service, "Scale", before ? floats(0, 0) : floats(2, 0.5f));
		const auto& scale = schema.endpoint(Category::Service, "Scale");
		p.responses.resetShape(schema.schema, scale.responseTypeId, ux::ValueModel::Section::Response,
		                       before ? QHash<QString, QString>{} : QHash<QString, QString>{{QStringLiteral("result"), QStringLiteral("1")}});
		if (state == QLatin1String("failed"))
			p.responses.setStale(true, QStringLiteral("From the call at 22:36:19."));
		p.changed();
	}
};

void settle(int rounds = 10)
{
	for (int i = 0; i < rounds; ++i) {
		QCoreApplication::processEvents();
		QTest::qWait(25);
	}
}
QQuickItem* find(QQuickItem* root, const QString& name, bool visibleOnly = true)
{
	if (!root || (visibleOnly && !root->isVisible()))
		return nullptr;
	if (root->objectName() == name)
		return root;
	for (auto* child : root->childItems())
		if (auto* found = find(child, name, visibleOnly))
			return found;
	return nullptr;
}
void collect(QQuickItem* root, const QString& name, QList<QQuickItem*>& out, bool visibleOnly = true)
{
	if (!root || (visibleOnly && !root->isVisible()))
		return;
	if (root->objectName() == name && (!visibleOnly || (root->width() > 0 && root->height() > 0)))
		out.append(root);
	for (auto* child : root->childItems())
		collect(child, name, out, visibleOnly);
}
QList<QQuickItem*> all(QQuickItem* root, const QString& name)
{
	QList<QQuickItem*> out;
	collect(root, name, out);
	std::sort(out.begin(), out.end(), [](QQuickItem* a, QQuickItem* b) {
		return a->mapToScene(QPointF{}).y() < b->mapToScene(QPointF{}).y();
	});
	return out;
}
QQuickItem* need(QQuickItem* root, const char* name)
{
	auto* found = find(root, QString::fromLatin1(name));
	if (!found)
		throw std::runtime_error(std::string("no visible ") + name);
	return found;
}
QRectF in(QQuickItem* item, QQuickItem* reference)
{
	return item->mapRectToItem(reference, QRectF{0, 0, item->width(), item->height()});
}
bool near(qreal a, qreal b, qreal tolerance = 0.75) { return std::abs(a - b) <= tolerance; }
QColor color(QQuickItem* item, const char* name = "color") { return item->property(name).value<QColor>(); }
QColor fillOf(QQuickItem* row)
{
	auto* background = qvariant_cast<QQuickItem*>(row->property("background"));
	return background ? background->property("color").value<QColor>() : QColor{};
}
QQuickItem* rowNamed(QQuickItem* root, const char* rows, const QString& name)
{
	for (auto* row : all(root, QString::fromLatin1(rows)))
		if (row->property("cellName").toString() == name)
			return row;
	return nullptr;
}
void click(QQuickWindow* window, QQuickItem* item)
{
	const auto point = item->mapToScene(QPointF{item->width() / 2, item->height() / 2}).toPoint();
	QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
	settle(4);
}
void shot(QQuickWindow* window, const QString& directory, const QString& name)
{
	CHECK(window->grabWindow().save(QDir(directory).filePath(name + QStringLiteral(".png"))));
}

// Every string 40 % longer, as a translation into a longer language would be.
class LongerWords final : public QTranslator {
public:
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		if (text.isEmpty())
			return {};
		return text + QString(int(std::ceil(text.size() * 0.4)), QChar(0x00e5));
	}
	bool isEmpty() const override { return false; }
};
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
	const auto output = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
	try {
		Fixtures fixtures;
		Provider details{&fixtures.definitions};
		Definitions itemDefinitions;
		itemDefinitions.setSourceModel(&fixtures.tree);
		Provider itemProvider{&itemDefinitions};
		Batch batch{&itemProvider};
		QQmlApplicationEngine engine;
		engine.rootContext()->setContextProperty(QStringLiteral("detailsProvider"), &details);
		engine.rootContext()->setContextProperty(QStringLiteral("batchProvider"), &batch);
		fixtures.field(details, true);
		engine.load(QUrl(QStringLiteral("qrc:/DetailsLayout.qml")));
		CHECK(!engine.rootObjects().isEmpty());
		auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
		CHECK(window && QTest::qWaitForWindowExposed(window));
		settle();
		auto* theme = window->property("theme").value<QObject*>();
		auto* type = window->property("type").value<QObject*>();
		CHECK(theme && type);
		const auto token = [&](const char* name) { return theme->property(name).value<QColor>(); };
		const auto size = [&](const char* name) { return type->property(name).toReal(); };
		auto* panel = need(window->contentItem(), "details");
		const qreal baseBody = size("body");

		// The blocks of the panel as the checks see them, mapped to its body column.
		struct Block {
			QString name;
			QRectF rect;
		};
		const auto blocks = [&](QQuickItem* scope) {
			auto* column = need(scope, "endpointBodyColumn");
			QList<Block> out;
			const auto add = [&](const char* name, QQuickItem* item) {
				if (item && item->isVisible() && item->height() > 0)
					out.append({QString::fromLatin1(name), in(item, column)});
			};
			add("title", find(scope, QStringLiteral("endpointSelectionTitle")));
			add("line2", find(scope, QStringLiteral("endpointSelectionInfo")));
			add("status", find(scope, QStringLiteral("endpointStatusLine")));
			add("strip", find(scope, QStringLiteral("endpointRefusal")));
			add("valueHeading", find(scope, QStringLiteral("valueTableHeader")));
			add("quiet", find(scope, QStringLiteral("noArgumentsLabel")));
			add("valueTree", find(scope, QStringLiteral("valueTableSlot")));
			add("metadataTree", find(scope, QStringLiteral("metadataTableSlot")));
			add("valueNote", find(scope, QStringLiteral("valueSectionCaption")));
			add("responseHeading", find(scope, QStringLiteral("responseTableHeader")));
			add("responseQuiet", find(scope, QStringLiteral("noResponseLabel")));
			add("responseTree", find(scope, QStringLiteral("responseTableSlot")));
			add("responseNote", find(scope, QStringLiteral("responseStaleNote")));
			return out;
		};
		struct Named {
			const char* name;
			std::function<void()> show;
		};
		const QList<Named> kinds{
		    {"field", [&] { fixtures.field(details, true); }},
		    {"field-unread", [&] { fixtures.field(details, false); }},
		    {"command-none", [&] { fixtures.commandNone(details); }},
		    {"command-args", [&] { fixtures.commandArguments(details); }},
		    {"service-before", [&] { fixtures.service(details, QStringLiteral("before")); }},
		    {"service-after", [&] { fixtures.service(details, QStringLiteral("after")); }},
		    {"service-failed", [&] { fixtures.service(details, QStringLiteral("failed")); }}};
		const auto showKind = [&](const Named& kind) {
			kind.show();
			settle();
		};

		const auto layoutChecks = [&](bool exact) {
			// 1. Height does not matter; 2. fixed gaps, consecutive tops.
			for (const auto& kind : kinds) {
				window->setWidth(560);
				QList<QList<Block>> seen;
				for (const int height : {300, 700, 1400}) {
					window->setHeight(height);
					showKind(kind);
					seen.append(blocks(panel));
				}
				CHECK(seen[0].size() >= 4);
				for (int at = 1; at < seen.size(); ++at) {
					CHECK(seen[at].size() == seen[0].size());
					for (int b = 0; b < seen[0].size(); ++b)
						CHECK(seen[at][b].name == seen[0][b].name && near(seen[at][b].rect.top(), seen[0][b].rect.top()));
				}
				const auto& shown = seen[1];
				const auto top = [&](const QString& name) {
					for (const auto& block : shown)
						if (block.name == name)
							return block.rect;
					return QRectF{};
				};
				CHECK(near(top(QStringLiteral("title")).top(), size("groupSpacing")));
				CHECK(near(top(QStringLiteral("line2")).top(), top(QStringLiteral("title")).bottom() + size("tight")));
				CHECK(near(top(QStringLiteral("status")).top(), top(QStringLiteral("line2")).bottom() + size("tight")));
				const auto above = top(QStringLiteral("strip")).isNull() ? top(QStringLiteral("status")) : top(QStringLiteral("strip"));
				CHECK(near(top(QStringLiteral("valueHeading")).top(), above.bottom() + size("sectionSpacing")));
				// No gap between two consecutive blocks exceeds sectionSpacing;
				// blocks never overlap.
				QList<QRectF> ordered;
				for (const auto& block : shown)
					ordered.append(block.rect);
				std::sort(ordered.begin(), ordered.end(), [](const QRectF& a, const QRectF& b) { return a.top() < b.top(); });
				for (int at = 1; at < ordered.size(); ++at) {
					CHECK(ordered[at].top() >= ordered[at - 1].bottom() - 0.75);
					CHECK(ordered[at].top() - ordered[at - 1].bottom() <= size("sectionSpacing") + 0.75);
				}
				if (!top(QStringLiteral("responseHeading")).isNull()) {
					const auto last = !top(QStringLiteral("valueNote")).isNull() ? top(QStringLiteral("valueNote"))
					                  : !top(QStringLiteral("valueTree")).isNull() ? top(QStringLiteral("valueTree"))
					                                                               : top(QStringLiteral("quiet"));
					CHECK(near(top(QStringLiteral("responseHeading")).top(), last.bottom() + size("sectionSpacing")));
				}
				// The pane's content ends at the last block and the bottom padding.
				auto* body = need(panel, "endpointBodyScroll");
				CHECK(near(body->property("contentHeight").toReal(), ordered.last().bottom() + size("groupSpacing")));
				if (exact && QString::fromLatin1(kind.name) == QStringLiteral("service-after")) {
					std::printf("Details layout service-after tops: title %g line2 %g status %g request %g rows %g note %g response %g "
					            "row %g content %g (caption line %g)\n",
					            top(QStringLiteral("title")).top(), top(QStringLiteral("line2")).top(), top(QStringLiteral("status")).top(),
					            top(QStringLiteral("valueHeading")).top(), top(QStringLiteral("valueTree")).top(),
					            top(QStringLiteral("valueNote")).top(), top(QStringLiteral("responseHeading")).top(),
					            top(QStringLiteral("responseTree")).top(), body->property("contentHeight").toReal(),
					            top(QStringLiteral("line2")).height());
					std::fflush(stdout);
					// The spec's numbers at 100 %: 8, 35, 54, 86, 108, 155, 187,
					// 209, content 239 with Segoe UI's 16 px caption line; with
					// another font's caption line c the same sums.
					const auto c = top(QStringLiteral("line2")).height();
					CHECK(c >= 12 && c <= 18);
					CHECK(near(top(QStringLiteral("title")).top(), 8) && near(top(QStringLiteral("line2")).top(), 35));
					CHECK(near(top(QStringLiteral("status")).top(), 38 + c) && near(top(QStringLiteral("valueHeading")).top(), 54 + 2 * c));
					CHECK(near(top(QStringLiteral("valueTree")).top(), 76 + 2 * c) && near(top(QStringLiteral("valueNote")).top(), 123 + 2 * c));
					CHECK(near(top(QStringLiteral("responseHeading")).top(), 139 + 3 * c) &&
					      near(top(QStringLiteral("responseTree")).top(), 161 + 3 * c));
					CHECK(near(body->property("contentHeight").toReal(), 191 + 3 * c));
				}
			}
			// 4. One arrangement: Request above Response at every width, the
			// heading rows on the pane's full width, Value and Type at one x.
			fixtures.service(details, QStringLiteral("after"));
			window->setHeight(700);
			for (const int width : {360, 560, 900, 1200}) {
				window->setWidth(width);
				settle();
				auto* column = need(panel, "endpointBodyColumn");
				auto* request = need(panel, "valueTableHeader");
				auto* response = need(panel, "responseTableHeader");
				const auto requestRect = in(request, column), responseRect = in(response, column);
				CHECK(near(requestRect.left(), 0) && near(responseRect.left(), 0));
				CHECK(near(requestRect.width(), responseRect.width()) && near(requestRect.width(), column->width()));
				CHECK(responseRect.top() > in(need(panel, "valueSectionCaption"), column).bottom());
				const auto labelX = [&](QQuickItem* heading, const char* name) {
					return in(need(heading, name), column).left();
				};
				CHECK(near(labelX(request, "tableHeadingValue"), labelX(response, "tableHeadingValue")));
				CHECK(near(labelX(request, "tableHeadingType"), labelX(response, "tableHeadingType")));
				// 7. Grid lines: everything on L and R.
				const auto left = size("padding");
				CHECK(near(in(need(panel, "endpointSelectionTitle"), column).left(), left));
				CHECK(near(in(need(panel, "endpointSelectionInfo"), column).left(), left));
				CHECK(near(in(need(panel, "endpointStatusLine"), column).left(), left));
				CHECK(near(in(need(request, "tableHeadingName"), column).left(), left));
				const auto right = column->width() - size("padding");
				CHECK(near(in(need(panel, "callServiceButton"), column).right(), right));
				CHECK(near(in(need(request, "tableHeadingType"), column).right(), right));
				auto* row = all(panel, QStringLiteral("valueRow")).value(0);
				CHECK(row);
				auto* fill = qvariant_cast<QQuickItem*>(row->property("background"));
				CHECK(fill && near(in(fill, column).left(), 0) && near(in(fill, column).width(), column->width()));
				CHECK(near(column->width(), panel->width()));
			}
			// 6. No arguments.
			fixtures.commandNone(details);
			window->setWidth(560);
			settle();
			{
				auto* column = need(panel, "endpointBodyColumn");
				CHECK(need(panel, "endpointSelectionInfo")->property("text").toString() == QStringLiteral("Command · ()"));
				auto* heading = need(panel, "valueTableHeader");
				CHECK(need(heading, "tableHeadingName")->property("text").toString() == QStringLiteral("Arguments"));
				CHECK(!find(heading, QStringLiteral("tableHeadingValue")) && !find(heading, QStringLiteral("tableHeadingType")));
				CHECK(!find(heading, QStringLiteral("tableHeadingRule")));
				auto* quiet = need(panel, "noArgumentsLabel");
				CHECK(quiet->property("text").toString() == QStringLiteral("No arguments."));
				CHECK(near(in(quiet, column).top(), in(heading, column).bottom()));
				CHECK(near(quiet->height(), size("rowHeight")));
				CHECK(near(in(quiet, column).left(), size("padding") + size("indent")));
			}
		};
		layoutChecks(true);

		// 3. Tables at full height - each table's slot as tall as all its
		// rows, the table a window over it no taller than the pane (the
		// fallback check 15 chose) - and only the pane scrolls.
		fixtures.service(details, QStringLiteral("after"));
		window->setWidth(560);
		window->setHeight(700);
		settle();
		for (const char* name : {"valueTree", "responseTree"}) {
			auto* tree = need(panel, name);
			auto* slot = tree->parentItem();
			auto* body = need(panel, "endpointBodyScroll");
			CHECK(slot && near(slot->height(), tree->property("contentHeight").toReal()));
			CHECK(near(tree->height(), std::min(tree->property("contentHeight").toReal(), body->height())));
			CHECK(!tree->property("interactive").toBool());
			for (auto* child : tree->findChildren<QQuickItem*>())
				CHECK(QString::fromLatin1(child->metaObject()->className()).indexOf(QStringLiteral("ScrollBar")) < 0);
		}
		{
			// Down through every row of a long table in a 200 px pane: the
			// current row stays in view.
			fixtures.table(details);
			window->setHeight(200);
			settle();
			auto* tree = need(panel, "valueTree");
			auto* body = need(panel, "endpointBodyScroll");
			CHECK(body->property("interactive").toBool());
			tree->forceActiveFocus(Qt::TabFocusReason);
			settle(4);
			for (int step = 0; step < 12; ++step) {
				QTest::keyClick(window, Qt::Key_Down);
				settle(2);
				const int row = tree->property("currentRow").toInt();
				QQuickItem* current = nullptr;
				CHECK(QMetaObject::invokeMethod(tree, "itemAtCell", Q_RETURN_ARG(QQuickItem*, current), Q_ARG(QPoint, QPoint(0, row))));
				CHECK(current);
				const auto rect = in(current, body);
				CHECK(rect.top() >= -0.75 && rect.bottom() <= body->height() + 0.75);
			}
			// An editor opened below the fold scrolls into view.
			fixtures.service(details, QStringLiteral("after"));
			window->setHeight(140);
			settle();
			body->setProperty("contentY", 0);
			settle(2);
			auto* factor = rowNamed(panel, "valueRow", QStringLiteral("factor"));
			CHECK(factor);
			CHECK(in(factor, body).bottom() > body->height());
			auto* cell = qvariant_cast<QQuickItem*>(factor->property("valueItem"));
			CHECK(cell && cell->property("canOpen").toBool());
			QVariant opened;
			CHECK(QMetaObject::invokeMethod(cell, "begin", Q_RETURN_ARG(QVariant, opened)) && opened.toBool());
			settle(4);
			CHECK(body->property("contentY").toReal() > 0);
			CHECK(in(factor, body).bottom() <= body->height() + 0.75 && in(factor, body).top() >= -0.75);
			QMetaObject::invokeMethod(cell, "revert");
			settle(2);
		}

		// 5. Structure, never a placeholder.
		window->setHeight(700);
		window->setWidth(560);
		for (const auto& kind : kinds) {
			showKind(kind);
			CHECK(!find(window->contentItem(), QStringLiteral("emptyValueLabel"), false));
			CHECK(!find(window->contentItem(), QStringLiteral("emptyResponseLabel"), false));
			int said = 0;
			for (const auto& words : {QStringLiteral("Not read yet"), QStringLiteral("Not called yet"), QStringLiteral("Not executed yet")}) {
				QVariant count;
				CHECK(QMetaObject::invokeMethod(window, "countLabels", Q_RETURN_ARG(QVariant, count), Q_ARG(QVariant, words)));
				CHECK(count.toInt() <= 1);
				said += count.toInt();
			}
			const auto status = need(panel, "endpointStatusLine")->property("text").toString();
			CHECK(said == (status.startsWith(QStringLiteral("Not ")) ? 1 : 0));
			QVariant callToSee;
			CHECK(QMetaObject::invokeMethod(window, "countLabels", Q_RETURN_ARG(QVariant, callToSee), Q_ARG(QVariant, QStringLiteral("Call to see"))));
			CHECK(callToSee.toInt() == 0);
		}
		fixtures.field(details, false);
		settle();
		{
			auto* tree = need(panel, "metadataTree");
			CHECK(tree->property("rows").toInt() == 4);
			const auto rows = all(panel, QStringLiteral("endpointRow"));
			CHECK(rows.size() == 4 && rows[0]->property("cellType").toString() == QStringLiteral("struct (3)"));
			for (int at = 1; at < rows.size(); ++at) {
				auto* value = need(rows[at], "metadataValueLabel");
				CHECK(value->property("text").toString() == QStringLiteral("–") && color(value) == token("textMuted"));
			}
		}
		fixtures.service(details, QStringLiteral("before"));
		settle();
		{
			auto* tree = need(panel, "responseTree");
			CHECK(tree->property("rows").toInt() == 1);
			auto* row = all(panel, QStringLiteral("responseRow")).value(0);
			CHECK(row && row->property("cellName").toString() == QStringLiteral("result"));
			CHECK(row->property("cellValue").toString() == QStringLiteral("–") &&
			      row->property("cellType").toString() == QStringLiteral("f32"));
			auto* value = need(row, "valueText");
			CHECK(color(value) == token("textMuted"));
		}

		// 9. Failure: the glyph, the words in the primary colour, the stale
		// footnote under the response, the value italic and muted.
		fixtures.service(details, QStringLiteral("failed"));
		settle();
		{
			CHECK(need(panel, "endpointStatusGlyph"));
			CHECK(color(need(panel, "endpointStatusLine")) == token("textPrimary"));
			auto* note = need(panel, "responseStaleNote");
			CHECK(note->property("text").toString() == QStringLiteral("From the call at 22:36:19."));
			auto* slot = need(panel, "responseTableSlot");
			auto* column = need(panel, "endpointBodyColumn");
			CHECK(near(in(note, column).top(), in(slot, column).bottom() + size("tight")));
			auto* value = need(all(panel, QStringLiteral("responseRow")).value(0), "valueText");
			CHECK(value->property("font").value<QFont>().italic() && color(value) == token("textMuted"));
		}

		// 8. Selection: none until the user makes one.
		fixtures.field(details, true);
		settle();
		{
			auto* tree = need(panel, "valueTree");
			auto* selection = tree->property("selectionModel").value<QItemSelectionModel*>();
			CHECK(selection && !selection->currentIndex().isValid());
			for (auto* row : all(panel, QStringLiteral("valueRow"))) {
				CHECK(fillOf(row) != token("stateSelected") && fillOf(row) != token("stateSelectedInactive"));
				QList<QQuickItem*> guides;
				collect(row, QStringLiteral("indentGuide"), guides);
				for (auto* guide : guides)
					CHECK(color(guide) == token("outlineVariant"));
			}
			// Tab from Read into the tree: its first row current, with the ring.
			auto* read = need(panel, "refreshButton");
			read->forceActiveFocus(Qt::TabFocusReason);
			settle(2);
			QTest::keyClick(window, Qt::Key_Tab);
			settle(4);
			CHECK(tree->hasActiveFocus() && selection->currentIndex().isValid());
			auto* first = all(panel, QStringLiteral("valueRow")).value(0);
			CHECK(first && fillOf(first) == token("stateSelected") && need(first, "detailFocusRing")->isVisible());
			// A click on Gain: its fill and the ring.
			auto* gain = rowNamed(panel, "valueRow", QStringLiteral("Gain"));
			click(window, need(gain, "detailCellName"));
			CHECK(gain->property("current").toBool() && fillOf(gain) == token("stateSelected"));
			CHECK(need(gain, "detailFocusRing")->isVisible());
			// The focus elsewhere: the inactive fill, no ring, text on it.
			read->forceActiveFocus(Qt::TabFocusReason);
			settle(2);
			CHECK(fillOf(gain) == token("stateSelectedInactive") && !find(gain, QStringLiteral("detailFocusRing")));
			CHECK(color(need(gain, "detailCellName")) == token("selectedForeground"));
			// A Read (a model reset) keeps Gain current.
			fixtures.field(details, true);
			settle();
			CHECK(rowNamed(panel, "valueRow", QStringLiteral("Gain"))->property("current").toBool());
			// Another Field and back: none current.
			fixtures.commandNone(details);
			settle();
			details.selection = QStringLiteral("device.Other");
			details.changed();
			settle(2);
			fixtures.field(details, true);
			settle();
			for (auto* row : all(panel, QStringLiteral("valueRow")))
				CHECK(!row->property("current").toBool());
		}

		// The action's result seen (the addendum): busy words, the time and
		// a changed value flashing once, an unchanged value not.
		{
			fixtures.field(details, true, QStringLiteral("Read 22:35:47 · took 9 ms"));
			settle();
			auto* read = need(panel, "refreshButton");
			const int flashes = panel->property("statusFlashes").toInt();
			click(window, read);
			CHECK(details.refreshes >= 1);
			details.busy = true;
			details.detailsStatus = Fixtures::status(QStringLiteral("Reading…"));
			details.changed();
			settle(2);
			CHECK(read->property("text").toString() == QStringLiteral("Reading…") && !read->isEnabled());
			CHECK(need(panel, "endpointStatusLine")->property("text").toString() == QStringLiteral("Reading…"));
			// Done at the same values: the new time flashes, no cell does.
			details.values.noteUpdate();
			fixtures.field(details, true, QStringLiteral("Read 22:35:48 · took 7 ms"));
			details.busy = false;
			details.changed();
			settle(2);
			CHECK(panel->property("statusFlashes").toInt() == flashes + 1 && panel->property("statusFlashing").toBool());
			CHECK(read->property("text").toString() == QStringLiteral("Read") && read->isEnabled());
			for (auto* cell : all(panel, QStringLiteral("valueCell")))
				CHECK(cell->property("flashes").toInt() == 0);
			// Gain changed on the device: Gain flashes, Enabled does not.
			details.busy = true;
			details.changed();
			settle(2);
			details.values.noteUpdate();
			details.values.resetShape(fixtures.schema.schema, fixtures.fieldType("Config"), ux::ValueModel::Section::Value,
			                          {{QStringLiteral("Gain"), QStringLiteral("2.5")}, {QStringLiteral("Enabled"), QStringLiteral("false")},
			                           {QStringLiteral("SamplePeriodMs"), QStringLiteral("100")}});
			details.detailsStatus = Fixtures::status(QStringLiteral("Read 22:35:49 · took 8 ms"));
			details.busy = false;
			details.changed();
			settle(4);
			const auto cellOf = [&](const QString& name) {
				for (auto* cell : all(panel, QStringLiteral("valueCell")))
					if (cell->property("scalarName").toString() == name)
						return cell;
				return static_cast<QQuickItem*>(nullptr);
			};
			CHECK(cellOf(QStringLiteral("Gain")) && cellOf(QStringLiteral("Gain"))->property("flashes").toInt() == 1);
			CHECK(cellOf(QStringLiteral("Enabled"))->property("flashes").toInt() == 0);
			CHECK(panel->property("statusFlashes").toInt() == flashes + 2);
			// Reduced motion: the status line changes, nothing flashes.
			panel->setProperty("reducedMotion", true);
			details.busy = true;
			details.changed();
			settle(2);
			details.values.noteUpdate();
			details.values.resetShape(fixtures.schema.schema, fixtures.fieldType("Config"), ux::ValueModel::Section::Value,
			                          {{QStringLiteral("Gain"), QStringLiteral("3")}, {QStringLiteral("Enabled"), QStringLiteral("false")},
			                           {QStringLiteral("SamplePeriodMs"), QStringLiteral("100")}});
			details.detailsStatus = Fixtures::status(QStringLiteral("Read 22:35:50 · took 8 ms"));
			details.busy = false;
			details.changed();
			settle(4);
			CHECK(need(panel, "endpointStatusLine")->property("text").toString().startsWith(QStringLiteral("Read 22:35:50")));
			CHECK(panel->property("statusFlashes").toInt() == flashes + 2);
			CHECK(cellOf(QStringLiteral("Gain"))->property("flashes").toInt() == 0);
			panel->setProperty("reducedMotion", false);
			// A Command and a Service say what they are doing too.
			fixtures.commandArguments(details);
			details.busy = true;
			details.changed();
			settle(2);
			CHECK(need(panel, "executeButton")->property("text").toString() == QStringLiteral("Executing…"));
			CHECK(!need(panel, "executeButton")->isEnabled());
			fixtures.service(details, QStringLiteral("after"));
			details.busy = true;
			details.changed();
			settle(2);
			CHECK(need(panel, "callServiceButton")->property("text").toString() == QStringLiteral("Calling…"));
			details.busy = false;
			details.changed();
			settle(2);
		}

		// 15. The cost of "*" on a table of 2,000 rows: the next frame.
		{
			fixtures.table(details);
			window->setHeight(700);
			settle();
			auto* tree = need(panel, "valueTree");
			tree->forceActiveFocus(Qt::TabFocusReason);
			settle(4);
			QSignalSpy frames(window, &QQuickWindow::frameSwapped);
			QElapsedTimer clock;
			clock.start();
			QTest::keyClick(window, Qt::Key_Asterisk);
			const bool framed = frames.wait(2000);
			const auto elapsed = clock.elapsed();
			settle(4);
			const int rows = tree->property("rows").toInt();
			std::printf("Details layout cost of * rows=%d next frame=%lldms framed=%d\n", rows, static_cast<long long>(elapsed), framed);
			CHECK(rows >= 1900);
			CHECK(elapsed <= 200);
		}

		// 16. Screenshots, and the same at 125 %.
		const auto shots = [&](const QString& suffix) {
			window->setHeight(520);
			for (const auto& kind : kinds) {
				for (const int width : {360, 560}) {
					window->setWidth(width);
					showKind(kind);
					shot(window, output, QStringLiteral("details-%1-%2%3").arg(QString::fromLatin1(kind.name)).arg(width).arg(suffix));
				}
			}
		};
		shots(QString{});

		// The Batch with its three items (10-13).
		const auto showItem = [&](int row) {
			itemProvider.item = row + 1;
			itemProvider.switchable = false;
			if (row == 2) {
				fixtures.reset(itemProvider);
				itemProvider.selection = QStringLiteral("device.Config");
				itemProvider.operation = QStringLiteral("read");
				itemProvider.switchable = true;
				itemProvider.selectionInfo = {{QStringLiteral("detail"), QStringLiteral("Field · struct (3) · 9 bytes · read and write")},
				                              {QStringLiteral("typeText"), QStringLiteral("struct (3)")}, {QStringLiteral("wireBytes"), 9},
				                              {QStringLiteral("writable"), false}};
				itemProvider.detailsStatus = Fixtures::status(batch.status.startsWith(QStringLiteral("Executed"))
				                                              ? QStringLiteral("Read 14:02:11 · in the Batch")
				                                              : QStringLiteral("Not read yet · Execute Batch reads it"));
				itemProvider.values.clear();
				itemDefinitions.select(fixtures.tree.config->index());
			} else if (row == 1) {
				fixtures.commandNone(itemProvider);
				itemProvider.operation = QStringLiteral("command");
				itemProvider.detailsStatus = Fixtures::status(QStringLiteral("Not executed yet · Execute Batch runs it"));
			} else {
				fixtures.service(itemProvider, batch.status.startsWith(QStringLiteral("Executed")) ? QStringLiteral("after") : QStringLiteral("before"));
				itemProvider.operation = QStringLiteral("service");
				itemProvider.detailsStatus = Fixtures::status(batch.status.startsWith(QStringLiteral("Executed"))
				                                              ? QStringLiteral("Called 14:02:11 · in the Batch")
				                                              : QStringLiteral("Not called yet · Execute Batch calls it"));
			}
			itemProvider.changed();
		};
		batch.onSelect = showItem;
		batch.add(QStringLiteral("control.Scale"), QStringLiteral("service"), QStringLiteral("value=0 factor=0"), QStringLiteral("–"));
		batch.add(QStringLiteral("control.Reset"), QStringLiteral("command"), QString{}, QStringLiteral("–"));
		batch.add(QStringLiteral("device.Config"), QStringLiteral("read"), QString{}, QStringLiteral("–"));
		batch.status = QStringLiteral("Not executed yet");
		batch.changed();
		window->setProperty("mode", QStringLiteral("batch"));
		auto* batchPanel = need(window->contentItem(), "batch");
		need(batchPanel, "batchToggle")->setProperty("checked", true);
		const auto batchChecks = [&](bool exact) {
			window->setWidth(760);
			window->setHeight(600);
			settle();
			auto* list = need(batchPanel, "batchList");
			list->setProperty("currentIndex", 2);
			settle();
			// 10. The split.
			auto* split = need(batchPanel, "batchSplit");
			const auto sizes = [&](int width) {
				window->setWidth(width);
				settle();
				return std::pair{need(batchPanel, "batchListColumn")->width(), need(batchPanel, "batchItemPane")->width()};
			};
			if (exact) {
				const auto at760 = sizes(760);
				CHECK(near(at760.first, 380) && near(at760.second, 379));
				CHECK(split->property("orientation").toInt() == Qt::Horizontal);
				const auto at1200 = sizes(1200);
				CHECK(near(at1200.first, 520) && near(at1200.second, 679));
			}
			sizes(719);
			CHECK(split->property("orientation").toInt() == Qt::Vertical);
			window->setWidth(760);
			window->setHeight(600);
			settle();
			// 11. The list: its header row, its rows, the "+" under the last.
			auto* header = need(batchPanel, "batchListHeader");
			const QStringList labels{QStringLiteral("#"), QStringLiteral("Operation"), QStringLiteral("Item"), QStringLiteral("Outcome")};
			const char* names[] = {"batchListNumberLabel", "batchListOperationLabel", "batchListItemLabel", "batchListOutcomeLabel"};
			for (int at = 0; at < 4; ++at) {
				CHECK(need(header, names[at])->property("text").toString() == labels[at]);
				// Each column is as wide as its words, at every zoom.
				CHECK(!need(header, names[at])->property("truncated").toBool());
			}
			auto* rule = need(header, "batchListRule");
			CHECK(near(rule->height(), 1) && color(rule) == token("outlineVariant"));
			const auto rows = all(batchPanel, QStringLiteral("batchRow"));
			CHECK(rows.size() == 3);
			auto* outcomeLabel = need(header, "batchListOutcomeLabel");
			const auto outcomeRight = outcomeLabel->mapToScene(QPointF{outcomeLabel->width(), 0}).x();
			for (auto* row : rows) {
				CHECK(near(row->height(), size("rowHeight")));
				auto* kind = need(row, "batchRowKind");
				CHECK(kind->property("implicitWidth").toReal() <= kind->width() + 0.75);
				auto* outcome = need(row, "batchRowOutcome");
				CHECK(near(outcome->mapToScene(QPointF{outcome->width(), 0}).x(), outcomeRight));
				CHECK(outcome->property("text").toString() == QStringLiteral("–"));
			}
			auto* column = need(batchPanel, "batchListColumn");
			auto* plus = need(batchPanel, "batchAddRow");
			CHECK(near(in(plus, column).top(), header->height() + 3 * size("rowHeight")));
			if (exact)
				CHECK(near(in(plus, column).top(), 88));
			// No highlight of the list's own (the radius one went): the row draws
			// its square fill; a ListView's stand-in item draws nothing.
			CHECK(!list->property("highlight").value<QObject*>());
			if (auto* highlight = list->property("highlightItem").value<QQuickItem*>())
				CHECK(QString::fromLatin1(highlight->metaObject()->className()) == QStringLiteral("QQuickItem") &&
				      highlight->childItems().isEmpty());
			auto* current = rows[2];
			auto* fill = need(current, "batchRowFill");
			CHECK(color(fill) == token("stateSelectedInactive") || color(fill) == token("stateSelected"));
			list->forceActiveFocus(Qt::TabFocusReason);
			settle(2);
			CHECK(color(fill) == token("stateSelected") && need(current, "batchRowFocusRing")->isVisible());
			need(batchPanel, "executeBatchButton")->forceActiveFocus(Qt::TabFocusReason);
			settle(2);
			CHECK(color(fill) == token("stateSelectedInactive") && !find(current, QStringLiteral("batchRowFocusRing")));
			// 13. The item pane: Item 3 before the run.
			auto* pane = need(batchPanel, "batchItemPane");
			CHECK(need(pane, "metadataTree")->property("rows").toInt() == 4);
			for (auto* label : all(pane, QStringLiteral("endpointRow")).mid(1))
				CHECK(need(label, "metadataValueLabel")->property("text").toString() == QStringLiteral("–"));
			CHECK(need(pane, "endpointStatusLine")->property("text").toString() ==
			      QStringLiteral("Not read yet · Execute Batch reads it"));
			QVariant notRead;
			CHECK(QMetaObject::invokeMethod(window, "countLabels", Q_RETURN_ARG(QVariant, notRead), Q_ARG(QVariant, QStringLiteral("Not read yet"))));
			CHECK(notRead.toInt() == 1);
			CHECK(need(pane, "endpointSelectionInfo")->property("text").toString().startsWith(QStringLiteral("Item 3 · Field")));
			// 12. The status line in every state; the run's line alone moves.
			auto* status = need(batchPanel, "batchOutcomeLabel");
			auto* progress = find(pane, QStringLiteral("endpointProgress"), false);
			CHECK(progress && !progress->isVisible());
			const auto stateOf = [&](const QString& text, bool pending, bool attention, int count) {
				batch.status = text;
				batch.pending = pending;
				batch.attention = attention;
				batch.count = count;
				batch.changed();
				settle(2);
				CHECK(status->isVisible() && !status->property("text").toString().isEmpty());
				CHECK(color(status) != token("warning"));
				CHECK(color(status) == (attention ? token("textPrimary") : token("textSecondary")));
				CHECK(bool(find(batchPanel, QStringLiteral("batchStatusGlyph"))) == attention);
				CHECK(!progress->isVisible());
			};
			stateOf(QString{}, false, false, 0);
			CHECK(status->property("text").toString().startsWith(QStringLiteral("Add Fields, Commands and Services")));
			stateOf(QString{}, false, false, 3);
			CHECK(status->property("text").toString() == QStringLiteral("Not executed yet"));
			stateOf(QStringLiteral("Sending 3 items in one request…"), true, false, 3);
			CHECK(need(batchPanel, "executeBatchButton")->property("text").toString() == QStringLiteral("Sending…"));
			stateOf(QStringLiteral("Executed 14:02:11 · 3 of 3 done"), false, false, 3);
			stateOf(QStringLiteral("Executed 14:02:11 · 2 of 3 done · item 3 refused its arguments"), false, true, 3);
			stateOf(QStringLiteral("Not executed yet"), false, false, 3);
			list->setProperty("currentIndex", 2);
			settle();
			shot(window, output, QStringLiteral("batch-760") + (exact ? QString{} : QStringLiteral("-125")));
			// 1,200 px, after the run: item 1 in the pane.
			batch.status = QStringLiteral("Executed 14:02:11 · 3 of 3 done");
			const QStringList done{QStringLiteral("Done"), QStringLiteral("Executed"), QStringLiteral("Read")};
			for (int at = 0; at < 3; ++at)
				batch.items.item(at)->setData(done[at], Batch::Outcome);
			batch.items.item(0)->setData(QStringLiteral("value=2 factor=0.5"), Batch::Line);
			batch.changed();
			window->setWidth(1200);
			list->setProperty("currentIndex", 0);
			settle();
			showItem(0);
			settle();
			shot(window, output, QStringLiteral("batch-1200") + (exact ? QString{} : QStringLiteral("-125")));
			for (int at = 0; at < 3; ++at) {
				batch.items.item(at)->setData(QStringLiteral("–"), Batch::Outcome);
			}
			batch.items.item(0)->setData(QStringLiteral("value=0 factor=0"), Batch::Line);
			batch.status = QStringLiteral("Not executed yet");
			batch.changed();
		};
		batchChecks(true);

		// 14. Narrow and long words.
		window->setProperty("mode", QStringLiteral("details"));
		const auto narrowChecks = [&] {
			for (const auto& kind : kinds) {
				window->setWidth(240);
				window->setHeight(520);
				showKind(kind);
				auto* body = need(panel, "endpointBodyScroll");
				CHECK(near(body->property("contentWidth").toReal(), body->width()));
				for (const char* name : {"valueTree", "responseTree", "metadataTree"})
					if (auto* tree = find(panel, QString::fromLatin1(name)))
						CHECK(tree->property("contentWidth").toReal() <= tree->width() + 0.75);
				auto* title = need(panel, "endpointSelectionTitle");
				auto* row = need(panel, "endpointTitleRow");
				// The title elides before the actions take their own row.
				if (title->width() >= size("bodyPixels") * 6 - 0.75)
					CHECK(row->property("columns").toInt() == 2);
				auto* line2 = need(panel, "endpointSelectionInfo");
				if (line2->property("truncated").toBool()) {
					// A tooltip is a popup: a child object, not a child item.
					auto* tip = line2->findChild<QObject*>(QStringLiteral("endpointSelectionInfoToolTip"));
					CHECK(tip && tip->property("text").toString() == line2->property("text").toString());
				}
				// Nothing overlaps: the title and the actions, the heading's labels.
				auto* actions = need(panel, "endpointSelectionActions");
				const auto titleRect = title->mapRectToScene(QRectF{0, 0, title->width(), title->height()});
				const auto actionsRect = actions->mapRectToScene(QRectF{0, 0, actions->width(), actions->height()});
				CHECK(!titleRect.adjusted(0.5, 0.5, -0.5, -0.5).intersects(actionsRect));
				CHECK(actionsRect.right() <= panel->mapToScene(QPointF{panel->width(), 0}).x() + 0.75);
				for (auto* heading : {find(panel, QStringLiteral("valueTableHeader")), find(panel, QStringLiteral("responseTableHeader"))}) {
					if (!heading)
						continue;
					auto* name = find(heading, QStringLiteral("tableHeadingName"));
					auto* value = find(heading, QStringLiteral("tableHeadingValue"));
					auto* typeLabel = find(heading, QStringLiteral("tableHeadingType"));
					if (name && value)
						CHECK(name->mapToScene(QPointF{name->width(), 0}).x() <= value->mapToScene(QPointF{}).x() + 0.75);
					if (value && typeLabel)
						CHECK(value->mapToScene(QPointF{value->width(), 0}).x() <= typeLabel->mapToScene(QPointF{}).x() + 0.75);
				}
			}
		};
		narrowChecks();
		{
			LongerWords longer;
			QCoreApplication::installTranslator(&longer);
			engine.retranslate();
			settle();
			CHECK(need(panel, "endpointSelectionTitle"));
			narrowChecks();
			window->setWidth(560);
			showKind(kinds[5]);
			CHECK(need(panel, "callServiceButton")->property("text").toString().size() > QStringLiteral("Call").size());
			QCoreApplication::removeTranslator(&longer);
			engine.retranslate();
			settle();
		}

		// At 125 %: the same rules, and the screenshots.
		type->setProperty("body", baseBody * 1.25);
		settle();
		window->setProperty("mode", QStringLiteral("details"));
		CHECK(near(size("sectionSpacing"), size("groupSpacing") * 2));
		layoutChecks(false);
		shots(QStringLiteral("-125"));
		window->setProperty("mode", QStringLiteral("batch"));
		batchChecks(false);
		type->setProperty("body", baseBody);
		settle();

		std::printf("Details layout checks=%d warnings=%d\n", checks, warnings);
		return warnings == 0 ? 0 : 1;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Details layout check failed after %d checks: %s\n", checks, error.what());
		return 1;
	}
}

#include "DetailsLayout.moc"
