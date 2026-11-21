// The Log's lines (LG-1, the designer's §7) in a real QQuickWindow over a real
// LogModel and ProblemsModel: a Command run is one line naming it; a refused
// write one error line with the path, the value and the reason; a Service its
// request and its answer; a grouped warning one line whose current row opens
// its list (20 paths, then "and 12 more"), and its resolution a line saying
// what changed. Severity by glyph and colour from the outcome's kind; the
// words give way before the path; below 360 px the source column goes and the
// accessible name keeps it. No device, no provider. MIT.
#include <Telemetry/UX/EventLog.hpp>
#include <Telemetry/UX/QmlTypes.hpp>
#include <QtCore/QDir>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QTest>
#include <cstdio>
#include <stdexcept>

using telemetry::ux::LogModel;
using telemetry::ux::ProblemsModel;
using telemetry::ux::Severity;

namespace {
int checks = 0, warnings = 0;
void check(bool condition, const char* expression)
{
	++checks;
	if (!condition)
		throw std::runtime_error(expression);
}
#define CHECK(x) check(bool(x), #x)

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
	QTest::qWait(80);
}

QVariantMap operation(const QString& key, const QString& source, const QString& kind, const QString& path,
                      const QString& values, const QString& result, const QString& text)
{
	return {{QStringLiteral("key"), key},       {QStringLiteral("source"), source}, {QStringLiteral("kind"), kind},
	        {QStringLiteral("path"), path},     {QStringLiteral("values"), values}, {QStringLiteral("result"), result},
	        {QStringLiteral("text"), text}};
}

QStringList paths(int count)
{
	QStringList out;
	for (int i = 1; i <= count; ++i)
		out << QStringLiteral("signals/Signal%1").arg(i, 2, 10, QLatin1Char('0'));
	return out;
}

// The delegate showing the Log's row `row`.
QQuickItem* rowItem(QQuickItem* list, int row)
{
	auto* content = list->property("contentItem").value<QQuickItem*>();
	for (auto* child : content ? content->childItems() : QList<QQuickItem*>{})
		if (child->objectName() == QStringLiteral("logRow") && child->property("index").toInt() == row &&
		    child->isVisible())
			return child;
	return nullptr;
}

QString text(QQuickItem* item, const char* property = "text")
{
	return item ? item->property(property).toString() : QString{};
}

QColor colour(QQuickItem* item, const char* property = "color")
{
	return item ? item->property(property).value<QColor>() : QColor{};
}

// Every piece of the line inside its row, nothing under another: the values
// after the path as it is shown, the words after the values.
bool fits(QQuickItem* row)
{
	auto* target = named(row, QStringLiteral("logLineTarget"));
	auto* path = named(row, QStringLiteral("logLinePath"));
	auto* values = named(row, QStringLiteral("logLineValues"));
	auto* words = named(row, QStringLiteral("logLineWords"));
	if (!target || !path || !values || !words)
		return false;
	for (auto* piece : {path, values, words})
		if (piece->isVisible() && piece->x() + piece->width() > target->width() + 1)
			return false;
	const qreal pathEnd = path->isVisible() ? path->x() + path->property("usedWidth").toReal() : 0;
	if (values->isVisible() && values->x() < pathEnd)
		return false;
	const bool below = named(row, QStringLiteral("logLine"))->property("wordsBelow").toBool();
	if (words->isVisible() && !below && words->x() < (values->isVisible() ? values->x() + values->width() : pathEnd))
		return false;
	return target->x() + target->width() <= row->width() + 1;
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	telemetry::ux::registerQmlTypes();
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
			++warnings;
		std::fprintf(stderr, "%s\n", qPrintable(message));
	});
	try {
		CHECK(argc > 1);
		const QDir output(QString::fromLocal8Bit(argv[1]));
		LogModel log;
		ProblemsModel problems(&log);
		// The Log as a start, a Command, a refused write and a Service leave it,
		// newest first.
		problems.raise(QStringLiteral("project.watch"), Severity::Warning, QStringLiteral("Watch"),
		               QStringLiteral("32 watched values wait for a device"), {}, paths(32));
		log.append(Severity::Info, QStringLiteral("Connection"), QStringLiteral("Connected to Emulated device."));
		problems.resolveWith(QStringLiteral("project.watch"),
		                     QStringLiteral("All 32 watched values found on Emulated device"));
		log.record(operation(QStringLiteral("op:1"), QStringLiteral("Command"), QStringLiteral("command"),
		                     QStringLiteral("control/Reset"), {}, QStringLiteral("ok"), QStringLiteral("executed")));
		log.record(operation(QStringLiteral("op:2"), QStringLiteral("Field"), QStringLiteral("value"),
		                     QStringLiteral("device/Config.Gain"), QStringLiteral("= 345"), QStringLiteral("refused"),
		                     QStringLiteral("refused · the device does not take this value · it holds 3")));
		log.record(operation(QStringLiteral("op:3"), QStringLiteral("Service"), QStringLiteral("service"),
		                     QStringLiteral("control/Scale"), QStringLiteral("(2, 3) → 6"), QStringLiteral("ok"),
		                     QStringLiteral("called · 19 ms")));
		CHECK(log.count() == 6 && problems.count() == 0);
		enum { Scale = 0, Refused = 1, Reset = 2, Found = 3, Connected = 4, Waiting = 5 };

		QQmlApplicationEngine engine;
		engine.rootContext()->setContextProperty(QStringLiteral("logLinesModel"), &log);
		engine.load(QUrl(QStringLiteral("qrc:/LogLines.qml")));
		CHECK(!engine.rootObjects().isEmpty());
		auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
		CHECK(window && QTest::qWaitForWindowExposed(window));
		settle();
		auto* list = named(window->contentItem(), QStringLiteral("logLinesView"));
		CHECK(list);
		const auto theme = [&](const char* role) {
			QColor value;
			if (auto* themeObject = engine.singletonInstance<QObject*>("Telemetry.UX", "Theme"))
				value = themeObject->property(role).value<QColor>();
			return value;
		};
		const auto rowHeight = [&] {
			auto* typeObject = engine.singletonInstance<QObject*>("Telemetry.UX", "Type");
			return typeObject ? typeObject->property("rowHeight").toReal() : 0.0;
		}();
		CHECK(rowHeight > 0);

		// A Command run: one line naming it - the kind word, the arrow, its
		// path and what happened; an info, so no glyph and quieter words.
		auto* reset = rowItem(list, Reset);
		CHECK(reset);
		CHECK(text(named(reset, QStringLiteral("logLineSource"))) == QStringLiteral("Command"));
		auto* resetKind = named(reset, QStringLiteral("logLineKind"));
		CHECK(resetKind && resetKind->isVisible() && text(resetKind, "kind") == QStringLiteral("command") &&
		      text(resetKind, "name") == QStringLiteral("command"));
		CHECK(text(named(reset, QStringLiteral("logLinePath")), "path") == QStringLiteral("control/Reset") &&
		      text(named(reset, QStringLiteral("logLinePathLeaf"))) == QStringLiteral("Reset"));
		CHECK(text(named(reset, QStringLiteral("logLineWords"))) == QStringLiteral("executed"));
		CHECK(!named(reset, QStringLiteral("logLineValues"))->isVisible());
		CHECK(text(named(reset, QStringLiteral("logLineSeverity")), "name").isEmpty());
		CHECK(colour(named(reset, QStringLiteral("logLineWords"))) == theme("textSecondary"));
		CHECK(qAbs(reset->height() - rowHeight) < 1);

		// A refused write: one error line - the glyph in the error colour, the
		// path, the value and the reason in the primary colour.
		auto* refused = rowItem(list, Refused);
		CHECK(refused);
		CHECK(text(named(refused, QStringLiteral("logLineSource"))) == QStringLiteral("Field"));
		CHECK(text(named(refused, QStringLiteral("logLineSeverity")), "name") == QStringLiteral("error") &&
		      colour(named(refused, QStringLiteral("logLineSeverity"))) == theme("error"));
		CHECK(text(named(refused, QStringLiteral("logLinePath")), "path") == QStringLiteral("device/Config.Gain") &&
		      text(named(refused, QStringLiteral("logLineValues"))) == QStringLiteral("= 345"));
		CHECK(text(named(refused, QStringLiteral("logLineWords"))).startsWith(
		    QStringLiteral("refused · the device does not take this value")));
		CHECK(colour(named(refused, QStringLiteral("logLineWords"))) == theme("textPrimary"));
		CHECK(text(named(refused, QStringLiteral("logLineKind")), "kind") == QStringLiteral("value"));

		// A Service: its request and its answer after the path, then the time.
		auto* scale = rowItem(list, Scale);
		CHECK(scale && text(named(scale, QStringLiteral("logLineKind")), "kind") == QStringLiteral("service") &&
		      text(named(scale, QStringLiteral("logLineValues"))) == QStringLiteral("(2, 3) → 6") &&
		      text(named(scale, QStringLiteral("logLineWords"))) == QStringLiteral("called · 19 ms"));

		// The grouped warning: one line, no path, its list closed.
		auto* waiting = rowItem(list, Waiting);
		CHECK(waiting);
		CHECK(text(named(waiting, QStringLiteral("logLineSource"))) == QStringLiteral("Watch") &&
		      text(named(waiting, QStringLiteral("logLineWords"))) == QStringLiteral("32 watched values wait for a device") &&
		      text(named(waiting, QStringLiteral("logLineSeverity")), "name") == QStringLiteral("warning") &&
		      colour(named(waiting, QStringLiteral("logLineSeverity"))) == theme("warning"));
		CHECK(!named(waiting, QStringLiteral("logLinePath"))->isVisible() &&
		      !named(waiting, QStringLiteral("logLineKind"))->isVisible() &&
		      !named(waiting, QStringLiteral("logLineDetail"))->isVisible() && qAbs(waiting->height() - rowHeight) < 1);
		// Its resolution says what changed.
		auto* found = rowItem(list, Found);
		CHECK(found && text(named(found, QStringLiteral("logLineWords"))) ==
		                   QStringLiteral("All 32 watched values found on Emulated device") &&
		      text(named(found, QStringLiteral("logLineSeverity")), "name").isEmpty() &&
		      text(named(found, QStringLiteral("logLineSource"))) == QStringLiteral("Watch"));
		// Every line fits its row, nothing cut at this width.
		for (int row = 0; row < log.count(); ++row) {
			auto* item = rowItem(list, row);
			CHECK(item && fits(item));
			CHECK(!named(item, QStringLiteral("logLineWords"))->property("truncated").toBool());
			CHECK(!named(item, QStringLiteral("logLinePath"))->property("elided").toBool());
		}
		// The accessible name: the time, the severity, the source, the leaf
		// first, the words.
		const auto spoken = text(named(reset, QStringLiteral("logLine")), "spoken");
		CHECK(spoken.contains(QStringLiteral(", Info, Command, Reset")) && spoken.endsWith(QStringLiteral(", executed")));
		CHECK(text(named(refused, QStringLiteral("logLine")), "spoken")
		          .contains(QStringLiteral(", Error, Field, Gain")));

		// The current row opens the list: 20 paths, then "and 12 more".
		list->setProperty("currentIndex", int(Waiting));
		list->forceActiveFocus();
		settle();
		waiting = rowItem(list, Waiting);
		auto* detail = named(waiting, QStringLiteral("logLineDetail"));
		const auto lines = text(detail).split(QLatin1Char('\n'));
		CHECK(detail->isVisible() && lines.size() == LogModel::detailLines + 1 &&
		      lines.front() == QStringLiteral("signals/Signal01") && lines.back() == QStringLiteral("and 12 more"));
		CHECK(waiting->height() > rowHeight * 10 && detail->y() + detail->height() <= waiting->height());
		CHECK(text(named(waiting, QStringLiteral("logLine")), "spoken").contains(
		    QStringLiteral("signals/Signal20, and 12 more")));
		// On the selection a warning's words stay bright: the selection's own
		// colour with focus, the primary one without.
		CHECK(colour(named(waiting, QStringLiteral("logLineWords"))) ==
		      theme(list->hasActiveFocus() ? "selectedForeground" : "textPrimary"));
		CHECK(window->grabWindow().save(output.filePath(QStringLiteral("log-lines-900.png"))));

		// Narrow: the source column goes, the word stays in what is said;
		// the path keeps its leaf whole, the words give way first.
		window->resize(320, 560);
		list->setProperty("currentIndex", -1);
		settle();
		reset = rowItem(list, Reset);
		refused = rowItem(list, Refused);
		CHECK(reset && refused && !named(reset, QStringLiteral("logLineSource"))->isVisible());
		CHECK(text(named(reset, QStringLiteral("logLine")), "spoken").contains(QStringLiteral("Command")));
		for (int row = 0; row < log.count(); ++row) {
			auto* item = rowItem(list, row);
			CHECK(item && fits(item) && qAbs(item->height() - rowHeight) < 1);
		}
		auto* refusedPath = named(refused, QStringLiteral("logLinePath"));
		CHECK(refusedPath->property("leafFits").toBool() &&
		      named(refused, QStringLiteral("logLineWords"))->property("truncated").toBool());
		CHECK(window->grabWindow().save(output.filePath(QStringLiteral("log-lines-320.png"))));
		// Current, the long words go under the path, whole, and the row grows.
		list->setProperty("currentIndex", int(Refused));
		settle();
		refused = rowItem(list, Refused);
		auto* refusedLine = named(refused, QStringLiteral("logLine"));
		auto* refusedWords = named(refused, QStringLiteral("logLineWords"));
		CHECK(refusedLine->property("wordsBelow").toBool() && !refusedWords->property("truncated").toBool() &&
		      refusedWords->property("lineCount").toInt() >= 2 && refused->height() > rowHeight * 1.5 &&
		      fits(refused));
		CHECK(window->grabWindow().save(output.filePath(QStringLiteral("log-lines-320-current.png"))));

		// A line that changes in place (a pending that became its outcome)
		// shows the new words in the same row.
		log.record(operation(QStringLiteral("op:1"), QStringLiteral("Command"), QStringLiteral("command"),
		                     QStringLiteral("control/Reset"), {}, QStringLiteral("failed"),
		                     QStringLiteral("failed · no reply in time")));
		settle();
		reset = rowItem(list, Reset);
		CHECK(log.count() == 6 && text(named(reset, QStringLiteral("logLineWords"))) == QStringLiteral("failed · no reply in time") &&
		      text(named(reset, QStringLiteral("logLineSeverity")), "name") == QStringLiteral("error"));
		std::printf("Log lines checks=%d warnings=%d\n", checks, warnings);
		return warnings == 0 ? 0 : 1;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Log lines check failed after %d checks: %s\n", checks, error.what());
		return 1;
	}
}
