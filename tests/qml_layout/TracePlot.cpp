// The live Plot (LP-1): every watched value's readings kept in a ring by its
// path - ten minutes, at most 65,536 - a read that brought no number breaking
// the line, the decoder the recording shares, and TracePlot drawing what is
// plotted: chips with the newest reading, the time of day under the grid,
// raw lines while two readings fall on a pixel or fewer and each pixel's
// extremes beyond, Follow moving the right edge with now and holding it when
// unticked, sixteen curves at most. Fed with frames as the provider's
// completed reads deliver them. MIT.
#include "Telemetry/UX/QmlTypes.hpp"
#include "Telemetry/UX/WatchTraces.hpp"
#include "Telemetry/UX/UndoHistory.hpp"
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtGui/QClipboard>
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtGui/QWheelEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QTest>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace ux = telemetry::ux;

namespace {
int checks = 0, warnings = 0;
void check(bool condition, const char* expression)
{
	++checks;
	if (!condition)
		throw std::runtime_error(expression);
}
#define CHECK(x) check(bool(x), #x)

QQuickItem* named(QQuickItem* root, const char* name)
{
	if (root->objectName() == QLatin1String(name) && root->isVisible())
		return root;
	for (auto* child : root->childItems())
		if (auto* found = named(child, name))
			return found;
	return nullptr;
}
QQuickItem* shown(QQuickItem* root, const char* name)
{
	auto* found = root ? named(root, name) : nullptr;
	if (!found)
		throw std::runtime_error(std::string("not shown: ") + name);
	return found;
}
QList<QQuickItem*> all(QQuickItem* root, const char* name)
{
	QList<QQuickItem*> out;
	if (root->objectName() == QLatin1String(name) && root->isVisible())
		out.push_back(root);
	for (auto* child : root->childItems())
		out += all(child, name);
	return out;
}
void shot(QQuickWindow* window, int argc, char** argv, const QString& name)
{
	if (argc < 2)
		return;
	QDir().mkpath(QString::fromLocal8Bit(argv[1]));
	CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(name + QStringLiteral(".png"))));
}
void click(QQuickWindow* window, QQuickItem* item)
{
	QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
	                  item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
}
// A visible item whose text starts with prefix: a tooltip's words.
QQuickItem* withText(QQuickItem* root, const QString& prefix)
{
	if (root->isVisible() && root->property("text").toString().startsWith(prefix))
		return root;
	for (auto* child : root->childItems())
		if (auto* found = withText(child, prefix))
			return found;
	return nullptr;
}
// Every time label the view drew is whole: none cut at either edge.
bool labelsWhole(QQuickItem* view)
{
	const auto rects = view->property("timeLabelRects").toList();
	if (rects.isEmpty() || rects.size() != view->property("timeLabels").toStringList().size())
		return false;
	for (const auto& rect : rects)
		if (rect.toRectF().left() < 0 || rect.toRectF().right() > view->width())
			return false;
	return true;
}
void settle(int ms = 60)
{
	QCoreApplication::processEvents();
	QTest::qWait(ms);
	QCoreApplication::processEvents();
}

ux::WatchValueBits bits(ux::WatchScalar scalar, quint8 width, quint64 raw, bool enumeration = false)
{
	ux::WatchValueBits value;
	value.type = {7, scalar, width, enumeration};
	std::byte bytes[8];
	for (int i = 0; i < 8; ++i)
		bytes[i] = std::byte((raw >> (8 * i)) & 0xff);
	value.assign(bytes, width);
	return value;
}
quint64 floatBits(float number)
{
	quint32 out;
	std::memcpy(&out, &number, sizeof(out));
	return out;
}

struct Row {
	quint32 id, leaf;
	ux::WatchValueBits value;
};
// A completed read at ns, as the provider's observer gets it.
ux::WatchFrame frame(qint64 ns, std::initializer_list<Row> rows, bool answered = true)
{
	ux::WatchFrame out;
	out.completed = {ns, QDateTime::currentMSecsSinceEpoch() - (ux::WatchTraces::nowNs() - ns) / 1'000'000};
	out.phase = answered ? ux::WatchCompletionPhase::Current : ux::WatchCompletionPhase::Failed;
	out.native = {0, 0, 0, 0, 0};
	out.count = int(rows.size());
	int i = 0;
	for (const auto& row : rows) {
		auto& target = out.rows[std::size_t(i++)];
		target.id = row.id;
		target.leaf = row.leaf;
		target.itemStatus = 0;
		target.valueStatus = 0;
		target.value = row.value;
	}
	return out;
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	// TraceView is a C++ type of the module: its registration is linked in.
	ux::registerQmlTypes();
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& text) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
			++warnings;
		std::fprintf(stderr, "%s\n", qPrintable(text));
	});
	try {
		using S = ux::WatchScalar;
		// The decoder the recording shares: a number a double holds exactly,
		// a finite float, a bool as 0 or 1, nothing else.
		CHECK(ux::plotValue(bits(S::Boolean, 1, 1)) == 1.0 && !ux::plotValue(bits(S::Boolean, 1, 2)));
		CHECK(ux::plotValue(bits(S::S8, 1, 0xff)) == -1.0 && ux::plotValue(bits(S::S16, 2, 0x8000)) == -32768.0);
		CHECK(ux::plotValue(bits(S::U64, 8, (quint64{1} << 53) - 1)) == double((quint64{1} << 53) - 1));
		CHECK(!ux::plotValue(bits(S::U64, 8, quint64{1} << 53)) && !ux::plotValue(bits(S::S64, 8, quint64{1} << 63)));
		CHECK(ux::plotValue(bits(S::F32, 4, floatBits(1.5f))) == 1.5 &&
		      !ux::plotValue(bits(S::F32, 4, floatBits(std::numeric_limits<float>::quiet_NaN()))));
		CHECK(!ux::plotValue(ux::WatchValueBits{}));

		ux::WatchTraces traces;
		// The Plot's changes go to a history, as a provider gives its own (UR-1).
		ux::UndoHistory history;
		traces.setHistory(&history);
		CHECK(traces.history() == &history);
		constexpr quint64 gain = 3 | quint64{1} << 32;
		traces.setPaths(1, {{1, QStringLiteral("device.Voltage")}, {2, QStringLiteral("device.Ready")}, {gain, QStringLiteral("device.Config.Gain")}},
		                {{QStringLiteral("device.Ready"), true}});
		// Thirty seconds of readings, two a second, whether plotted or not.
		const qint64 now = ux::WatchTraces::nowNs();
		for (int i = 0; i < 60; ++i)
			traces.append(frame(now - 30'000'000'000 + qint64(i) * 500'000'000,
			                    {{1, 0, bits(S::F32, 4, floatBits(1.0f + float(i) * 0.1f))},
			                     {2, 0, bits(S::Boolean, 1, quint64((i / 10) % 2))},
			                     {3, 1, bits(S::S16, 2, quint64(qint16(-100 + i)) & 0xffff)}}));
		CHECK(traces.count(QStringLiteral("device.Voltage")) == 60 && traces.count(QStringLiteral("device.Ready")) == 60 &&
		      traces.count(QStringLiteral("device.Config.Gain")) == 60);
		CHECK(traces.latest(QStringLiteral("device.Config.Gain")).toDouble() == -41.0 && traces.categorical(QStringLiteral("device.Ready")) &&
		      !traces.categorical(QStringLiteral("device.Voltage")));
		CHECK(traces.anchor().monotonicNs == now - 30'000'000'000 && traces.newestNs() == now - 500'000'000);
		// A read that failed keeps its place but brings no number.
		traces.append(frame(now - 400'000'000, {{1, 0, bits(S::F32, 4, floatBits(9.0f))}}, false));
		CHECK(traces.count(QStringLiteral("device.Voltage")) == 61 && !traces.latest(QStringLiteral("device.Voltage")).isValid());
		// A row whose key no path has is not kept.
		traces.append(frame(now - 300'000'000, {{9, 0, bits(S::F32, 4, floatBits(1.0f))}}));
		CHECK(traces.readings(QStringLiteral("device.Missing")) == nullptr);

		QQmlApplicationEngine engine;
		engine.rootContext()->setContextProperty(QStringLiteral("fixtureTraces"), &traces);
		engine.load(QUrl(QStringLiteral("qrc:/TracePlot.qml")));
		CHECK(!engine.rootObjects().isEmpty());
		auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
		CHECK(window && QTest::qWaitForWindowExposed(window));
		settle();
		auto* content = window->contentItem();
		// Nothing plotted: how to plot, and no empty grid.
		CHECK(shown(content, "plotEmpty") && !named(content, "plotView") && !named(content, "plotLegend"));
		shot(window, argc, argv, QStringLiteral("trace-plot-empty-760"));
		// Rows have no grip: the empty Plot says that they can be dragged, and
		// a drag from the Device tree says it joins the Watchlist first.
		auto* tracePlot = window->findChild<QObject*>(QStringLiteral("tracePlot"));
		CHECK(tracePlot && shown(content, "plotEmptyHint")->property("text").toString() ==
		                       QStringLiteral("Right-click a row and choose Plot, press Ctrl+P, or drag rows here."));
		const auto dropWords = [&](const QString& from, const QStringList& paths) {
			QVariant words;
			CHECK(QMetaObject::invokeMethod(tracePlot, "dropWords", Q_RETURN_ARG(QVariant, words),
			                                Q_ARG(QVariant, QVariant(QVariantMap{{QStringLiteral("from"), from}, {QStringLiteral("paths"), paths}}))));
			return words.toString();
		};
		CHECK(dropWords(QStringLiteral("device"), {QStringLiteral("device.Config")}) ==
		      QStringLiteral("Drop to add device.Config to the Watchlist and plot it"));
		CHECK(dropWords(QStringLiteral("device"), {QStringLiteral("device.A"), QStringLiteral("device.B"), QStringLiteral("device.C")}) ==
		      QStringLiteral("Drop to add 3 to the Watchlist and plot them"));
		CHECK(dropWords(QStringLiteral("watchlist"), {QStringLiteral("device.Voltage"), QStringLiteral("device.Ready")}) ==
		      QStringLiteral("Drop to plot 2 values"));
		const double headerBefore = shown(content, "plotFollow")->parentItem()->height();

		// Plotted before anything is read: it says it is waiting.
		traces.setPaths(4, {{1, QStringLiteral("device.Voltage")}, {2, QStringLiteral("device.Ready")},
		                    {gain, QStringLiteral("device.Config.Gain")}, {8, QStringLiteral("device.Later")}},
		                {{QStringLiteral("device.Ready"), true}}, now - 450'000'000);
		CHECK(traces.plot(QStringLiteral("device.Later")).isEmpty());
		settle(400);
		CHECK(shown(content, "plotWaiting")->property("text").toString() ==
		      QStringLiteral("Waiting for the first reading of device.Later…") && !named(content, "plotView"));
		traces.unplot(QStringLiteral("device.Later"));
		settle();

		// Plotted: a chip with its path and newest reading, the grid, the
		// time of day under it.
		CHECK(traces.plot(QStringLiteral("device.Voltage")).isEmpty() && traces.plot(QStringLiteral("device.Ready")).isEmpty());
		CHECK(traces.colorIndex(QStringLiteral("device.Voltage")) == 0 && traces.colorIndex(QStringLiteral("device.Ready")) == 1);
		CHECK(traces.plot(QStringLiteral("device.Voltage")).isEmpty() && traces.plotted().size() == 2);
		traces.append(frame(now - 200'000'000, {{1, 0, bits(S::F32, 4, floatBits(7.25f))}, {2, 0, bits(S::Boolean, 1, 1)}}));
		settle(400);
		CHECK(!named(content, "plotEmpty"));
		const auto chips = all(content, "plotChipName");
		CHECK(chips.size() == 2 && chips[0]->property("text").toString() == QStringLiteral("device.Voltage"));
		const auto values = all(content, "plotChipValue");
		CHECK(values.size() == 2 && values[0]->property("text").toString() == QStringLiteral("7.25") &&
		      values[1]->property("text").toString() == QStringLiteral("true"));
		auto* view = shown(content, "plotView");
		CHECK(view->property("following").toBool() && view->property("spanSeconds").toDouble() == 60.0);
		CHECK(view->property("drawnCurves").toInt() == 2 && view->property("rawCurves").toInt() == 2);
		// LP-1d. A bool has a lane of its own under the numbers, named by its
		// leaf; it is never on their axis.
		CHECK(view->property("laneLabels").toStringList() == QStringList({QStringLiteral("Ready")}));
		const QRegularExpression seconds(QStringLiteral("^\\d\\d:\\d\\d:\\d\\d$"));
		const auto timeLabels = view->property("timeLabels").toStringList();
		CHECK(timeLabels.size() >= 3);
		for (const auto& label : timeLabels)
			CHECK(seconds.match(label).hasMatch());
		CHECK(!view->property("valueLabels").toStringList().isEmpty());
		shot(window, argc, argv, QStringLiteral("trace-plot-760"));
		// The Measure toggle is a segment as the spans are: no taller than
		// they, so the header keeps its height when the first curve comes.
		CHECK(shown(content, "plotMeasureToggle")->height() == shown(content, "plotPreset_10")->height() &&
		      shown(content, "plotFollow")->parentItem()->height() == headerBefore);
		// Only whole time labels: one the edge would cut is left out.
		CHECK(labelsWhole(view));

		// Following, the right edge is the newest reading: it rests while
		// nothing comes and moves with the next one; unticked, it holds.
		const double before = view->property("rightUtcMs").toDouble();
		settle(300);
		CHECK(view->property("rightUtcMs").toDouble() == before);
		traces.append(frame(ux::WatchTraces::nowNs(), {{2, 0, bits(S::Boolean, 1, 1)}}));
		settle(150);
		CHECK(view->property("rightUtcMs").toDouble() > before + 150);
		auto* follow = shown(content, "plotFollow");
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
		                  follow->mapToScene(QPointF(follow->width() / 2, follow->height() / 2)).toPoint());
		settle();
		CHECK(!view->property("following").toBool() && !follow->property("checked").toBool());
		const double held = view->property("rightUtcMs").toDouble();
		traces.append(frame(ux::WatchTraces::nowNs(), {{1, 0, bits(S::F32, 4, floatBits(8.0f))}}));
		settle(300);
		CHECK(view->property("rightUtcMs").toDouble() == held);
		CHECK(view->setProperty("following", true));
		settle();
		CHECK(follow->property("checked").toBool());

		// Under a second, the labels carry the decimals the step needs.
		CHECK(view->setProperty("spanSeconds", 1.0));
		settle(120);
		const QRegularExpression tenths(QStringLiteral("^\\d\\d:\\d\\d:\\d\\d\\.\\d$"));
		const auto fine = view->property("timeLabels").toStringList();
		CHECK(!fine.isEmpty());
		for (const auto& label : fine)
			CHECK(tenths.match(label).hasMatch());
		CHECK(view->setProperty("spanSeconds", 60.0));

		// More readings than two a pixel: each pixel's extremes, not every one.
		const qint64 dense = ux::WatchTraces::nowNs();
		for (int i = 0; i < 20000; ++i)
			traces.append(frame(dense - 50'000'000'000 + qint64(i) * 2'500'000,
			                    {{3, 1, bits(S::S16, 2, quint64(qint16(i % 200 - 100)) & 0xffff)}}));
		CHECK(traces.plot(QStringLiteral("device.Config.Gain")).isEmpty());
		settle(200);
		CHECK(view->property("drawnCurves").toInt() == 3 && view->property("rawCurves").toInt() == 2);
		const auto area = view->property("plotArea").toRectF();
		CHECK(area.width() > 400 && view->property("drawnPoints").toInt() < 400 + int(area.width() * window->effectiveDevicePixelRatio()) * 4 + 8);
		shot(window, argc, argv, QStringLiteral("trace-plot-dense-760"));

		// LP-1d. An enum's lane names its states where they fit; its chip says
		// the state in words, a code no name has as "Code 7".
		traces.setPaths(5, {{1, QStringLiteral("device.Voltage")}, {2, QStringLiteral("device.Ready")},
		                    {gain, QStringLiteral("device.Config.Gain")}, {10, QStringLiteral("device.Mode")}},
		                {{QStringLiteral("device.Ready"), true}, {QStringLiteral("device.Mode"), true}});
		traces.setStates(QStringLiteral("device.Mode"), {{0, QStringLiteral("Idle")}, {1, QStringLiteral("Run")}});
		const qint64 modeAt = ux::WatchTraces::nowNs();
		for (int i = 0; i < 20; ++i)
			traces.append(frame(modeAt - 20'000'000'000 + qint64(i) * 1'000'000'000,
			                    {{10, 0, bits(S::U8, 1, quint64(i < 10 ? 0 : i < 18 ? 1 : 7), true)}}));
		CHECK(traces.categorical(QStringLiteral("device.Mode")) &&
		      traces.stateText(QStringLiteral("device.Mode"), 1.0) == QStringLiteral("Run") &&
		      traces.stateText(QStringLiteral("device.Mode"), 7.0) == QStringLiteral("Code 7") &&
		      traces.stateText(QStringLiteral("device.Voltage"), 1.0).isEmpty());
		CHECK(traces.plot(QStringLiteral("device.Mode")).isEmpty());
		settle(400);
		CHECK(view->property("laneLabels").toStringList() == QStringList({QStringLiteral("Ready"), QStringLiteral("Mode")}));
		const auto states = view->property("laneStates").toStringList();
		CHECK(states.contains(QStringLiteral("Idle")) && states.contains(QStringLiteral("Run")));
		const auto modeValues = all(content, "plotChipValue");
		CHECK(modeValues.size() == 4 && modeValues[3]->property("text").toString() == QStringLiteral("Code 7"));
		shot(window, argc, argv, QStringLiteral("trace-plot-lanes-760"));
		traces.unplot(QStringLiteral("device.Mode"));
		settle();

		// LP-1b. Nine minutes kept, so there is somewhere to go.
		traces.append(frame(dense - 540'000'000'000, {{3, 1, bits(S::S16, 2, 0)}}));
		settle(150);
		// A preset sets the span and follows the newest again.
		const auto span = [view] { return view->property("spanSeconds").toDouble(); };
		const auto following = [view] { return view->property("following").toBool(); };
		click(window, shown(content, "plotPreset_10"));
		settle();
		CHECK(span() == 10 && following() && view->property("preset").toInt() == 10 &&
		      shown(content, "plotPreset_10")->property("checked").toBool() && !named(content, "plotBehind"));
		click(window, shown(content, "plotPreset_0"));
		settle(150);
		CHECK(view->property("showAll").toBool() && view->property("preset").toInt() == 0 && following());
		click(window, shown(content, "plotPreset_60"));
		settle(150);
		CHECK(span() == 60 && !view->property("showAll").toBool() && view->property("preset").toInt() == 60);
		// A drag pans back in time and stops following; the chip in the
		// corner says how far behind and follows again.
		const auto middle = view->mapToScene(QPointF(view->width() / 2, view->height() / 2)).toPoint();
		QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, middle);
		QTest::mouseMove(window, middle + QPoint(100, 0));
		QTest::mouseMove(window, middle + QPoint(200, 0));
		QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, middle + QPoint(200, 0));
		settle(200);
		CHECK(!following() && view->property("preset").toInt() == 60 && view->property("behindSeconds").toDouble() > 10);
		auto* behind = shown(content, "plotBehind");
		CHECK(behind->property("text").toString().endsWith(QStringLiteral(" behind · Follow (End)")) &&
		      !shown(content, "plotFollow")->property("checked").toBool());
		shot(window, argc, argv, QStringLiteral("trace-plot-behind-760"));
		click(window, behind);
		settle(150);
		CHECK(following() && !named(content, "plotBehind"));
		// Keys on the graph: + zooms in about the middle and leaves every
		// preset; End follows; Left pans a tenth back; Space holds and
		// follows; Home goes to the oldest reading kept; 4 and 2 choose.
		view->forceActiveFocus(Qt::TabFocusReason);
		QTest::keyClick(window, Qt::Key_Plus);
		settle();
		CHECK(std::abs(span() - 48) < 1e-6 && view->property("preset").toInt() == -1 && !following() &&
		      !shown(content, "plotPreset_60")->property("checked").toBool());
		QTest::keyClick(window, Qt::Key_End);
		settle(150);
		CHECK(following());
		QTest::keyClick(window, Qt::Key_Left);
		settle(150);
		CHECK(!following() && std::abs(view->property("behindSeconds").toDouble() - 4.8) < 0.2);
		QTest::keyClick(window, Qt::Key_Space);
		settle();
		CHECK(following());
		QTest::keyClick(window, Qt::Key_Space);
		settle();
		CHECK(!following());
		const double newestUtc = view->property("rightUtcMs").toDouble();
		QTest::keyClick(window, Qt::Key_Home);
		settle(150);
		CHECK(!following() && view->property("rightUtcMs").toDouble() < newestUtc);
		QTest::keyClick(window, Qt::Key_4);
		settle(150);
		CHECK(view->property("showAll").toBool() && following());
		QTest::keyClick(window, Qt::Key_2);
		settle(150);
		CHECK(span() == 60 && following());
		// The wheel zooms about the pointer: the time under it stays.
		{
			const auto area = view->property("plotArea").toRectF();
			const QPointF at(area.left() + area.width() * 0.25, area.center().y());
			QWheelEvent wheel(at, view->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
			                  Qt::NoScrollPhase, false);
			QCoreApplication::sendEvent(view, &wheel);
			settle(150);
			CHECK(std::abs(span() - 48) < 1e-6 && !following() &&
			      std::abs(view->property("behindSeconds").toDouble() - 0.75 * 12) < 0.3);
		}
		// The zoom buttons, out and in.
		click(window, shown(content, "plotZoomOut"));
		settle();
		CHECK(std::abs(span() - 60) < 1e-6);
		click(window, shown(content, "plotZoomIn"));
		settle();
		CHECK(std::abs(span() - 48) < 1e-6);
		// Out no further than what is kept: there it is All, and held All
		// stands still while readings come.
		for (int i = 0; i < 12; ++i)
			click(window, shown(content, "plotZoomOut"));
		settle(150);
		CHECK(view->property("showAll").toBool() && view->property("preset").toInt() == 0 && !following());
		const double heldRight = view->property("rightUtcMs").toDouble();
		traces.append(frame(ux::WatchTraces::nowNs(), {{3, 1, bits(S::S16, 2, 5)}}));
		settle(150);
		CHECK(view->property("rightUtcMs").toDouble() == heldRight && view->property("behindSeconds").toDouble() > 0);
		// A pan never leaves empty time at the left: back a long way, the
		// left edge stops at the oldest reading kept.
		click(window, shown(content, "plotPreset_60"));
		settle(150);
		for (int i = 0; i < 200; ++i)
			QMetaObject::invokeMethod(view, "pan", Q_ARG(qreal, -0.5));
		settle(150);
		const double oldestRight = view->property("rightUtcMs").toDouble();
		QMetaObject::invokeMethod(view, "toOldest");
		settle(150);
		CHECK(std::abs(view->property("rightUtcMs").toDouble() - oldestRight) < 2);
		// The keys' focus frame is round the graph.
		view->forceActiveFocus(Qt::TabFocusReason);
		settle();
		CHECK(shown(content, "plotFocus"));
		click(window, shown(content, "plotPreset_60"));
		settle(150);
		CHECK(following() && span() == 60);

		// LP-1c. The cursor follows the pointer, snapped to a reading; its
		// time shows under the axis, each chip its value there.
		CHECK(view->setProperty("following", false));
		settle(150);
		const auto plotArea = view->property("plotArea").toRectF();
		const auto pointAt = [&](qreal part) {
			return view->mapToScene(QPointF(plotArea.left() + plotArea.width() * part, plotArea.center().y())).toPoint();
		};
		// Two moves: the first enters the window as offscreen delivers it.
		QTest::mouseMove(window, pointAt(0.55));
		settle(60);
		QTest::mouseMove(window, pointAt(0.6));
		settle(150);
		CHECK(view->property("cursorShown").toBool() && shown(content, "plotCursorChip"));
		const QRegularExpression clock(QStringLiteral("^\\d\\d:\\d\\d:\\d\\d\\.\\d{3}$"));
		CHECK(clock.match(shown(content, "plotCursorText")->property("text").toString()).hasMatch());
		QVariant gainAt;
		CHECK(QMetaObject::invokeMethod(view, "valueAt", Q_RETURN_ARG(QVariant, gainAt), Q_ARG(QString, QStringLiteral("device.Config.Gain"))));
		const auto cursorValues = all(content, "plotChipValue");
		CHECK(gainAt.isValid() && cursorValues.size() == 3 &&
		      cursorValues[2]->property("text").toString() ==
		          QString::number(qint64(gainAt.toDouble())).replace(QLatin1Char('-'), QChar(0x2212)));
		// Snapped: the cursor's time is a reading's time exactly.
		const auto* gainReadings = traces.readings(QStringLiteral("device.Config.Gain"));
		const double cursorMs = view->property("cursorUtcMs").toDouble();
		bool onReading = false;
		for (const auto& reading : *gainReadings)
			onReading = onReading || std::abs(traces.anchor().utcMs + double(reading.ns - traces.anchor().monotonicNs) / 1e6 - cursorMs) < 1e-3;
		CHECK(onReading);
		shot(window, argc, argv, QStringLiteral("trace-plot-cursor-760"));
		// A click pins a reference there; the cursor then says the time since
		// it and each chip its value's difference; Esc takes it away.
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pointAt(0.6));
		settle(150);
		CHECK(view->property("referenceShown").toBool() && shown(content, "plotReferenceChip") &&
		      !view->property("following").toBool());
		QTest::mouseMove(window, pointAt(0.3));
		settle(150);
		CHECK(shown(content, "plotCursorText")->property("text").toString().startsWith(QStringLiteral("Δ −")));
		const auto deltas = all(content, "plotChipDelta");
		CHECK(deltas.size() == 3 && deltas[2]->property("text").toString().startsWith(QStringLiteral("Δ ")));
		shot(window, argc, argv, QStringLiteral("trace-plot-reference-760"));
		QTest::keyClick(window, Qt::Key_Escape);
		settle();
		CHECK(!view->property("referenceShown").toBool() && all(content, "plotChipDelta").isEmpty());
		// Shift+Right steps the cursor to the next reading.
		const double cursorBefore = view->property("cursorUtcMs").toDouble();
		QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
		settle();
		CHECK(view->property("cursorUtcMs").toDouble() > cursorBefore);
		// Without a cursor, Shift+Left starts on the newest reading in view.
		QTest::keyClick(window, Qt::Key_Escape);
		settle();
		CHECK(!view->property("cursorShown").toBool());
		QTest::keyClick(window, Qt::Key_Left, Qt::ShiftModifier);
		settle();
		CHECK(view->property("cursorShown").toBool() &&
		      std::abs(view->property("cursorUtcMs").toDouble() - view->property("rightUtcMs").toDouble()) < 1);
		// Enter pins a reference where the keyboard's cursor is.
		QTest::keyClick(window, Qt::Key_Return);
		settle();
		CHECK(view->property("referenceShown").toBool());
		QTest::keyClick(window, Qt::Key_Escape);
		settle();
		// Off the graph, the cursor goes; the chips show the newest reading.
		QTest::mouseMove(window, QPoint(4, 4));
		settle(150);
		CHECK(!view->property("cursorShown").toBool() && !named(content, "plotCursorChip"));
		CHECK(view->setProperty("following", true));
		settle(150);

		// LP-1e. Ctrl and the wheel zoom the values about the pointer, by hand:
		// the time stays, and "Auto (A)" offers the fit back.
		{
			const auto valuesArea = view->property("valuesArea").toRectF();
			const double spanBefore = view->property("spanSeconds").toDouble();
			const auto labelsBefore = view->property("valueLabels").toStringList();
			const QPointF inside(valuesArea.center());
			QWheelEvent zoomIn(inside, view->mapToGlobal(inside), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::ControlModifier,
			                   Qt::NoScrollPhase, false);
			QCoreApplication::sendEvent(view, &zoomIn);
			settle(150);
			CHECK(!view->property("valueAuto").toBool() && view->property("spanSeconds").toDouble() == spanBefore &&
			      view->property("valueLabels").toStringList() != labelsBefore && shown(content, "plotAuto"));
			shot(window, argc, argv, QStringLiteral("trace-plot-values-zoomed-760"));
			// A fits them again.
			view->forceActiveFocus(Qt::TabFocusReason);
			QTest::keyClick(window, Qt::Key_A);
			settle(150);
			CHECK(view->property("valueAuto").toBool() && !named(content, "plotAuto"));
			// The wheel over the value labels zooms the values too.
			const QPointF labels(valuesArea.left() - 8, valuesArea.center().y());
			QWheelEvent overLabels(labels, view->mapToGlobal(labels), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
			                       Qt::NoScrollPhase, false);
			QCoreApplication::sendEvent(view, &overLabels);
			settle(150);
			CHECK(!view->property("valueAuto").toBool() && view->property("spanSeconds").toDouble() == spanBefore);
			// Up pans them a tenth; the Follow and the time are left as they are.
			const auto manualLabels = view->property("valueLabels").toStringList();
			QTest::keyClick(window, Qt::Key_Up);
			settle(150);
			CHECK(view->property("valueLabels").toStringList() != manualLabels && view->property("following").toBool());
			// A double-click on the value labels fits them again.
			QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, view->mapToScene(labels).toPoint());
			settle(150);
			CHECK(view->property("valueAuto").toBool());
			// A drag over the value labels pans them.
			const auto fittedLabels = view->property("valueLabels").toStringList();
			const auto from = view->mapToScene(labels).toPoint();
			QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
			QTest::mouseMove(window, from + QPoint(0, 30));
			QTest::mouseMove(window, from + QPoint(0, 60));
			QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, from + QPoint(0, 60));
			settle(150);
			CHECK(!view->property("valueAuto").toBool() && view->property("valueLabels").toStringList() != fittedLabels &&
			      !view->property("referenceShown").toBool());
			QTest::keyClick(window, Qt::Key_A);
			settle(150);
			// A click on a chip's name fits the values to that curve alone:
			// Voltage, 1 to 8, not Gain's -100 to 100.
			auto* voltageName = all(content, "plotChipName").first();
			click(window, voltageName);
			settle(200);
			CHECK(view->property("focusedPath").toString() == QStringLiteral("device.Voltage") &&
			      shown(content, "plotFitted")->property("text").toString() == QStringLiteral("Fitted to device.Voltage"));
			double highest = -1e9;
			for (const auto& label : view->property("valueLabels").toStringList())
				highest = std::max(highest, label.toDouble());
			CHECK(highest < 20);
			shot(window, argc, argv, QStringLiteral("trace-plot-focused-760"));
			click(window, voltageName);
			settle(200);
			CHECK(view->property("focusedPath").toString().isEmpty() && !named(content, "plotFitted"));
			// The cursor's chip never covers the reference's: beside it.
			CHECK(view->setProperty("following", false));
			settle(150);
			const auto area = view->property("plotArea").toRectF();
			const auto at = view->mapToScene(QPointF(area.left() + area.width() * 0.5, area.center().y())).toPoint();
			QTest::mouseMove(window, at + QPoint(-20, 0));
			settle(60);
			QTest::mouseMove(window, at);
			settle(100);
			QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
			settle(150);
			QTest::mouseMove(window, at + QPoint(3, 0));
			settle(150);
			auto* cursorChip = shown(content, "plotCursorChip");
			auto* referenceChip = shown(content, "plotReferenceChip");
			const double cx = cursorChip->x(), cw = cursorChip->width(), rx = referenceChip->x(), rw = referenceChip->width();
			CHECK(cx >= rx + rw + 3.5 || cx + cw <= rx - 3.5);
			shot(window, argc, argv, QStringLiteral("trace-plot-chips-apart-760"));
			QTest::keyClick(window, Qt::Key_Escape);
			QTest::mouseMove(window, QPoint(4, 4));
			CHECK(view->setProperty("following", true));
			settle(150);
		}

		// LP-1f. A curve's look: its colour, k and b, kept for its path; a
		// colour not #rrggbb or a k or b not finite is refused and changes
		// nothing.
		{
			const QString voltage = QStringLiteral("device.Voltage");
			CHECK(!traces.setAppearance(voltage, QStringLiteral("red"), 1, 0) &&
			      !traces.setAppearance(voltage, QString(), std::numeric_limits<double>::quiet_NaN(), 0) &&
			      !traces.setAppearance(voltage, QString(), 1, std::numeric_limits<double>::infinity()) &&
			      traces.multiplier(voltage) == 1 && traces.color(voltage).isEmpty());
			QColor drawnIn;
			CHECK(QMetaObject::invokeMethod(view, "curveColor", Q_RETURN_ARG(QColor, drawnIn), Q_ARG(QString, voltage)) &&
			      drawnIn == QColor(QStringLiteral("#58a6ff")));
			const auto valueBefore = all(content, "plotChipValue").first()->property("text").toString();
			CHECK(traces.setAppearance(voltage, QStringLiteral("#FF7E8F"), 100, 0.5) && traces.color(voltage) == QStringLiteral("#ff7e8f"));
			settle(150);
			CHECK(QMetaObject::invokeMethod(view, "curveColor", Q_RETURN_ARG(QColor, drawnIn), Q_ARG(QString, voltage)) &&
			      drawnIn == QColor(QStringLiteral("#ff7e8f")));
			// The chip says how it is drawn, and still gives the reading.
			CHECK(all(content, "plotChipTransform").size() == 1 &&
			      all(content, "plotChipValue").first()->property("text").toString() == valueBefore);
			bool tagged = false;
			for (auto* child : all(content, "plotChipTransform").first()->childItems())
				tagged = tagged || child->property("text").toString() == QStringLiteral("×100 +0.5");
			CHECK(tagged);
			// Focused, the values fit the drawn curve: 100 x 1..8 + 0.5.
			view->setProperty("focusedPath", voltage);
			settle(200);
			double highest = -1e9;
			for (const auto& label : view->property("valueLabels").toStringList())
				highest = std::max(highest, QString(label).remove(QChar(',')).toDouble());
			CHECK(highest > 300);
			shot(window, argc, argv, QStringLiteral("trace-plot-transformed-760"));
			view->setProperty("focusedPath", QString());
			settle(150);

			// Measurement points: none by default, every number's with the
			// Plot's setting, none on a curve that hides its own.
			CHECK(view->property("drawnDots").toInt() == 0);
			traces.setShowPoints(true);
			settle(200);
			const int allDots = view->property("drawnDots").toInt();
			CHECK(allDots > 0 && traces.pointsShown(voltage));
			shot(window, argc, argv, QStringLiteral("trace-plot-points-760"));
			traces.setPoints(voltage, 2);
			settle(200);
			const int hidden = view->property("drawnDots").toInt();
			CHECK(hidden < allDots && !traces.pointsShown(voltage));
			traces.setShowPoints(false);
			traces.setPoints(voltage, 1);
			settle(200);
			CHECK(view->property("drawnDots").toInt() == allDots - hidden);
			traces.setPoints(voltage, 0);
			settle(200);
			CHECK(view->property("drawnDots").toInt() == 0);

			// What a project keeps: the curves in order and their looks.
			const auto saved = traces.capture();
			const auto order = traces.plotted();
			traces.unplot(voltage);
			CHECK(traces.setAppearance(voltage, QString(), 1, 0));
			traces.setShowPoints(true);
			CHECK(traces.restore(saved) && traces.plotted() == order && traces.multiplier(voltage) == 100 &&
			      traces.offset(voltage) == 0.5 && traces.color(voltage) == QStringLiteral("#ff7e8f") && !traces.showPoints());
			// A saved look that is not one is skipped; the rest is restored.
			auto damaged = saved;
			damaged[QStringLiteral("curves")] = QVariantMap{{voltage, QVariantMap{{QStringLiteral("color"), QStringLiteral("blue")}}}};
			CHECK(!traces.restore(damaged) && traces.plotted() == order);
			CHECK(traces.setAppearance(voltage, QString(), 1, 0) && all(content, "plotChipTransform").isEmpty());
			settle(150);

			// The chip's colour mark opens its settings: a swatch, k and b, Apply.
			click(window, all(content, "plotChipSettings").first());
			settle(300);
			auto* colorField = shown(content, "plotCurveColor");
			CHECK(colorField->property("text").toString() == QStringLiteral("#58a6ff"));
			shot(window, argc, argv, QStringLiteral("trace-plot-curve-settings-760"));
			click(window, shown(content, "plotColorPreset_4"));
			shown(content, "plotCurveMultiplier")->setProperty("text", QStringLiteral("2"));
			shown(content, "plotCurveOffset")->setProperty("text", QStringLiteral("-1.5"));
			settle();
			CHECK(colorField->property("text").toString() == QStringLiteral("#ff7e8f"));
			click(window, shown(content, "plotCurveApply"));
			settle(300);
			CHECK(!named(content, "plotCurveColor") && traces.multiplier(voltage) == 2 && traces.offset(voltage) == -1.5 &&
			      traces.color(voltage) == QStringLiteral("#ff7e8f"));
			// Not a number: said in words, Apply off; Cancel changes nothing.
			click(window, all(content, "plotChipSettings").first());
			settle(300);
			shown(content, "plotCurveMultiplier")->setProperty("text", QStringLiteral("abc"));
			settle();
			CHECK(shown(content, "plotCurveSettingsProblem")->property("text").toString() ==
			      QStringLiteral("Enter finite numbers for k and b.") &&
			      !shown(content, "plotCurveApply")->property("enabled").toBool());
			click(window, shown(content, "plotCurveCancel"));
			settle(300);
			CHECK(!named(content, "plotCurveColor") && traces.multiplier(voltage) == 2);
			// Its palette colour and Reset transform make it plain again.
			click(window, all(content, "plotChipSettings").first());
			settle(300);
			click(window, shown(content, "plotColorPreset_0"));
			click(window, shown(content, "plotCurveResetTransform"));
			click(window, shown(content, "plotCurveApply"));
			settle(300);
			CHECK(traces.color(voltage).isEmpty() && traces.multiplier(voltage) == 1 && traces.offset(voltage) == 0);

			// The chip's menu (right-click): its points shown.
			auto* chipName = all(content, "plotChipName").first();
			QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
			                  chipName->mapToScene(QPointF(chipName->width() / 2, chipName->height() / 2)).toPoint());
			settle(300);
			click(window, shown(content, "plotChipPointsShow"));
			settle(300);
			CHECK(traces.points(voltage) == 1 && !named(content, "plotChipPointsShow"));
			CHECK(history.undoText() == QStringLiteral("Change the points of %1").arg(voltage));
			traces.setPoints(voltage, 0);
			// The graph's menu (right-click): every curve's points.
			const auto graph = view->property("plotArea").toRectF();
			QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
			                  view->mapToScene(QPointF(graph.left() + graph.width() * 0.3, graph.center().y())).toPoint());
			settle(300);
			click(window, shown(content, "plotShowPoints"));
			settle(300);
			CHECK(traces.showPoints() && view->property("drawnDots").toInt() > 0);
			CHECK(history.undoText() == QStringLiteral("Show measurement points"));
			traces.setShowPoints(false);
			settle(150);

			// LP-1g. The graph's menu: Follow off and on, a reference pinned
			// where it was opened and removed, every curve off and back.
			const auto openGraphMenu = [&](double part) {
				QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
				                  view->mapToScene(QPointF(graph.left() + graph.width() * part, graph.center().y())).toPoint());
				settle(300);
			};
			openGraphMenu(0.3);
			click(window, shown(content, "plotMenuFollow"));
			settle(200);
			CHECK(!view->property("following").toBool());
			openGraphMenu(0.4);
			click(window, shown(content, "plotMenuPin"));
			settle(200);
			CHECK(view->property("referenceShown").toBool());
			openGraphMenu(0.4);
			click(window, shown(content, "plotMenuUnpin"));
			settle(200);
			CHECK(!view->property("referenceShown").toBool());
			openGraphMenu(0.4);
			click(window, shown(content, "plotMenuFollow"));
			settle(200);
			CHECK(view->property("following").toBool());
			const auto before = traces.capture();
			QHash<QString, int> colours;
			for (const auto& path : order)
				colours.insert(path, traces.colorIndex(path));
			const auto steps = history.count();
			openGraphMenu(0.4);
			// The menu is taller than this 360 px window: the item is chosen
			// as a key would choose it, and the menu closed.
			QMetaObject::invokeMethod(shown(content, "plotMenuRemoveAll"), "triggered");
			if (auto* menu = window->findChild<QObject*>(QStringLiteral("plotGraphMenu")))
				QMetaObject::invokeMethod(menu, "close");
			settle(200);
			CHECK(traces.plotted().isEmpty() && traces.count(voltage) > 0 && shown(content, "plotEmpty"));
			// One step (UR-1): Undo puts every curve back in its place, with
			// its colour; a project's Plot put back records nothing.
			CHECK(history.count() == steps + 1 && history.undoText() == QStringLiteral("Remove all curves from the Plot"));
			CHECK(history.undo() && traces.plotted() == order);
			for (const auto& path : order)
				CHECK(traces.colorIndex(path) == colours.value(path));
			CHECK(traces.restore(before) && traces.plotted() == order && history.count() == steps + 1);
			settle(200);
			// A stop, then readings again: a band where nothing was read.
			CHECK(view->property("drawnGaps").toInt() == 0);
			traces.breakAll();
			CHECK(traces.pauses().size() == 1 && traces.pauses().back().toNs == 0);
			settle(700);
			traces.append(frame(ux::WatchTraces::nowNs(), {{1, 0, bits(S::F32, 4, floatBits(7.5f))}, {2, 0, bits(S::Boolean, 1, 1)}}));
			settle(300);
			CHECK(traces.pauses().back().toNs > traces.pauses().back().fromNs + 500'000'000);
			CHECK(view->property("drawnGaps").toInt() == 1);
			shot(window, argc, argv, QStringLiteral("trace-plot-gap-760"));
			// Delete on a chip's settings button takes that curve off.
			auto* settingsButton = all(content, "plotChipSettings").first();
			settingsButton->forceActiveFocus(Qt::TabFocusReason);
			QTest::keyClick(window, Qt::Key_Delete);
			settle(200);
			CHECK(!traces.isPlotted(voltage) && traces.plotted().size() == order.size() - 1);
			CHECK(history.undoText() == QStringLiteral("Remove %1 from the Plot").arg(voltage) && history.undo() &&
			      traces.plotted() == order);
			QTest::mouseMove(window, QPoint(4, 4));
			settle(150);
		}

		// LP-1h. The time axis from the start of reading: labels in time since
		// the first reading of this run, the cursor chip with both times; its
		// menu brings the time of day back. (The fixture appended readings
		// older than its first frame, so this view starts below zero.)
		{
			const auto clockLabels = view->property("timeLabels").toStringList();
			CHECK(shown(content, "plotTimeTitle")->property("text").toString().startsWith(QStringLiteral("Time of day")));
			view->setProperty("fromStart", true);
			settle(200);
			const auto since = view->property("timeLabels").toStringList();
			bool elapsed = !since.isEmpty();
			for (const auto& label : since)
				elapsed = elapsed && (label.endsWith(QStringLiteral(" s")) || label.count(QLatin1Char(':')) == 1);
			CHECK(elapsed && since != clockLabels && view->property("originUtcMs").toDouble() > 0 &&
			      view->property("originUtcMs").toDouble() <= view->property("rightUtcMs").toDouble());
			CHECK(shown(content, "plotTimeTitle")->property("text").toString().startsWith(QStringLiteral("From start")));
			shot(window, argc, argv, QStringLiteral("trace-plot-from-start-760"));
			// One fixed start: the first reading of this run (the anchor), not
			// the end of the stop before; panned back across that stop it stays,
			// and the CSV's seconds count from it as the axis does.
			const double origin = view->property("originUtcMs").toDouble();
			CHECK(!traces.pauses().empty() && traces.pauses().back().toNs > traces.anchor().monotonicNs &&
			      origin == double(traces.anchor().utcMs) && labelsWhole(view));
			QMetaObject::invokeMethod(view, "pan", Q_ARG(qreal, -0.25));
			settle(150);
			const double stopEndUtc = double(traces.anchor().utcMs) + double(traces.pauses().back().toNs - traces.anchor().monotonicNs) / 1e6;
			CHECK(!view->property("following").toBool() && view->property("rightUtcMs").toDouble() < stopEndUtc &&
			      view->property("originUtcMs").toDouble() == origin && labelsWhole(view));
			QString csv;
			CHECK(QMetaObject::invokeMethod(view, "valuesText", Q_RETURN_ARG(QString, csv), Q_ARG(bool, true)));
			const auto csvRows = csv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
			CHECK(csvRows.size() > 2);
			for (const auto& row : {csvRows[1], csvRows.last()}) {
				const auto fields = row.split(QLatin1Char(','));
				const auto at = QDateTime::fromString(fields[0], QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
				CHECK(at.isValid() && std::abs(fields[1].toDouble() * 1000 - (double(at.toMSecsSinceEpoch()) - origin)) < 1.5);
			}
			CHECK(view->setProperty("following", true));
			settle(150);
			click(window, shown(content, "plotTimeTitle"));
			settle(300);
			click(window, shown(content, "plotAxisTimeOfDay"));
			settle(200);
			CHECK(!view->property("fromStart").toBool() &&
			      shown(content, "plotTimeTitle")->property("text").toString().startsWith(QStringLiteral("Time of day")));
		}

		// Analysis: Ctrl and a drag draw a box, and its release zooms the time
		// to its width and the values to its height; Backspace goes back to the
		// view before it, and the Back button says when there is none.
		{
			CHECK(view->setProperty("following", false));
			settle(150);
			const double spanBefore = view->property("spanSeconds").toDouble();
			const auto area = view->property("plotArea").toRectF();
			const auto from = view->mapToScene(QPointF(area.left() + area.width() * 0.3, area.top() + area.height() * 0.2)).toPoint();
			const auto to = view->mapToScene(QPointF(area.left() + area.width() * 0.5, area.top() + area.height() * 0.6)).toPoint();
			QTest::mousePress(window, Qt::LeftButton, Qt::ControlModifier, from);
			QTest::mouseMove(window, (from + to) / 2);
			QTest::mouseMove(window, to);
			settle(100);
			CHECK(view->property("boxShown").toBool());
			// Its size on a chip at the corner under the pointer: outside the
			// box, inside the plot area.
			const auto caption = view->property("boxCaptionRect").toRectF();
			const auto corner = view->mapFromScene(QPointF(to));
			CHECK(!caption.isEmpty() && area.contains(caption) && caption.left() >= corner.x() && caption.top() >= corner.y() &&
			      caption.left() - corner.x() < 12 && caption.top() - corner.y() < 12);
			shot(window, argc, argv, QStringLiteral("trace-plot-box-760"));
			QTest::mouseRelease(window, Qt::LeftButton, Qt::ControlModifier, to);
			settle(200);
			CHECK(!view->property("boxShown").toBool() && !view->property("following").toBool() &&
			      !view->property("valueAuto").toBool() && view->property("spanSeconds").toDouble() < spanBefore * 0.3 &&
			      view->property("canGoBack").toBool() && shown(content, "plotBack")->property("enabled").toBool());
			view->forceActiveFocus(Qt::TabFocusReason);
			QTest::keyClick(window, Qt::Key_Backspace);
			settle(200);
			CHECK(view->property("valueAuto").toBool() && qFuzzyCompare(view->property("spanSeconds").toDouble(), spanBefore));
			// Everything gone back on: the button says there is nothing left.
			while (view->property("canGoBack").toBool())
				QMetaObject::invokeMethod(view, "goBack");
			settle(100);
			CHECK(!shown(content, "plotBack")->property("enabled").toBool());
			CHECK(view->setProperty("following", true));
			settle(150);
		}

		// Analysis: the numbers stacked in lanes of their own (L). A lane each,
		// apart, in the order plotted, each fitted to its own number with its
		// own labels; the cursor's dot on each number in its lane at its
		// reading; a click on a lane's name focuses it; a box and the keys act
		// on the lane under the pointer, and Back takes it back. The traces keep
		// it (a project) and it is one step to undo. Too short for 40 px a
		// lane, the numbers share one axis again and L says why.
		{
			auto* plotRoot = window->findChild<QObject*>(QStringLiteral("tracePlot"));
			const auto plottedBefore = traces.plotted();
			const QString voltage = QStringLiteral("device.Voltage"), gainPath = QStringLiteral("device.Config.Gain");
			for (const auto& path : {voltage, gainPath})
				if (!traces.plotted().contains(path))
					CHECK(traces.plot(path).isEmpty());
			CHECK(view->setProperty("following", false));
			settle(200);
			CHECK(view->property("stackFits").toBool() && view->property("numberLanes").toList().isEmpty() &&
			      !view->property("stacked").toBool());
			view->forceActiveFocus(Qt::TabFocusReason);
			QTest::keyClick(window, Qt::Key_L);
			settle(300);
			CHECK(traces.stacked() && view->property("stacked").toBool());
			// One step, the newest.
			CHECK(history.undoText() == QStringLiteral("Stack the numbers in lanes") &&
			      traces.capture().value(QStringLiteral("stacked")).toBool());
			QStringList numbers;
			for (const auto& path : traces.plotted())
				if (!traces.categorical(path))
					numbers << path;
			auto lanes = view->property("numberLanes").toList();
			CHECK(numbers.size() >= 2 && lanes.size() == numbers.size() && view->property("valueAuto").toBool());
			const auto valuesArea = view->property("valuesArea").toRectF();
			const auto lane = [&](int i) { return view->property("numberLanes").toList().value(i).toMap(); };
			// A lane fitted to its own number: its readings in view, drawn as
			// k x value + b, inside its values with no more than the margins.
			const auto fitted = [&](int i) {
				const auto drawn = lane(i);
				const auto path = drawn.value(QStringLiteral("path")).toString();
				QVariantMap measured;
				if (!QMetaObject::invokeMethod(view, "statistics", Q_RETURN_ARG(QVariantMap, measured), Q_ARG(QString, path)))
					return false;
				const double k = traces.multiplier(path), b = traces.offset(path);
				const double one = k * measured.value(QStringLiteral("min")).toDouble() + b,
				             other = k * measured.value(QStringLiteral("max")).toDouble() + b;
				const double low = drawn.value(QStringLiteral("low")).toDouble(), high = drawn.value(QStringLiteral("high")).toDouble();
				return measured.value(QStringLiteral("count")).toInt() > 1 && low <= std::min(one, other) &&
				       high >= std::max(one, other) && high - low <= std::abs(other - one) * 1.25 + 1e-9;
			};
			QRectF above;
			for (int i = 0; i < lanes.size(); ++i) {
				const auto drawn = lanes[i].toMap();
				const auto path = drawn.value(QStringLiteral("path")).toString();
				const auto rect = drawn.value(QStringLiteral("rect")).toRectF();
				const auto values = drawn.value(QStringLiteral("valueRect")).toRectF();
				// Its own lane: at least 40 px, inside the numbers' part, under
				// the one above and never over it, its values inside it.
				CHECK(path == numbers[i] && rect.height() >= 40 && rect.top() >= valuesArea.top() - 0.5 &&
				      rect.bottom() <= valuesArea.bottom() + 0.5 && !rect.intersects(above) &&
				      (i == 0 || rect.top() >= above.bottom() - 0.01) && rect.contains(values));
				// Its own values: fitted to its own readings in view, drawn as
				// k x value + b, with labels of its own.
				QVariantMap measured;
				CHECK(QMetaObject::invokeMethod(view, "statistics", Q_RETURN_ARG(QVariantMap, measured), Q_ARG(QString, path)));
				const double k = traces.multiplier(path), b = traces.offset(path);
				const double drawnMin = std::min(k * measured.value(QStringLiteral("min")).toDouble() + b,
				                                 k * measured.value(QStringLiteral("max")).toDouble() + b);
				const double drawnMax = std::max(k * measured.value(QStringLiteral("min")).toDouble() + b,
				                                 k * measured.value(QStringLiteral("max")).toDouble() + b);
				const double low = drawn.value(QStringLiteral("low")).toDouble(), high = drawn.value(QStringLiteral("high")).toDouble();
				CHECK(measured.value(QStringLiteral("count")).toInt() > 1 && low <= drawnMin && high >= drawnMax &&
				      high - low <= (drawnMax - drawnMin) * 1.25 + 1e-9);
				const auto labels = drawn.value(QStringLiteral("labels")).toStringList();
				CHECK(labels.size() >= 2 && labels.size() <= 4);
				above = rect;
			}
			// The two numbers apart: one axis would hold both.
			CHECK(lane(0).value(QStringLiteral("high")).toDouble() < lane(1).value(QStringLiteral("high")).toDouble() ||
			      lane(0).value(QStringLiteral("low")).toDouble() > lane(1).value(QStringLiteral("low")).toDouble());
			CHECK(!named(content, "plotFittedChip") || !shown(content, "plotFittedChip")->isVisible());
			// The cursor: a dot on each number, in its lane, at its reading.
			const auto area = view->property("plotArea").toRectF();
			const auto middle = view->mapToScene(QPointF(area.left() + area.width() * 0.6, lane(0).value(QStringLiteral("rect")).toRectF().center().y())).toPoint();
			QTest::mouseMove(window, middle + QPoint(-5, 0));
			QTest::mouseMove(window, middle);
			settle(200);
			CHECK(view->property("cursorShown").toBool());
			for (int i = 0; i < numbers.size(); ++i) {
				const auto drawn = lane(i);
				const auto values = drawn.value(QStringLiteral("valueRect")).toRectF();
				const double low = drawn.value(QStringLiteral("low")).toDouble(), high = drawn.value(QStringLiteral("high")).toDouble();
				const double y = drawn.value(QStringLiteral("cursorY")).toDouble();
				QVariant at;
				CHECK(QMetaObject::invokeMethod(view, "valueAt", Q_RETURN_ARG(QVariant, at), Q_ARG(QString, numbers[i])) && at.isValid());
				const double expected = traces.multiplier(numbers[i]) * at.toDouble() + traces.offset(numbers[i]);
				const double read = high - (y - values.top()) / values.height() * (high - low);
				CHECK(y >= drawn.value(QStringLiteral("rect")).toRectF().top() && y <= drawn.value(QStringLiteral("rect")).toRectF().bottom() &&
				      std::abs(read - expected) <= (high - low) * 0.01);
			}
			shot(window, argc, argv, QStringLiteral("trace-plot-stacked-760"));
			// A click on a lane's name focuses its curve, again unfocuses it.
			const auto name = view->mapToScene(lane(1).value(QStringLiteral("nameRect")).toRectF().center()).toPoint();
			QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, name);
			settle(150);
			CHECK(view->property("focusedPath").toString() == numbers[1]);
			QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, name);
			settle(150);
			CHECK(view->property("focusedPath").toString().isEmpty());
			// A box in the second lane zooms its values and leaves the first.
			const double secondBeforeBox = lane(1).value(QStringLiteral("high")).toDouble() - lane(1).value(QStringLiteral("low")).toDouble();
			const auto second = lane(1).value(QStringLiteral("valueRect")).toRectF();
			const auto from = view->mapToScene(QPointF(area.left() + area.width() * 0.3, second.top() + second.height() * 0.2)).toPoint();
			const auto to = view->mapToScene(QPointF(area.left() + area.width() * 0.5, second.top() + second.height() * 0.7)).toPoint();
			QTest::mousePress(window, Qt::LeftButton, Qt::ControlModifier, from);
			QTest::mouseMove(window, (from + to) / 2);
			QTest::mouseMove(window, to);
			settle(100);
			CHECK(view->property("boxShown").toBool() && view->property("boxCaptionRect").toRectF().isValid());
			QTest::mouseRelease(window, Qt::LeftButton, Qt::ControlModifier, to);
			settle(250);
			const double secondSpan = lane(1).value(QStringLiteral("high")).toDouble() - lane(1).value(QStringLiteral("low")).toDouble();
			// The first lane still fits its own readings, now in the boxed time.
			CHECK(!view->property("valueAuto").toBool() && secondSpan < secondBeforeBox * 0.6 && secondSpan > secondBeforeBox * 0.4 &&
			      fitted(0));
			// Auto (A) stands after the first lane's name, not over it.
			auto* autoButton = shown(content, "plotAuto");
			CHECK(autoButton && autoButton->mapToItem(view, QPointF(0, 0)).x() >= lane(0).value(QStringLiteral("nameRect")).toRectF().right());
			view->forceActiveFocus(Qt::TabFocusReason);
			QTest::keyClick(window, Qt::Key_Backspace);
			settle(250);
			CHECK(view->property("valueAuto").toBool());
			// Up over the first lane moves its values, not the second's.
			const auto firstCentre = view->mapToScene(QPointF(area.center().x(), lane(0).value(QStringLiteral("rect")).toRectF().center().y())).toPoint();
			QTest::mouseMove(window, firstCentre);
			settle(100);
			const auto secondBefore = lane(1);
			const double firstLow = lane(0).value(QStringLiteral("low")).toDouble();
			QTest::keyClick(window, Qt::Key_Up);
			settle(250);
			CHECK(lane(0).value(QStringLiteral("low")).toDouble() > firstLow &&
			      lane(1).value(QStringLiteral("low")) == secondBefore.value(QStringLiteral("low")) && !view->property("valueAuto").toBool());
			// A fits every lane again.
			QTest::keyClick(window, Qt::Key_A);
			settle(250);
			CHECK(view->property("valueAuto").toBool());
			// Too short for a 40 px lane each: one axis again, and L says why.
			const int tall = window->height();
			window->setHeight(170);
			settle(300);
			CHECK(!view->property("stackFits").toBool() && view->property("numberLanes").toList().isEmpty() &&
			      !view->property("valueLabels").toStringList().isEmpty());
			view->forceActiveFocus(Qt::TabFocusReason);
			QTest::keyClick(window, Qt::Key_L);
			settle(150);
			CHECK(!traces.stacked());
			QTest::keyClick(window, Qt::Key_L);
			settle(150);
			CHECK(!traces.stacked() &&
			      plotRoot->property("notice").toString() ==
			          QStringLiteral("Too many numbers to stack at this height. Make the Plot taller or remove a curve."));
			plotRoot->setProperty("notice", QString());
			window->setHeight(tall);
			settle(300);
			// One step: undo stacks them again, and undo once more puts them on
			// one axis.
			CHECK(history.undoText() == QStringLiteral("Put the numbers on one axis") && history.undo() && traces.stacked());
			settle(200);
			CHECK(view->property("numberLanes").toList().size() == numbers.size());
			CHECK(history.undo() && !traces.stacked());
			settle(200);
			CHECK(view->property("numberLanes").toList().isEmpty() && view->property("valueAuto").toBool());
			for (const auto& path : traces.plotted())
				if (!plottedBefore.contains(path))
					traces.unplot(path);
			CHECK(view->setProperty("following", true));
			settle(150);
		}

		// Analysis: the readings in view as text for a spreadsheet and as a
		// CSV file, and the picture onto the clipboard, each said once.
		{
			auto* plotRoot = window->findChild<QObject*>(QStringLiteral("tracePlot"));
			CHECK(plotRoot);
			int readings = 0;
			CHECK(QMetaObject::invokeMethod(view, "readingsInRange", Q_RETURN_ARG(int, readings)) && readings > 0);
			QString text;
			CHECK(QMetaObject::invokeMethod(view, "valuesText", Q_RETURN_ARG(QString, text), Q_ARG(bool, false)));
			const auto lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
			CHECK(lines.size() == readings + 1 && lines.first().startsWith(QStringLiteral("Time of day\tSeconds from start\t")) &&
			      lines.first().contains(QStringLiteral("device.Voltage")));
			// A state in words, a number as read.
			CHECK(text.contains(QStringLiteral("\ttrue")) || text.contains(QStringLiteral("\tfalse")));
			CHECK(QMetaObject::invokeMethod(plotRoot, "copyValues"));
			settle(50);
			CHECK(QGuiApplication::clipboard()->text() == text &&
			      plotRoot->property("notice").toString().startsWith(QStringLiteral("Copied ")));
			const auto file = QDir::temp().filePath(QStringLiteral("telemetry-plot-check.csv"));
			QFile::remove(file);
			QVariant savedOk;
			CHECK(QMetaObject::invokeMethod(plotRoot, "saveValuesTo", Q_RETURN_ARG(QVariant, savedOk),
			                                Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(file)))) && savedOk.toBool());
			QFile csv(file);
			const bool saved = csv.open(QIODevice::ReadOnly);
			const auto written = saved ? QString::fromUtf8(csv.readAll()) : QString();
			CHECK(saved && written.startsWith(QStringLiteral("Time of day,Seconds from start,")) &&
			      written.count(QLatin1Char('\n')) == readings + 1);
			csv.close();
			QFile::remove(file);
			CHECK(plotRoot->property("notice").toString().startsWith(QStringLiteral("Saved ")));
			CHECK(plotRoot->property("noticeKind").toString() == QStringLiteral("done"));
			// The picture: the graph with its chips, without the toolbar, the
			// notice line or the pointer's cursor.
			const auto middle = view->mapToScene(view->property("plotArea").toRectF().center()).toPoint();
			QTest::mouseMove(window, middle + QPoint(-5, 0));
			QTest::mouseMove(window, middle);
			settle(100);
			CHECK(view->property("cursorShown").toBool());
			QGuiApplication::clipboard()->clear();
			CHECK(QMetaObject::invokeMethod(plotRoot, "copyPicture"));
			CHECK(!view->property("cursorShown").toBool());
			CHECK(QTest::qWaitFor([] { return !QGuiApplication::clipboard()->image().isNull(); }, 3000));
			auto* picture = shown(content, "plotPicture");
			auto* follow = shown(content, "plotFollow");
			const auto image = QGuiApplication::clipboard()->image();
			const double ratio = window->effectiveDevicePixelRatio();
			CHECK(std::abs(image.height() - picture->height() * ratio) <= 1 &&
			      image.height() < qobject_cast<QQuickItem*>(plotRoot)->height() * ratio - 8 &&
			      picture->mapToScene(QPointF(0, 0)).y() >= follow->mapToScene(QPointF(0, follow->height())).y());
			CHECK(plotRoot->property("notice").toString() == QStringLiteral("Copied the picture."));
			// "Copied …" leaves on its own after five seconds.
			QElapsedTimer noticeShown;
			noticeShown.start();
			CHECK(QTest::qWaitFor([plotRoot] { return plotRoot->property("notice").toString().isEmpty(); }, 7000));
			CHECK(noticeShown.elapsed() >= 4500 && !named(content, "plotNotice"));
		}

		// Analysis: the measurements. M turns the chips into a table of every
		// curve over the time in view; a reference and an end (Shift and a
		// click) make the range theirs, shaded; a state gives its shares of
		// the time; Min and Max put the cursor on their reading.
		{
			auto* plotRoot = window->findChild<QObject*>(QStringLiteral("tracePlot"));
			CHECK(view->setProperty("following", false));
			settle(150);
			CHECK(plotRoot->setProperty("measuring", true));
			settle(300);
			CHECK(shown(content, "plotMeasure") && !named(content, "plotLegend") &&
			      all(content, "plotMeasureRow").size() == traces.plotted().size());
			CHECK(shown(content, "plotMeasureRange")->property("text").toString().startsWith(QStringLiteral("In view · ")));
			const auto stat = [&](const QString& path) {
				QVariantMap out;
				CHECK(QMetaObject::invokeMethod(view, "statistics", Q_RETURN_ARG(QVariantMap, out), Q_ARG(QString, path)));
				return out;
			};
			const auto voltage = stat(QStringLiteral("device.Voltage"));
			const double low = voltage.value("min").toDouble(), high = voltage.value("max").toDouble(),
			             mean = voltage.value("mean").toDouble();
			CHECK(voltage.value("count").toInt() > 0 && low <= mean && mean <= high &&
			      qFuzzyCompare(voltage.value("peakToPeak").toDouble() + 1, high - low + 1) && voltage.value("deviation").toDouble() >= 0);
			const auto ready = stat(QStringLiteral("device.Ready"));
			double shares = 0;
			for (const auto& share : ready.value("shares").toList())
				shares += share.toMap().value("share").toDouble();
			CHECK(ready.value("count").toInt() > 0 && std::abs(shares - 1) < 1e-9 && ready.value("changes").toInt() >= 0);
			CHECK(shown(content, "plotMeasureShares")->property("text").toString().contains(QStringLiteral("% of the time")));
			// Min puts the cursor on the lowest reading.
			CHECK(QMetaObject::invokeMethod(view, "putCursorAt", Q_ARG(qreal, voltage.value("minUtcMs").toDouble())));
			settle(100);
			CHECK(view->property("cursorShown").toBool() &&
			      std::abs(view->property("cursorUtcMs").toDouble() - voltage.value("minUtcMs").toDouble()) < 1);
			// A reference, then Shift and a click: the end; the range is theirs.
			const auto area = view->property("plotArea").toRectF();
			const auto at = [&](double part) {
				return view->mapToScene(QPointF(area.left() + area.width() * part, area.center().y())).toPoint();
			};
			QTest::mouseMove(window, at(0.4) + QPoint(-5, 0));
			QTest::mouseMove(window, at(0.4));
			settle(60);
			QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at(0.4));
			settle(60);
			QTest::mouseMove(window, at(0.6) + QPoint(-5, 0));
			QTest::mouseMove(window, at(0.6));
			settle(60);
			QTest::mouseClick(window, Qt::LeftButton, Qt::ShiftModifier, at(0.6));
			settle(200);
			QVariantMap range;
			CHECK(QMetaObject::invokeMethod(view, "measureRange", Q_RETURN_ARG(QVariantMap, range)));
			CHECK(view->property("referenceShown").toBool() && view->property("endShown").toBool() && range.value("pinned").toBool() &&
			      range.value("seconds").toDouble() > 0 &&
			      shown(content, "plotMeasureRange")->property("text").toString().startsWith(QStringLiteral("Reference to end · ")));
			// The end's chip: how long after the reference, then its time of
			// day, beside the reference's chip; the cursor's chip steps aside
			// while the cursor sits on the end.
			const QRegularExpression endWords(QStringLiteral("^Δ \\d+\\.\\d{3} s · \\d\\d:\\d\\d:\\d\\d\\.\\d{3}$"));
			auto* endChip = shown(content, "plotEndChip");
			auto* referenceChip = shown(content, "plotReferenceChip");
			CHECK(endWords.match(shown(content, "plotEndText")->property("text").toString()).hasMatch() &&
			      (endChip->x() >= referenceChip->x() + referenceChip->width() + 3.5 ||
			       endChip->x() + endChip->width() <= referenceChip->x() - 3.5) &&
			      view->property("cursorUtcMs").toDouble() == view->property("endUtcMs").toDouble() && !named(content, "plotCursorChip"));
			CHECK(shown(content, "plotMeasureTitle_now")->property("text").toString() == QStringLiteral("At cursor"));
			// The pointer up to the table: the cursor goes, and the first column
			// gives the readings at the end, Δ the end minus the reference.
			auto* rangeCaption = shown(content, "plotMeasureRange");
			const auto onCaption = rangeCaption->mapToScene(QPointF(rangeCaption->width() / 2, rangeCaption->height() / 2)).toPoint();
			QTest::mouseMove(window, onCaption);
			settle(200);
			CHECK(!view->property("cursorShown").toBool() && shown(content, "plotEndChip") &&
			      shown(content, "plotMeasureTitle_now")->property("text").toString() == QStringLiteral("At end"));
			const auto rowOf = [&](const QString& path) {
				for (auto* row : all(content, "plotMeasureRow"))
					if (row->property("path").toString() == path)
						return row;
				throw std::runtime_error("no Measure row for " + path.toStdString());
			};
			const auto cellText = [](QQuickItem* row, const char* name) { return shown(row, name)->property("text").toString(); };
			const auto plotText = [&](const char* function, const QVariant& value) {
				QVariant out;
				CHECK(QMetaObject::invokeMethod(plotRoot, function, Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, value)));
				return out.toString();
			};
			const QString voltagePath = QStringLiteral("device.Voltage");
			QVariant atEnd, atReference;
			CHECK(QMetaObject::invokeMethod(view, "endValue", Q_RETURN_ARG(QVariant, atEnd), Q_ARG(QString, voltagePath)) &&
			      QMetaObject::invokeMethod(view, "referenceValue", Q_RETURN_ARG(QVariant, atReference), Q_ARG(QString, voltagePath)) &&
			      atEnd.isValid() && atReference.isValid());
			auto* voltageRow = rowOf(voltagePath);
			CHECK(cellText(voltageRow, "plotMeasure_now") == plotText("valueText", atEnd) &&
			      cellText(voltageRow, "plotMeasure_delta") == plotText("deltaText", QVariant(atEnd.toDouble() - atReference.toDouble())));
			// A state gives its reading in words and no Δ; its shares span the
			// figures after them.
			auto* readyRow = rowOf(QStringLiteral("device.Ready"));
			auto* measure = shown(content, "plotMeasure");
			const double cell = measure->property("cell").toDouble();
			const auto readyNow = cellText(readyRow, "plotMeasure_now");
			CHECK((readyNow == QStringLiteral("true") || readyNow == QStringLiteral("false")) &&
			      cellText(readyRow, "plotMeasure_delta").isEmpty() && measure->property("columns").toInt() == 7 &&
			      std::abs(shown(readyRow, "plotMeasureShares")->width() - cell * 5) < 0.5);
			// Mean and Std dev to one decimal more than Min and Max; the × kept
			// apart from the last figure.
			const auto decimals = [](const QString& text) {
				const auto at = text.indexOf(QLatin1Char('.'));
				return at < 0 ? 0 : int(text.size() - at - 1);
			};
			for (auto* row : {voltageRow, rowOf(QStringLiteral("device.Config.Gain"))}) {
				const int places = std::max(decimals(cellText(row, "plotMeasure_min")), decimals(cellText(row, "plotMeasure_max"))) + 1;
				auto* last = shown(row, "plotMeasure_std");
				auto* remove = shown(row, "plotMeasureRemove");
				CHECK(decimals(cellText(row, "plotMeasure_mean")) == places && decimals(cellText(row, "plotMeasure_std")) == places &&
				      remove->x() - (last->x() + last->width()) >= 3);
			}
			// Min and Max are links: the pointing hand, and what a click does.
			auto* minimum = shown(voltageRow, "plotMeasure_min");
			QTest::mouseMove(window, minimum->mapToScene(QPointF(minimum->width() - 4, minimum->height() / 2)).toPoint());
			settle(900);
			CHECK(withText(content, QStringLiteral("Put the cursor on this reading, ")) &&
			      window->cursor().shape() == Qt::PointingHandCursor);
			QTest::mouseMove(window, onCaption);
			settle(200);
			// A curve drawn ×k+b says so after its name, as its chip does; the
			// figures stay the readings.
			CHECK(traces.setAppearance(voltagePath, QString(), 2, 0));
			settle(150);
			bool scaled = false;
			for (auto* child : shown(voltageRow, "plotMeasureTransform")->childItems())
				scaled = scaled || child->property("text").toString() == QStringLiteral("×2");
			CHECK(scaled && cellText(voltageRow, "plotMeasure_now") == plotText("valueText", atEnd));
			shot(window, argc, argv, QStringLiteral("trace-plot-measure-760"));
			CHECK(traces.setAppearance(voltagePath, QString(), 1, 0));
			// Narrower: the Name column keeps nine body characters, five figures
			// beside it, then three (At…, Min, Max); the row's tooltip gives the
			// others. In a 320 px window the toolbar holds the Plot at its own
			// width, about 480 px in this style, and the table lays out there.
			const auto narrowed = [&](int width, int columns, const QStringList& tip) {
				window->setWidth(width);
				settle(300);
				auto* row = rowOf(voltagePath);
				auto* nameText = shown(row, "plotMeasureName");
				auto* remove = shown(row, "plotMeasureRemove");
				CHECK(measure->property("columns").toInt() == columns &&
				      nameText->parentItem()->width() >= measure->property("nameWidth").toDouble() - 0.5 &&
				      remove->x() + remove->width() <= row->width() + 0.5);
				const auto hidden = row->property("hidden").toString();
				CHECK(hidden.split(QStringLiteral(" · ")).size() == tip.size());
				for (int i = 0; i < tip.size(); ++i)
					CHECK(hidden.split(QStringLiteral(" · "))[i].startsWith(tip[i] + QLatin1Char(' ')));
				QTest::mouseMove(window, nameText->mapToScene(QPointF(4, nameText->height() / 2)).toPoint());
				settle(900);
				CHECK(withText(content, hidden));
				QTest::mouseMove(window, rangeCaption->mapToScene(QPointF(8, rangeCaption->height() / 2)).toPoint());
				settle(100);
			};
			narrowed(560, 5, {QStringLiteral("Pk-pk"), QStringLiteral("Std dev")});
			CHECK(named(rowOf(voltagePath), "plotMeasure_delta") && !named(rowOf(voltagePath), "plotMeasure_pkpk"));
			narrowed(320, 3, {QStringLiteral("Δ"), QStringLiteral("Mean"), QStringLiteral("Pk-pk"), QStringLiteral("Std dev")});
			CHECK(!named(rowOf(voltagePath), "plotMeasure_delta") && named(rowOf(voltagePath), "plotMeasure_max"));
			window->setWidth(760);
			settle(300);
			// Many curves: the rows stop at 40 % of the Plot's height and
			// scroll, and the graph keeps its room.
			for (int i = 0; i < 12; ++i)
				CHECK(traces.plot(QStringLiteral("device.Many%1").arg(i)).isEmpty());
			settle(300);
			{
				auto* rows = shown(content, "plotMeasureRows");
				const double rowHeight = all(content, "plotMeasureRow").first()->height();
				const double rowsCap = std::max(3 * rowHeight, qobject_cast<QQuickItem*>(plotRoot)->height() * 0.4);
				CHECK(rows->height() <= rowsCap + 0.5 && rows->property("contentHeight").toDouble() > rows->height() + rowHeight &&
				      view->height() > 100);
				shot(window, argc, argv, QStringLiteral("trace-plot-measure-many-760"));
			}
			for (int i = 0; i < 12; ++i)
				traces.unplot(QStringLiteral("device.Many%1").arg(i));
			settle(200);
			// Esc: the end first, then the reference.
			view->forceActiveFocus(Qt::TabFocusReason);
			QTest::keyClick(window, Qt::Key_Escape);
			settle(100);
			CHECK(!view->property("endShown").toBool() && view->property("referenceShown").toBool());
			QTest::keyClick(window, Qt::Key_Escape);
			settle(100);
			CHECK(!view->property("referenceShown").toBool());
			// M turns it off again.
			QTest::keyClick(window, Qt::Key_M);
			settle(200);
			CHECK(!plotRoot->property("measuring").toBool() && shown(content, "plotLegend"));
			QTest::mouseMove(window, QPoint(4, 4));
			CHECK(view->setProperty("following", true));
			settle(150);
		}

		// Analysis: Ctrl+Right and Ctrl+Left step the cursor from change to
		// change of the states (device.Ready flips every five seconds); past
		// the last, the notice says so.
		{
			auto* plotRoot = window->findChild<QObject*>(QStringLiteral("tracePlot"));
			CHECK(view->setProperty("following", false));
			QMetaObject::invokeMethod(view, "hideCursor");
			settle(150);
			view->forceActiveFocus(Qt::TabFocusReason);
			QTest::keyClick(window, Qt::Key_Right, Qt::ControlModifier);
			settle(100);
			CHECK(view->property("cursorShown").toBool());
			const double first = view->property("cursorUtcMs").toDouble();
			QTest::keyClick(window, Qt::Key_Right, Qt::ControlModifier);
			settle(100);
			const double second = view->property("cursorUtcMs").toDouble();
			CHECK(second > first);
			QTest::keyClick(window, Qt::Key_Left, Qt::ControlModifier);
			settle(100);
			CHECK(std::abs(view->property("cursorUtcMs").toDouble() - first) < 1);
			// Focused on the state, past its last change: said in words.
			view->setProperty("focusedPath", QStringLiteral("device.Ready"));
			bool stepped = true;
			for (int i = 0; i < 40 && stepped; ++i)
				CHECK(QMetaObject::invokeMethod(view, "stepChange", Q_RETURN_ARG(bool, stepped), Q_ARG(int, 1)));
			settle(100);
			CHECK(!stepped && plotRoot->property("notice").toString().startsWith(QStringLiteral("No change of device.Ready after ")) &&
			      plotRoot->property("noticeKind").toString() == QStringLiteral("find"));
			// One that finds its change takes those words away.
			CHECK(QMetaObject::invokeMethod(view, "stepChange", Q_RETURN_ARG(bool, stepped), Q_ARG(int, -1)) && stepped &&
			      plotRoot->property("notice").toString().isEmpty());
			view->setProperty("focusedPath", QString());
			QMetaObject::invokeMethod(view, "hideCursor");
			CHECK(view->setProperty("following", true));
			settle(150);
		}

		// A drop passes text over and plots the rest; the notice counts what
		// was text and stays until its ×. Dropped alone, text has its own
		// sentence.
		{
			auto* plotRoot = window->findChild<QObject*>(QStringLiteral("tracePlot"));
			const QString ready = QStringLiteral("device.Ready"), name = QStringLiteral("device.Name"),
			              label = QStringLiteral("device.Label"), gainPath = QStringLiteral("device.Config.Gain");
			traces.setPaths(6, {{1, QStringLiteral("device.Voltage")}, {2, ready}, {gain, gainPath}, {10, QStringLiteral("device.Mode")},
			                    {11, name}, {12, label}},
			                {{ready, true}, {QStringLiteral("device.Mode"), true}});
			traces.append(frame(ux::WatchTraces::nowNs(), {{11, 0, ux::WatchValueBits{}}, {12, 0, ux::WatchValueBits{}}}));
			CHECK(!traces.refusal(name).isEmpty() && !traces.refusal(label).isEmpty() && traces.refusal(ready).isEmpty());
			const auto order = traces.plotted();
			const auto drop = [&](const QStringList& paths) {
				QVariant plottedAny;
				CHECK(QMetaObject::invokeMethod(plotRoot, "dropPaths", Q_RETURN_ARG(QVariant, plottedAny),
				                                Q_ARG(QVariant, QVariant(QVariantMap{{QStringLiteral("from"), QStringLiteral("watchlist")},
				                                                                     {QStringLiteral("paths"), paths}}))));
				return plottedAny.toBool();
			};
			traces.unplot(ready);
			traces.unplot(gainPath);
			CHECK(drop({ready, name, gainPath}) && traces.plotted() == order &&
			      plotRoot->property("notice").toString() == QStringLiteral("1 of 3 values is text and was not plotted.") &&
			      plotRoot->property("noticeKind").toString() == QStringLiteral("refusal"));
			// What was plotted is one step of the history.
			CHECK(history.undoText() == QStringLiteral("Plot 2 values"));
			traces.unplot(ready);
			traces.unplot(gainPath);
			CHECK(drop({ready, name, label, gainPath}) && traces.plotted() == order &&
			      plotRoot->property("notice").toString() == QStringLiteral("2 of 4 values are text and were not plotted."));
			CHECK(!drop({name}) && plotRoot->property("notice").toString() == traces.refusal(name));
			settle(100);
			click(window, shown(content, "plotNoticeClose"));
			settle();
			CHECK(plotRoot->property("notice").toString().isEmpty());
		}

		// The chip's × takes a value off the Plot; its readings stay.
		const auto removes = all(content, "plotChipRemove");
		CHECK(removes.size() == 3);
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
		                  removes[0]->mapToScene(QPointF(removes[0]->width() / 2, removes[0]->height() / 2)).toPoint());
		settle();
		CHECK(traces.plotted() == QStringList({QStringLiteral("device.Ready"), QStringLiteral("device.Config.Gain")}) &&
		      traces.count(QStringLiteral("device.Voltage")) == 65);
		// Plotted again, it keeps its colour; a colour another curve took
		// meanwhile is never shared.
		CHECK(traces.plot(QStringLiteral("device.Voltage")).isEmpty() && traces.colorIndex(QStringLiteral("device.Voltage")) == 0);
		traces.unplot(QStringLiteral("device.Config.Gain"));
		CHECK(traces.plot(QStringLiteral("device.Other")).isEmpty() && traces.colorIndex(QStringLiteral("device.Other")) == 2);
		CHECK(traces.plot(QStringLiteral("device.Config.Gain")).isEmpty() && traces.colorIndex(QStringLiteral("device.Config.Gain")) == 3);

		// Sixteen curves at most, the seventeenth refused in words.
		for (int i = 0; traces.plotted().size() < ux::WatchTraces::limit; ++i)
			CHECK(traces.plot(QStringLiteral("device.Extra%1").arg(i)).isEmpty());
		CHECK(traces.plot(QStringLiteral("device.More")) ==
		      QStringLiteral("The Plot holds 16 curves. Remove one to plot device.More."));
		settle();

		// A value that leaves the Watchlist breaks its line there; its
		// readings stay.
		traces.setPaths(2, {{2, QStringLiteral("device.Ready")}, {gain, QStringLiteral("device.Config.Gain")}});
		CHECK(traces.count(QStringLiteral("device.Voltage")) == 66 && !traces.latest(QStringLiteral("device.Voltage")).isValid());
		CHECK(!traces.watched(QStringLiteral("device.Voltage")) && traces.watched(QStringLiteral("device.Ready")) &&
		      !std::isnan(traces.lastReadUtcMs(QStringLiteral("device.Voltage"))));
		settle(600);
		{
			bool since = false;
			for (auto* chip : all(content, "plotChip")) {
				auto* name = named(chip, "plotChipName");
				auto* value = named(chip, "plotChipValue");
				if (name && value && name->property("text").toString() == QStringLiteral("device.Voltage"))
					since = value->property("text").toString().startsWith(QStringLiteral("not read since "));
			}
			CHECK(since);
			CHECK(view->property("drawnEnds").toInt() >= 1);
			// Sixteen chips: three rows of them, then they scroll, and the
			// graph keeps its room.
			auto* legendScroll = shown(content, "plotLegendScroll");
			const double legendCap = 3 * all(content, "plotChip").first()->height() + 2 * shown(content, "plotLegend")->property("spacing").toDouble();
			CHECK(legendScroll->height() <= legendCap + 0.5 && legendScroll->property("contentHeight").toDouble() > legendScroll->height() &&
			      view->height() > 150);
			shot(window, argc, argv, QStringLiteral("trace-plot-not-read-760"));
		}

		// TR-2. A recording shown with the same view: two hours of one run,
		// set by the host - past the live ten minutes and 65,536 readings -
		// with a time of day from its anchor, drawn per pixel, its chip never
		// "not read since".
		{
			ux::WatchTraces recorded;
			const qint64 start = 1'000'000'000'000;
			recorded.setAnchor({start, 1'763'200'000'000});
			std::vector<ux::TraceReading> readings;
			for (int i = 0; i < 72'000; ++i)
				readings.push_back({start + qint64(i) * 100'000'000, std::sin(i / 600.0) * 10});
			readings[36'000].value = std::numeric_limits<double>::quiet_NaN();
			recorded.setRecorded(QStringLiteral("run.Speed"), std::move(readings), false);
			recorded.setRecorded(QStringLiteral("run.Running"), {{start, 1}, {start + 3'600'000'000'000, 0}}, true);
			CHECK(recorded.count(QStringLiteral("run.Speed")) == 72'000 && recorded.watched(QStringLiteral("run.Speed")) &&
			      recorded.categorical(QStringLiteral("run.Running")) && recorded.anchor().utcMs == 1'763'200'000'000);
			CHECK(recorded.plot(QStringLiteral("run.Speed")).isEmpty() && recorded.plot(QStringLiteral("run.Running")).isEmpty());
			engine.rootContext()->setContextProperty(QStringLiteral("fixtureTraces"), &recorded);
			settle(300);
			CHECK(QMetaObject::invokeMethod(view, "choosePreset", Q_ARG(int, 0)));
			settle(300);
			// 72,000 readings drawn as each pixel's extremes, over the run's two hours.
			const auto hours = view->property("timeLabels").toStringList();
			CHECK(view->property("showAll").toBool() && view->property("drawnPoints").toInt() > 0 &&
			      view->property("drawnPoints").toInt() < 10'000 && view->property("laneLabels").toStringList().size() == 1 &&
			      !hours.isEmpty() && hours.first() != hours.last());
			bool gone = false;
			for (auto* value : all(content, "plotChipValue"))
				gone = gone || value->property("text").toString().startsWith(QStringLiteral("not read"));
			CHECK(!gone);
			CHECK(labelsWhole(view));
			shot(window, argc, argv, QStringLiteral("trace-plot-recorded-760"));
			engine.rootContext()->setContextProperty(QStringLiteral("fixtureTraces"), &traces);
			settle(300);
		}

		// At most 65,536 readings, and ten minutes, a value.
		traces.setPaths(3, {{5, QStringLiteral("device.Fast")}, {6, QStringLiteral("device.Slow")}});
		const qint64 start = ux::WatchTraces::nowNs();
		for (int i = 0; i < 70000; ++i)
			traces.append(frame(start + qint64(i) * 1000, {{5, 0, bits(S::U8, 1, quint64(i & 0xff))}}));
		CHECK(traces.count(QStringLiteral("device.Fast")) == int(ux::WatchTraces::keptReadings));
		std::size_t sum = 0;
		for (const auto& path : {"device.Voltage", "device.Ready", "device.Config.Gain", "device.Mode", "device.Name", "device.Label",
		                         "device.Fast"})
			sum += std::size_t(traces.count(QString::fromLatin1(path)));
		CHECK(traces.total() == sum);
		for (int i = 0; i < 700; ++i)
			traces.append(frame(start + qint64(i) * 1'000'000'000, {{6, 0, bits(S::U8, 1, 1)}}));
		const auto* slow = traces.readings(QStringLiteral("device.Slow"));
		CHECK(slow && slow->back().ns - slow->front().ns <= ux::WatchTraces::keptNs && slow->size() <= 601);

		window->close();
		settle();
		CHECK(warnings == 0);
		qInfo().noquote() << QStringLiteral("Trace plot checks=%1 warnings=%2").arg(checks).arg(warnings);
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Trace plot failure: %s\n", error.what());
		return 1;
	}
}
