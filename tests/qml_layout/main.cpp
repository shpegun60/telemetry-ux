// Layout only: real disconnected SDK Session, no transport or device requests. MIT.
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <integration/qt/ConnectionSession.hpp>
#include <QGuiApplication>
#include <QImage>
#include <QWheelEvent>
#include <QtTest/qtestspontaneevent.h>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTest>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace {
unsigned checks = 0;
unsigned warnings = 0;

void check(bool condition, const char* expression)
{
	++checks;
	if (!condition)
		throw std::runtime_error(expression);
}

#define CHECK(expression) check(bool(expression), #expression)

QQuickItem* item(QQuickItem* root, const QString& name)
{
	if (root->objectName() == name)
		return root;
	for (auto* child : root->childItems())
		if (auto* found = item(child, name))
			return found;
	return nullptr;
}

QQuickItem* itemWithText(QQuickItem* root, const QString& name, const QString& text)
{
    if (root->objectName() == name && root->property("text").toString() == text)
        return root;
    for (auto* child : root->childItems())
        if (auto* found = itemWithText(child, name, text))
            return found;
    return nullptr;
}

QQuickItem* rowItem(QQuickItem* root, int row)
{
	if (root->objectName() == QStringLiteral("endpointRow") &&
	    root->property("row").toInt() == row)
		return root;
	for (auto* child : root->childItems())
		if (auto* found = rowItem(child, row))
			return found;
	return nullptr;
}

void settle()
{
	QCoreApplication::processEvents();
	QTest::qWait(60);
}

QRectF rectangle(QQuickItem* object)
{
	return object->mapRectToScene(QRectF(0, 0, object->width(), object->height()));
}

void click(QQuickWindow* window, QQuickItem* target)
{
	CHECK(target != nullptr);
	// Settle all ancestor layouts before mapping the real mouse position.
	QList<QQuickItem*> ancestors;
	for (auto* current = target; current; current = current->parentItem())
		ancestors.prepend(current);
	for (auto* current : ancestors)
		current->ensurePolished();
	QCoreApplication::processEvents();
	QTest::mouseClick(
	    window, Qt::LeftButton, Qt::NoModifier,
	    target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
	settle();
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	if (argc != 2)
		return 2;
	telemetry::integration::qt::ConnectionSession session;
	telemetry::ux::QtConnectionBinding binding(&session);
	auto* provider = binding.createProvider(&app);
	QQmlApplicationEngine engine;
	QObject::connect(&engine, &QQmlEngine::warnings, &app, [](const QList<QQmlError>& errors) {
		warnings += unsigned(errors.size());
		for (const auto& error : errors)
			std::fprintf(stderr, "%s\n", error.toString().toUtf8().constData());
	});
	engine.setInitialProperties({{"ownerProvider", QVariant::fromValue(provider)}});
	engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
	auto* window = engine.rootObjects().isEmpty()
	                   ? nullptr
	                   : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
	try {
		CHECK(window != nullptr);
		for (const QSize size : {QSize(1160, 800), QSize(820, 620)}) {
			window->resize(size);
			settle();
			auto* view = item(window->contentItem(), QStringLiteral("layoutConfigurator"));
			auto* tree = item(view, QStringLiteral("endpointTree"));
			auto* values = item(view, QStringLiteral("valueTree"));
			auto* watchToggle = item(view, QStringLiteral("watchlistExpanded"));
			auto* batchToggle = item(view, QStringLiteral("batchToggle"));
			CHECK(tree && values && watchToggle && batchToggle);
			watchToggle->setProperty("checked", false);
			batchToggle->setProperty("checked", false);
			settle();
			auto* watchPanel = item(view, QStringLiteral("watchlistPane"));
			CHECK(watchPanel);
			const auto collapsedWatchHeight = watchPanel->property("implicitHeight").toReal();
			// Both collapsed and expanded must preserve a usable primary workspace.
			CHECK(tree->height() >= 200);
			CHECK(values->height() >= 200);
			CHECK(window->grabWindow().save(
			    QString::fromUtf8(argv[1]) +
			    QStringLiteral("/%1x%2-collapsed.png").arg(size.width()).arg(size.height())));
			watchToggle->setProperty("checked", true);
			batchToggle->setProperty("checked", true);
			settle();
			CHECK(watchPanel->property("implicitHeight").toReal() >= collapsedWatchHeight + 120);
			std::printf("Layout %dx%d expanded endpoint=%.1f values=%.1f\n", size.width(),
			            size.height(), tree->height(), values->height());
			CHECK(tree->height() >= 200);
			CHECK(values->height() >= 200);
			auto* scroll = item(view, QStringLiteral("configuratorScroll"));
			CHECK(scroll != nullptr);
			auto* flick =
			    qobject_cast<QQuickItem*>(scroll->property("contentItem").value<QObject*>());
			CHECK(flick != nullptr);
			const qreal maximum = flick->property("contentHeight").toReal() - flick->height();
			CHECK(maximum > 0);
			flick->setProperty("contentY", 0);
			settle();
			CHECK(rectangle(tree).top() >= rectangle(view).top());
			CHECK(rectangle(tree).top() < rectangle(view).bottom());
			CHECK(window->grabWindow().save(
			    QString::fromUtf8(argv[1]) +
			    QStringLiteral("/%1x%2-top.png").arg(size.width()).arg(size.height())));
			view->forceActiveFocus(Qt::TabFocusReason);
			batchToggle->forceActiveFocus(Qt::TabFocusReason);
			settle();
			CHECK(rectangle(batchToggle).top() >= rectangle(view).top());
			CHECK(rectangle(batchToggle).bottom() <= rectangle(view).bottom());
			flick->setProperty("contentY", maximum);
			settle();
			auto* footer = item(view, QStringLiteral("customFooter"));
			auto* batch = item(view, QStringLiteral("batchPane"));
			auto* response = item(view, QStringLiteral("batchResponseTree"));
			CHECK(footer && batch && response);
			CHECK(footer->height() >= 34);
			CHECK(rectangle(footer).bottom() <= rectangle(view).bottom() + 1);
			CHECK(rectangle(batch).bottom() <= rectangle(footer).top());
			CHECK(response->height() >= 100);
			CHECK(rectangle(response).top() >= rectangle(batch).top());
			CHECK(rectangle(response).bottom() <= rectangle(batch).bottom());
			auto* heading = item(view, QStringLiteral("batchResponseHeading"));
			auto* empty = item(view, QStringLiteral("batchResponseEmpty"));
			CHECK(heading && empty);
			std::printf("heading y=%.1f h=%.1f empty y=%.1f h=%.1f response h=%.1f\n",
			            rectangle(heading).y(), heading->height(), rectangle(empty).y(),
			            empty->height(), response->height());
			CHECK(rectangle(heading).bottom() <= rectangle(empty).top());
			// Expanded controls stay reachable and interactive after scrolling.
			flick->setProperty("contentY", std::max(qreal(0), maximum - 80));
			settle();
			click(window, batchToggle);
			CHECK(!batchToggle->property("checked").toBool());
			batchToggle->setProperty("checked", true);
			settle();
			flick->setProperty("contentY",
			                   flick->property("contentHeight").toReal() - flick->height());
			settle();
			CHECK(window->grabWindow().save(
			    QString::fromUtf8(argv[1]) +
			    QStringLiteral("/%1x%2.png").arg(size.width()).arg(size.height())));
			CHECK(warnings == 0);
		}
		// Optional host slots really disappear, rather than leaving layout gaps.
		auto* view = item(window->contentItem(), QStringLiteral("layoutConfigurator"));
		CHECK(view->setProperty("headerComponent", QVariant::fromValue<QQmlComponent*>(nullptr)));
		CHECK(view->setProperty("footerComponent", QVariant::fromValue<QQmlComponent*>(nullptr)));
		settle();
		CHECK(!item(view, QStringLiteral("customHeader")));
		CHECK(!item(view, QStringLiteral("customFooter")));
		CHECK(view->setProperty("watchlistVisible", false));
		CHECK(view->setProperty("batchVisible", false));
		settle();
		auto* primary = item(view, QStringLiteral("primaryWorkspace"));
		CHECK(primary && primary->height() >= view->height() - 40);
		auto* scroll = item(view, QStringLiteral("configuratorScroll"));
		auto* flick = qobject_cast<QQuickItem*>(scroll->property("contentItem").value<QObject*>());
		CHECK(flick && flick->property("contentHeight").toReal() <= flick->height() + 1);
		CHECK(warnings == 0);
		CHECK(!session.ready() && !session.connecting());
        // Embedding a reusable panel must preserve the host's customized palette.
        auto* embeddedTreePanel = item(view, QStringLiteral("endpointTreePane"));
        auto* embeddedWatchPanel = item(view, QStringLiteral("watchlistPane"));
        CHECK(embeddedTreePanel && embeddedWatchPanel);
        CHECK(window->setProperty("testButtonText", QColor(QStringLiteral("#e38d27"))));
        settle();
        QQmlExpression treeText(qmlContext(embeddedTreePanel), embeddedTreePanel,
                                QStringLiteral("palette.buttonText"));
        QQmlExpression watchText(qmlContext(embeddedWatchPanel), embeddedWatchPanel,
                                 QStringLiteral("palette.buttonText"));
        CHECK(treeText.evaluate().value<QColor>() == QColor(QStringLiteral("#e38d27")) && !treeText.hasError());
        CHECK(watchText.evaluate().value<QColor>() == QColor(QStringLiteral("#e38d27")) && !watchText.hasError());
        CHECK(warnings == 0);
        // Independently embedded panels share one caller-owned provider.
        // The fake provider records intent without creating a transport.
        engine.setInitialProperties({});
        engine.load(QUrl(QStringLiteral("qrc:/Standalone.qml")));
        auto* separateWindow = engine.rootObjects().size() > 1
            ? qobject_cast<QQuickWindow*>(engine.rootObjects().last()) : nullptr;
        CHECK(separateWindow != nullptr);
        settle();
        auto* separateTreePanel = item(separateWindow->contentItem(), QStringLiteral("standaloneTree"));
        auto* separateTreePanel2 = item(separateWindow->contentItem(), QStringLiteral("standaloneTree2"));
        auto* legacyTreePanel = item(separateWindow->contentItem(), QStringLiteral("legacyTree"));
        auto* separateWatchPanel = item(separateWindow->contentItem(), QStringLiteral("standaloneWatch"));
        auto* selected = item(separateWindow->contentItem(), QStringLiteral("standaloneSelection"));
        CHECK(separateTreePanel && separateTreePanel2 && legacyTreePanel && separateWatchPanel && selected);
        auto* shared = separateTreePanel->property("provider").value<QObject*>();
        CHECK(shared && shared == separateWatchPanel->property("provider").value<QObject*>());
        auto* row = item(separateTreePanel, QStringLiteral("endpointRow"));
        CHECK(row != nullptr);
        row = rowItem(separateTreePanel, 0);
        auto* secondRow = rowItem(separateTreePanel, 1);
        auto* mirrorRow = rowItem(separateTreePanel2, 0);
        auto* mirrorSecondRow = rowItem(separateTreePanel2, 1);
        auto* legacyRow = rowItem(legacyTreePanel, 0);
        CHECK(row && secondRow && mirrorRow && mirrorSecondRow && legacyRow);
        CHECK(!legacyRow->property("highlighted").toBool());
        auto* rowContent = row->property("contentItem").value<QObject*>();
        CHECK(rowContent && rowContent->property("color").value<QColor>() == QColor(QStringLiteral("#e8edf5")));
        click(separateWindow, row);
        CHECK(shared->property("selections").toInt() == 1);
        CHECK(selected->property("text").toString() == QStringLiteral("Field voltage"));
        CHECK(row->property("highlighted").toBool() && mirrorRow->property("highlighted").toBool());
        CHECK(!secondRow->property("highlighted").toBool() && !mirrorSecondRow->property("highlighted").toBool());
        CHECK(shared->setProperty("refuseSelection", true));
        click(separateWindow, secondRow);
        CHECK(shared->property("selections").toInt() == 1 && row->property("highlighted").toBool());
        CHECK(!secondRow->property("highlighted").toBool());
        CHECK(shared->setProperty("refuseSelection", false));
        click(separateWindow, secondRow);
        CHECK(shared->property("selections").toInt() == 2);
        CHECK(!row->property("highlighted").toBool() && secondRow->property("highlighted").toBool());
        CHECK(!mirrorRow->property("highlighted").toBool() && mirrorSecondRow->property("highlighted").toBool());
        CHECK(!legacyRow->property("highlighted").toBool());
        auto* keyboardTree = item(separateTreePanel, QStringLiteral("endpointTree"));
        CHECK(keyboardTree != nullptr);
        keyboardTree->forceActiveFocus();
        settle();
        QTest::keyClick(separateWindow, Qt::Key_Up);
        settle();
        CHECK(row->property("current").toBool());
        CHECK(secondRow->property("highlighted").toBool()); // Cursor != accepted selection.
        auto* focusIndicator = item(row, QStringLiteral("endpointFocusIndicator"));
        CHECK(focusIndicator && focusIndicator->isVisible());
        QTest::keyClick(separateWindow, Qt::Key_Return);
        settle();
        CHECK(row->property("highlighted").toBool() && !secondRow->property("highlighted").toBool());
        click(separateWindow, item(separateWatchPanel, QStringLiteral("addWatchButton")));
        CHECK(shared->property("watchCount").toInt() == 1);
        click(separateWindow, item(separateWatchPanel, QStringLiteral("startWatchButton")));
        CHECK(shared->property("watchActive").toBool());
        CHECK(shared->property("starts").toInt() == 1 && shared->property("stops").toInt() == 0);
        separateWatchPanel->setVisible(false);
        settle();
        separateWatchPanel->setVisible(true);
        settle();
        CHECK(shared->property("watchActive").toBool());
        CHECK(shared->property("starts").toInt() == 1 && shared->property("stops").toInt() == 0);
        click(separateWindow, item(separateWatchPanel, QStringLiteral("startWatchButton")));
        CHECK(!shared->property("watchActive").toBool() && shared->property("stops").toInt() == 1);
        CHECK(warnings == 0);
        // A small standalone dock must wrap its controls and offer real wheel
        // scrolling without changing the provider's polling state.
        engine.setInitialProperties({});
        engine.load(QUrl(QStringLiteral("qrc:/Narrow.qml")));
        auto* narrowWindow = engine.rootObjects().size() > 2
            ? qobject_cast<QQuickWindow*>(engine.rootObjects().last()) : nullptr;
        CHECK(narrowWindow != nullptr);
        settle();
        auto* narrowPanel = item(narrowWindow->contentItem(), QStringLiteral("narrowWatch"));
        auto* narrowProvider = narrowPanel ? narrowPanel->property("provider").value<QObject*>() : nullptr;
        auto* narrowScroll = item(narrowPanel, QStringLiteral("watchlistScroll"));
        auto* narrowFlick = narrowScroll ? qobject_cast<QQuickItem*>(narrowScroll->property("contentItem").value<QObject*>()) : nullptr;
        CHECK(narrowPanel && narrowProvider && narrowScroll && narrowFlick);
        for (const auto* name : {"watchlistExpanded", "addWatchButton", "refreshWatchButton", "startWatchButton"}) {
            auto* control = item(narrowPanel, QString::fromLatin1(name));
            CHECK(control && rectangle(control).left() >= rectangle(narrowPanel).left());
            CHECK(rectangle(control).right() <= rectangle(narrowPanel).right());
        }
        const qreal collapsedHeight = narrowPanel->property("implicitHeight").toReal();
        click(narrowWindow, item(narrowPanel, QStringLiteral("watchlistExpanded")));
        CHECK(narrowPanel->property("implicitHeight").toReal() >= collapsedHeight + 120);
        click(narrowWindow, item(narrowPanel, QStringLiteral("watchlistExpanded")));
        CHECK(narrowPanel->property("implicitHeight").toReal() <= collapsedHeight + 1);
        click(narrowWindow, item(narrowPanel, QStringLiteral("addWatchButton")));
        CHECK(narrowProvider->property("watchCount").toInt() == 1);
        CHECK(narrowFlick->property("contentHeight").toReal() > narrowFlick->height());
        narrowWindow->grabWindow().save(QString::fromUtf8(argv[1]) + QStringLiteral("/narrow-before-wheel.png"));
        auto* narrowBar = item(narrowPanel, QStringLiteral("watchlistScrollBar"));
        CHECK(narrowBar && narrowBar->isVisible());
        const QRectF bar = rectangle(narrowBar);
        CHECK(bar.height() > 100 && bar.right() <= rectangle(narrowPanel).right());
        const QPoint press = QPointF(bar.center().x(), bar.bottom() - 5).toPoint();
        QTest::mouseClick(narrowWindow, Qt::LeftButton, Qt::NoModifier, press);
        settle();
        const auto origin = narrowFlick->property("originY").toReal();
        CHECK(narrowFlick->property("contentY").toReal() > origin);
        narrowFlick->setProperty("contentY", origin);
        settle();
        // Match Qt Quick Test's event delivery: the event enters the window
        // at a real pointer position, as a spontaneous input with a timestamp.
        const auto wheelAt = [&](QPointF pos, int delta) {
            static ulong timestamp = 100000;
            QWheelEvent event(pos, narrowWindow->mapToGlobal(pos.toPoint()), {},
                              QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            event.setTimestamp(++timestamp);
            QSpontaneKeyEvent::setSpontaneous(&event);
            qApp->notify(narrowWindow, &event);
            settle();
        };
        wheelAt(QPointF(100, 40), -360);
        CHECK(narrowFlick->property("contentY").toReal() > origin);
        CHECK(narrowProvider->property("starts").toInt() == 0 && narrowProvider->property("stops").toInt() == 0);
        narrowFlick->setProperty("contentY", origin + narrowFlick->property("contentHeight").toReal() - narrowFlick->height());
        settle();
        auto* value = item(narrowPanel, QStringLiteral("watchValueText"));
        auto* rowStatus = item(narrowPanel, QStringLiteral("watchRowStatus"));
        auto* remove = item(narrowPanel, QStringLiteral("removeWatchButton"));
        auto* narrowList = item(narrowPanel, QStringLiteral("watchlistView"));
        CHECK(value && rowStatus && remove && narrowList);
        CHECK(value->property("text").toString() == QStringLiteral("18446744073709551615"));
        CHECK(rowStatus->property("text").toString() == QStringLiteral("SubmissionRefused"));
        for (auto* label : {value, rowStatus}) {
            std::printf("label %s h=%.2f paint=%.2f w=%.2f paintw=%.2f\n", label->objectName().toUtf8().constData(), label->height(), label->property("paintedHeight").toReal(), label->width(), label->property("paintedWidth").toReal());
            CHECK(label->property("paintedHeight").isValid());
            CHECK(label->property("paintedHeight").toReal() <= label->height() + 1);
            CHECK(label->property("paintedWidth").toReal() <= label->width() + 1);
        }
        for (auto* control : {value, rowStatus, remove}) {
            CHECK(rectangle(control).left() >= rectangle(narrowList).left() - 1);
            CHECK(rectangle(control).right() <= rectangle(narrowList).right() + 1);
            CHECK(rectangle(control).top() >= rectangle(narrowList).top() - 1);
            CHECK(rectangle(control).bottom() <= rectangle(narrowList).bottom() + 1);
            CHECK(rectangle(control).top() >= rectangle(narrowPanel).top() - 1);
            CHECK(rectangle(control).bottom() <= rectangle(narrowPanel).bottom() + 1);
        }
        CHECK(narrowWindow->grabWindow().save(QString::fromUtf8(argv[1]) + QStringLiteral("/narrow-watchlist.png")));
        narrowFlick->setProperty("contentY", origin);
        settle();
        auto* interval = item(narrowPanel, QStringLiteral("watchFrequency"));
        auto* clear = item(narrowPanel, QStringLiteral("clearWatchButton"));
        CHECK(interval && clear);
        interval->forceActiveFocus(Qt::TabFocusReason);
        settle();
        QTest::keyClick(narrowWindow, Qt::Key_Tab);
        settle();
        CHECK(clear->hasActiveFocus());
        CHECK(rectangle(clear).top() >= rectangle(narrowPanel).top() - 1);
        CHECK(rectangle(clear).bottom() <= rectangle(narrowPanel).bottom() + 1);
        CHECK(narrowProvider->property("starts").toInt() == 0 && narrowProvider->property("stops").toInt() == 0);
        // A large watchlist stays a single virtualized viewport. Wheel input
        // over the rows must scroll it without scheduling a read or polling.
        CHECK(QMetaObject::invokeMethod(narrowProvider, "populate", Q_ARG(QVariant, 65)));
        settle();
        CHECK(narrowProvider->property("watchCount").toInt() == 65);
        CHECK(QMetaObject::invokeMethod(narrowList, "positionViewAtEnd"));
        settle();
        auto* longStatus = itemWithText(narrowPanel, QStringLiteral("watchRowStatus"),
            QStringLiteral("SubmissionRefusedBecauseTheDeviceIsTemporarilyUnavailable"));
        CHECK(longStatus && longStatus->property("paintedHeight").toReal() > 40);
        CHECK(longStatus->property("paintedHeight").toReal() <= longStatus->height() + 1);
        const qreal beforeWheelUp = narrowFlick->property("contentY").toReal();
        wheelAt(QPointF(100, 175), 360);
        CHECK(narrowFlick->property("contentY").toReal() < beforeWheelUp);
        CHECK(narrowProvider->property("starts").toInt() == 0 &&
              narrowProvider->property("stops").toInt() == 0 &&
              narrowProvider->property("reads").toInt() == 0);
        CHECK(warnings == 0);
		std::printf("QML layout checks=%u warnings=%u\n", checks, warnings);
		return 0;
	} catch (const std::exception& error) {
		if (window)
			window->grabWindow().save(QString::fromUtf8(argv[1]) + QStringLiteral("/failure.png"));
		std::fprintf(stderr, "QML layout failed check=%u: %s (warnings=%u)\n", checks, error.what(),
		             warnings);
		return 1;
	}
}
