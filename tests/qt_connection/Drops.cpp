// Drag and drop with nested groups (the designer's section 16) and the
// Watchlist on a group tree (part B of section 14), through the real
// provider of a real TCP connection: a device whose descriptor is a group
// tree with named types (served by the demo peer in place of its own; the
// checks read nothing from the device). Saved Watchlists of either form,
// completion one segment at a time, the provider's plan and drop for the
// Watchlist, the Plot and the Batch at any depth, one Undo per drop; then the
// panels, dragged over with a real drag's events: the frames' words, a
// refusal in words, colour and glyph, the drag's badge. MIT.
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <Telemetry/UX/UndoHistory.hpp>
#include <Telemetry/UX/WatchlistModel.hpp>
#include <Telemetry/UX/WatchTraces.hpp>
#include <integration/qt/ConnectionSession.hpp>
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include "TcpDemoServer.hpp"
#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMimeData>
#include <QtGui/QDrag>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickItemGrabResult>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>

namespace drops_fixture {
namespace ts = telemetry;
} // namespace drops_fixture

namespace adc {
enum class Mode : std::uint8_t { Idle, Run };
struct Config {
	float Gain = 1.0f;
	Mode mode = Mode::Idle;
};
struct Reference {
	float volts = 0;
};
struct Offset {
	float value = 0;
};
} // namespace adc
namespace metrology {
enum class Window : std::uint8_t { Rect, Hann };
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

namespace drops_fixture {
template<int N>
float value() noexcept
{
	return float(N);
}
inline adc::Config config() noexcept { return {}; }
inline ts::WriteResult setConfig(const adc::Config&) noexcept { return ts::WriteResult::Applied; }
inline metrology::Spectrum spectrum() noexcept { return {}; }
inline metrology::Config meterConfig() noexcept { return {}; }
inline ts::CommandResult reset() noexcept { return ts::CommandResult::Executed; }
inline ts::CommandResult clear() noexcept { return ts::CommandResult::Executed; }
inline adc::Offset calibrate(const adc::Reference& reference) noexcept { return {reference.volts}; }

inline constexpr auto adcBranch =
    ts::group("adc", ts::group("channel1", ts::field<&value<1>>("Gain")), ts::field<&config, &setConfig>("Config"),
              ts::command<&reset>("Reset"), ts::service<&calibrate>("Calibrate"));
inline constexpr auto powerBranch = ts::group(
    "power",
    ts::group("metrology",
              ts::group("harmonics", ts::group("phaseA", ts::field<&value<2>>("H3"), ts::field<&spectrum>("Spectrum")),
                        ts::group("phaseB", ts::field<&value<3>>("H3"), ts::field<&spectrum>("Spectrum"))),
              ts::field<&meterConfig>("Config"), ts::command<&clear>("Clear")),
    ts::group("meter", ts::field<&value<4>>("V01"), ts::field<&value<5>>("V02"), ts::field<&value<6>>("V03"),
              ts::field<&value<7>>("V04")));
inline constexpr ts::Model model{adcBranch, powerBranch};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();
} // namespace drops_fixture

namespace {
namespace ux = telemetry::ux;
namespace host = telemetry::integration::qt;
unsigned checked = 0;
#define DCHECK(value)                                                                              \
	do {                                                                                           \
		++checked;                                                                                 \
		if (!(value)) {                                                                            \
			std::fprintf(stderr, "Drops check %u line %d: %s\n", checked, __LINE__, #value);       \
			std::abort();                                                                          \
		}                                                                                          \
	} while (false)

bool waitFor(const std::function<bool()>& ready)
{
	QElapsedTimer clock;
	clock.start();
	while (!ready() && clock.elapsed() < 5000)
		QTest::qWait(5);
	return ready();
}
QQuickItem* find(QQuickItem* root, const QString& name)
{
	if (!root)
		return nullptr;
	if (root->objectName() == name)
		return root;
	for (auto* child : root->childItems())
		if (auto* found = find(child, name))
			return found;
	return nullptr;
}
QVariantMap item(const char* kind, const char* path)
{
	return {{QStringLiteral("kind"), QString::fromLatin1(kind)}, {QStringLiteral("path"), QString::fromLatin1(path)}};
}
QStringList paths(const QVariantList& candidates)
{
	QStringList out;
	for (const auto& candidate : candidates)
		out << candidate.toMap().value(QStringLiteral("path")).toString();
	return out;
}
QStringList names(QAbstractItemModel* list)
{
	QStringList out;
	for (int row = 0; row < list->rowCount(); ++row)
		out << list->index(row, 0).data(ux::WatchlistModel::NameRole).toString();
	return out;
}
// A drag from the Device tree as a platform delivers it to a window.
QMimeData* payload(const QVariantList& items)
{
	auto* data = new QMimeData;
	QJsonArray list;
	QStringList written;
	for (const auto& entry : items) {
		list.append(QJsonObject::fromVariantMap(entry.toMap()));
		written << entry.toMap().value(QStringLiteral("path")).toString();
	}
	const QJsonObject object{{QStringLiteral("from"), QStringLiteral("device")}, {QStringLiteral("items"), list},
	                         {QStringLiteral("paths"), QJsonArray::fromStringList(written)}};
	data->setData(QStringLiteral("application/x-telemetry-paths"), QJsonDocument(object).toJson(QJsonDocument::Compact));
	data->setText(written.join(QLatin1Char('\n')));
	return data;
}
QPoint centre(QQuickItem* item)
{
	return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}
void enter(QQuickWindow* window, QPoint at, QMimeData* data)
{
	QDragEnterEvent entered(QPointF(at), Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(window, &entered);
	QDragMoveEvent moved(QPointF(at), Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(window, &moved);
	QTest::qWait(60);
}
bool drop(QQuickWindow* window, QPoint at, QMimeData* data)
{
	QDropEvent dropped(QPointF(at), Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(window, &dropped);
	QTest::qWait(60);
	// The window forgets this drag; the next one enters afresh.
	QDragLeaveEvent left;
	QCoreApplication::sendEvent(window, &left);
	QTest::qWait(30);
	return dropped.isAccepted();
}
void leave(QQuickWindow* window)
{
	QDragLeaveEvent left;
	QCoreApplication::sendEvent(window, &left);
	QTest::qWait(30);
}
} // namespace

unsigned dropsAtAnyDepth()
{
	inspector_qml::TcpDemoServer peer;
	peer.setDescriptorOverride(QByteArray(reinterpret_cast<const char*>(drops_fixture::descriptorBytes.data()),
	                                      qsizetype(drops_fixture::descriptorBytes.size())));
	DCHECK(peer.listen());
	host::ConnectionSession connection;
	ux::QtConnectionBinding presentation{&connection};
	auto* provider = presentation.createProvider();
	ux::UndoHistory history;
	provider->setHistory(&history);
	DCHECK(waitFor([&] { return !peer.hasClient(); }));
	DCHECK(connection.connectTcpReference(QStringLiteral("127.0.0.1"), peer.port()));
	if (!waitFor([&] { return connection.ready(); }))
		std::fprintf(stderr, "Drops admission: %s\n", qPrintable(connection.lastError()));
	DCHECK(connection.ready());
	DCHECK(waitFor([&] { return provider->connected() && provider->endpoints()->rowCount() == 2; }));
	auto* tree = qobject_cast<ux::EndpointModel*>(provider->endpoints());
	DCHECK(tree && tree->indexOfGroupPath(QStringLiteral("power/metrology/harmonics/phaseA")).isValid());

	// ---- The Watchlist on a group tree (14.3, 14.4 item 5) ----
	// Completion one segment per Tab, at any depth.
	DCHECK(paths(provider->watchPathCandidates(QStringLiteral("po"))) == QStringList{QStringLiteral("power/")});
	DCHECK(paths(provider->watchPathCandidates(QStringLiteral("power/"))) ==
	       (QStringList{QStringLiteral("power/metrology/"), QStringLiteral("power/meter/")}));
	const auto metrology = provider->watchPathCandidates(QStringLiteral("power/metrology/"));
	DCHECK(paths(metrology) == (QStringList{QStringLiteral("power/metrology/harmonics/"), QStringLiteral("power/metrology/Config")}));
	DCHECK(metrology.first().toMap().value(QStringLiteral("values")).toInt() == 4 &&
	       metrology.first().toMap().value(QStringLiteral("container")).toBool());
	DCHECK(paths(provider->watchPathCandidates(QStringLiteral("power/metrology/harmonics/phaseA/S"))) ==
	       QStringList{QStringLiteral("power/metrology/harmonics/phaseA/Spectrum")});
	DCHECK(provider->checkWatchPath(QStringLiteral("power/metrology/harmonics/phaseA/Spectrum.window")).value("adds").toInt() == 1);
	DCHECK(provider->checkWatchPath(QStringLiteral("power/metrology/Clear")).value("message").toString() ==
	       QStringLiteral("power/metrology/Clear is a Command. Only Fields can be watched."));
	// Saved by the group's path; an older project's flat binding reopens; a
	// group this device lacks is kept, its path written as a tree's.
	auto added = provider->addWatchPaths({QStringLiteral("power/metrology/harmonics/phaseA/H3"), QStringLiteral("adc/Config.Gain")});
	DCHECK(added.value("added").toInt() == 2);
	const auto saved = provider->watchBindings();
	DCHECK(saved.size() == 2 &&
	       saved[0].toMap() == (QVariantMap{{QStringLiteral("group"), QStringLiteral("power/metrology/harmonics/phaseA")},
	                                        {QStringLiteral("name"), QStringLiteral("H3")}}) &&
	       saved[1].toMap() == (QVariantMap{{QStringLiteral("group"), QStringLiteral("adc")},
	                                        {QStringLiteral("name"), QStringLiteral("Config")},
	                                        {QStringLiteral("member"), QStringLiteral("Gain")}}));
	QVariantList reopened = saved;
	reopened.append(QVariantMap{{QStringLiteral("group"), QStringLiteral("power/old/nested")}, {QStringLiteral("name"), QStringLiteral("X")}});
	const auto restored = provider->restoreWatchBindings(reopened);
	DCHECK(restored.value("missing").toList() == QVariantList{2});
	DCHECK(names(provider->watchlist()) == (QStringList{QStringLiteral("power/metrology/harmonics/phaseA/H3"),
	                                                    QStringLiteral("adc/Config.Gain"), QStringLiteral("power/old/nested/X")}));
	// The dotted key of an older project reads against the groups.
	DCHECK(provider->checkWatchPath(QStringLiteral("adc.Config.mode")).value("adds").toInt() == 1);
	provider->clearWatch();
	DCHECK(provider->watchCount() == 0);

	// ---- The provider's drops (16.1) ----
	const QVariantList metrologyGroup{item("group", "power/metrology")};
	// The Watchlist: every value under the group at any depth, Clear left out.
	DCHECK(provider->addWatchPaths({QStringLiteral("power/metrology/harmonics/phaseA/H3")}).value("added").toInt() == 1);
	auto plan = provider->dropPlan(metrologyGroup, QStringLiteral("watchlist"));
	DCHECK(plan.value("status") == "ok" && plan.value("count").toInt() == 25 && plan.value("already").toInt() == 1);
	DCHECK(plan.value("words") == QStringLiteral("Drop to add 24 values from metrology · 1 already watched"));
	DCHECK(plan.value("leftOut") == QStringLiteral("1 Command"));
	auto done = provider->dropOn(metrologyGroup, QStringLiteral("watchlist"));
	DCHECK(done.value("added").toInt() == 24 && provider->watchCount() == 25);
	DCHECK(done.value("message") == QStringLiteral("Added 24 values from metrology · 1 Command is not a value"));
	DCHECK(done.value("serial").toInt() == history.undoSerial());
	// All held: said, and the rows that hold them pointed at.
	plan = provider->dropPlan(metrologyGroup, QStringLiteral("watchlist"));
	DCHECK(plan.value("status") == "held" && plan.value("words") == QStringLiteral("Everything in metrology is already watched"));
	done = provider->dropOn(metrologyGroup, QStringLiteral("watchlist"));
	DCHECK(done.value("rows").toList().size() == 25 && provider->watchCount() == 25);
	// One Undo takes the drop back.
	DCHECK(history.undo() && provider->watchCount() == 1);
	// Commands and Services alone are refused in words.
	plan = provider->dropPlan({item("command", "adc/Reset"), item("service", "adc/Calibrate")}, QStringLiteral("watchlist"));
	DCHECK(plan.value("status") == "refused" &&
	       plan.value("refusal") == QStringLiteral("Commands and Services are not values; the Watchlist takes Fields."));
	// A row under a picked group counts once: the group and its own Field.
	plan = provider->dropPlan({item("group", "power/meter"), item("field", "power/meter/V02")}, QStringLiteral("watchlist"));
	DCHECK(plan.value("count").toInt() == 4);

	// The Plot: all or nothing, the Watchlist untouched by a refusal.
	auto* traces = qobject_cast<ux::WatchTraces*>(provider->watchTraces());
	DCHECK(traces && traces->plotted().isEmpty());
	plan = provider->dropPlan(metrologyGroup, QStringLiteral("plot"));
	DCHECK(plan.value("status") == "refused" &&
	       plan.value("refusal") == QStringLiteral("Can't plot 25 values: this Plot has 16 free curves of its 16."));
	done = provider->dropOn(metrologyGroup, QStringLiteral("plot"));
	DCHECK(done.value("added").toInt() == 0 && provider->watchCount() == 1 && traces->plotted().isEmpty());
	const QVariantList two{item("field", "power/metrology/harmonics/phaseB/H3"), item("member", "adc/Config.Gain")};
	plan = provider->dropPlan(two, QStringLiteral("plot"));
	DCHECK(plan.value("status") == "ok" && plan.value("words") == QStringLiteral("Drop to plot 2 values · 2 join the Watchlist"));
	done = provider->dropOn(two, QStringLiteral("plot"));
	DCHECK(done.value("added").toInt() == 2 && provider->watchCount() == 3 && traces->plotted().size() == 2);
	// One step for the rows and the curves: one Undo takes both.
	DCHECK(history.undoText() == QStringLiteral("Plot 2 values") && done.value("serial").toInt() == history.undoSerial());
	DCHECK(history.undo() && traces->plotted().isEmpty() && provider->watchCount() == 1);
	plan = provider->dropPlan({item("field", "power/metrology/harmonics/phaseA/H3")}, QStringLiteral("plot"));
	DCHECK(plan.value("words") == QStringLiteral("Drop to plot power/metrology/harmonics/phaseA/H3"));

	// The Batch: one item per Field under a group; a Command only when
	// picked by itself; this device has no Batch, which is said.
	plan = provider->dropPlan({item("group", "power")}, QStringLiteral("batch"));
	DCHECK(plan.value("lines").toStringList() ==
	       (QStringList{QStringLiteral("power/metrology/harmonics/phaseA/H3"), QStringLiteral("power/metrology/harmonics/phaseA/Spectrum"),
	                    QStringLiteral("power/metrology/harmonics/phaseB/H3"), QStringLiteral("power/metrology/harmonics/phaseB/Spectrum"),
	                    QStringLiteral("power/metrology/Config"), QStringLiteral("power/meter/V01"), QStringLiteral("power/meter/V02"),
	                    QStringLiteral("power/meter/V03"), QStringLiteral("power/meter/V04")}));
	DCHECK(plan.value("leftOut") == QStringLiteral("1 Command") && plan.value("count").toInt() == 9);
	DCHECK(plan.value("status") == "refused" && plan.value("refusal") == QStringLiteral("This device has no Batch."));
	plan = provider->dropPlan({item("command", "power/metrology/Clear")}, QStringLiteral("batch"));
	DCHECK(plan.value("lines").toStringList() == QStringList{QStringLiteral("power/metrology/Clear")});
	plan = provider->dropPlan({item("group", "adc"), item("field", "adc/Config")}, QStringLiteral("batch"));
	DCHECK(plan.value("lines").toStringList() == (QStringList{QStringLiteral("adc/channel1/Gain"), QStringLiteral("adc/Config")}));
	plan = provider->dropPlan({item("group", "adc")}, QStringLiteral("batch"));
	DCHECK(plan.value("leftOut") == QStringLiteral("1 Command and 1 Service"));

	// ---- The panels, dragged over with a real drag's events (16.3) ----
	QQmlEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("dropProvider"), provider);
	QQmlComponent component(&engine);
	component.setData(R"qml(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX
ApplicationWindow {
    width: 1180; height: 640; visible: true
    color: Theme.surfaceWindow
    RowLayout {
        anchors.fill: parent
        EndpointTreePanel { objectName: "dropTree"; provider: dropProvider; Layout.fillHeight: true; Layout.preferredWidth: 300 }
        WatchlistPanel { objectName: "dropWatch"; provider: dropProvider; collapsible: false; Layout.fillHeight: true; Layout.preferredWidth: 420 }
        TracePlot { objectName: "dropPlot"; traces: dropProvider.watchTraces; provider: dropProvider; Layout.fillHeight: true; Layout.fillWidth: true }
    }
}
)qml", QUrl(QStringLiteral("drops.qml")));
	if (!component.isReady())
		std::fprintf(stderr, "%s\n", qPrintable(component.errorString()));
	DCHECK(component.isReady());
	std::unique_ptr<QObject> object(component.create());
	auto* window = qobject_cast<QQuickWindow*>(object.get());
	DCHECK(window && QTest::qWaitForWindowExposed(window));
	QTest::qWait(200);
	auto* treePanel = window->findChild<QQuickItem*>(QStringLiteral("dropTree"));
	auto* watch = window->findChild<QQuickItem*>(QStringLiteral("dropWatch"));
	auto* plot = window->findChild<QQuickItem*>(QStringLiteral("dropPlot"));
	DCHECK(treePanel && watch && plot);
	auto* watchList = find(watch, QStringLiteral("watchlistView"));
	DCHECK(watchList);
	// Four rows already, so the list stands tall enough to show its frame.
	DCHECK(provider->addWatchPaths({QStringLiteral("power/meter/V01"), QStringLiteral("power/meter/V02"),
	                                QStringLiteral("power/meter/V03"), QStringLiteral("power/meter/V04")})
	           .value("added").toInt() == 4);
	QTest::qWait(100);
	// Over the Watchlist: the frame says what the drop adds, before the release.
	const auto watchAt = centre(watchList);
	std::unique_ptr<QMimeData> group(payload(metrologyGroup));
	enter(window, watchAt, group.get());
	auto* frame = find(watch, QStringLiteral("watchlistDropFrame"));
	auto* words = find(watch, QStringLiteral("watchlistDropText"));
	DCHECK(frame && frame->isVisible() && words);
	DCHECK(words->property("text").toString() == QStringLiteral("Drop to add 24 values from metrology · 1 already watched"));
	const auto themeColor = [&](const char* name) {
		QQmlComponent probe(&engine);
		probe.setData(QByteArray("import QtQuick\nimport Telemetry.UX\nQtObject { property color c: Theme.") + name + "}", QUrl());
		std::unique_ptr<QObject> made(probe.create());
		return made->property("c").value<QColor>();
	};
	DCHECK(frame->property("border").value<QObject*>()->property("color").value<QColor>() == themeColor("focusRing"));
	// The drag's badge as the platform carries it, made at the press: the
	// screenshot draws it at the cursor over the window.
	auto* badge = treePanel->findChild<QQuickItem*>(QStringLiteral("dragBadge"));
	DCHECK(badge);
	QVariant detail;
	DCHECK(QMetaObject::invokeMethod(treePanel, "badgeDetail", Q_RETURN_ARG(QVariant, detail),
	                                 Q_ARG(QVariant, QVariant::fromValue(tree->indexOfGroupPath(QStringLiteral("power/metrology")))),
	                                 Q_ARG(QVariant, QVariantList{})));
	DCHECK(detail.toString() == QStringLiteral("5 Fields · 1 Command"));
	QMetaObject::invokeMethod(badge, "make", Q_ARG(QVariant, QStringLiteral("group")), Q_ARG(QVariant, QStringLiteral("power/metrology")),
	                          Q_ARG(QVariant, detail));
	DCHECK(waitFor([&] { return !badge->property("image").toUrl().isEmpty(); }));
	{
		auto shot = window->grabWindow();
		QImage image;
		// The grab's image is the badge's own rendering.
		QSharedPointer<QQuickItemGrabResult> grab = badge->grabToImage();
		DCHECK(grab && waitFor([&] { return !grab->image().isNull(); }));
		image = grab->image();
		DCHECK(!image.isNull() && image.width() <= 240 * window->devicePixelRatio() + 1);
		QPainter painter(&shot);
		painter.drawImage(QPointF(watchAt) * window->devicePixelRatio() + QPointF(12, 12), image);
		painter.end();
		DCHECK(shot.save(QStringLiteral("drag-badge-over-watchlist.png")));
	}
	drop(window, watchAt, group.get());
	DCHECK(provider->watchCount() == 29);
	// A refusal: the frame turns the error colour with the glyph and words;
	// the drop is refused and the Watchlist stays.
	std::unique_ptr<QMimeData> calls(payload({item("command", "adc/Reset")}));
	// The list moved down under the line that said what the drop did.
	const auto listAt = centre(watchList);
	enter(window, listAt, calls.get());
	DCHECK(words->property("text").toString() == QStringLiteral("Commands and Services are not values; the Watchlist takes Fields."));
	DCHECK(frame->property("border").value<QObject*>()->property("color").value<QColor>() == themeColor("error"));
	DCHECK(find(frame, QStringLiteral("errorGlyph")) && find(frame, QStringLiteral("errorGlyph"))->isVisible());
	drop(window, listAt, calls.get());
	DCHECK(provider->watchCount() == 29);
	DCHECK(!frame->isVisible());
	// Over the Plot: more values than free curves refused, in words.
	const auto plotAt = centre(plot);
	enter(window, plotAt, group.get());
	auto* plotFrame = find(plot, QStringLiteral("plotDropFrame"));
	auto* plotWords = find(plot, QStringLiteral("plotDropText"));
	DCHECK(plotFrame && plotFrame->isVisible() && plotWords);
	DCHECK(plotWords->property("text").toString() == QStringLiteral("Can't plot 25 values: this Plot has 16 free curves of its 16."));
	DCHECK(plotFrame->property("border").value<QObject*>()->property("color").value<QColor>() == themeColor("error"));
	DCHECK(window->grabWindow().save(QStringLiteral("plot-refusal.png")));
	drop(window, plotAt, group.get());
	DCHECK(traces->plotted().isEmpty() && provider->watchCount() == 29);
	// What fits plots, its values joining the Watchlist, one Undo.
	std::unique_ptr<QMimeData> pair(payload(two));
	enter(window, plotAt, pair.get());
	DCHECK(plotWords->property("text").toString() == QStringLiteral("Drop to plot 2 values · 1 joins the Watchlist"));
	drop(window, plotAt, pair.get());
	DCHECK(traces->plotted().size() == 2 && provider->watchCount() == 30);
	DCHECK(history.undo() && traces->plotted().isEmpty() && provider->watchCount() == 29);
	// Esc, or a release outside any target: the frame clears, nothing is said.
	enter(window, plotAt, pair.get());
	DCHECK(plotFrame->isVisible());
	leave(window);
	DCHECK(!plotFrame->isVisible() && traces->plotted().isEmpty());
	object.reset();
	connection.disconnect();
	return checked;
}
