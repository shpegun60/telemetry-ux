/* Actual installed Qt-only view: provider strings must not become rich text. MIT. */
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
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
	const auto check = [&](bool condition) {
		++checks;
		if (!condition) {
			passed = false;
			std::fprintf(stderr, "Literal view failed check=%u\n", checks);
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
		auto* expanded = window->findChild<QObject*>("watchlistExpanded");
		check(expanded && expanded->setProperty("checked", true));
		drain();
		QList<QQuickItem*> items;
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
		for (const auto* tag :
		     {"selection", "endpoint", "member", "enum", "responseHeader", "responseMember",
		      "responseEnum", "watchName", "watchStatus", "watchValue", "watchSummary",
		      "writeOutcome", "commandOutcome", "serviceOutcome", "status"})
			check(names.contains(QString::fromLatin1(tag)));
		for (const auto* name : {"endpointToolTip", "valueToolTip", "responseToolTip"}) {
			QObject* tip = nullptr;
			for (auto* item : items) {
				tip = item->findChild<QObject*>(QString::fromLatin1(name));
				if (tip)
					break;
			}
			if (!check(tip != nullptr))
				continue;
			check(tip->property("text").toString().contains(QStringLiteral("<img")));
			check(QMetaObject::invokeMethod(tip, "open"));
			drain();
			auto* content = tip->property("contentItem").value<QObject*>();
			check(tip->property("visible").toBool());
			check(content && content->property("textFormat").toInt() == Qt::PlainText);
			check(content &&
			      content->property("text").toString() == tip->property("text").toString());
			check(QMetaObject::invokeMethod(tip, "close"));
		}
	}
	drain();
	check(!warnings);
	std::printf("Literal Configurator checks=%u warnings=%u mode=%s Qt=%s\n", checks,
	            warnings ? 1u : 0u, unsafe ? "unsafe-control" : "literal", qVersion());
	return passed ? 0 : 3;
}
