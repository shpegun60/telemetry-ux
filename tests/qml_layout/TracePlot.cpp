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
		      values[1]->property("text").toString() == QStringLiteral("1"));
		auto* view = shown(content, "plotView");
		CHECK(view->property("following").toBool() && view->property("spanSeconds").toDouble() == 60.0);
		CHECK(view->property("drawnCurves").toInt() == 2 && view->property("rawCurves").toInt() == 2);
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

		// LP-1b. A preset sets the span and follows the newest again.
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
		click(window, shown(content, "plotPreset_60"));
		settle(150);
		CHECK(following() && span() == 60);

		// The chip's × takes a value off the Plot; its readings stay.
		const auto removes = all(content, "plotChipRemove");
		CHECK(removes.size() == 3);
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
		                  removes[0]->mapToScene(QPointF(removes[0]->width() / 2, removes[0]->height() / 2)).toPoint());
		settle();
		CHECK(traces.plotted() == QStringList({QStringLiteral("device.Ready"), QStringLiteral("device.Config.Gain")}) &&
		      traces.count(QStringLiteral("device.Voltage")) == 63);
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
		CHECK(traces.count(QStringLiteral("device.Voltage")) == 64 && !traces.latest(QStringLiteral("device.Voltage")).isValid());

		// At most 65,536 readings, and ten minutes, a value.
		traces.setPaths(3, {{5, QStringLiteral("device.Fast")}, {6, QStringLiteral("device.Slow")}});
		const qint64 start = ux::WatchTraces::nowNs();
		for (int i = 0; i < 70000; ++i)
			traces.append(frame(start + qint64(i) * 1000, {{5, 0, bits(S::U8, 1, quint64(i & 0xff))}}));
		CHECK(traces.count(QStringLiteral("device.Fast")) == int(ux::WatchTraces::keptReadings));
		std::size_t sum = 0;
		for (const auto& path : {"device.Voltage", "device.Ready", "device.Config.Gain", "device.Fast"})
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
