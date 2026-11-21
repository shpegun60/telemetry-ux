// The designer's review of 20 November 2026, part B (section 14, reconciled
// in 15) and the Device tree's side of drag and drop (section 16), in a real
// QQuickWindow over a real EndpointModel made from real descriptors v3.1:
// - nested groups from the descriptor's group records at any depth, their
//   counts and paths, subgroups first;
// - full type names "scope::name", the scope muted, "…::" as the Type
//   column's give-way step 3 with the header's words, find by either form, a
//   row matched by its type showing its scope, the type in words;
// - the host's ceilings: a tree deeper than the SDK builds by default and a
//   name past its default, read and laid out; the Descriptor view's words
//   for a limit; the Descriptor view's nested groups and its typeName export;
// - a descriptor with advisory attributes giving the same tree as without;
// - picking rows (Ctrl, Shift, Shift+arrows, Ctrl+Space), what a drag
//   carries, its badge, and Space, Ctrl+P, Ctrl+B and the row menu's drops.
// No network or device I/O. MIT.
#include <Telemetry/UX/DescriptorContent.hpp>
#include <Telemetry/UX/DynamicModels.hpp>
#include <integration/encoded/Backend.hpp>
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QItemSelectionModel>
#include <QtCore/QTimer>
#include <QtGui/QColor>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
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
#include <vector>

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
namespace ts = telemetry;
} // namespace

// The design's own layout (14.1, 13.1): an adc branch with two channels and
// its own Config, Status and Mode, and a power branch whose harmonics nest
// three levels deep, each phase with an H3 and a Spectrum, beside a meter of
// sixteen Fields. Type names are automatic: adc::Config, metrology::Spectrum.
namespace adc {
enum class Mode : std::uint8_t { Idle, Run, Hold };
struct Config {
	float Gain = 1.0f;
	bool Enabled = true;
	Mode mode = Mode::Idle;
};
struct Status {
	std::uint32_t raw = 0;
	std::uint32_t errors = 0;
};
struct Reference {
	float volts = 0;
};
struct Offset {
	float value = 0;
};
} // namespace adc
namespace metrology {
enum class Window : std::uint8_t { Rect, Hann, Flat };
struct Limits {
	float low = 0;
	float high = 1;
};
struct Spectrum {
	std::array<float, 8> Bins{};
	Window window = Window::Hann;
	Limits limits{};
};
struct Config {
	std::uint32_t periodMs = 100;
};
} // namespace metrology
namespace named_tree {
template<int N>
float value() noexcept
{
	return float(N);
}
template<int N>
ts::WriteResult setValue(const float&) noexcept
{
	return ts::WriteResult::Applied;
}
inline adc::Config config() noexcept { return {}; }
inline ts::WriteResult setConfig(const adc::Config&) noexcept { return ts::WriteResult::Applied; }
inline adc::Status status() noexcept { return {}; }
inline adc::Mode mode() noexcept { return adc::Mode::Run; }
inline ts::WriteResult setMode(const adc::Mode&) noexcept { return ts::WriteResult::Applied; }
inline metrology::Spectrum spectrum() noexcept { return {}; }
inline metrology::Config meterConfig() noexcept { return {}; }
inline ts::CommandResult reset() noexcept { return ts::CommandResult::Executed; }
inline ts::CommandResult clear() noexcept { return ts::CommandResult::Executed; }
inline adc::Offset calibrate(const adc::Reference& reference) noexcept { return {reference.volts / 2}; }

inline constexpr auto adcBranch =
    ts::group("adc", ts::group("channel1", ts::field<&value<1>, &setValue<1>>("Gain")),
              ts::group("channel2", ts::field<&value<2>, &setValue<2>>("Gain")),
              ts::field<&config, &setConfig>("Config"), ts::field<&status>("Status"),
              ts::field<&mode, &setMode>("Mode"), ts::command<&reset>("Reset"), ts::service<&calibrate>("Calibrate"));
inline constexpr auto powerBranch = ts::group(
    "power",
    ts::group("metrology",
              ts::group("harmonics", ts::group("phaseA", ts::field<&value<3>>("H3"), ts::field<&spectrum>("Spectrum")),
                        ts::group("phaseB", ts::field<&value<4>>("H3"), ts::field<&spectrum>("Spectrum")),
                        ts::group("phaseC", ts::field<&value<5>>("H3"), ts::field<&spectrum>("Spectrum"))),
              ts::field<&meterConfig>("Config"), ts::command<&clear>("Clear")),
    ts::group("meter", ts::field<&value<10>>("V01"), ts::field<&value<11>>("V02"), ts::field<&value<12>>("V03"),
              ts::field<&value<13>>("V04"), ts::field<&value<14>>("V05"), ts::field<&value<15>>("V06"),
              ts::field<&value<16>>("V07"), ts::field<&value<17>>("V08"), ts::field<&value<18>>("V09"),
              ts::field<&value<19>>("V10"), ts::field<&value<20>>("V11"), ts::field<&value<21>>("V12"),
              ts::field<&value<22>>("V13"), ts::field<&value<23>>("V14"), ts::field<&value<24>>("V15"),
              ts::field<&value<25>>("V16")));
inline constexpr ts::Model model{adcBranch, powerBranch};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();

template<class Profile, class F, class C, class S>
constexpr auto withProfile(const ts::Model<F, C, S>& source) noexcept
{
	return telemetry::descriptor::Descriptor<F, C, S, Profile>{source};
}

// Twelve levels, past the SDK's default of eight (a host reads 255): a
// device built with a deeper profile on purpose.
struct Deeper : ts::Limits {
	static constexpr std::uint32_t maxGroupDepth = 16;
};
inline constexpr auto deep11 = ts::group("l11", ts::field<&value<7>>("x"));
inline constexpr auto deep10 = ts::group("l10", deep11);
inline constexpr auto deep9 = ts::group("l9", deep10);
inline constexpr auto deep8 = ts::group("l8", deep9);
inline constexpr auto deep7 = ts::group("l7", deep8);
inline constexpr auto deep6 = ts::group("l6", deep7);
inline constexpr auto deep5 = ts::group("l5", deep6);
inline constexpr auto deep4 = ts::group("l4", deep5);
inline constexpr auto deep3 = ts::group("l3", deep4);
inline constexpr auto deep2 = ts::group("l2", deep3);
inline constexpr auto deep1 = ts::group("l1", deep2);
inline constexpr auto deepChain = ts::group("l0", deep1);
inline constexpr ts::Model deepModel{deepChain};
inline constexpr auto deepDescriptor = withProfile<Deeper>(deepModel);
inline constexpr auto deepBytes = telemetry::descriptor::packDescriptor<deepDescriptor>();

// A 120-byte group name, past the SDK's default 64 (a host reads 4096).
struct Wider : ts::Limits {
	static constexpr std::uint32_t maxNameBytes = 160;
	static constexpr std::uint32_t maxPathBytes = 512;
};
#define LONG_NAME "band_from_2000_Hz_to_4000_Hz_above_the_carrier_of_the_inverter_measured_on_phase_A_after_the_filter_stage_one"
inline constexpr auto longBranch = ts::group("plant", ts::group(LONG_NAME, ts::field<&value<8>>("H37")));
inline constexpr ts::Model longModel{longBranch};
inline constexpr auto longDescriptor = withProfile<Wider>(longModel);
inline constexpr auto longBytes = telemetry::descriptor::packDescriptor<longDescriptor>();
} // namespace named_tree

namespace {
template<std::size_t N>
QByteArray bytesOf(const std::array<std::byte, N>& packed)
{
	return QByteArray(reinterpret_cast<const char*>(packed.data()), qsizetype(N));
}

// A fixture copied from the SDK's goldens (fixtures/README.md): hex pairs.
QByteArray hexFixture(const QString& name)
{
	QFile file(QStringLiteral(UX_FIXTURES) + QLatin1Char('/') + name);
	CHECK(file.open(QIODevice::ReadOnly));
	const auto text = QString::fromLatin1(file.readAll()).simplified().remove(QLatin1Char(' '));
	return QByteArray::fromHex(text.toLatin1());
}

// A descriptor parsed and admitted by a real Workbench, as a connection
// leaves it: what EndpointModel is made from.
struct Backing {
	std::uint32_t now = 0;
	cl::Clock clock{this, +[](void* self) noexcept { return static_cast<Backing*>(self)->now; }};
	std::vector<std::byte> bytes;
	std::vector<rt::RuntimeType> types;
	std::vector<rt::RuntimeMember> members;
	std::vector<rt::RuntimeEnumEntry> enums;
	std::vector<rt::RuntimeCatalog> catalogs;
	std::vector<rt::RuntimeEndpoint> endpoints;
	std::unique_ptr<rt::RuntimeSchemaWorkspace> metadata;
	rt::RuntimeSchema schema;
	std::array<std::byte, 65536> tx{}, rx{}, serviceResponse{};
	en::EncodedBackend backend{tx, rx, clock};
	ux::Workbench owner{backend, clock, serviceResponse};
	explicit Backing(const QByteArray& descriptor)
	    : bytes(reinterpret_cast<const std::byte*>(descriptor.constData()),
	            reinterpret_cast<const std::byte*>(descriptor.constData()) + descriptor.size())
	{
		const std::span<const std::byte> input{bytes};
		rt::RuntimeSchemaWorkspace sizing{{}, {}, {}, {}, {}};
		const auto required = rt::RuntimeSchema::parse(input, sizing);
		CHECK(required || required.status == rt::Status::Capacity);
		types.resize(required.requirements.types);
		members.resize(required.requirements.members);
		enums.resize(required.requirements.enumEntries);
		catalogs.resize(required.requirements.catalogs);
		endpoints.resize(required.requirements.endpoints);
		metadata = std::make_unique<rt::RuntimeSchemaWorkspace>(types, members, enums, catalogs, endpoints);
		const auto parsed = rt::RuntimeSchema::parse(input, *metadata);
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

// The Device tree's provider as the panel reads it, with boxes on Fields and
// the drops of section 16 recorded: dropPlan counts what it is given,
// dropOn says what it did.
class Provider final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel* endpoints READ endpoints CONSTANT)
	Q_PROPERTY(QModelIndex selectedEndpointIndex READ selected NOTIFY selectionChanged)
	Q_PROPERTY(bool connected READ connected CONSTANT)
	Q_PROPERTY(int watchRevision READ watchRevision CONSTANT)
public:
	explicit Provider(ux::EndpointModel* shown) : model(shown) {}
	QAbstractItemModel* endpoints() const { return model; }
	QModelIndex selected() const { return selection; }
	bool connected() const { return true; }
	int watchRevision() const { return 0; }
	Q_INVOKABLE bool selectEndpoint(const QModelIndex& index)
	{
		if (model->endpoint(index).isEmpty())
			return false;
		selection = index;
		++selections;
		Q_EMIT selectionChanged();
		return true;
	}
	Q_INVOKABLE int watchState(const QModelIndex& index) const
	{
		return index.data(ux::EndpointModel::CategoryRole).toInt() == 1 ? int(Qt::Unchecked) : -1;
	}
	Q_INVOKABLE QVariantMap setWatchMany(const QVariantList& indexes, bool watched)
	{
		lastWatch = indexes;
		lastWatched = watched;
		return {{QStringLiteral("status"), QStringLiteral("ok")}, {QStringLiteral("changed"), int(indexes.size())},
		        {QStringLiteral("serial"), 0}};
	}
	Q_INVOKABLE QVariantMap dropPlan(const QVariantList& items, const QString& target) const
	{
		plans << target;
		const bool held = target == QLatin1String("watchlist") && heldAll;
		return {{QStringLiteral("status"), held ? QStringLiteral("held") : QStringLiteral("ok")},
		        {QStringLiteral("count"), int(items.size()) * 10},
		        {QStringLiteral("words"), QStringLiteral("Drop to add %1").arg(items.size())},
		        {QStringLiteral("refusal"), QString{}},
		        {QStringLiteral("from"), QStringLiteral("power")}};
	}
	Q_INVOKABLE QVariantMap dropOn(const QVariantList& items, const QString& target)
	{
		drops.push_back({target, items});
		return {{QStringLiteral("status"), QStringLiteral("ok")}, {QStringLiteral("added"), int(items.size())},
		        {QStringLiteral("message"), QStringLiteral("%1: %2").arg(target).arg(items.size())},
		        {QStringLiteral("serial"), 0}};
	}
	ux::EndpointModel* model;
	QPersistentModelIndex selection;
	int selections = 0;
	QVariantList lastWatch;
	bool lastWatched = false;
	bool heldAll = false;
	mutable QStringList plans;
	struct Drop {
		QString target;
		QVariantList items;
	};
	std::vector<Drop> drops;
Q_SIGNALS:
	void selectionChanged();
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
bool live(QQuickItem* row)
{
	auto* view = row->parentItem() ? row->parentItem()->parentItem() : nullptr;
	QQuickItem* shown = nullptr;
	return view && QMetaObject::invokeMethod(view, "itemAtCell", Q_RETURN_ARG(QQuickItem*, shown),
	                                         Q_ARG(QPoint, QPoint(0, row->property("row").toInt()))) && shown == row;
}
QList<QQuickItem*> rowsShown(QQuickItem* view)
{
	QList<QQuickItem*> all, out;
	collect(view, QStringLiteral("endpointRow"), all);
	for (auto* row : all)
		if (live(row))
			out.append(row);
	return out;
}

struct Tree {
	QQuickWindow* window = nullptr;
	QQuickItem* panel = nullptr;
	QQuickItem* view = nullptr;
	ux::EndpointModel* model = nullptr;

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
	int row(const QModelIndex& source) const
	{
		int result = -1;
		CHECK(QMetaObject::invokeMethod(view, "rowAtIndex", Q_RETURN_ARG(int, result), Q_ARG(QModelIndex, shown(source))));
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
	QQuickItem* item(const QModelIndex& source) const
	{
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
	QVariant call(const char* method, const QVariant& a = {}, const QVariant& b = {}) const
	{
		QVariant result;
		if (b.isValid())
			CHECK(QMetaObject::invokeMethod(panel, method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, a), Q_ARG(QVariant, b)));
		else
			CHECK(QMetaObject::invokeMethod(panel, method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, a)));
		return result;
	}
	void shot(const QString& directory, const QString& name) const
	{
		settle(150);
		CHECK(window->grabWindow().save(QDir(directory).filePath(name)));
	}
};

// A model's rows as text, its Fields' members fetched: what two
// descriptors of one model must give alike.
QStringList dump(ux::EndpointModel& model, const QModelIndex& parent = {}, int depth = 0)
{
	QStringList out;
	if (model.canFetchMore(parent))
		model.fetchMore(parent);
	for (int row = 0; row < model.rowCount(parent); ++row) {
		const auto index = model.index(row, 0, parent);
		out << QStringLiteral("%1%2 | %3 | %4 | %5 | %6")
		           .arg(QString(depth * 2, QLatin1Char(' ')), index.data(ux::EndpointModel::NameRole).toString(),
		                index.data(ux::EndpointModel::KindRole).toString(), index.data(ux::EndpointModel::TypeTextRole).toString(),
		                index.data(ux::EndpointModel::PathRole).toString(), index.data(ux::EndpointModel::DetailRole).toString());
		if (depth < 8)
			out << dump(model, index, depth + 1);
	}
	return out;
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
		const auto output = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
		auto backing = std::make_unique<Backing>(bytesOf(named_tree::descriptorBytes));
		CHECK(backing->schema.groupTree() && backing->schema.minorVersion() == 1);
		ux::EndpointModel model;
		model.setSchemaSource([schema = backing->schema] { return schema; });
		model.rebuild(backing->owner);
		Provider provider(&model);

		// ---- Nested groups from the group records, at any depth (14.1) ----
		{
			QStringList tops;
			for (int row = 0; row < model.rowCount(); ++row)
				tops << model.index(row, 0).data(ux::EndpointModel::NameRole).toString();
			CHECK(tops == (QStringList{QStringLiteral("adc"), QStringLiteral("power")}));
			CHECK(model.deepestGroup() == 3);
			const auto phaseA = model.indexOfGroupPath(QStringLiteral("power/metrology/harmonics/phaseA"));
			CHECK(phaseA.isValid() && phaseA.data(ux::EndpointModel::NameRole).toString() == QStringLiteral("phaseA") &&
			      phaseA.data(ux::EndpointModel::KindRole).toString() == QStringLiteral("group") &&
			      phaseA.data(ux::EndpointModel::PathRole).toString() == QStringLiteral("power/metrology/harmonics/phaseA"));
			CHECK(phaseA.parent() == model.indexOfGroupPath(QStringLiteral("power/metrology/harmonics")));
			// Subgroups first, then Fields, Commands, Services, each in declaration order.
			const auto names = [&](const QModelIndex& parent) {
				QStringList out;
				for (int row = 0; row < model.rowCount(parent); ++row)
					out << model.index(row, 0, parent).data(ux::EndpointModel::NameRole).toString();
				return out;
			};
			CHECK(names(model.indexOfGroupPath(QStringLiteral("adc"))) ==
			      (QStringList{QStringLiteral("channel1"), QStringLiteral("channel2"), QStringLiteral("Config"),
			                   QStringLiteral("Status"), QStringLiteral("Mode"), QStringLiteral("Reset"),
			                   QStringLiteral("Calibrate")}));
			CHECK(names(model.indexOfGroupPath(QStringLiteral("power/metrology"))) ==
			      (QStringList{QStringLiteral("harmonics"), QStringLiteral("Config"), QStringLiteral("Clear")}));
			// Counts cover everything under a group, with its groups.
			const auto power = model.indexOfGroupPath(QStringLiteral("power"));
			CHECK(power.data(ux::EndpointModel::FieldCountRole).toInt() == 23 &&
			      power.data(ux::EndpointModel::CommandCountRole).toInt() == 1 &&
			      power.data(ux::EndpointModel::GroupCountRole).toInt() == 6);
			CHECK(power.data(ux::EndpointModel::DetailRole).toString() == QStringLiteral("23 Fields · 1 Command in 6 groups"));
			CHECK(model.indexOfGroupPath(QStringLiteral("adc")).data(ux::EndpointModel::DetailRole).toString() ==
			      QStringLiteral("5 Fields · 1 Command · 1 Service in 2 groups"));
			CHECK(model.fieldTotal() == 28 && model.commandTotal() == 2 && model.serviceTotal() == 1);
			// Paths in the one grammar; a group's key for the Watchlist.
			const auto h3 = model.indexOfPath(QStringLiteral("power/metrology/harmonics/phaseA/H3"));
			CHECK(model.pathOf(h3) == QStringLiteral("power/metrology/harmonics/phaseA/H3"));
			CHECK(model.pathOf(model.indexOfPath(QStringLiteral("adc/channel2/Gain"))) == QStringLiteral("adc/channel2/Gain"));
			CHECK(model.groupNames().contains(QStringLiteral("power/metrology/harmonics/phaseA")));
			bool found = false;
			for (const auto& entry : model.endpointList())
				if (entry.path == QStringLiteral("power/metrology/harmonics/phaseC/Spectrum"))
					found = entry.group == QStringLiteral("power/metrology/harmonics/phaseC") && entry.category == 1;
			CHECK(found);
		}

		// ---- Full type names: text, name, words, spoken (14.2) ----
		const auto config = model.indexOfPath(QStringLiteral("adc/Config"));
		const auto modeIndex = model.indexOfPath(QStringLiteral("adc/Mode"));
		const auto spectrum = model.indexOfPath(QStringLiteral("power/metrology/harmonics/phaseA/Spectrum"));
		const auto calibrate = model.indexOfPath(QStringLiteral("adc/Calibrate"));
		{
			const auto role = [](const QModelIndex& index, int r) { return index.data(r).toString(); };
			CHECK(role(config, ux::EndpointModel::TypeTextRole) == QStringLiteral("adc::Config") &&
			      role(config, ux::EndpointModel::TypeNameRole) == QStringLiteral("adc::Config"));
			CHECK(role(config, ux::EndpointModel::TypeSpokenRole) == QStringLiteral("struct Config in adc, 3 members"));
			CHECK(role(config, ux::EndpointModel::DetailRole).startsWith(
			    QStringLiteral("Field · struct adc::Config · 3 members · ")));
			CHECK(role(modeIndex, ux::EndpointModel::TypeTextRole) == QStringLiteral("adc::Mode") &&
			      role(modeIndex, ux::EndpointModel::TypeNamesRole) == QStringLiteral("Idle, Run, Hold") &&
			      role(modeIndex, ux::EndpointModel::TypeSpokenRole) == QStringLiteral("enum Mode in adc, 3 values"));
			CHECK(role(spectrum, ux::EndpointModel::TypeTextRole) == QStringLiteral("metrology::Spectrum"));
			// A signature names its structs; find takes it as a named type.
			CHECK(role(calibrate, ux::EndpointModel::TypeTextRole) == QStringLiteral("(adc::Reference) -> adc::Offset") &&
			      role(calibrate, ux::EndpointModel::TypeNameRole) == QStringLiteral("(adc::Reference) -> adc::Offset"));
			CHECK(role(model.indexOfPath(QStringLiteral("adc/Reset")), ux::EndpointModel::TypeTextRole) == QStringLiteral("()") &&
			      role(model.indexOfPath(QStringLiteral("adc/Reset")), ux::EndpointModel::TypeNameRole).isEmpty());
			// Members: their own types, an array of floats without a name.
			const auto window = model.indexOfPath(QStringLiteral("power/metrology/harmonics/phaseA/Spectrum/window"));
			const auto bins = model.indexOfPath(QStringLiteral("power/metrology/harmonics/phaseA/Spectrum/Bins"));
			CHECK(role(window, ux::EndpointModel::TypeTextRole) == QStringLiteral("metrology::Window") &&
			      role(window, ux::EndpointModel::TypeSpokenRole) == QStringLiteral("enum Window in metrology, 3 values"));
			CHECK(role(window, ux::EndpointModel::PathRole) == QStringLiteral("power/metrology/harmonics/phaseA/Spectrum.window"));
			CHECK(role(bins, ux::EndpointModel::TypeTextRole) == QStringLiteral("f32[8]") &&
			      role(bins, ux::EndpointModel::TypeSpokenRole) == QStringLiteral("array of 8 f32"));
			CHECK(role(model.indexOfPath(QStringLiteral("power/metrology/harmonics/phaseA/Spectrum/limits")),
			           ux::EndpointModel::TypeTextRole) == QStringLiteral("metrology::Limits"));
			// The column is as wide as the widest type in either form.
			CHECK(model.widestFieldType() == QStringLiteral("metrology::Spectrum") &&
			      model.widestShortFieldType() == QStringLiteral("…::Spectrum"));
		}

		// ---- The same tree with or without advisory attributes (7b, slice 4) ----
		{
			auto plain = std::make_unique<Backing>(hexFixture(QStringLiteral("named.hex")));
			auto attributed = std::make_unique<Backing>(hexFixture(QStringLiteral("attrs.hex")));
			CHECK(plain->bytes.size() == 1027 && attributed->bytes.size() == 1106);
			CHECK(plain->schema.fingerprint() != attributed->schema.fingerprint());
			ux::EndpointModel a, b;
			a.setSchemaSource([schema = plain->schema] { return schema; });
			b.setSchemaSource([schema = attributed->schema] { return schema; });
			a.rebuild(plain->owner);
			b.rebuild(attributed->owner);
			const auto left = dump(a), right = dump(b);
			CHECK(left.size() > 10 && left == right);
			CHECK(left.join(QLatin1Char('\n')).contains(QStringLiteral("fixture::MotorConfig")));
			// The Descriptor view too: the same rows and types.
			ux::DescriptorContent viewA(hexFixture(QStringLiteral("named.hex")));
			ux::DescriptorContent viewB(hexFixture(QStringLiteral("attrs.hex")));
			CHECK(viewA.valid() && viewB.valid() &&
			      viewA.textOfAll(QStringLiteral("tsv")) == viewB.textOfAll(QStringLiteral("tsv")));
		}

		// ---- The Descriptor view: nested groups, the typeName export ----
		{
			ux::DescriptorContent tree(hexFixture(QStringLiteral("tree.hex")));
			CHECK(tree.valid() && tree.groupCount() == 4);
			auto* rows = tree.endpoints();
			const auto fields = rows->index(0, 0);
			CHECK(rows->data(fields).toString() == QStringLiteral("Fields"));
			const auto adcRow = rows->index(0, 0, fields);
			CHECK(rows->data(adcRow).toString() == QStringLiteral("adc"));
			const auto channel = rows->index(0, 0, adcRow);
			CHECK(rows->data(channel).toString() == QStringLiteral("channel1") &&
			      rows->data(channel, ux::DescriptorTreeModel::KindRole).toString() == QStringLiteral("group"));
			CHECK(tree.pathOf(rows->index(0, 0, channel)) == QStringLiteral("adc/channel1.Gain"));
			const auto json = tree.textOfAll(QStringLiteral("json"));
			CHECK(json.contains(QStringLiteral("\"typeName\": \"adc::Config\"")));
			const auto tsv = tree.textOfAll(QStringLiteral("tsv"));
			CHECK(tsv.startsWith(QStringLiteral("path\tkind\ttype\taccess\ttypeName\n")) &&
			      tsv.contains(QStringLiteral("\tadc::Config\n")));
			// A host's ceiling, in words.
			const auto limit = ux::DescriptorContent::phrase({ux::DescriptorContent::Problem::Limit});
			CHECK(limit.contains(QStringLiteral("255 levels")) && limit.contains(QStringLiteral("4,096 bytes")));
		}

		// ---- The host's ceilings: deeper and longer than the SDK builds by default ----
		auto deepBacking = std::make_unique<Backing>(bytesOf(named_tree::deepBytes));
		ux::EndpointModel deep;
		deep.setSchemaSource([schema = deepBacking->schema] { return schema; });
		deep.rebuild(deepBacking->owner);
		CHECK(deep.deepestGroup() == 11);
		CHECK(deep.pathOf(deep.indexOfPath(QStringLiteral("l0/l1/l2/l3/l4/l5/l6/l7/l8/l9/l10/l11/x"))) ==
		      QStringLiteral("l0/l1/l2/l3/l4/l5/l6/l7/l8/l9/l10/l11/x"));
		auto longBacking = std::make_unique<Backing>(bytesOf(named_tree::longBytes));
		ux::EndpointModel longModel;
		longModel.setSchemaSource([schema = longBacking->schema] { return schema; });
		longModel.rebuild(longBacking->owner);
		CHECK(longModel.pathOf(longModel.indexOfPath(QStringLiteral("plant/" LONG_NAME "/H37"))) ==
		      QStringLiteral("plant/" LONG_NAME "/H37"));

		// ---- In a window ----
		QQmlApplicationEngine engine;
		engine.rootContext()->setContextProperty(QStringLiteral("namedProvider"), &provider);
		engine.load(QUrl(QStringLiteral("qrc:/DeviceTreeNamed.qml")));
		CHECK(!engine.rootObjects().isEmpty());
		Tree t;
		t.window = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
		CHECK(t.window);
		settle(200);
		t.panel = named(t.window->contentItem(), QStringLiteral("namedTree"));
		t.view = named(t.panel, QStringLiteral("endpointTree"));
		t.model = &model;
		CHECK(t.panel && t.view);
		const auto token = [&](const char* name) { return eval(t.panel, QStringLiteral("Theme.") + QLatin1String(name)).value<QColor>(); };
		const auto toned = [](const QColor& color) { return QStringLiteral("color=\"%1\"").arg(color.name(QColor::HexRgb)); };

		// Two tones: the scope and "::" muted, the own name brighter (14.2).
		t.width(420);
		CHECK(!t.panel->property("typeScopesCut").toBool());
		auto* configType = t.part(config, "endpointRowType");
		CHECK(configType && configType->property("typeText").toString() == QStringLiteral("adc::Config") &&
		      configType->property("shown").toString() == QStringLiteral("adc::Config"));
		const auto rich = configType->property("rich").toString();
		CHECK(rich.contains(toned(token("textMuted")) + QStringLiteral(">adc::</font>")) &&
		      rich.contains(toned(token("textSecondary")) + QStringLiteral(">Config</font>")));
		// A signature: each scope muted.
		auto* calibrateType = t.part(calibrate, "endpointRowType");
		CHECK(calibrateType->property("rich").toString().contains(toned(token("textMuted")) + QStringLiteral(">adc::</font>")));
		// In words for a screen reader: the kind, the own name, its scope, its size.
		const auto spoken = [&](const QModelIndex& index) { return eval(t.item(index), QStringLiteral("Accessible.name")).toString(); };
		CHECK(spoken(config).contains(QStringLiteral("struct Config in adc, 3 members")));
		CHECK(spoken(modeIndex).contains(QStringLiteral("enum Mode in adc, 3 values")));
		t.reveal(model.indexOfGroupPath(QStringLiteral("power/metrology/harmonics/phaseA")));
		t.reveal(spectrum);
		t.shot(output, QStringLiteral("device-tree-scoped-types-420.png"));
		t.shot(output, QStringLiteral("device-tree-nested-420.png"));

		// Give-way step 3 at 280 px: rw gone, the scopes give way for the
		// whole column, the header says so, a type's tooltip has the whole.
		t.width(280);
		CHECK(t.panel->property("accessColumn").toDouble() == 0 && t.panel->property("typeScopesCut").toBool());
		CHECK(t.panel->property("typeColumn").toDouble() < t.panel->property("typeWide").toDouble());
		configType = t.part(config, "endpointRowType");
		CHECK(configType->property("shown").toString() == QStringLiteral("…::Config") &&
		      configType->property("cutShown").toBool());
		CHECK(configType->property("rich").toString().contains(toned(token("textMuted")) + QStringLiteral(">…::</font>")));
		auto* typeTip = configType->findChild<QObject*>(QStringLiteral("endpointRowTypeToolTip"));
		CHECK(typeTip && typeTip->property("text").toString() == QStringLiteral("adc::Config"));
		auto* header = named(t.panel, QStringLiteral("endpointColumnHeader_2"));
		auto* headerTip = header->findChild<QObject*>(QStringLiteral("endpointTypeHeaderToolTip"));
		CHECK(headerTip && headerTip->property("text").toString() ==
		                       QStringLiteral("Scopes are hidden for room; hover a type for its full name."));
		CHECK(eval(header, QStringLiteral("Accessible.description")).toString() == headerTip->property("text").toString());
		// A short array of a named type keeps its brackets: "…::Window" etc.
		CHECK(t.part(spectrum, "endpointRowType")->property("shown").toString() == QStringLiteral("…::Spectrum"));
		// The indent step for a deepest group of 3 (D = 5) at 280 px.
		CHECK(t.panel->property("indentDepth").toInt() == 5);
		const double step = t.panel->property("indentStep").toDouble();
		CHECK(step >= 8 && step <= 16);
		t.shot(output, QStringLiteral("device-tree-nested-280.png"));

		// Find by either form (14.2); a row matched by its type shows its
		// scope even while the column cuts them.
		auto* search = named(t.panel, QStringLiteral("endpointSearch"));
		auto* filter = t.panel->findChild<QObject*>(QStringLiteral("endpointTreeFilter"));
		CHECK(search && filter);
		const auto find = [&](const QString& text) {
			CHECK(search->setProperty("text", text));
			settle(250);
			return filter->property("count").toInt();
		};
		CHECK(find(QStringLiteral("adc::Config")) == 1);
		configType = t.part(config, "endpointRowType");
		CHECK(configType->property("shown").toString() == QStringLiteral("adc::Config") &&
		      configType->property("rich").toString().contains(QStringLiteral("<u>")));
		// "adc::" every adc type: Config, Status, Mode and Calibrate's signature.
		CHECK(find(QStringLiteral("adc::")) == 4);
		// "Config" both branches' Configs, by name; "Spectrum" the three phases.
		CHECK(find(QStringLiteral("Config")) == 2);
		CHECK(find(QStringLiteral("metrology::Spectrum")) == 3);
		// Builtin words are no names, a signature's "f32" included.
		CHECK(find(QStringLiteral("f32")) == 0);
		CHECK(find(QStringLiteral("harm/phaseB/H3")) == 1);
		CHECK(find(QString()) == 0);
		t.width(420);

		// ---- Section 16, the tree's side ----
		// A press takes what is picked; the payload names it, never expanded.
		const auto power = t.at(QStringLiteral("power"));
		const auto h3 = t.at(QStringLiteral("power/metrology/harmonics/phaseA/H3"));
		const auto reset = t.at(QStringLiteral("adc/Reset"));
		const auto gainMember = t.at(QStringLiteral("adc/Config/Gain"));
		const auto items = [&](const QModelIndex& pressed) { return t.call("dropItems", QVariant::fromValue(pressed)).toList(); };
		const auto describe = [](const QVariantList& list) {
			QStringList out;
			for (const auto& item : list)
				out << item.toMap().value(QStringLiteral("kind")).toString() + QLatin1Char(':') +
				           item.toMap().value(QStringLiteral("path")).toString();
			return out;
		};
		CHECK(describe(items(power)) == QStringList{QStringLiteral("group:power")});
		CHECK(describe(items(config)) == QStringList{QStringLiteral("field:adc/Config")});
		CHECK(describe(items(gainMember)) == QStringList{QStringLiteral("member:adc/Config.Gain")});
		CHECK(describe(items(reset)) == QStringList{QStringLiteral("command:adc/Reset")});
		CHECK(describe(items(calibrate)) == QStringList{QStringLiteral("service:adc/Calibrate")});
		// Ctrl+click picks one more, in tree order whatever the clicks' order;
		// the pressed row out of the selection drags alone.
		t.call("pickClick", QVariant::fromValue(QModelIndex(h3)), int(Qt::NoModifier));
		t.call("pickClick", QVariant::fromValue(QModelIndex(reset)), int(Qt::ControlModifier));
		t.call("pickClick", QVariant::fromValue(QModelIndex(config)), int(Qt::ControlModifier));
		settle();
		CHECK(t.panel->property("pickedCount").toInt() == 3);
		CHECK(describe(items(h3)) == (QStringList{QStringLiteral("field:adc/Config"), QStringLiteral("command:adc/Reset"),
		                                          QStringLiteral("field:power/metrology/harmonics/phaseA/H3")}));
		CHECK(describe(items(power)) == QStringList{QStringLiteral("group:power")});
		CHECK(t.item(config)->property("picked").toBool() && t.item(config)->property("highlighted").toBool());
		CHECK(eval(t.item(config), QStringLiteral("Accessible.name")).toString().contains(QStringLiteral("selected")));
		// Ctrl+click again lets go; Shift+click picks the shown rows between.
		t.call("pickClick", QVariant::fromValue(QModelIndex(config)), int(Qt::ControlModifier));
		CHECK(t.panel->property("pickedCount").toInt() == 2);
		t.call("pickClick", QVariant::fromValue(QModelIndex(config)), int(Qt::NoModifier));
		t.call("pickClick", QVariant::fromValue(QModelIndex(modeIndex)), int(Qt::ShiftModifier));
		CHECK(t.panel->property("pickedCount").toInt() == 3);  // Config, Status, Mode
		CHECK(describe(items(modeIndex)) == (QStringList{QStringLiteral("field:adc/Config"), QStringLiteral("field:adc/Status"),
		                                                 QStringLiteral("field:adc/Mode")}));
		// From the keyboard: Shift+Down extends, Ctrl+Space toggles.
		t.call("pickClick", QVariant::fromValue(QModelIndex(config)), int(Qt::NoModifier));
		t.reveal(config, true);
		t.view->forceActiveFocus();
		settle();
		t.key(Qt::Key_Down, Qt::ShiftModifier);
		CHECK(t.panel->property("pickedCount").toInt() == 2);
		t.key(Qt::Key_Down, Qt::ShiftModifier);
		CHECK(t.panel->property("pickedCount").toInt() == 3);
		t.key(Qt::Key_Space, Qt::ControlModifier);
		CHECK(t.panel->property("pickedCount").toInt() == 2);
		t.key(Qt::Key_Space, Qt::ControlModifier);
		CHECK(t.panel->property("pickedCount").toInt() == 3);
		// Space acts on the picked rows as a drop on the Watchlist does;
		// Ctrl+P on the Plot, Ctrl+B on the Batch (16.4).
		provider.drops.clear();
		t.key(Qt::Key_Space);
		CHECK(provider.drops.size() == 1 && provider.drops[0].target == QStringLiteral("watchlist") &&
		      provider.drops[0].items.size() == 3);
		t.key(Qt::Key_P, Qt::ControlModifier);
		CHECK(provider.drops.size() == 2 && provider.drops[1].target == QStringLiteral("plot"));
		t.key(Qt::Key_B, Qt::ControlModifier);
		CHECK(provider.drops.size() == 3 && provider.drops[2].target == QStringLiteral("batch") &&
		      provider.drops[2].items.size() == 3);
		auto* status = named(t.panel, QStringLiteral("endpointWatchSelectionStatus"), false);
		CHECK(status && status->property("message").toString() == QStringLiteral("batch: 3"));
		// Everything watched already: Space takes them out, as the box does.
		provider.heldAll = true;
		t.key(Qt::Key_Space);
		CHECK(provider.drops.size() == 3 && provider.lastWatch.size() == 3 && !provider.lastWatched);
		provider.heldAll = false;
		// The row menu counts each drop before Enter, its key beside it.
		{
			QVariant opened;
			CHECK(QMetaObject::invokeMethod(t.panel, "openEndpointMenu", Q_RETURN_ARG(QVariant, opened),
			                                Q_ARG(QVariant, QVariant::fromValue(QModelIndex(config))), Q_ARG(QVariant, true),
			                                Q_ARG(QVariant, QVariant::fromValue<QObject*>(t.item(config))),
			                                Q_ARG(QVariant, 16), Q_ARG(QVariant, 10)));
			CHECK(opened.toBool());
			settle(100);
			auto* menu = t.panel->findChild<QObject*>(QStringLiteral("endpointContextMenu"));
			CHECK(menu);
			const auto itemText = [&](const char* name) {
				auto* item = menu->findChild<QObject*>(QString::fromLatin1(name));
				CHECK(item);
				return item->property("text").toString();
			};
			CHECK(itemText("endpointMenuDropWatch") == QStringLiteral("Add 30 values to the Watchlist\tSpace"));
			CHECK(itemText("endpointMenuDropPlot") == QStringLiteral("Plot 30 values\tCtrl+P"));
			CHECK(itemText("endpointMenuDropBatch") == QStringLiteral("Add 30 items to the Batch\tCtrl+B"));
			QMetaObject::invokeMethod(menu, "close");
			settle(100);
		}
		// The badge, made at the press: the first row's path and its words.
		{
			auto* badge = t.panel->findChild<QQuickItem*>(QStringLiteral("dragBadge"));
			CHECK(badge);
			QMetaObject::invokeMethod(badge, "make", Q_ARG(QVariant, QStringLiteral("group")), Q_ARG(QVariant, QStringLiteral("power")),
			                          Q_ARG(QVariant, t.call("badgeDetail", QVariant::fromValue(QModelIndex(power)), QVariantList{})));
			settle(200);
			CHECK(badge->property("detail").toString() == QStringLiteral("23 Fields · 1 Command"));
			CHECK(badge->width() <= 240 + 0.5 && !badge->property("image").toUrl().isEmpty());
			CHECK(t.call("badgeDetail", QVariant::fromValue(QModelIndex(config)), QVariantList{QVariant(1), QVariant(2), QVariant(3)})
			          .toString() == QStringLiteral("and 2 more"));
			CHECK(t.call("badgeDetail", QVariant::fromValue(QModelIndex(config)), QVariantList{}).toString() ==
			      QStringLiteral("adc::Config"));
		}

		// ---- Ceilings in the window: a twelve-level tree at 280 px ----
		{
			ux::EndpointModel* shown = &deep;
			Provider deepProvider(shown);
			engine.rootContext()->setContextProperty(QStringLiteral("namedProvider"), &deepProvider);
			settle(200);
			t.model = shown;
			t.width(280);
			// D = 13 > the producer's 10: the step may go under 8, to 4.
			CHECK(t.panel->property("indentDepth").toInt() == 13);
			const double deepStep = t.panel->property("indentStep").toDouble();
			CHECK(deepStep >= 4 && deepStep < 8);
			const auto x = shown->indexOfPath(QStringLiteral("l0/l1/l2/l3/l4/l5/l6/l7/l8/l9/l10/l11/x"));
			auto* xName = t.part(x, "endpointRowName");
			CHECK(xName && xName->width() > 0);
			engine.rootContext()->setContextProperty(QStringLiteral("namedProvider"), &provider);
			settle(200);
			t.model = &model;
		}

		CHECK(warnings == 0);
		std::printf("Device tree named checks=%d warnings=0\n", checks);
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Device tree named check %d failed: %s\n", checks, error.what());
		return 1;
	}
}
#include "DeviceTreeNamed.moc"
