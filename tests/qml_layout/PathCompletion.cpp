// The Watchlist's and the Batch's "+" rows completing in the one grammar
// (WL-10a, the owner's screenshot of 21 November 2026: picking a group wrote
// "measure/."), by the mouse and the keys, through the real provider of a
// real TCP connection to the demo peer serving a group tree in place of its
// own descriptor (the checks read nothing from the device). In both rows:
// a group gives "measure/" and lists what it holds; a subgroup the same one
// level down (demo/adc/channel1, as the client's emulator has it); an
// endpoint gives its whole path; "." and "[" follow only an endpoint whose
// value has members or elements; a member and an element complete and are
// added. The demo peer has no Batch, so the Batch's row is given a provider
// that is the real one with a Batch: its candidates and words, "unsupported"
// read as "adds", and what the row adds recorded. MIT.
#include <Telemetry/UX/QmlTypes.hpp>
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <Telemetry/UX/WatchlistModel.hpp>
#include <integration/qt/ConnectionSession.hpp>
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include "TcpDemoServer.hpp"
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtGui/QGuiApplication>
#include <QtQml/QJSValue>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QTest>
#include <array>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace completion {
struct Config {
	float Gain = 1.0f;
	bool Enabled = false;
	std::array<std::uint16_t, 4> Taps{};
};
struct Point {
	float x = 0;
	float y = 0;
};
struct ScaleRequest {
	float value = 0;
	float factor = 0;
};
struct ScaleResult {
	float result = 0;
};
} // namespace completion

namespace completion_fixture {
namespace ts = telemetry;
namespace c = completion;
template<int N>
float value() noexcept
{
	return float(N);
}
inline c::Config config() noexcept { return {}; }
inline ts::WriteResult setConfig(const c::Config&) noexcept { return ts::WriteResult::Applied; }
inline std::array<float, 8> samples() noexcept { return {}; }
inline std::array<c::Point, 3> points() noexcept { return {}; }
inline ts::CommandResult reset() noexcept { return ts::CommandResult::Executed; }
inline c::ScaleResult scale(const c::ScaleRequest& request) noexcept { return {request.value * request.factor}; }

// The client's emulator in small: measure, device and signals first, a
// subgroup chain demo/adc/channel1, and control's Command and Service.
inline constexpr auto measureBranch =
    ts::group("measure", ts::field<&value<1>>("Voltage"), ts::field<&value<2>>("Current"), ts::field<&value<3>>("Power"));
inline constexpr auto deviceBranch =
    ts::group("device", ts::field<&config, &setConfig>("Config"), ts::field<&value<4>>("Setpoint"));
inline constexpr auto signalsBranch = ts::group("signals", ts::field<&samples>("Samples"), ts::field<&points>("Points"));
inline constexpr auto demoBranch =
    ts::group("demo", ts::group("adc", ts::group("channel1", ts::field<&value<5>>("Gain")), ts::field<&value<6>>("Offset")));
inline constexpr auto controlBranch = ts::group("control", ts::command<&reset>("Reset"), ts::service<&scale>("Scale"));
inline constexpr ts::Model model{measureBranch, deviceBranch, signalsBranch, demoBranch, controlBranch};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();
} // namespace completion_fixture

namespace {
namespace ux = telemetry::ux;
namespace host = telemetry::integration::qt;
int checks = 0, warnings = 0;
void check(bool condition, const char* expression, int line)
{
	++checks;
	if (!condition)
		throw std::runtime_error(std::string(expression) + " (line " + std::to_string(line) + ")");
}
#define CHECK(x) check(bool(x), #x, __LINE__)

bool waitFor(const std::function<bool()>& ready)
{
	QElapsedTimer clock;
	clock.start();
	while (!ready() && clock.elapsed() < 5000)
		QTest::qWait(5);
	return ready();
}
void settle(int rounds = 6)
{
	for (int i = 0; i < rounds; ++i) {
		QCoreApplication::processEvents();
		QTest::qWait(25);
	}
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
QQuickItem* need(QQuickItem* root, const QString& name)
{
	QList<QQuickItem*> found;
	collect(root, name, found);
	if (found.isEmpty())
		throw std::runtime_error("no visible " + name.toStdString());
	return found.first();
}

// One "+" row in the window, by its prefix ("watchAdd", "batchAdd").
struct AddRow {
	QQuickWindow* window = nullptr;
	QQuickItem* row = nullptr;
	QString prefix;
	QQuickItem* field() const { return need(row, prefix + QStringLiteral("Field")); }
	QString text() const { return field()->property("text").toString(); }
	QString checkLine() const { return row->property("check").toMap().value(QStringLiteral("message")).toString(); }
	// The line under the field as shown: empty while hidden.
	QString shownLine() const
	{
		QList<QQuickItem*> found;
		collect(row, prefix + QStringLiteral("Check"), found);
		return found.isEmpty() ? QString{} : found.first()->property("text").toString();
	}
	QVariantList candidates() const { return row->property("candidates").toList(); }
	// The suggestions' paths, headings left out.
	QStringList paths() const
	{
		QStringList out;
		for (const auto& c : candidates())
			if (!c.toMap().value(QStringLiteral("heading")).toBool())
				out << c.toMap().value(QStringLiteral("path")).toString();
		return out;
	}
	QVariantMap candidate(const QString& path) const
	{
		for (const auto& c : candidates())
			if (!c.toMap().value(QStringLiteral("heading")).toBool() && c.toMap().value(QStringLiteral("path")) == path)
				return c.toMap();
		return {};
	}
	bool popupOpen() const
	{
		auto* popup = row->findChild<QObject*>(prefix + QStringLiteral("Popup"));
		return popup && popup->property("visible").toBool();
	}
	void begin() const
	{
		auto* rest = need(row, prefix + QStringLiteral("Rest"));
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, rest->mapToScene(QPointF(4, rest->height() / 2)).toPoint());
		settle();
		CHECK(field()->hasActiveFocus());
	}
	// The field emptied, then the keys typed into it, the suggestions made.
	void type(const QString& keys) const
	{
		QMetaObject::invokeMethod(row, "clear");
		settle(2);
		more(keys);
	}
	void more(const QString& keys) const
	{
		for (const auto ch : keys.toLatin1())
			QTest::keyClick(window, ch);
		settle(8);
	}
	// A click on the suggestion of this path, as a user picks it.
	void pick(const QString& path) const
	{
		CHECK(popupOpen());
		QList<QQuickItem*> rows;
		collect(window->contentItem(), prefix + QStringLiteral("Candidate"), rows);
		QQuickItem* hit = nullptr;
		for (auto* item : rows) {
			// The delegate's modelData is the adapter's JS object.
			const auto data = item->property("modelData");
			const auto shown = data.canConvert<QJSValue>() ? data.value<QJSValue>().property(QStringLiteral("path")).toString()
			                                               : data.toMap().value(QStringLiteral("path")).toString();
			if (shown == path)
				hit = item;
		}
		if (!hit)
			throw std::runtime_error("no suggestion " + path.toStdString() + " among " + paths().join(QLatin1Char(' ')).toStdString() +
			                         " (" + std::to_string(rows.size()) + " rows shown)");
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, hit->mapToScene(QPointF(hit->width() / 3, hit->height() / 2)).toPoint());
		settle(8);
	}
	void key(Qt::Key which) const
	{
		QTest::keyClick(window, which);
		settle(8);
	}
};
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
	const QString output = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
	try {
		inspector_qml::TcpDemoServer peer;
		peer.setDescriptorOverride(QByteArray(reinterpret_cast<const char*>(completion_fixture::descriptorBytes.data()),
		                                      qsizetype(completion_fixture::descriptorBytes.size())));
		CHECK(peer.listen());
		host::ConnectionSession connection;
		ux::QtConnectionBinding presentation{&connection};
		auto* provider = presentation.createProvider();
		CHECK(connection.connectTcpReference(QStringLiteral("127.0.0.1"), peer.port()));
		if (!waitFor([&] { return connection.ready(); }))
			std::fprintf(stderr, "Path completion admission: %s\n", qPrintable(connection.lastError()));
		CHECK(connection.ready());
		CHECK(waitFor([&] { return provider->connected() && provider->endpoints()->rowCount() == 5; }));

		QQmlEngine engine;
		engine.rootContext()->setContextProperty(QStringLiteral("completionProvider"), provider);
		QQmlComponent component(&engine);
		component.setData(R"qml(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX
ApplicationWindow {
    width: 560; height: 640; visible: true
    color: Theme.surfaceWindow
    font.pointSize: Type.body
    // The real provider as a device with a Batch would answer: the demo peer
    // has none, so "unsupported" reads as "adds"; what the row adds is kept.
    QtObject {
        id: batchShim
        objectName: "batchShim"
        readonly property bool batchSupported: true
        readonly property bool batchPending: false
        property var added: []
        function checkBatchPath(text) {
            const out = completionProvider.checkBatchPath(text)
            if (out.status === "unsupported")
                out.status = "adds"
            return out
        }
        function batchPathCandidates(text) { return completionProvider.batchPathCandidates(text) }
        function addBatchPaths(lines) {
            added = added.concat([lines.join("\n")])
            return { status: "ok", added: lines.length, first: 1, serial: 0,
                     items: lines.map(line => ({ path: line, what: line })) }
        }
    }
    function batchAdded() { return batchShim.added }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Type.padding
        spacing: 0
        WatchAddRow { objectName: "watchAddRow"; provider: completionProvider; Layout.fillWidth: true }
        // Room for the Watchlist's suggestions under its row.
        Item { Layout.preferredHeight: 230 }
        BatchAddRow { objectName: "batchAddRow"; provider: batchShim; Layout.fillWidth: true }
        Item { Layout.fillHeight: true }
    }
}
)qml", QUrl(QStringLiteral("path-completion.qml")));
		if (!component.isReady())
			std::fprintf(stderr, "%s\n", qPrintable(component.errorString()));
		CHECK(component.isReady());
		std::unique_ptr<QObject> object(component.create());
		auto* window = qobject_cast<QQuickWindow*>(object.get());
		CHECK(window && QTest::qWaitForWindowExposed(window));
		settle();
		const auto shot = [&](const QString& name) {
			CHECK(window->grabWindow().save(QDir(output).filePath(name + QStringLiteral(".png"))));
		};
		// No completion ever writes the old grammar's step after a group.
		const auto oneGrammar = [](const QString& text) { return !text.contains(QStringLiteral("/.")) && !text.contains(QStringLiteral("/[")); };

		// ---- The Watchlist's "+" ----
		AddRow watch{window, need(window->contentItem(), QStringLiteral("watchAddRow")), QStringLiteral("watchAdd")};
		watch.begin();
		// An empty field lists the root's groups that hold values; control
		// holds none.
		CHECK(watch.popupOpen());
		CHECK(watch.paths() == (QStringList{QStringLiteral("measure/"), QStringLiteral("device/"), QStringLiteral("signals/"),
		                                    QStringLiteral("demo/")}));
		// A group: "measure/", never "measure/.", and its Fields listed.
		watch.type(QStringLiteral("meas"));
		CHECK(watch.paths().first() == QStringLiteral("measure/"));
		const auto measure = watch.candidate(QStringLiteral("measure/"));
		CHECK(measure.value(QStringLiteral("container")).toBool() && measure.value(QStringLiteral("next")).toString().isEmpty());
		watch.pick(QStringLiteral("measure/"));
		CHECK(watch.text() == QStringLiteral("measure/"));
		CHECK(watch.paths() == (QStringList{QStringLiteral("measure/Voltage"), QStringLiteral("measure/Current"),
		                                    QStringLiteral("measure/Power")}));
		CHECK(watch.popupOpen());
		CHECK(!watch.shownLine().contains(QStringLiteral("Write a path")));
		shot(QStringLiteral("path-completion-watchlist-group"));
		// The same by the keys: Down to the group, Enter goes into it.
		watch.type(QStringLiteral("meas"));
		watch.key(Qt::Key_Down);
		CHECK(watch.row->property("highlighted").toInt() == 0);
		watch.key(Qt::Key_Return);
		CHECK(watch.text() == QStringLiteral("measure/") && watch.paths().size() == 3);
		// Tab completes the common part the same way.
		watch.type(QStringLiteral("mea"));
		watch.key(Qt::Key_Tab);
		CHECK(watch.text() == QStringLiteral("measure/"));
		// A subgroup, one level a pick: demo/ -> demo/adc/ -> demo/adc/channel1/.
		watch.type(QStringLiteral("demo/"));
		CHECK(watch.paths() == QStringList{QStringLiteral("demo/adc/")});
		watch.pick(QStringLiteral("demo/adc/"));
		CHECK(watch.text() == QStringLiteral("demo/adc/"));
		CHECK(watch.paths() == (QStringList{QStringLiteral("demo/adc/channel1/"), QStringLiteral("demo/adc/Offset")}));
		watch.pick(QStringLiteral("demo/adc/channel1/"));
		CHECK(watch.text() == QStringLiteral("demo/adc/channel1/"));
		CHECK(watch.paths() == QStringList{QStringLiteral("demo/adc/channel1/Gain")});
		// An endpoint: its whole path, nothing after a scalar; Enter adds it.
		watch.pick(QStringLiteral("demo/adc/channel1/Gain"));
		CHECK(watch.text() == QStringLiteral("demo/adc/channel1/Gain") && !watch.popupOpen());
		watch.key(Qt::Key_Return);
		CHECK(provider->watchCount() == 1 && watch.text().isEmpty());
		// A member: a struct's Field gets "." and lists its members.
		watch.type(QStringLiteral("device/"));
		CHECK(watch.paths() == (QStringList{QStringLiteral("device/Config"), QStringLiteral("device/Setpoint")}));
		CHECK(watch.candidate(QStringLiteral("device/Config")).value(QStringLiteral("next")) == QStringLiteral("."));
		CHECK(watch.candidate(QStringLiteral("device/Setpoint")).value(QStringLiteral("next")).toString().isEmpty());
		watch.pick(QStringLiteral("device/Config"));
		CHECK(watch.text() == QStringLiteral("device/Config."));
		CHECK(watch.paths() == (QStringList{QStringLiteral("device/Config.Gain"), QStringLiteral("device/Config.Enabled"),
		                                    QStringLiteral("device/Config.Taps")}));
		// A member that is an array gets "[".
		CHECK(watch.candidate(QStringLiteral("device/Config.Taps")).value(QStringLiteral("next")) == QStringLiteral("["));
		watch.pick(QStringLiteral("device/Config.Gain"));
		CHECK(watch.text() == QStringLiteral("device/Config.Gain"));
		watch.key(Qt::Key_Return);
		CHECK(provider->watchCount() == 2);
		// A scalar Field: its path alone.
		watch.type(QStringLiteral("device/Set"));
		watch.pick(QStringLiteral("device/Setpoint"));
		CHECK(watch.text() == QStringLiteral("device/Setpoint"));
		// An element: an array's Field gets "[" and lists its elements; the
		// index typed, Enter adds it.
		watch.type(QStringLiteral("signals/"));
		CHECK(watch.candidate(QStringLiteral("signals/Samples")).value(QStringLiteral("next")) == QStringLiteral("["));
		watch.pick(QStringLiteral("signals/Samples"));
		CHECK(watch.text() == QStringLiteral("signals/Samples["));
		CHECK(watch.paths() == QStringList{QStringLiteral("signals/Samples[0 … 7]")});
		watch.pick(QStringLiteral("signals/Samples[0 … 7]"));
		CHECK(watch.text() == QStringLiteral("signals/Samples["));
		watch.more(QStringLiteral("3]"));
		CHECK(watch.text() == QStringLiteral("signals/Samples[3]"));
		watch.key(Qt::Key_Return);
		CHECK(provider->watchCount() == 3);
		// An element that is a struct: its members after the index.
		watch.type(QStringLiteral("signals/Points[1]."));
		CHECK(watch.paths() == (QStringList{QStringLiteral("signals/Points[1].x"), QStringLiteral("signals/Points[1].y")}));
		watch.pick(QStringLiteral("signals/Points[1].y"));
		CHECK(watch.text() == QStringLiteral("signals/Points[1].y"));
		watch.key(Qt::Key_Return);
		CHECK(provider->watchCount() == 4);
		const QStringList watched{QStringLiteral("demo/adc/channel1/Gain"), QStringLiteral("device/Config.Gain"),
		                          QStringLiteral("signals/Samples[3]"), QStringLiteral("signals/Points[1].y")};
		for (int row = 0; row < watched.size(); ++row)
			CHECK(provider->watchlist()->index(row, 0).data(ux::WatchlistModel::NameRole).toString() == watched[row]);
		CHECK(oneGrammar(watch.text()));
		QTest::keyClick(window, Qt::Key_Escape);
		QTest::keyClick(window, Qt::Key_Escape);
		settle();

		// ---- The Batch's "+": the same rule, every kind it takes ----
		AddRow batch{window, need(window->contentItem(), QStringLiteral("batchAddRow")), QStringLiteral("batchAdd")};
		const auto added = [&] {
			QVariant list;
			QMetaObject::invokeMethod(window, "batchAdded", Q_RETURN_ARG(QVariant, list));
			return list.toStringList();
		};
		batch.begin();
		// Down in an empty field: the root's groups first, each saying what
		// it holds, a group of Commands among them.
		batch.key(Qt::Key_Down);
		CHECK(batch.popupOpen());
		CHECK(batch.paths().mid(0, 5) == (QStringList{QStringLiteral("measure/"), QStringLiteral("device/"), QStringLiteral("signals/"),
		                                              QStringLiteral("demo/"), QStringLiteral("control/")}));
		CHECK(batch.candidate(QStringLiteral("measure/")).value(QStringLiteral("typeText")) == QStringLiteral("3 Fields"));
		CHECK(batch.candidate(QStringLiteral("control/")).value(QStringLiteral("typeText")) == QStringLiteral("1 Command, 1 Service"));
		shot(QStringLiteral("path-completion-batch-root"));
		QTest::keyClick(window, Qt::Key_Escape);
		settle();
		// A group: "measure/" and its Fields listed.
		batch.type(QStringLiteral("meas"));
		CHECK(batch.paths().first() == QStringLiteral("measure/"));
		CHECK(batch.candidate(QStringLiteral("measure/")).value(QStringLiteral("next")).toString().isEmpty());
		batch.pick(QStringLiteral("measure/"));
		CHECK(batch.text() == QStringLiteral("measure/"));
		CHECK(batch.paths() == (QStringList{QStringLiteral("measure/Voltage"), QStringLiteral("measure/Current"),
		                                    QStringLiteral("measure/Power")}));
		CHECK(added().isEmpty());
		shot(QStringLiteral("path-completion-batch-group"));
		// A subgroup by Enter: the group highlighted goes in, one level a press.
		batch.type(QStringLiteral("demo/"));
		CHECK(batch.paths().first() == QStringLiteral("demo/adc/"));
		CHECK(batch.checkLine() == QStringLiteral("Enter goes into demo/adc/."));
		batch.key(Qt::Key_Return);
		CHECK(batch.text() == QStringLiteral("demo/adc/") && added().isEmpty());
		CHECK(batch.paths() == (QStringList{QStringLiteral("demo/adc/channel1/"), QStringLiteral("demo/adc/channel1/Gain"),
		                                    QStringLiteral("demo/adc/Offset")}));
		batch.key(Qt::Key_Return);
		CHECK(batch.text() == QStringLiteral("demo/adc/channel1/") && added().isEmpty());
		// An endpoint: its whole path; Enter adds it.
		batch.pick(QStringLiteral("demo/adc/channel1/Gain"));
		CHECK(batch.text() == QStringLiteral("demo/adc/channel1/Gain") && !batch.popupOpen());
		batch.key(Qt::Key_Return);
		CHECK(added() == QStringList{QStringLiteral("demo/adc/channel1/Gain")});
		CHECK(batch.text().isEmpty());
		// A struct's Field is the Batch's item: picked, it is its path; "."
		// typed after it lists its members, one of which adds a read of it.
		batch.type(QStringLiteral("device/"));
		batch.pick(QStringLiteral("device/Config"));
		CHECK(batch.text() == QStringLiteral("device/Config"));
		batch.more(QStringLiteral("."));
		CHECK(batch.paths() == (QStringList{QStringLiteral("device/Config.Gain"), QStringLiteral("device/Config.Enabled"),
		                                    QStringLiteral("device/Config.Taps")}));
		CHECK(batch.candidate(QStringLiteral("device/Config.Taps")).value(QStringLiteral("next")) == QStringLiteral("["));
		batch.pick(QStringLiteral("device/Config.Gain"));
		CHECK(batch.text() == QStringLiteral("device/Config.Gain"));
		batch.key(Qt::Key_Return);
		CHECK(added().size() == 2 && added().last() == QStringLiteral("device/Config.Gain"));
		// An element: "[" typed lists them, the index typed, Enter adds.
		batch.type(QStringLiteral("signals/Samples["));
		CHECK(batch.paths() == QStringList{QStringLiteral("signals/Samples[0 … 7]")});
		batch.pick(QStringLiteral("signals/Samples[0 … 7]"));
		CHECK(batch.text() == QStringLiteral("signals/Samples["));
		batch.more(QStringLiteral("3]"));
		CHECK(batch.text() == QStringLiteral("signals/Samples[3]"));
		batch.key(Qt::Key_Return);
		CHECK(added().size() == 3 && added().last() == QStringLiteral("signals/Samples[3]"));
		// An element that is a struct, by the keys: Down to a member, Enter adds.
		batch.type(QStringLiteral("signals/Points[2]."));
		CHECK(batch.paths() == (QStringList{QStringLiteral("signals/Points[2].x"), QStringLiteral("signals/Points[2].y")}));
		batch.key(Qt::Key_Down);
		batch.key(Qt::Key_Down);
		batch.key(Qt::Key_Return);
		CHECK(added().size() == 4 && added().last() == QStringLiteral("signals/Points[2].y"));
		// A group of Commands and Services lists them; a Command adds as is.
		batch.type(QStringLiteral("control/"));
		CHECK(batch.paths() == (QStringList{QStringLiteral("control/Reset"), QStringLiteral("control/Scale")}));
		batch.pick(QStringLiteral("control/Reset"));
		CHECK(batch.text() == QStringLiteral("control/Reset"));
		CHECK(oneGrammar(batch.text()));

		object.reset();
		connection.disconnect();
		settle();
		CHECK(warnings == 0);
		std::printf("Path completion checks=%d warnings=%d\n", checks, warnings);
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Path completion check failed after %d checks: %s\n", checks, error.what());
		return 1;
	}
}
