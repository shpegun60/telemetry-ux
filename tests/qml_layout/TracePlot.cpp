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
#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include <QtGui/QWheelEvent>
#include <QtGui/QGuiApplication>
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
			traces.setPoints(voltage, 0);
			// The graph's menu (right-click): every curve's points.
			const auto graph = view->property("plotArea").toRectF();
			QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
			                  view->mapToScene(QPointF(graph.left() + graph.width() * 0.3, graph.center().y())).toPoint());
			settle(300);
			click(window, shown(content, "plotShowPoints"));
			settle(300);
			CHECK(traces.showPoints() && view->property("drawnDots").toInt() > 0);
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
			openGraphMenu(0.4);
			click(window, shown(content, "plotMenuRemoveAll"));
			settle(200);
			CHECK(traces.plotted().isEmpty() && traces.count(voltage) > 0 && shown(content, "plotEmpty"));
			CHECK(traces.restore(before) && traces.plotted() == order);
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
			CHECK(traces.restore(before) && traces.plotted() == order);
			QTest::mouseMove(window, QPoint(4, 4));
			settle(150);
		}

		// LP-1h. The time axis from the start of reading: labels in time since
		// the end of the last stop, the cursor chip with both times; its menu
		// brings the time of day back.
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
		for (const auto& path : {"device.Voltage", "device.Ready", "device.Config.Gain", "device.Mode", "device.Fast"})
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
