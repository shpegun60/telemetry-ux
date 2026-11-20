// The Device tree design's checks (18 November 2026, section J, 1-14) in a
// real QQuickWindow, over a real EndpointModel made from a real descriptor:
// the demo device with every kind of row - categories, groups, scalar, array,
// struct and enum Fields, a struct's members, Commands and a Service - and a
// group of 65 Fields. Check 15 (contrast) is tests/theme_contrast's. No
// network or device I/O. MIT.
#include <Telemetry/UX/DynamicModels.hpp>
#include <integration/encoded/Backend.hpp>
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <QtCore/QDir>
#include <QtCore/QItemSelectionModel>
#include <QtCore/QTimer>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlExpression>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QTest>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
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
namespace cl = telemetry::client;
namespace en = telemetry::integration::encoded;

// The demo device of the design's mocks, with one array Field and one enum.
namespace demo {
enum class Mode : std::uint8_t { Off, Low, High };
struct Config {
	float Gain = 1.0f;
	bool Enabled = false;
	std::uint32_t SamplePeriodMs = 100;
};
struct SetEnabledArguments {
	bool enabled = false;
};
struct SetVoltageArguments {
	float volts = 0;
};
struct ScaleRequest {
	float value = 0;
	float factor = 0;
};
// A Service answers a struct (the SDK has it so): "(f32, f32) -> (f32)".
struct ScaleResult {
	float result = 0;
};
struct Device {
	Config config{};
	Mode mode = Mode::Off;
	float voltage() const noexcept { return 230.0f; }
	float current() const noexcept { return 1.5f; }
	float power() const noexcept { return 345.0f; }
	std::uint64_t counter() const noexcept { return 7; }
	std::array<float, 256> samples() const noexcept { return {}; }
	Config readConfig() const noexcept { return config; }
	telemetry::WriteResult writeConfig(const Config& value) noexcept
	{
		config = value;
		return telemetry::WriteResult::Applied;
	}
	Mode readMode() const noexcept { return mode; }
	telemetry::WriteResult writeMode(const Mode& value) noexcept
	{
		mode = value;
		return telemetry::WriteResult::Applied;
	}
	double signal() const noexcept { return 0.0; }
	telemetry::CommandResult reset() noexcept { return telemetry::CommandResult::Executed; }
	telemetry::CommandResult setEnabled(const SetEnabledArguments&) noexcept { return telemetry::CommandResult::Executed; }
	telemetry::CommandResult setVoltage(const SetVoltageArguments&) noexcept { return telemetry::CommandResult::Executed; }
	telemetry::ServiceResult<ScaleResult> scale(const ScaleRequest& request) noexcept
	{
		return telemetry::ServiceResult<ScaleResult>::success({request.value * request.factor});
	}
};
inline Device device;
#define DEMO_SIGNAL(n) telemetry::field<&Device::signal>("Signal" #n, device)
inline constexpr telemetry::FieldTable measureFields{
    telemetry::field<&Device::voltage>("Voltage", device), telemetry::field<&Device::current>("Current", device),
    telemetry::field<&Device::power>("Power", device), telemetry::field<&Device::counter>("Counter", device),
    telemetry::field<&Device::samples>("Samples", device)};
inline constexpr telemetry::FieldTable deviceFields{
    telemetry::field<&Device::readConfig, &Device::writeConfig>("Config", device),
    telemetry::field<&Device::readMode, &Device::writeMode>("Mode", device)};
// 65 Fields: two that "volt" finds, then Signal0 ... Signal62.
inline constexpr telemetry::FieldTable signalFields{
    telemetry::field<&Device::signal>("BusVolts", device), telemetry::field<&Device::signal>("CellVoltMin", device),
    DEMO_SIGNAL(0), DEMO_SIGNAL(1), DEMO_SIGNAL(2), DEMO_SIGNAL(3), DEMO_SIGNAL(4), DEMO_SIGNAL(5), DEMO_SIGNAL(6),
    DEMO_SIGNAL(7), DEMO_SIGNAL(8), DEMO_SIGNAL(9), DEMO_SIGNAL(10), DEMO_SIGNAL(11), DEMO_SIGNAL(12), DEMO_SIGNAL(13),
    DEMO_SIGNAL(14), DEMO_SIGNAL(15), DEMO_SIGNAL(16), DEMO_SIGNAL(17), DEMO_SIGNAL(18), DEMO_SIGNAL(19), DEMO_SIGNAL(20),
    DEMO_SIGNAL(21), DEMO_SIGNAL(22), DEMO_SIGNAL(23), DEMO_SIGNAL(24), DEMO_SIGNAL(25), DEMO_SIGNAL(26), DEMO_SIGNAL(27),
    DEMO_SIGNAL(28), DEMO_SIGNAL(29), DEMO_SIGNAL(30), DEMO_SIGNAL(31), DEMO_SIGNAL(32), DEMO_SIGNAL(33), DEMO_SIGNAL(34),
    DEMO_SIGNAL(35), DEMO_SIGNAL(36), DEMO_SIGNAL(37), DEMO_SIGNAL(38), DEMO_SIGNAL(39), DEMO_SIGNAL(40), DEMO_SIGNAL(41),
    DEMO_SIGNAL(42), DEMO_SIGNAL(43), DEMO_SIGNAL(44), DEMO_SIGNAL(45), DEMO_SIGNAL(46), DEMO_SIGNAL(47), DEMO_SIGNAL(48),
    DEMO_SIGNAL(49), DEMO_SIGNAL(50), DEMO_SIGNAL(51), DEMO_SIGNAL(52), DEMO_SIGNAL(53), DEMO_SIGNAL(54), DEMO_SIGNAL(55),
    DEMO_SIGNAL(56), DEMO_SIGNAL(57), DEMO_SIGNAL(58), DEMO_SIGNAL(59), DEMO_SIGNAL(60), DEMO_SIGNAL(61), DEMO_SIGNAL(62)};
#undef DEMO_SIGNAL
inline constexpr telemetry::CommandTable controlCommands{
    telemetry::command<&Device::setEnabled>("SetEnabled", device), telemetry::command<&Device::reset>("Reset", device),
    telemetry::command<&Device::setVoltage>("SetVoltage", device)};
inline constexpr telemetry::ServiceTable controlServices{telemetry::service<&Device::scale>("Scale", device)};
inline constexpr telemetry::FieldCatalogTable fields{telemetry::group("measure", measureFields),
                                                      telemetry::group("device", deviceFields),
                                                      telemetry::group("signals", signalFields)};
inline constexpr telemetry::CommandCatalogTable commands{telemetry::group("control", controlCommands)};
inline constexpr telemetry::ServiceCatalogTable services{telemetry::group("control", controlServices)};
inline constexpr telemetry::Model model{fields, commands, services};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();
} // namespace demo

// The descriptor parsed and admitted by a real Workbench, as a connection
// leaves it: what EndpointModel is made from.
struct Backing {
	std::uint32_t now = 0;
	cl::Clock clock{this, +[](void* self) noexcept { return static_cast<Backing*>(self)->now; }};
	std::array<std::byte, demo::descriptorBytes.size()> bytes = demo::descriptorBytes;
	std::array<rt::RuntimeType, 64> types{};
	std::array<rt::RuntimeMember, 64> members{};
	std::array<rt::RuntimeEnumEntry, 16> enums{};
	std::array<rt::RuntimeCatalog, 16> catalogs{};
	std::array<rt::RuntimeEndpoint, 128> endpoints{};
	rt::RuntimeSchemaWorkspace metadata{types, members, enums, catalogs, endpoints};
	rt::RuntimeSchema schema;
	std::array<std::byte, 65536> tx{}, rx{}, serviceResponse{};
	en::EncodedBackend backend{tx, rx, clock};
	ux::Workbench owner{backend, clock, serviceResponse};
	Backing()
	{
		const auto parsed = rt::RuntimeSchema::parse(bytes, metadata);
		CHECK(parsed);
		schema = parsed.schema;
		CHECK(owner.admit(schema, cl::Input{bytes}.first(64), 17) == cl::Status::Ok);
	}
	~Backing()
	{
		owner.close();
		owner.process(now);
	}
};

// The Device tree's provider as the panel reads it: selection by index,
// boxes on every row of the Fields, previews offered.
class Provider final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel* endpoints READ endpoints CONSTANT)
	Q_PROPERTY(QModelIndex selectedEndpointIndex READ selected NOTIFY selectionChanged)
	Q_PROPERTY(bool connected READ connected CONSTANT)
	Q_PROPERTY(bool previewsSupported READ previewsSupported CONSTANT)
	Q_PROPERTY(int previewIntervalMs MEMBER interval NOTIFY intervalChanged)
	Q_PROPERTY(int watchRevision READ watchRevision CONSTANT)
public:
	explicit Provider(ux::EndpointModel* shown) : model(shown)
	{
		// A reconnect: the provider finds its selection again by its
		// endpoint, once the model has finished changing.
		connect(model, &QAbstractItemModel::modelReset, this, [this] {
			QTimer::singleShot(0, this, [this] {
				selection = model->endpointIndex(category, id);
				Q_EMIT selectionChanged();
			});
		});
	}
	QAbstractItemModel* endpoints() const { return model; }
	QModelIndex selected() const { return selection; }
	bool connected() const { return true; }
	bool previewsSupported() const { return true; }
	int watchRevision() const { return 0; }
	Q_INVOKABLE bool selectEndpoint(const QModelIndex& index)
	{
		const auto endpoint = model->endpoint(index);
		if (endpoint.isEmpty())
			return false;
		category = endpoint.value(QStringLiteral("category")).toInt();
		id = endpoint.value(QStringLiteral("id")).toUInt();
		selection = index;
		++selections;
		Q_EMIT selectionChanged();
		return true;
	}
	Q_INVOKABLE int watchState(const QModelIndex& index) const
	{
		return index.data(ux::EndpointModel::CategoryRole).toInt() == 1 ? int(Qt::Unchecked) : -1;
	}
	Q_INVOKABLE QVariantMap setWatchMany(const QVariantList&, bool)
	{
		return {{QStringLiteral("status"), QStringLiteral("ok")}, {QStringLiteral("changed"), 0}};
	}
	Q_INVOKABLE void setPreviewDemand(QObject*, const QVariantList&) {}
	Q_INVOKABLE void clearPreviewDemand(QObject*) {}
	ux::EndpointModel* model;
	QPersistentModelIndex selection;
	int category = 0, selections = 0, interval = 2000;
	quint32 id = 0;
Q_SIGNALS:
	void selectionChanged();
	void intervalChanged();
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
QList<QQuickItem*> all(QQuickItem* root, const QString& name)
{
	QList<QQuickItem*> out;
	collect(root, name, out);
	return out;
}
QVariant eval(QObject* scope, const QString& expression)
{
	QQmlExpression e(qmlContext(scope), scope, expression);
	const auto value = e.evaluate();
	if (e.hasError())
		throw std::runtime_error(e.error().toString().toStdString());
	return value;
}
// Whether a row delegate is the one its view shows at its row, not one the
// view keeps in its pool for reuse (pooled delegates stay visible, culled).
bool live(QQuickItem* row)
{
	auto* view = row->parentItem() ? row->parentItem()->parentItem() : nullptr;
	QQuickItem* shown = nullptr;
	return view && QMetaObject::invokeMethod(view, "itemAtCell", Q_RETURN_ARG(QQuickItem*, shown),
	                                         Q_ARG(QPoint, QPoint(0, row->property("row").toInt()))) && shown == row;
}
QList<QQuickItem*> rowsShown(QQuickItem* view)
{
	QList<QQuickItem*> out;
	for (auto* row : all(view, QStringLiteral("endpointRow")))
		if (live(row))
			out.append(row);
	return out;
}
QColor color(QObject* item) { return item->property("color").value<QColor>(); }
double sceneX(QQuickItem* item) { return item->mapToScene(QPointF(0, 0)).x(); }
bool near(double a, double b, double within = 0.6) { return std::abs(a - b) <= within; }

struct Tree {
	QQuickWindow* window = nullptr;
	QQuickItem* panel = nullptr;
	QQuickItem* view = nullptr;
	QObject* filter = nullptr;
	ux::EndpointModel* model = nullptr;
	Provider* provider = nullptr;

	QModelIndex at(const QString& path) const
	{
		const auto index = model->indexOfPath(path);
		CHECK(index.isValid());
		return index;
	}
	QModelIndex shown(const QModelIndex& source) const
	{
		QVariant result;
		CHECK(QMetaObject::invokeMethod(panel, "shownOf", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant::fromValue(source))));
		return result.value<QModelIndex>();
	}
	QModelIndex source(const QModelIndex& shownIndex) const
	{
		QVariant result;
		CHECK(QMetaObject::invokeMethod(panel, "sourceOf", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant::fromValue(shownIndex))));
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
	void expand(const QModelIndex& source) const
	{
		reveal(source);
		if (!expanded(source))
			CHECK(QMetaObject::invokeMethod(view, "expand", Q_ARG(int, row(source))));
		settle();
	}
	void collapse(const QModelIndex& source) const
	{
		if (row(source) >= 0 && expanded(source))
			CHECK(QMetaObject::invokeMethod(view, "collapse", Q_ARG(int, row(source))));
		settle();
	}
	void reveal(const QModelIndex& source) const
	{
		QVariant result;
		CHECK(QMetaObject::invokeMethod(panel, "reveal", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant::fromValue(source)),
		                                Q_ARG(QVariant, false)));
		CHECK(result.toBool());
		settle();
	}
	void makeCurrent(const QModelIndex& source) const
	{
		QVariant result;
		CHECK(QMetaObject::invokeMethod(panel, "reveal", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant::fromValue(source)),
		                                Q_ARG(QVariant, true)));
		CHECK(result.toBool());
		settle();
	}
	QModelIndex current() const
	{
		auto* selection = qobject_cast<QItemSelectionModel*>(view->property("selectionModel").value<QObject*>());
		CHECK(selection);
		return source(selection->currentIndex());
	}
	// The row's delegate, brought into view.
	QQuickItem* item(const QModelIndex& source, bool show = true) const
	{
		if (show)
			reveal(source);
		for (auto* candidate : rowsShown(view))
			if (candidate->property("sourceIndex").value<QModelIndex>() == source)
				return candidate;
		return nullptr;
	}
	QQuickItem* part(const QModelIndex& source, const char* name) const
	{
		auto* row = item(source);
		CHECK(row);
		return named(row, QString::fromLatin1(name));
	}
	bool inView(const QModelIndex& source) const
	{
		const int at = row(source);
		return at >= view->property("topRow").toInt() && at <= view->property("bottomRow").toInt() && at >= 0;
	}
	QStringList openKeys() const
	{
		QStringList keys;
		QMetaObject::invokeMethod(view, "forceLayout");
		const int rows = view->property("rows").toInt();
		for (int at = 0; at < rows; ++at) {
			bool open = false;
			QMetaObject::invokeMethod(view, "isExpanded", Q_RETURN_ARG(bool, open), Q_ARG(int, at));
			if (!open)
				continue;
			QModelIndex index;
			QMetaObject::invokeMethod(view, "index", Q_RETURN_ARG(QModelIndex, index), Q_ARG(int, at), Q_ARG(int, 0));
			QString key;
			QMetaObject::invokeMethod(filter, "namePath", Q_RETURN_ARG(QString, key), Q_ARG(QModelIndex, index),
			                          Q_ARG(QString, QStringLiteral("/")));
			keys << key;
		}
		keys.sort();
		return keys;
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
		const auto output = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
		auto backing = std::make_unique<Backing>();
		ux::EndpointModel model;
		model.setSchemaSource([schema = backing->schema] { return schema; });
		model.rebuild(backing->owner);
		Provider provider(&model);
		// The model's own answers first: the kinds, the widest type, the counts.
		CHECK(model.widestFieldType() == QStringLiteral("struct (3)"));
		CHECK(model.data(model.indexOfPath(QStringLiteral("Fields/device/Mode")), ux::EndpointModel::TypeTextRole).toString()
		      == QStringLiteral("enum (3)"));
		CHECK(model.data(model.indexOfPath(QStringLiteral("Fields/device/Mode")), ux::EndpointModel::TypeNamesRole).toString()
		      == QStringLiteral("Off, Low, High"));
		CHECK(model.data(model.indexOfPath(QStringLiteral("Fields/device/Config/Gain")), ux::EndpointModel::KindRole).toString()
		      == QStringLiteral("value"));
		// A value inside a Field reads and writes as the Field.
		CHECK(model.data(model.indexOfPath(QStringLiteral("Fields/device/Config/Gain")), ux::EndpointModel::WritableRole).toBool());
		CHECK(model.indexOfPath(QStringLiteral("Commands/control")) != model.indexOfPath(QStringLiteral("Services/control")));
		CHECK(!model.indexOfPath(QStringLiteral("Fields/nothing")).isValid());

		QQmlApplicationEngine engine;
		engine.rootContext()->setContextProperty(QStringLiteral("kindsProvider"), &provider);
		engine.load(QUrl(QStringLiteral("qrc:/DeviceTreeKinds.qml")));
		CHECK(!engine.rootObjects().isEmpty());
		Tree t;
		t.window = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
		CHECK(t.window);
		settle(200);
		t.panel = named(t.window->contentItem(), QStringLiteral("kindsTree"));
		t.view = named(t.panel, QStringLiteral("endpointTree"));
		t.filter = t.panel->findChild<QObject*>(QStringLiteral("endpointTreeFilter"));
		t.model = &model;
		t.provider = &provider;
		CHECK(t.panel && t.view && t.filter);
		const auto token = [&](const char* name) { return eval(t.panel, QStringLiteral("Theme.") + QLatin1String(name)).value<QColor>(); };
		const auto type = [&](const char* name) { return eval(t.panel, QStringLiteral("Type.") + QLatin1String(name)); };
		const double iconSize = type("iconSize").toDouble();
		const double nameFloor = type("nameFloor").toDouble();
		const double gap = type("columnGap").toDouble();

		const auto fields = t.at(QStringLiteral("Fields"));
		const auto commands = t.at(QStringLiteral("Commands"));
		const auto services = t.at(QStringLiteral("Services"));
		const auto measure = t.at(QStringLiteral("Fields/measure"));
		const auto voltage = t.at(QStringLiteral("Fields/measure/Voltage"));
		const auto samples = t.at(QStringLiteral("Fields/measure/Samples"));
		const auto deviceGroup = t.at(QStringLiteral("Fields/device"));
		const auto config = t.at(QStringLiteral("Fields/device/Config"));
		const auto mode = t.at(QStringLiteral("Fields/device/Mode"));
		const auto signalGroup = t.at(QStringLiteral("Fields/signals"));
		const auto commandGroup = t.at(QStringLiteral("Commands/control"));
		const auto setEnabled = t.at(QStringLiteral("Commands/control/SetEnabled"));
		const auto serviceGroup = t.at(QStringLiteral("Services/control"));
		const auto scale = t.at(QStringLiteral("Services/control/Scale"));
		// Everything the mocks show open: the groups, Config, the Commands and Services.
		CHECK(t.expanded(fields) && t.expanded(measure) && t.expanded(deviceGroup));
		t.expand(config);
		const auto gain = t.at(QStringLiteral("Fields/device/Config/Gain"));
		const auto period = t.at(QStringLiteral("Fields/device/Config/SamplePeriodMs"));
		for (const auto& index : {commands, commandGroup, services, serviceGroup})
			t.expand(index);
		t.collapse(signalGroup);

		// The 420 px mock: Config selected and current in the focused tree.
		CHECK(provider.selectEndpoint(config));
		t.makeCurrent(config);
		t.view->forceActiveFocus();
		t.reveal(fields);
		t.shot(output, QStringLiteral("device-tree-kinds-420.png"));
		// The keyboard elsewhere: every glyph at rest, the selection quiet.
		named(t.panel, QStringLiteral("endpointSearch"))->forceActiveFocus();
		settle();

		// 1. A glyph per kind, in the kind's own slot, quiet.
		const QList<std::pair<QModelIndex, QString>> glyphs{
		    {fields, QStringLiteral("group")},   {measure, QStringLiteral("group")},    {voltage, QStringLiteral("value")},
		    {samples, QStringLiteral("array")},  {config, QStringLiteral("struct")},    {mode, QStringLiteral("value")},
		    {gain, QStringLiteral("value")},     {commands, QStringLiteral("group")},   {setEnabled, QStringLiteral("command")},
		    {services, QStringLiteral("group")}, {scale, QStringLiteral("service")}};
		for (const auto& [index, name] : glyphs) {
			auto* glyph = t.part(index, "kindIcon");
			CHECK(glyph && glyph->property("name").toString() == name);
			CHECK(near(glyph->width(), iconSize) && near(glyph->height(), iconSize));
			CHECK(color(glyph) == token("textSecondary"));
		}
		// Every glyph of one depth at one x, the Commands' and Services' too.
		const double depthTwo = sceneX(t.part(voltage, "kindIcon"));
		for (const auto& index : {config, mode, setEnabled, scale})
			CHECK(near(sceneX(t.part(index, "kindIcon")), depthTwo));
		CHECK(near(sceneX(t.part(measure, "kindIcon")), depthTwo - type("indent").toDouble()));
		CHECK(near(sceneX(t.part(commandGroup, "kindIcon")), depthTwo - type("indent").toDouble()));
		// Selected in a focused tree: the selection's foreground.
		CHECK(provider.selectEndpoint(config));
		t.view->forceActiveFocus();
		settle(120);
		CHECK(t.item(config)->property("highlighted").toBool());
		CHECK(color(t.part(config, "kindIcon")) == token("selectedForeground"));
		// Not focused: back to the quiet colour on the quieter fill.
		named(t.panel, QStringLiteral("endpointSearch"))->forceActiveFocus();
		settle();
		CHECK(color(t.part(config, "kindIcon")) == token("textSecondary"));
		CHECK(color(t.part(config, "endpointRowName")) == token("textPrimary"));

		// 2. Weight says container: categories and groups DemiBold, the rest Regular.
		const auto weight = [&](const QModelIndex& index) { return t.part(index, "endpointRowName")->property("font").value<QFont>().weight(); };
		const int heading = type("headingWeight").toInt();
		for (const auto& index : {fields, commands, services, measure, deviceGroup, commandGroup, serviceGroup})
			CHECK(weight(index) == heading);
		for (const auto& index : {config, samples, mode, gain, setEnabled, scale})
			CHECK(weight(index) == QFont::Normal);

		// 3. Types and counts never look alike.
		auto* configType = t.part(config, "endpointRowType");
		CHECK(configType->property("text").toString() == QStringLiteral("struct (3)"));
		CHECK(configType->property("font").value<QFont>().family() == type("fixedFamily").toString());
		CHECK(near(configType->property("font").value<QFont>().pointSizeF(), type("caption").toDouble(), 0.01));
		auto* measureCount = t.part(measure, "endpointRowCount");
		CHECK(measureCount->property("text").toString() == QStringLiteral("5 Fields"));
		CHECK(measureCount->property("font").value<QFont>().family() != type("fixedFamily").toString());
		CHECK(color(measureCount) == token("textMuted"));
		CHECK(!t.part(measure, "endpointRowType"));
		// Categories count in words too (the owner, 18 November 2026).
		CHECK(t.part(fields, "endpointRowCount")->property("text").toString() == QStringLiteral("72 Fields"));
		CHECK(t.part(commands, "endpointRowCount")->property("text").toString() == QStringLiteral("3 Commands"));
		CHECK(t.part(serviceGroup, "endpointRowCount")->property("text").toString() == QStringLiteral("1 Service"));
		auto* typeTitle = named(t.panel, QStringLiteral("endpointColumnHeader_2"));
		CHECK(typeTitle);
		const auto fieldRows = {voltage, samples, config, mode, gain, period};
		for (const auto& index : fieldRows)
			CHECK(near(sceneX(t.part(index, "endpointRowType")), sceneX(typeTitle)));
		// The rw of every row of a Field, its members included (the owner, 20 November 2026).
		CHECK(t.part(config, "endpointRowAccess")->property("text").toString() == QStringLiteral("rw"));
		CHECK(t.part(gain, "endpointRowAccess")->property("text").toString() == QStringLiteral("rw"));
		CHECK(t.part(voltage, "endpointRowAccess")->property("text").toString() == QStringLiteral("r-"));
		CHECK(t.part(setEnabled, "endpointRowAccess")->property("text").toString().isEmpty() ||
		      t.part(setEnabled, "endpointRowAccess")->property("text").toString() == QStringLiteral("Execute"));

		// 5. 420 px: rw shown; every count ends where the rw marks end.
		auto* accessTitle = named(t.panel, QStringLiteral("endpointColumnHeader_3"));
		CHECK(accessTitle && accessTitle->isVisible());
		const double rwRight = sceneX(accessTitle) + accessTitle->width();
		for (const auto& index : {fields, measure, deviceGroup, commands, commandGroup, services, serviceGroup}) {
			auto* count = t.part(index, "endpointRowCount");
			CHECK(count && near(sceneX(count) + count->width(), rwRight));
		}
		t.makeCurrent(config);
		t.view->forceActiveFocus();
		t.reveal(fields);

		// 8. Indent guides: one per ancestor, under its chevron; Config's
		// members alone carry the current branch's brighter one.
		const auto guides = [&](const QModelIndex& index) {
			auto* row = t.item(index);
			CHECK(row);
			QList<QQuickItem*> found;
			collect(row, QStringLiteral("indentGuide"), found);
			return found;
		};
		for (const auto& [index, depth] : QList<std::pair<QModelIndex, int>>{{fields, 0}, {measure, 1}, {voltage, 2}, {config, 2},
		                                                                     {gain, 3}, {period, 3}, {services, 0}, {scale, 2}}) {
			const auto found = guides(index);
			CHECK(found.size() == depth);
			for (int a = 0; a < found.size(); ++a)
				CHECK(near(found[a]->mapToItem(t.panel, QPointF(0, 0)).x(), std::round(6 + 16 * a + 8), 0.01));
		}
		settle(100);
		int bright = 0;
		for (const auto& index : {fields, measure, voltage, samples, deviceGroup, config, gain,
		                          t.at(QStringLiteral("Fields/device/Config/Enabled")), period, mode, commands, scale}) {
			const auto found = guides(index);
			for (int a = 0; a < found.size(); ++a) {
				const bool outline = color(found[a]) == token("outline");
				CHECK(outline || color(found[a]) == token("outlineVariant"));
				if (outline) {
					++bright;
					CHECK(a == 2 && (index == gain || index == period || index.parent() == config));
				}
			}
		}
		CHECK(bright == 3);

		// 9. A category after the first reads as a section: a rule on its top edge.
		CHECK(t.part(commands, "categoryRule") && t.part(services, "categoryRule"));
		CHECK(!t.part(fields, "categoryRule"));

		// 10. The same glyph and weight here as in the other trees.
		CHECK(t.part(config, "kindIcon")->property("name").toString() == QStringLiteral("struct"));
		CHECK(t.part(gain, "kindIcon")->property("name").toString() == QStringLiteral("value"));
		CHECK(weight(config) == QFont::Normal && weight(gain) == QFont::Normal);

		// 14. In words for a screen reader; the glyph is not read.
		const auto spoken = [&](const QModelIndex& index) { return eval(t.item(index), QStringLiteral("Accessible.name")).toString(); };
		CHECK(spoken(config).contains(QStringLiteral("Field")) && spoken(config).contains(QStringLiteral("struct with 3 members")) &&
		      spoken(config).contains(QStringLiteral("read and write")));
		CHECK(spoken(measure).contains(QStringLiteral("group")) && spoken(measure).contains(QStringLiteral("5 Fields")));
		CHECK(spoken(gain).contains(QStringLiteral("member")) && spoken(gain).contains(QStringLiteral("read and write")));
		CHECK(eval(t.part(config, "kindIcon"), QStringLiteral("Accessible.ignored")).toBool());
		// The path first in the name's tooltip, then the words; an enum's names.
		CHECK(t.item(config)->property("toolTipText").toString().startsWith(QStringLiteral("device.Config\nField · struct (3)")));
		CHECK(t.item(mode)->property("toolTipText").toString().contains(QStringLiteral("Off, Low, High")));

		// 7. Previews on at 420: a type never runs into a value.
		t.panel->setProperty("previewValues", true);
		settle(200);
		CHECK(t.panel->property("valueColumn").toDouble() > 0);
		int valued = 0;
		for (const auto& index : {voltage, samples, config, mode, gain, period, setEnabled, scale}) {
			auto* row = t.item(index);
			auto* value = named(row, QStringLiteral("endpointRowValue"));
			auto* rowType = named(row, QStringLiteral("endpointRowType"));
			CHECK(rowType);
			if (!value || !row->property("valueShown").toBool())
				continue;
			++valued;
			CHECK(rowType->x() >= value->x() + value->width() + gap - 0.5);
		}
		CHECK(valued >= 5);
		t.panel->setProperty("previewValues", false);
		settle(200);

		// 4. 280 px, previews off: rw has given way, every type is whole.
		t.width(280);
		CHECK(!named(t.panel, QStringLiteral("endpointColumnHeader_3")));
		CHECK(t.panel->property("accessColumn").toDouble() == 0);
		for (const auto& index : {config, samples, mode}) {
			auto* rowType = t.part(index, "endpointRowType");
			CHECK(rowType && !rowType->property("truncated").toBool());
		}
		// The design's "(f32, f32) -> f32" is whole here; the SDK answers a
		// Service with a struct, so the real one reads "-> (f32)", two
		// characters more. It takes all its row leaves after the whole name
		// "Scale", runs over the blank rw cell, and is whole from 290 px.
		auto* scaleType = t.part(scale, "endpointRowType");
		CHECK(scaleType->property("text").toString() == QStringLiteral("(f32, f32) -> (f32)"));
		auto* scaleName = t.part(scale, "endpointRowName");
		CHECK(scaleName->width() >= scaleName->implicitWidth() - 1 && !scaleName->property("truncated").toBool());
		CHECK(near(scaleType->x(), scaleName->x() + scaleName->implicitWidth() + gap, 1.0));
		t.width(290);
		CHECK(!t.part(scale, "endpointRowType")->property("truncated").toBool());
		t.width(280);
		auto* periodName = t.part(period, "endpointRowName");
		CHECK(periodName->property("truncated").toBool() && periodName->property("elide").toInt() == Qt::ElideMiddle);
		CHECK(near(sceneX(t.part(period, "endpointRowType")), sceneX(named(t.panel, QStringLiteral("endpointColumnHeader_2")))));
		t.makeCurrent(config);
		t.reveal(fields);
		t.shot(output, QStringLiteral("device-tree-kinds-280.png"));

		// 6. The floor at 240 px: a type elides only once its name keeps its first characters.
		t.width(240);
		t.expand(signalGroup);
		const auto floorHolds = [&] {
			for (auto* row : rowsShown(t.view)) {
				auto* rowType = named(row, QStringLiteral("endpointRowType"));
				auto* rowName = named(row, QStringLiteral("endpointRowName"));
				CHECK(rowName);
				if (rowType && rowType->property("truncated").toBool())
					CHECK(rowName->width() >= std::min(rowName->implicitWidth(), nameFloor) - 1);
			}
		};
		for (const auto& index : {fields, period, t.at(QStringLiteral("Fields/signals/Signal40")), scale}) {
			t.reveal(index);
			floorHolds();
		}
		t.collapse(signalGroup);
		t.width(420);

		// 11. Keys: Left and Right fold and step, * and - open and close all
		// under, Home and End, Enter on a group, and the menu says them.
		t.collapse(measure);
		t.makeCurrent(measure);
		t.view->forceActiveFocus();
		settle();
		t.key(Qt::Key_Right);
		CHECK(t.expanded(measure) && t.current() == measure);
		t.key(Qt::Key_Right);
		CHECK(t.current() == voltage);
		t.key(Qt::Key_Left);
		CHECK(t.current() == measure && t.expanded(measure));
		t.key(Qt::Key_Left);
		CHECK(t.current() == measure && !t.expanded(measure));
		CHECK(QMetaObject::invokeMethod(t.view, "collapseRecursively", Q_ARG(int, t.row(fields))));
		settle();
		t.makeCurrent(fields);
		t.view->forceActiveFocus();
		t.key(Qt::Key_Asterisk, Qt::ShiftModifier);
		CHECK(t.expanded(fields) && t.expanded(measure) && t.expanded(deviceGroup) && t.expanded(signalGroup));
		CHECK(!t.expanded(config) && !t.expanded(samples));
		t.key(Qt::Key_Minus);
		CHECK(!t.expanded(fields));
		t.expand(fields);
		CHECK(!t.expanded(measure) && !t.expanded(deviceGroup) && !t.expanded(signalGroup));
		t.makeCurrent(measure);
		t.view->forceActiveFocus();
		t.key(Qt::Key_Return);
		CHECK(t.expanded(measure));
		t.key(Qt::Key_Return);
		CHECK(!t.expanded(measure));
		CHECK(provider.selected() != measure);
		t.key(Qt::Key_End);
		const int rows = t.view->property("rows").toInt();
		CHECK(t.row(t.current()) == rows - 1 && t.inView(t.current()));
		t.key(Qt::Key_Home);
		CHECK(t.row(t.current()) == 0 && t.current() == fields && t.inView(fields));
		// Enter on a Field opens Details, as before.
		t.makeCurrent(voltage.isValid() && t.row(voltage) >= 0 ? voltage : mode);
		t.expand(measure);
		t.makeCurrent(voltage);
		t.view->forceActiveFocus();
		t.key(Qt::Key_Return);
		CHECK(provider.selected() == voltage);
		auto* menu = t.panel->findChild<QObject*>(QStringLiteral("endpointContextMenu"));
		CHECK(menu);
		t.key(Qt::Key_Menu);
		CHECK(menu->property("visible").toBool());
		auto* expandAll = menu->findChild<QObject*>(QStringLiteral("treeMenuExpandAll"));
		auto* collapseUnder = menu->findChild<QObject*>(QStringLiteral("treeMenuCollapseUnder"));
		auto* collapseAll = menu->findChild<QObject*>(QStringLiteral("treeMenuCollapseAll"));
		CHECK(expandAll && collapseUnder && collapseAll);
		CHECK(expandAll->property("text").toString() == QStringLiteral("Expand all under\t*"));
		CHECK(collapseUnder->property("text").toString() == QStringLiteral("Collapse all under\t-"));
		CHECK(collapseAll->property("text").toString() == QStringLiteral("Collapse all"));
		QTest::keyClick(t.window, Qt::Key_Escape);
		settle();
		CHECK(!menu->property("visible").toBool());
		// Collapse all: back to the three categories.
		t.view->forceActiveFocus();
		t.key(Qt::Key_Menu);
		CHECK(QMetaObject::invokeMethod(collapseAll, "triggered"));
		settle();
		CHECK(t.view->property("rows").toInt() == 3);

		// 12. Find filters in place and steps through the matches.
		t.expand(fields);
		t.expand(measure);
		t.expand(deviceGroup);
		t.makeCurrent(mode);
		const auto openBefore = t.openKeys();
		auto* search = named(t.panel, QStringLiteral("endpointSearch"));
		auto* status = named(t.panel, QStringLiteral("endpointSearchStatus"), false);
		CHECK(search && status);
		search->forceActiveFocus();
		CHECK(search->setProperty("text", QStringLiteral("volt")));
		settle(250);
		CHECK(status->isVisible() && status->property("text").toString() == QStringLiteral("4 matches"));
		CHECK(t.expanded(fields) && t.expanded(measure) && t.expanded(signalGroup) && t.expanded(commands) && t.expanded(commandGroup));
		CHECK(t.row(deviceGroup) < 0 && t.row(services) < 0);
		CHECK(t.view->property("rows").toInt() == 9);
		// The header stays.
		CHECK(named(t.panel, QStringLiteral("endpointColumnHeaders")));
		t.width(280);
		t.key(Qt::Key_Return);
		CHECK(t.current() == voltage && provider.selected() == voltage && t.inView(voltage));
		CHECK(status->property("text").toString() == QStringLiteral("1 of 4 matches"));
		// The matched run is marked: underlined, not only coloured.
		auto* voltageName = t.part(voltage, "endpointRowName");
		CHECK(voltageName->property("textFormat").toInt() == 4 /* Text.StyledText */);
		CHECK(voltageName->property("text").toString().contains(QStringLiteral("<u>")));
		t.key(Qt::Key_Return);
		CHECK(status->property("text").toString() == QStringLiteral("2 of 4 matches"));
		const auto busVolts = t.at(QStringLiteral("Fields/signals/BusVolts"));
		CHECK(t.current() == busVolts && t.inView(busVolts));
		t.shot(output, QStringLiteral("device-tree-kinds-filter.png"));
		t.key(Qt::Key_Return, Qt::ShiftModifier);
		CHECK(status->property("text").toString() == QStringLiteral("1 of 4 matches"));
		t.key(Qt::Key_Return);
		t.key(Qt::Key_Return);
		t.key(Qt::Key_Return);
		CHECK(status->property("text").toString() == QStringLiteral("4 of 4 matches"));
		CHECK(t.current() == t.at(QStringLiteral("Commands/control/SetVoltage")));
		t.key(Qt::Key_Return);
		CHECK(status->property("text").toString() == QStringLiteral("1 of 4 matches") && t.current() == voltage);
		// F3 from the tree steps on; Down from the field enters the tree at the match.
		t.view->forceActiveFocus();
		t.key(Qt::Key_F3);
		CHECK(status->property("text").toString() == QStringLiteral("2 of 4 matches"));
		t.key(Qt::Key_F3, Qt::ShiftModifier);
		search->forceActiveFocus();
		settle();
		t.key(Qt::Key_Down);
		CHECK(t.view->hasActiveFocus() && t.current() == voltage);
		// Esc: the tree as it was before the find, the current row kept, in view.
		search->forceActiveFocus();
		settle();
		t.key(Qt::Key_Escape);
		settle(150);
		CHECK(search->property("text").toString().isEmpty() && !status->isVisible());
		CHECK(t.openKeys() == openBefore);
		CHECK(t.current() == voltage && t.inView(voltage));
		// Nothing found: said, with the way back.
		CHECK(search->setProperty("text", QStringLiteral("no-such-name")));
		settle(250);
		CHECK(status->property("text").toString() == QStringLiteral("Nothing matches “no-such-name”."));
		auto* clearLink = named(t.panel, QStringLiteral("endpointSearchClearLink"));
		CHECK(clearLink && clearLink->isVisible());
		CHECK(QMetaObject::invokeMethod(clearLink, "clicked"));
		settle(150);
		CHECK(search->property("text").toString().isEmpty() && t.openKeys() == openBefore);
		t.width(420);

		// 13. What is open survives a reconnect, the selected Field in view;
		// a selection made elsewhere opens its path and shows it.
		t.expand(measure);
		t.expand(config);
		CHECK(provider.selectEndpoint(mode));
		settle(100);
		const auto openKept = t.openKeys();
		CHECK(openKept.contains(QStringLiteral("Fields/measure")) && openKept.contains(QStringLiteral("Fields/device/Config")));
		model.rebuild(backing->owner);
		settle(250);
		CHECK(t.openKeys() == openKept);
		const auto modeAgain = t.at(QStringLiteral("Fields/device/Mode"));
		CHECK(provider.selected() == modeAgain);
		CHECK(t.inView(modeAgain));
		const auto signalsAgain = t.at(QStringLiteral("Fields/signals"));
		t.collapse(signalsAgain);
		CHECK(!t.expanded(signalsAgain));
		const auto signal40 = t.at(QStringLiteral("Fields/signals/Signal40"));
		CHECK(provider.selectEndpoint(signal40));
		settle(150);
		CHECK(t.expanded(signalsAgain) && t.inView(signal40) && t.current() == signal40);

		CHECK(warnings == 0);
		std::printf("Device tree kinds checks=%d warnings=0\n", checks);
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Device tree kinds check %d failed: %s\n", checks, error.what());
		return 1;
	}
}
#include "DeviceTreeKinds.moc"
