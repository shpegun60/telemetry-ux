/* Actual installed Qt-only view: provider strings must not become rich text. MIT. */
#include <Telemetry/UX/TrendHistory.hpp>
#include <QAbstractItemModel>
#include <QColor>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>

#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
void drain()
{
	QEventLoop events;
	QTimer::singleShot(150, &events, &QEventLoop::quit);
	events.exec();
}

void collect(QQuickItem& item, QList<QQuickItem*>& items)
{
	items.append(&item);
	for (auto* child : item.childItems())
		collect(*child, items);
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	if (argc != 3 && argc != 4)
		return 1;
	const bool unsafe = argc == 4 && std::strcmp(argv[3], "--unsafe-control") == 0;
	if (argc == 4 && !unsafe)
		return 1;
	unsigned checks = 0;
	bool passed = true, warnings = false;
	const auto check = [&](bool condition, int line = __builtin_LINE()) {
		++checks;
		if (!condition) {
			passed = false;
			std::fprintf(stderr, "Literal view failed check=%u line=%d\n", checks, line);
		}
		return condition;
	};
	QQmlApplicationEngine engine;
	QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError>&) {
		warnings = true;
	});
	engine.setInitialProperties(
	    {{"imageBase", QString::fromUtf8(argv[2])}, {"unsafeControl", unsafe}});
	engine.load(QUrl::fromLocalFile(QString::fromUtf8(argv[1])));
	if (!check(engine.rootObjects().size() == 1))
		return 2;
	auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
	if (!check(window != nullptr))
		return 2;
	drain();
	if (unsafe) {
		auto* label = window->findChild<QObject*>("unsafeLabel");
		check(label && label->property("textFormat").toInt() == Qt::AutoText);
	} else {
        // Resolve and instantiate the new independent components through the
        // relocated installed resource module, using the same host provider.
        auto* owner = window->findChild<QObject*>("literalOwner");
        QQmlComponent independent(&engine);
        independent.setData(R"qml(import QtQuick
import Telemetry.UX
Item {
    required property var provider
    EndpointTreePanel { objectName: "installedTreePanel"; provider: parent.provider }
    WatchlistPanel { objectName: "installedWatchPanel"; provider: parent.provider }
})qml", QUrl());
        auto* views = independent.createWithInitialProperties({{"provider", QVariant::fromValue(owner)}});
        check(owner && views);
        if (views) {
            auto* treePanel = views->findChild<QObject*>("installedTreePanel");
            auto* watchPanel = views->findChild<QObject*>("installedWatchPanel");
            check(treePanel && watchPanel);
            check(treePanel && treePanel->property("provider").value<QObject*>() == owner);
            check(watchPanel && watchPanel->property("provider").value<QObject*>() == owner);
            delete views;
        }
        // The design singletons resolve through the relocated installed module.
        QQmlComponent design(&engine);
        design.setData(R"qml(import QtQuick
import Telemetry.UX
QtObject {
    readonly property color accent: Theme.accent
    readonly property real body: Type.body
    readonly property real heading: Type.heading
})qml", QUrl());
        auto* tokens = design.create();
        check(tokens && tokens->property("accent").value<QColor>() == QColor(QStringLiteral("#6fd4bf")));
        check(tokens && tokens->property("body").toDouble() > 0 &&
              tokens->property("heading").toDouble() > tokens->property("body").toDouble());
        delete tokens;
		auto* expanded = window->findChild<QObject*>("watchlistExpanded");
		check(expanded && expanded->setProperty("checked", true));
		drain();
		// Every literal marker on screen, each checked to be shown as plain text.
		const auto shownMarkers = [&](QList<QQuickItem*>& items) {
			items.clear();
			collect(*window->contentItem(), items);
			QSet<QString> names;
			for (auto* item : items) {
				const auto text = item->property("text").toString();
				if (!text.contains(QStringLiteral("marker-")) ||
				    !item->property("textFormat").isValid())
					continue;
				check(item->property("textFormat").toInt() == Qt::PlainText);
				const auto begin = text.indexOf(QStringLiteral("marker-")) + 7;
				names.insert(text.mid(begin, text.indexOf(QStringLiteral("</b>"), begin) - begin));
			}
			return names;
		};
		const auto expectMarkers = [&](const QSet<QString>& names, std::initializer_list<const char*> tags) {
			for (const auto* tag : tags) {
				if (!names.contains(QString::fromLatin1(tag)))
					std::fprintf(stderr, "No plain-text marker for %s\n", tag);
				check(names.contains(QString::fromLatin1(tag)));
			}
		};
		// A row's tooltip on screen opens with its literal text.
		const auto expectToolTip = [&](const QList<QQuickItem*>& items, const char* name) {
			// A row on screen: a pooled, hidden delegate's tooltip does not open.
			QObject* tip = nullptr;
			for (auto* item : items) {
				if (!item->isVisible())
					continue;
				tip = item->findChild<QObject*>(QString::fromLatin1(name), Qt::FindDirectChildrenOnly);
				if (tip)
					break;
			}
			if (!check(tip != nullptr)) {
				std::fprintf(stderr, "Literal view: no %s on a visible row\n", name);
				return;
			}
			check(tip->property("text").toString().contains(QStringLiteral("<img")));
			check(QMetaObject::invokeMethod(tip, "open"));
			drain();
			// A tooltip opens after its delay (the tree's waits 700 ms).
			QElapsedTimer waited;
			waited.start();
			while (!tip->property("visible").toBool() && waited.elapsed() < 2000) {
				QEventLoop pause;
				QTimer::singleShot(20, &pause, &QEventLoop::quit);
				pause.exec();
			}
			auto* content = tip->property("contentItem").value<QObject*>();
			if (!check(tip->property("visible").toBool()))
				std::fprintf(stderr, "Literal view: %s did not open within 2 s\n", name);
			check(content && content->property("textFormat").toInt() == Qt::PlainText);
			check(content &&
			      content->property("text").toString() == tip->property("text").toString());
			check(QMetaObject::invokeMethod(tip, "close"));
		};

		// Disconnected with a Field selected: the tree, the values, the
		// Watchlist and every outcome.
		QList<QQuickItem*> items;
		expectMarkers(shownMarkers(items),
		              {"selection", "endpoint", "member", "enum", "watchName", "watchStatus", "watchValue",
		               "watchSummary", "writeOutcome", "commandOutcome", "serviceOutcome", "status"});
		for (const auto* name : {"endpointToolTip", "valueToolTip"})
			expectToolTip(items, name);

		// A Service's response belongs to that Service's Details: connected,
		// with the Service that answered selected.
		if (owner) {
			const auto selection = owner->property("selection");
			check(owner->setProperty("connected", true) && owner->setProperty("serviceSelected", true) &&
			      owner->setProperty("selection", owner->property("serviceResponseEndpoint")));
			drain();
			expectMarkers(shownMarkers(items), {"responseHeader", "responseMember", "responseEnum"});
			expectToolTip(items, "responseToolTip");
			check(owner->setProperty("serviceSelected", false) && owner->setProperty("connected", false) &&
			      owner->setProperty("selection", selection));
			drain();
		}
		// A schema-supplied enum string reaches TrendHistory through the
		// Watchlist. Its error status must remain literal, even when it looks
		// like markup containing an HTTP image.
		auto* source = owner ? qobject_cast<QAbstractItemModel*>(
		                           owner->property("watchlist").value<QObject*>()) : nullptr;
		auto* history = new telemetry::ux::TrendHistory(&app);
		QQmlComponent trendComponent(&engine);
		trendComponent.setData(R"qml(import QtQuick
import Telemetry.UX
TrendPanel {
    required property var testProvider
    required property var testHistory
    provider: testProvider
    history: testHistory
    width: 500
    height: 300
})qml", QUrl());
		auto* trend = qobject_cast<QQuickItem*>(trendComponent.createWithInitialProperties(
		    {{"testProvider", QVariant::fromValue(owner)}, {"testHistory", QVariant::fromValue(history)}}));
		if (check(source && trend)) {
			trend->setParentItem(window->contentItem());
			history->setRow(0);
			check(QMetaObject::invokeMethod(owner, "publishTrendSample"));
			drain();
			auto* state = trend->findChild<QObject*>("trendState");
			check(state && state->property("textFormat").toInt() == Qt::PlainText);
			check(state && state->property("text").toString() == history->state());
			check(history->state().contains(QStringLiteral("<b>marker-trendValue</b><img src=\"")));
		}
		delete trend;
	}
	drain();
	check(!warnings);
	std::printf("Literal Configurator checks=%u warnings=%u mode=%s Qt=%s\n", checks,
	            warnings ? 1u : 0u, unsafe ? "unsafe-control" : "literal", qVersion());
	return passed ? 0 : 3;
}
