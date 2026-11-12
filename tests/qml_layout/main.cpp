// Layout only: real disconnected SDK Session, no transport or device requests. MIT.
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <integration/qt/ConnectionSession.hpp>
#include <QGuiApplication>
#include <QScreen>
#include <QElapsedTimer>
#include <QImage>
#include <QItemSelectionModel>
#include <QTimer>
#include <QWheelEvent>
#include <QtTest/qtestspontaneevent.h>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QStandardItemModel>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>

namespace {
unsigned checks = 0;
unsigned warnings = 0;

// Deliberately use non-Telemetry numeric role IDs: named QML roles, not a
// provider-specific layout, are the contract for reusable endpoint panels.
class SearchFixtureModel final : public QStandardItemModel {
public:
    QHash<int, QByteArray> roleNames() const override {
        return {{333, "name"}, {444, "detail"}, {487, "isEndpoint"}};
    }
    QStandardItem* append(QStandardItem* parent, const QString& name, bool endpoint) {
        auto* node = new QStandardItem;
        node->setData(name, 333);
        node->setData(QString(), 444);
        node->setData(endpoint, 487);
        if (parent) parent->appendRow(node); else appendRow(node);
        return node;
    }
};

void check(bool condition, const char* expression)
{
	++checks;
	if (!condition)
		throw std::runtime_error(expression);
}

#define CHECK(expression) check(bool(expression), #expression)

double contrastRatio(const QColor& foreground, const QColor& background)
{
    const auto luminance = [](const QColor& c) {
        const auto linear = [](double v) {
            return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
    };
    const auto a = luminance(foreground), b = luminance(background);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

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
			auto* selectionTitle = item(view, QStringLiteral("endpointSelectionTitle"));
			auto* selectionActions = item(view, QStringLiteral("endpointSelectionActions"));
			CHECK(selectionTitle && selectionActions && selectionTitle->parentItem());
			if (selectionTitle->parentItem()->width() < 560) {
				CHECK(rectangle(selectionTitle).bottom() <= rectangle(selectionActions).top());
				CHECK(selectionTitle->property("paintedWidth").toReal() <= selectionTitle->width() + 1);
			}
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
		// Real QQuickWindow, narrow panel and every action category. The fake
		// records clicks without performing transport operations.
		engine.setInitialProperties({});
		engine.load(QUrl(QStringLiteral("qrc:/EndpointStates.qml")));
		auto* stateWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
		CHECK(stateWindow);
		settle();
		auto* statePanel = item(stateWindow->contentItem(), QStringLiteral("endpointStatePanel"));
		auto* stateProvider = stateWindow->findChild<QObject*>(QStringLiteral("endpointStateProvider"));
		CHECK(statePanel && stateProvider);
		auto* title = item(statePanel, QStringLiteral("endpointSelectionTitle"));
		auto* actions = item(statePanel, QStringLiteral("endpointSelectionActions"));
		auto* edit = item(statePanel, QStringLiteral("editButton"));
		auto* cancel = item(statePanel, QStringLiteral("cancelEditButton"));
		auto* apply = item(statePanel, QStringLiteral("applyButton"));
		auto* refresh = item(statePanel, QStringLiteral("refreshButton"));
		auto* command = item(statePanel, QStringLiteral("executeButton"));
		auto* service = item(statePanel, QStringLiteral("callServiceButton"));
		auto* values = item(statePanel, QStringLiteral("valueTree"));
		CHECK(title && actions && edit && cancel && apply && refresh && command && service && values);
		// Every visible action lies inside the panel's content area (not merely
		// its outer frame) and no two overlap, at each narrow dock width. The
		// window ends at the narrowest width, so the clicks below happen there.
		const auto checkGeometry = [&] {
			for (const int width : {360, 320, 300, 280}) {
				stateWindow->setWidth(width);
				settle();
				const auto frame = rectangle(statePanel);
				const auto left = frame.left() + statePanel->property("leftPadding").toReal();
				const auto right = frame.right() - statePanel->property("rightPadding").toReal();
				CHECK(rectangle(title).bottom() <= rectangle(actions).top() + 1);
				// The title and the value tree stay inside too: a fixed minimum
				// width on the tree once pushed the whole panel past a 300 px dock.
				CHECK(rectangle(title).left() >= left - 1 && rectangle(title).right() <= right + 1);
				CHECK(rectangle(values).right() <= right + 1);
				QList<QQuickItem*> visibleButtons;
				for (auto* button : {edit, cancel, apply, refresh, command, service})
					if (button->isVisible()) visibleButtons.append(button);
				CHECK(!visibleButtons.isEmpty());
				for (int i = 0; i < visibleButtons.size(); ++i) {
					const auto bounds = rectangle(visibleButtons[i]);
					if (bounds.left() < left - 1 || bounds.right() > right + 1)
						qCritical() << "action outside the content at width" << width
						            << visibleButtons[i]->objectName() << bounds << left << right;
					CHECK(bounds.left() >= left - 1);
					CHECK(bounds.right() <= right + 1);
					for (int j = i + 1; j < visibleButtons.size(); ++j)
						CHECK(!bounds.intersects(rectangle(visibleButtons[j])));
				}
			}
		};
		CHECK(edit->isVisible() && refresh->isVisible() && !cancel->isVisible() &&
		      !apply->isVisible() && !command->isVisible() && !service->isVisible());
		checkGeometry();
		click(stateWindow, edit);
		CHECK(stateProvider->property("edits").toInt() == 1 && stateProvider->property("editing").toBool());
		CHECK(!edit->isVisible() && cancel->isVisible() && apply->isVisible() && refresh->isVisible());
		checkGeometry();
		CHECK(stateWindow->grabWindow().save(QString::fromUtf8(argv[1]) +
		                                     QStringLiteral("/endpoint-edit-280.png")));
		click(stateWindow, apply);
		CHECK(stateProvider->property("applies").toInt() == 1);
		click(stateWindow, cancel);
		CHECK(stateProvider->property("cancels").toInt() == 1 && !stateProvider->property("editing").toBool());
		CHECK(stateProvider->setProperty("commandSelected", true));
		settle();
		CHECK(command->isVisible() && !service->isVisible() && !edit->isVisible() && !refresh->isVisible());
		checkGeometry();
		click(stateWindow, command);
		CHECK(stateProvider->property("commands").toInt() == 1);
		CHECK(stateProvider->setProperty("commandSelected", false));
		CHECK(stateProvider->setProperty("serviceSelected", true));
		settle();
		CHECK(service->isVisible() && !command->isVisible() && !edit->isVisible() && !refresh->isVisible());
		checkGeometry();
		click(stateWindow, service);
		CHECK(stateProvider->property("services").toInt() == 1);
		delete stateWindow;
		QCoreApplication::processEvents();
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
        // Searching only inspects endpoint metadata; it must never select or
        // send a request until the user explicitly activates a source index.
        auto* searchInput = item(separateTreePanel, QStringLiteral("endpointSearch"));
        auto* searchStatus = item(separateTreePanel, QStringLiteral("endpointSearchStatus"));
        CHECK(searchInput && searchStatus);
        const auto beforeSearch = shared->property("selections").toInt();
        CHECK(searchInput->setProperty("text", QStringLiteral("voltage")));
        QTest::qWait(250);
        CHECK(searchStatus->property("text").toString() == QStringLiteral("2 matching endpoints"));
        CHECK(shared->property("selections").toInt() == beforeSearch);
        auto* searchRow = item(separateTreePanel, QStringLiteral("endpointSearchResult"));
        CHECK(searchRow != nullptr);
        CHECK(shared->setProperty("refuseSelection", true));
        click(separateWindow, searchRow);
        CHECK(shared->property("selections").toInt() == beforeSearch);
        CHECK(shared->setProperty("refuseSelection", false));
        click(separateWindow, searchRow);
        CHECK(shared->property("selections").toInt() == beforeSearch + 1);
        CHECK(searchInput->setProperty("text", QStringLiteral("not-present")));
        QTest::qWait(250);
        CHECK(searchStatus->property("text").toString() == QStringLiteral("No matching endpoints"));
        CHECK(searchInput->setProperty("text", QString()));
        settle();
        CHECK(!searchStatus->isVisible());
        CHECK(warnings == 0);
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
        // The expand box and the panel's `expanded` are one state: a host that
        // opens the list sees the box ticked, and unticking it closes the list.
        {
            auto* toggle = item(narrowPanel, QStringLiteral("watchlistExpanded"));
            CHECK(toggle && !narrowPanel->property("expanded").toBool() && !toggle->property("checked").toBool());
            CHECK(narrowPanel->setProperty("expanded", true));
            settle();
            CHECK(toggle->property("checked").toBool());
            CHECK(toggle->setProperty("checked", false));
            settle();
            CHECK(!narrowPanel->property("expanded").toBool());
            CHECK(narrowPanel->setProperty("expanded", true));
            settle();
            CHECK(toggle->property("checked").toBool());
            CHECK(narrowPanel->setProperty("expanded", false));
            settle();
            CHECK(!toggle->property("checked").toBool());
        }
        const auto controlsVisible = [&] {
            for (const auto* name : {"watchlistExpanded", "addWatchButton"}) {
                auto* control = item(narrowPanel, QString::fromLatin1(name));
                CHECK(control && rectangle(control).top() >= rectangle(narrowFlick).top() - 1);
                CHECK(rectangle(control).bottom() <= rectangle(narrowFlick).bottom() + 1);
            }
        };
        controlsVisible();
        for (const auto* name : {"watchlistExpanded", "addWatchButton", "refreshWatchButton", "startWatchButton"}) {
            auto* control = item(narrowPanel, QString::fromLatin1(name));
            CHECK(control && rectangle(control).left() >= rectangle(narrowPanel).left());
            CHECK(rectangle(control).right() <= rectangle(narrowPanel).right());
        }
        const qreal collapsedHeight = narrowPanel->property("implicitHeight").toReal();
        click(narrowWindow, item(narrowPanel, QStringLiteral("watchlistExpanded")));
        CHECK(narrowPanel->property("implicitHeight").toReal() >= collapsedHeight + 120);
        controlsVisible();
        click(narrowWindow, item(narrowPanel, QStringLiteral("watchlistExpanded")));
        CHECK(narrowPanel->property("implicitHeight").toReal() <= collapsedHeight + 1);
        controlsVisible();
        click(narrowWindow, item(narrowPanel, QStringLiteral("addWatchButton")));
        CHECK(narrowProvider->property("watchCount").toInt() == 1);
        QTest::qWait(300);
        auto* addControl = item(narrowPanel, QStringLiteral("addWatchButton"));
        auto* toggleControl = item(narrowPanel, QStringLiteral("watchlistExpanded"));
        auto* active = narrowWindow->activeFocusItem();
        std::printf("after add focus=%s current=%d origin=%.1f content=%.1f toggleY=%.1f viewY=%.1f\n",
                    active ? active->objectName().toUtf8().constData() : "none", narrowFlick->property("currentIndex").toInt(),
                    narrowFlick->property("originY").toReal(), narrowFlick->property("contentY").toReal(),
                    rectangle(toggleControl).top(), rectangle(narrowFlick).top());
        controlsVisible();
        CHECK(!narrowWindow->activeFocusItem() ||
              narrowWindow->activeFocusItem()->objectName() != QStringLiteral("watchlistRow"));
        CHECK(narrowFlick->property("contentHeight").toReal() > narrowFlick->height());
        // While expanded, adding from a deliberately scrolled position must
        // retain that position; the recovery above only applies at the top.
        const qreal manuallyScrolled = narrowFlick->property("originY").toReal() + 8;
        narrowFlick->setProperty("contentY", manuallyScrolled);
        settle();
        click(narrowWindow, addControl);
        CHECK(narrowProvider->property("watchCount").toInt() == 2);
        CHECK(qAbs(narrowFlick->property("contentY").toReal() - manuallyScrolled) <= 1);
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
        QTest::mouseClick(narrowWindow, Qt::LeftButton, Qt::NoModifier,
                          QPointF(bar.center().x(), bar.top() + 5).toPoint());
        settle();
        CHECK(narrowPanel->property("scrollAnchor").toString() == QStringLiteral("top"));
        narrowWindow->resize(300, 240);
        settle();
        controlsVisible();
        narrowWindow->resize(320, 240);
        settle();
        narrowFlick->setProperty("contentY", origin);
        settle();
        // Match Qt Quick Test's event delivery: the event enters the window
        // at a real pointer position, as a spontaneous input with a timestamp.
        const auto wheelAt = [&](QQuickWindow* target, QPointF pos, int delta) {
            static ulong timestamp = 100000;
            QWheelEvent event(pos, target->mapToGlobal(pos.toPoint()), {},
                              QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            event.setTimestamp(++timestamp);
            QSpontaneKeyEvent::setSpontaneous(&event);
            qApp->notify(target, &event);
            settle();
        };
        wheelAt(narrowWindow, QPointF(100, 40), -360);
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
        CHECK(narrowPanel->setProperty("scrollAnchor", QStringLiteral("top")));
        interval->forceActiveFocus(Qt::TabFocusReason);
        settle();
        QTest::keyClick(narrowWindow, Qt::Key_Tab);
        settle();
        CHECK(clear->hasActiveFocus());
        CHECK(rectangle(clear).top() >= rectangle(narrowPanel).top() - 1);
        CHECK(rectangle(clear).bottom() <= rectangle(narrowPanel).bottom() + 1);
        CHECK(narrowPanel->property("scrollAnchor").toString() != QStringLiteral("top"));
        narrowWindow->resize(290, 240);
        settle();
        CHECK(clear->hasActiveFocus());
        CHECK(rectangle(clear).top() >= rectangle(narrowFlick).top() - 1);
        CHECK(rectangle(clear).bottom() <= rectangle(narrowFlick).bottom() + 1);
        narrowWindow->resize(320, 240);
        settle();
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
        wheelAt(narrowWindow, QPointF(100, 175), 360);
        CHECK(narrowFlick->property("contentY").toReal() < beforeWheelUp);
        CHECK(narrowProvider->property("starts").toInt() == 0 &&
              narrowProvider->property("stops").toInt() == 0 &&
              narrowProvider->property("reads").toInt() == 0);
        // A wide dock shows one 28 px table row per Field under column titles:
        // the value right-aligned, a good reading as a dot, a problem as its
        // status word cut to one line, and a small remove button.
        {
            narrowWindow->resize(720, 360);
            settle();
            CHECK(narrowPanel->property("tableRows").toBool());
            CHECK(QMetaObject::invokeMethod(narrowList, "positionViewAtBeginning"));
            settle();
            std::function<QQuickItem*(QQuickItem*, int)> rowAt = [&](QQuickItem* root, int index) -> QQuickItem* {
                if (root->objectName() == QStringLiteral("watchlistRow") && root->property("index").toInt() == index)
                    return root;
                for (auto* child : root->childItems())
                    if (auto* found = rowAt(child, index))
                        return found;
                return nullptr;
            };
            auto* model = narrowProvider->property("watchlist").value<QObject*>();
            CHECK(model && QMetaObject::invokeMethod(model, "setProperty", Q_ARG(int, 0),
                                                     Q_ARG(QString, QStringLiteral("status")),
                                                     Q_ARG(QVariant, QStringLiteral("Ok"))));
            settle();
            auto* columns = item(narrowPanel, QStringLiteral("watchColumns"));
            auto* valueTitle = item(narrowPanel, QStringLiteral("watchColumnValue"));
            auto* statusTitle = item(narrowPanel, QStringLiteral("watchColumnStatus"));
            auto* okRow = rowAt(narrowList, 0);
            auto* failedRow = rowAt(narrowList, 1);
            CHECK(columns && columns->isVisible() && valueTitle && statusTitle && okRow && failedRow);
            // A one-line row is Type's row: the application font in pixels x 1.65.
            const qreal bodyPixels = QGuiApplication::font().pointSizeF() *
                                     QGuiApplication::primaryScreen()->logicalDotsPerInch() / 72.0;
            const qreal typeRow = std::round(bodyPixels * 1.65);
            for (auto* row : {okRow, failedRow}) {
                auto* rowValue = item(row, QStringLiteral("watchValueText"));
                auto* rowRemove = item(row, QStringLiteral("removeWatchButton"));
                CHECK(rowValue && rowRemove);
                CHECK(qAbs(row->height() - typeRow) < 0.5);
                CHECK(rowValue->property("paintedHeight").toReal() <= row->height());
                CHECK(rowValue->property("text").toString() == QStringLiteral("18446744073709551615"));
                CHECK(rowValue->property("horizontalAlignment").toInt() == Qt::AlignRight);
                CHECK(qAbs(rectangle(rowValue).right() - rectangle(valueTitle).right()) <= 1);
                CHECK(rowRemove->property("text").toString() == QStringLiteral("×"));
                CHECK(rectangle(rowRemove).right() <= rectangle(narrowList).right() + 1);
                // The overlay scrollbar never covers the remove button.
                CHECK(narrowBar->isVisible() && !rectangle(rowRemove).intersects(rectangle(narrowBar)));
                CHECK(rectangle(rowRemove).top() >= rectangle(row).top() - 1);
                CHECK(rectangle(rowRemove).bottom() <= rectangle(row).bottom() + 1);
            }
            auto* okStatus = item(okRow, QStringLiteral("watchRowStatus"));
            auto* failedStatus = item(failedRow, QStringLiteral("watchRowStatus"));
            CHECK(okStatus && !okStatus->isVisible());
            CHECK(failedStatus && failedStatus->isVisible());
            // A writable row is written in place: double-click the value, type,
            // Enter writes; Esc or clicking elsewhere cancels; a read-only row
            // offers nothing, and neither does a provider without permission.
            {
                auto* okValue = item(okRow, QStringLiteral("watchValueText"));
                auto* editor = item(okRow, QStringLiteral("watchValueEditor"));
                CHECK(okValue && editor && !editor->isVisible());
                const auto doubleClick = [&](QQuickItem* target) {
                    QTest::mouseDClick(narrowWindow, Qt::LeftButton, Qt::NoModifier,
                                       rectangle(target).center().toPoint());
                    settle();
                };
                doubleClick(okValue);
                CHECK(editor->isVisible() && editor->hasActiveFocus() && !okValue->isVisible());
                CHECK(editor->property("text").toString() == QStringLiteral("18446744073709551615"));
                QTest::keyClick(narrowWindow, Qt::Key_A, Qt::ControlModifier);
                QTest::keyClick(narrowWindow, '4');
                QTest::keyClick(narrowWindow, '2');
                QTest::keyClick(narrowWindow, Qt::Key_Return);
                settle();
                CHECK(!editor->isVisible() && okValue->isVisible());
                CHECK(narrowProvider->property("writes").toStringList() == QStringList{QStringLiteral("0:42")});
                auto* outcome = item(narrowPanel, QStringLiteral("watchWriteOutcome"));
                CHECK(outcome && outcome->property("text").toString() == QStringLiteral("Writing measure.Field0"));
                doubleClick(okValue);
                CHECK(editor->isVisible());
                QTest::keyClick(narrowWindow, Qt::Key_Escape);
                settle();
                CHECK(!editor->isVisible());
                doubleClick(okValue);
                CHECK(editor->isVisible());
                item(narrowPanel, QStringLiteral("watchFrequency"))->forceActiveFocus();
                settle();
                CHECK(!editor->isVisible());
                CHECK(narrowProvider->property("writes").toStringList().size() == 1);
                auto* readOnlyValue = item(failedRow, QStringLiteral("watchValueText"));
                doubleClick(readOnlyValue);
                CHECK(!item(failedRow, QStringLiteral("watchValueEditor"))->isVisible());
                CHECK(narrowProvider->setProperty("canWriteWatch", false));
                settle();
                doubleClick(okValue);
                CHECK(!editor->isVisible());
                CHECK(narrowProvider->setProperty("canWriteWatch", true));
                CHECK(narrowProvider->setProperty("watchWriteOutcome", QString()));
                settle();
            }
            auto* okDot = item(okRow, QStringLiteral("watchOkDot"));
            auto* failedDot = item(failedRow, QStringLiteral("watchOkDot"));
            CHECK(okDot && okDot->isVisible() && failedDot && !failedDot->isVisible());
            CHECK(failedStatus->property("text").toString() == QStringLiteral("SubmissionRefused"));
            CHECK(qAbs(rectangle(failedStatus).left() - rectangle(statusTitle).left()) <= 1);
            CHECK(narrowWindow->grabWindow().save(QString::fromUtf8(argv[1]) + QStringLiteral("/wide-watchlist.png")));
            CHECK(QMetaObject::invokeMethod(narrowList, "positionViewAtEnd"));
            settle();
            auto* lastRow = rowAt(narrowList, 64);
            auto* wideLongStatus = lastRow ? item(lastRow, QStringLiteral("watchRowStatus")) : nullptr;
            CHECK(wideLongStatus && wideLongStatus->property("truncated").toBool());
            CHECK(wideLongStatus->property("paintedHeight").toReal() < 24);
            // A host that docks the list for good hides the expand box.
            auto* expandBox = item(narrowPanel, QStringLiteral("watchlistExpanded"));
            CHECK(expandBox && expandBox->isVisible());
            CHECK(narrowPanel->setProperty("collapsible", false));
            settle();
            CHECK(!expandBox->isVisible() && narrowPanel->property("expanded").toBool());
            // A list that cannot fold shows its rows even with `expanded` false.
            CHECK(narrowPanel->setProperty("expanded", false));
            settle();
            CHECK(columns->isVisible() && narrowList->property("count").toInt() == 65);
            CHECK(narrowPanel->setProperty("collapsible", true));
            settle();
            CHECK(!columns->isVisible() && narrowList->property("count").toInt() == 0);
            // Add opens the list through `expanded`, so a host that closes it
            // later also unticks the box.
            auto* wideAdd = item(narrowPanel, QStringLiteral("addWatchButton"));
            CHECK(wideAdd && QMetaObject::invokeMethod(wideAdd, "clicked"));
            settle();
            CHECK(narrowPanel->property("expanded").toBool() && expandBox->property("checked").toBool());
            CHECK(narrowPanel->setProperty("expanded", false));
            settle();
            CHECK(!expandBox->property("checked").toBool());
            CHECK(narrowProvider->property("starts").toInt() == 0 && narrowProvider->property("reads").toInt() == 0);
        }
        // Keyboard activation keeps the focus indicator and both top actions.
        engine.load(QUrl(QStringLiteral("qrc:/Narrow.qml")));
        auto* keyboardWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
        CHECK(keyboardWindow && keyboardWindow != narrowWindow);
        settle();
        auto* keyboardPanel = item(keyboardWindow->contentItem(), QStringLiteral("narrowWatch"));
        CHECK(keyboardPanel);
        auto* keyboardAdd = item(keyboardPanel, QStringLiteral("addWatchButton"));
        auto* keyboardToggle = item(keyboardPanel, QStringLiteral("watchlistExpanded"));
        auto* keyboardViewport = item(keyboardPanel, QStringLiteral("watchlistView"));
        CHECK(keyboardAdd && keyboardToggle && keyboardViewport);
        keyboardAdd->forceActiveFocus(Qt::TabFocusReason);
        std::printf("keyboard before origin=%.1f content=%.1f atBeginning=%d focus=%d\n",
                    keyboardViewport->property("originY").toReal(), keyboardViewport->property("contentY").toReal(),
                    keyboardViewport->property("atYBeginning").toBool(), keyboardAdd->hasActiveFocus());
        QTest::keyClick(keyboardWindow, Qt::Key_Space);
        settle();
        QTest::qWait(300);
        CHECK(keyboardAdd->hasActiveFocus() && keyboardAdd->property("visualFocus").toBool());
        std::printf("keyboard origin=%.1f content=%.1f toggleY=%.1f viewY=%.1f\n",
                    keyboardViewport->property("originY").toReal(),
                    keyboardViewport->property("contentY").toReal(),
                    rectangle(keyboardToggle).top(), rectangle(keyboardViewport).top());
        for (auto* control : {keyboardAdd, keyboardToggle}) {
            CHECK(rectangle(control).top() >= rectangle(keyboardViewport).top() - 1);
            CHECK(rectangle(control).bottom() <= rectangle(keyboardViewport).bottom() + 1);
        }
        wheelAt(keyboardWindow, QPointF(100, 170), -120);
        CHECK(keyboardViewport->property("contentY").toReal() > keyboardViewport->property("originY").toReal());
        CHECK(keyboardPanel->property("scrollAnchor").toString() != QStringLiteral("top"));
        keyboardWindow->resize(290, 240);
        settle();
        CHECK(keyboardViewport->property("contentY").toReal() > keyboardViewport->property("originY").toReal() + 1);
        // A host that deliberately positions the view has the same right to
        // keep that position across a later width/layout change.
        CHECK(keyboardPanel->setProperty("scrollAnchor", QStringLiteral("top")));
        keyboardViewport->setProperty("contentY", keyboardViewport->property("originY").toReal() + 7);
        settle();
        CHECK(keyboardPanel->property("scrollAnchor").toString() != QStringLiteral("top"));
        keyboardWindow->resize(320, 240);
        settle();
        std::printf("programmatic after resize origin=%.1f content=%.1f following=%d\n",
                    keyboardViewport->property("originY").toReal(), keyboardViewport->property("contentY").toReal(),
                    keyboardPanel->property("scrollAnchor").toString() == QStringLiteral("top"));
        CHECK(keyboardViewport->property("contentY").toReal() > keyboardViewport->property("originY").toReal() + 1);
        // A subsequent user focus change before layout settles must not be
        // undone by the one-shot header adjustment after Add.
        engine.load(QUrl(QStringLiteral("qrc:/Narrow.qml")));
        auto* raceWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
        CHECK(raceWindow && raceWindow != narrowWindow);
        settle();
        auto* racePanel = item(raceWindow->contentItem(), QStringLiteral("narrowWatch"));
        CHECK(racePanel);
        auto* raceAdd = item(racePanel, QStringLiteral("addWatchButton"));
        CHECK(raceAdd);
        QTest::mouseClick(raceWindow, Qt::LeftButton, Qt::NoModifier,
                          rectangle(raceAdd).center().toPoint());
        auto* raceMode = item(racePanel, QStringLiteral("watchMode"));
        CHECK(raceMode && raceMode->isVisible());
        raceMode->forceActiveFocus(Qt::TabFocusReason);
        settle();
        QTest::qWait(300);
        CHECK(raceMode->hasActiveFocus());
        engine.load(QUrl(QStringLiteral("qrc:/Narrow.qml")));
        auto* wheelRaceWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
        CHECK(wheelRaceWindow && wheelRaceWindow != raceWindow);
        settle();
        auto* wheelRacePanel = item(wheelRaceWindow->contentItem(), QStringLiteral("narrowWatch"));
        CHECK(wheelRacePanel);
        auto* wheelRaceAdd = item(wheelRacePanel, QStringLiteral("addWatchButton"));
        auto* wheelRaceView = item(wheelRacePanel, QStringLiteral("watchlistView"));
        CHECK(wheelRaceAdd && wheelRaceView);
        QTest::mouseClick(wheelRaceWindow, Qt::LeftButton, Qt::NoModifier,
                          rectangle(wheelRaceAdd).center().toPoint());
        settle();
        wheelAt(wheelRaceWindow, QPointF(100, 170), -120);
        QTest::qWait(300);
        CHECK(wheelRacePanel->property("scrollAnchor").toString() == QStringLiteral("free"));
        CHECK(wheelRaceView->property("contentY").toReal() > wheelRaceView->property("originY").toReal());
        // Reopening and resizing a dock may relayout an expanded header well
        // after it was hidden. Check actual scene bounds, not just originY.
        engine.load(QUrl(QStringLiteral("qrc:/Narrow.qml")));
        auto* lifecycleWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
        CHECK(lifecycleWindow && lifecycleWindow != raceWindow);
        settle();
        auto* lifecyclePanel = item(lifecycleWindow->contentItem(), QStringLiteral("narrowWatch"));
        CHECK(lifecyclePanel);
        auto* lifecycleToggle = item(lifecyclePanel, QStringLiteral("watchlistExpanded"));
        auto* lifecycleAdd = item(lifecyclePanel, QStringLiteral("addWatchButton"));
        auto* lifecycleView = item(lifecyclePanel, QStringLiteral("watchlistView"));
        CHECK(lifecycleToggle && lifecycleAdd && lifecycleView);
        lifecycleWindow->setVisible(false);
        QTest::qWait(200);
        lifecycleWindow->setVisible(true);
        CHECK(lifecycleToggle->setProperty("checked", true));
        lifecycleWindow->resize(300, 240);
        settle();
        QTest::qWait(300);
        for (auto* control : {lifecycleToggle, lifecycleAdd}) {
            CHECK(rectangle(control).top() >= rectangle(lifecycleView).top() - 1);
            CHECK(rectangle(control).bottom() <= rectangle(lifecycleView).bottom() + 1);
            CHECK(rectangle(control).top() >= 0);
            CHECK(rectangle(control).bottom() <= lifecycleWindow->height());
        }
        CHECK(lifecycleWindow->grabWindow().save(QString::fromUtf8(argv[1]) + QStringLiteral("/narrow-reopened.png")));
        engine.load(QUrl(QStringLiteral("qrc:/Narrow.qml")));
        auto* destroyWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
        CHECK(destroyWindow && destroyWindow != lifecycleWindow);
        settle();
        auto* destroyPanel = item(destroyWindow->contentItem(), QStringLiteral("narrowWatch"));
        CHECK(destroyPanel);
        auto* destroyAdd = item(destroyPanel, QStringLiteral("addWatchButton"));
        CHECK(destroyAdd);
        QTest::mouseClick(destroyWindow, Qt::LeftButton, Qt::NoModifier,
                          rectangle(destroyAdd).center().toPoint());
        delete destroyWindow;
        QTest::qWait(300);
        CHECK(warnings == 0);
        SearchFixtureModel searchFixture;
        auto* alpha = searchFixture.append(nullptr, QStringLiteral("Alpha"), false);
        auto* alphaVoltage = searchFixture.append(alpha, QStringLiteral("Voltage"), true);
        // QML's required bool role accepts integer 0/1 from third-party models.
        alpha->setData(0, 487);
        alphaVoltage->setData(1, 487);
        auto* beta = searchFixture.append(nullptr, QStringLiteral("Beta"), false);
        searchFixture.append(beta, QStringLiteral("Voltage"), true);
        engine.rootContext()->setContextProperty(QStringLiteral("searchFixtureModel"), &searchFixture);
        engine.load(QUrl(QStringLiteral("qrc:/Search.qml")));
        auto* searchWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
        CHECK(searchWindow && searchWindow != lifecycleWindow);
        settle();
        auto* searchPanel = item(searchWindow->contentItem(), QStringLiteral("searchTree"));
        auto* query = item(searchPanel, QStringLiteral("endpointSearch"));
        auto* status = item(searchPanel, QStringLiteral("endpointSearchStatus"));
        auto* results = item(searchPanel, QStringLiteral("endpointSearchResults"));
        auto* fake = searchPanel->property("provider").value<QObject*>();
        CHECK(searchPanel && query && status && results && fake);
        CHECK(query->setProperty("text", QStringLiteral("ALPHA / voltage")));
        QTest::qWait(250);
        CHECK(status->property("text").toString() == QStringLiteral("1 matching endpoints"));
        CHECK(results->property("count").toInt() == 1 && fake->property("calls").toInt() == 0);
        auto* resultRow = item(searchPanel, QStringLiteral("endpointSearchResult"));
        CHECK(resultRow && resultRow->property("text").toString() == QStringLiteral("Alpha / Voltage"));
        auto* resultBackground = resultRow->property("background").value<QObject*>();
        auto* resultLabel = resultRow->property("contentItem").value<QObject*>();
        auto* treeBackground = item(searchPanel, QStringLiteral("endpointTreePanelBackground"));
        // No heading repeats the dock's title and no standing note takes rows;
        // the watch status line is reserved at the bottom, blank when idle.
        auto* watchStatus = item(searchPanel, QStringLiteral("endpointWatchSelectionStatus"));
        CHECK(resultBackground && resultLabel && treeBackground && watchStatus);
        CHECK(!item(searchPanel, QStringLiteral("endpointTreeTitle")) &&
              !item(searchPanel, QStringLiteral("endpointTreeHelp")) && watchStatus->isVisible());
        const auto treeColor = [&] { return treeBackground->property("color").value<QColor>(); };
        const auto textColor = [&] { return resultLabel->property("color").value<QColor>(); };
        CHECK(!resultRow->property("highlighted").toBool());
        CHECK(resultBackground->property("color").value<QColor>().alpha() == 0);
        CHECK(resultLabel->property("color").value<QColor>() == QColor(QStringLiteral("#e8edf5")));
        CHECK(contrastRatio(textColor(), treeColor()) >= 4.5);
        CHECK(contrastRatio(watchStatus->property("color").value<QColor>(), treeColor()) >= 4.5);
        CHECK(contrastRatio(status->property("color").value<QColor>(), treeColor()) >= 4.5);
        resultRow->forceActiveFocus(Qt::TabFocusReason);
        settle();
        QQmlExpression focusWidth(qmlContext(resultRow), resultRow, QStringLiteral("background.border.width"));
        QQmlExpression focusColor(qmlContext(resultRow), resultRow, QStringLiteral("background.border.color"));
        CHECK(focusWidth.evaluate().toInt() == 2 && !focusWidth.hasError());
        CHECK(contrastRatio(focusColor.evaluate().value<QColor>(), treeColor()) >= 3.0 && !focusColor.hasError());
        click(searchWindow, resultRow);
        CHECK(fake->property("calls").toInt() == 1);
        CHECK(fake->property("selectedEndpointIndex").value<QModelIndex>() == alphaVoltage->index());
        CHECK(resultRow->property("highlighted").toBool());
        CHECK(resultBackground->property("color").value<QColor>() == QColor(QStringLiteral("#215a68")));
        CHECK(resultLabel->property("color").value<QColor>() == QColor(QStringLiteral("#ffffff")));
        CHECK(contrastRatio(textColor(), resultBackground->property("color").value<QColor>()) >= 4.5);
        // A host-supplied light palette must replace every selected/unselected
        // foreground and background, not inherit hard-coded dark colors.
        CHECK(searchWindow->setProperty("testHighlight", QColor(QStringLiteral("#c3e0ff"))));
        CHECK(searchWindow->setProperty("testHighlightedText", QColor(QStringLiteral("#121212"))));
        CHECK(searchWindow->setProperty("testButtonText", QColor(QStringLiteral("#242424"))));
        CHECK(searchWindow->setProperty("testBase", QColor(QStringLiteral("#ffffff"))));
        CHECK(searchWindow->setProperty("testText", QColor(QStringLiteral("#121212"))));
        settle();
        CHECK(treeColor() == QColor(QStringLiteral("#ffffff")));
        CHECK(contrastRatio(watchStatus->property("color").value<QColor>(), treeColor()) >= 4.5);
        CHECK(contrastRatio(status->property("color").value<QColor>(), treeColor()) >= 4.5);
        CHECK(resultBackground->property("color").value<QColor>() == QColor(QStringLiteral("#c3e0ff")));
        CHECK(resultLabel->property("color").value<QColor>() == QColor(QStringLiteral("#121212")));
        CHECK(contrastRatio(textColor(), resultBackground->property("color").value<QColor>()) >= 4.5);
        resultRow->forceActiveFocus(Qt::TabFocusReason);
        settle();
        CHECK(focusWidth.evaluate().toInt() == 2 && !focusWidth.hasError());
        CHECK(focusColor.evaluate().value<QColor>() == QColor(QStringLiteral("#121212")) && !focusColor.hasError());
        CHECK(contrastRatio(focusColor.evaluate().value<QColor>(), resultBackground->property("color").value<QColor>()) >= 3.0);
        // A light canvas can still use a dark selected-row highlight. The
        // keyboard cursor must use highlightedText on the selected tree row.
        CHECK(query->setProperty("text", QString()));
        CHECK(searchWindow->setProperty("testHighlight", QColor(QStringLiteral("#163860"))));
        CHECK(searchWindow->setProperty("testHighlightedText", QColor(QStringLiteral("#ffffff"))));
        settle();
        auto* sourceTree = item(searchPanel, QStringLiteral("endpointTree"));
        auto* selectedTreeRow = rowItem(searchPanel, 1);
        CHECK(sourceTree && selectedTreeRow && selectedTreeRow->property("highlighted").toBool());
        auto* selectedFocus = item(selectedTreeRow, QStringLiteral("endpointFocusIndicator"));
        auto* selectionModel = qobject_cast<QItemSelectionModel*>(sourceTree->property("selectionModel").value<QObject*>());
        CHECK(selectedFocus && selectionModel);
        selectionModel->setCurrentIndex(alphaVoltage->index(), QItemSelectionModel::NoUpdate);
        sourceTree->forceActiveFocus(Qt::TabFocusReason);
        settle();
        CHECK(selectedFocus->isVisible());
        QQmlExpression selectedTreeFocus(qmlContext(selectedFocus), selectedFocus, QStringLiteral("border.color"));
        CHECK(contrastRatio(selectedTreeFocus.evaluate().value<QColor>(), QColor(QStringLiteral("#163860"))) >= 3.0 && !selectedTreeFocus.hasError());
        // Repeat the selected focus contrast with the default dark canvas.
        CHECK(searchWindow->setProperty("testBase", QColor(QStringLiteral("#17202b"))));
        CHECK(searchWindow->setProperty("testText", QColor(QStringLiteral("#e8edf5"))));
        CHECK(searchWindow->setProperty("testHighlight", QColor(QStringLiteral("#215a68"))));
        settle();
        CHECK(selectedFocus->isVisible());
        CHECK(contrastRatio(selectedTreeFocus.evaluate().value<QColor>(), QColor(QStringLiteral("#215a68"))) >= 3.0 && !selectedTreeFocus.hasError());
        CHECK(searchWindow->setProperty("testBase", QColor(QStringLiteral("#ffffff"))));
        CHECK(searchWindow->setProperty("testText", QColor(QStringLiteral("#121212"))));
        CHECK(searchWindow->setProperty("testHighlight", QColor(QStringLiteral("#c3e0ff"))));
        CHECK(searchWindow->setProperty("testHighlightedText", QColor(QStringLiteral("#121212"))));
        CHECK(query->setProperty("text", QStringLiteral("alpha.voltage")));
        QTest::qWait(250);
        CHECK(results->property("count").toInt() == 1);
        searchFixture.append(alpha, QStringLiteral("<b>Current</b>"), true);
        CHECK(query->setProperty("text", QStringLiteral("<b>current")));
        QTest::qWait(250);
        CHECK(results->property("count").toInt() == 1);
        resultRow = item(searchPanel, QStringLiteral("endpointSearchResult"));
        CHECK(resultRow && resultRow->property("text").toString() == QStringLiteral("Alpha / <b>Current</b>"));
        CHECK(resultRow->property("contentItem").value<QObject*>()->property("textFormat").toInt() == int(Qt::PlainText));
        CHECK(!resultRow->property("highlighted").toBool());
        CHECK(resultRow->property("background").value<QObject*>()->property("color").value<QColor>().alpha() == 0);
        CHECK(resultRow->property("contentItem").value<QObject*>()->property("color").value<QColor>() == QColor(QStringLiteral("#242424")));
        CHECK(contrastRatio(resultRow->property("contentItem").value<QObject*>()->property("color").value<QColor>(), treeColor()) >= 4.5);
        resultRow->forceActiveFocus(Qt::TabFocusReason);
        settle();
        QQmlExpression lightFocusColor(qmlContext(resultRow), resultRow, QStringLiteral("background.border.color"));
        CHECK(contrastRatio(lightFocusColor.evaluate().value<QColor>(), treeColor()) >= 3.0 && !lightFocusColor.hasError());
        CHECK(query->setProperty("text", QStringLiteral("alpha.voltage")));
        QTest::qWait(250);
        resultRow = item(searchPanel, QStringLiteral("endpointSearchResult"));
        CHECK(resultRow);
        resultRow->forceActiveFocus();
        QTest::keyClick(searchWindow, Qt::Key_Return);
        settle();
        CHECK(fake->property("calls").toInt() == 2);
        // Reset invalidates indexes synchronously, before any deferred scan.
        searchFixture.clear();
        CHECK(results->property("count").toInt() == 0);
        alpha = searchFixture.append(nullptr, QStringLiteral("Alpha"), false);
        alphaVoltage = searchFixture.append(alpha, QStringLiteral("Voltage"), true);
        QTest::qWait(250);
        CHECK(results->property("count").toInt() == 1 && fake->property("calls").toInt() == 2);
        resultRow = item(searchPanel, QStringLiteral("endpointSearchResult"));
        CHECK(resultRow);
        click(searchWindow, resultRow);
        CHECK(fake->property("calls").toInt() == 3);
        CHECK(fake->property("selectedEndpointIndex").value<QModelIndex>() == alphaVoltage->index());
        CHECK(query->setProperty("text", QStringLiteral("item")));
        auto* large = searchFixture.append(nullptr, QStringLiteral("Large"), false);
        for (int i = 0; i < 499; ++i)
            searchFixture.append(large, QStringLiteral("item %1").arg(i), true);
        QTest::qWait(1000);
        CHECK(!searchPanel->property("searchRunning").toBool());
        CHECK(results->property("count").toInt() == 499);
        CHECK(status->property("text").toString() == QStringLiteral("499 matching endpoints"));
        searchFixture.append(large, QStringLiteral("item 499"), true);
        QTest::qWait(500);
        CHECK(results->property("count").toInt() == 500);
        CHECK(status->property("text").toString() == QStringLiteral("Search limited; refine search"));
        searchFixture.append(large, QStringLiteral("item 500"), true);
        QTest::qWait(500);
        CHECK(results->property("count").toInt() == 500);
        CHECK(status->property("text").toString() == QStringLiteral("Search limited; refine search"));
        CHECK(fake->property("calls").toInt() == 3);
        delete searchWindow;
        QCoreApplication::processEvents();
        searchFixture.clear();
        large = searchFixture.append(nullptr, QStringLiteral("Large"), false);
        for (int i = 0; i < 10000; ++i)
            searchFixture.append(large, QStringLiteral("item %1").arg(i), true);
        engine.load(QUrl(QStringLiteral("qrc:/Search.qml")));
        searchWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
        CHECK(searchWindow);
        settle();
        searchPanel = item(searchWindow->contentItem(), QStringLiteral("searchTree"));
        query = item(searchPanel, QStringLiteral("endpointSearch"));
        status = item(searchPanel, QStringLiteral("endpointSearchStatus"));
        CHECK(searchPanel && query && status);
        int timerTicks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(1);
        QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++timerTicks; });
        heartbeat.start();
        QElapsedTimer elapsed;
        elapsed.start();
        CHECK(query->setProperty("text", QStringLiteral("never-match")));
        while (searchPanel->property("searchRunning").toBool() && elapsed.elapsed() < 15000)
            QTest::qWait(10);
        heartbeat.stop();
        std::printf("Search 10000 nodes elapsed=%lldms heartbeat=%d\n", elapsed.elapsed(), timerTicks);
        CHECK(!searchPanel->property("searchRunning").toBool());
        CHECK(timerTicks > 0);
        CHECK(status->property("text").toString() == QStringLiteral("No matching endpoints"));
        delete searchWindow;
        QCoreApplication::processEvents();
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
