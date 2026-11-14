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
        // The content wrapper also lays out metadata and preview text. Check
        // the actual visible endpoint name, whose theme contrast is the contract.
        auto* rowName = item(row, QStringLiteral("endpointRowName"));
        CHECK(rowName && rowName->isVisible() &&
              rowName->property("color").value<QColor>() == QColor(QStringLiteral("#e8edf5")));
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
        click(separateWindow, item(separateWatchPanel, QStringLiteral("watchMoreButton")));
        click(separateWindow, item(separateWindow->contentItem(), QStringLiteral("addWatchItem")));
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
        // The Watchlist in a small dock: one toolbar row that stays put, the
        // rows under it, and what gives way as the dock narrows.
        engine.setInitialProperties({});
        engine.load(QUrl(QStringLiteral("qrc:/Narrow.qml")));
        auto* narrowWindow = engine.rootObjects().size() > 2
            ? qobject_cast<QQuickWindow*>(engine.rootObjects().last()) : nullptr;
        CHECK(narrowWindow != nullptr);
        settle();
        auto* narrowPanel = item(narrowWindow->contentItem(), QStringLiteral("narrowWatch"));
        auto* narrowProvider = narrowPanel ? narrowPanel->property("provider").value<QObject*>() : nullptr;
        auto* narrowList = narrowPanel ? item(narrowPanel, QStringLiteral("watchlistView")) : nullptr;
        auto* toolbar = narrowPanel ? item(narrowPanel, QStringLiteral("watchToolbar")) : nullptr;
        CHECK(narrowPanel && narrowProvider && narrowList && toolbar);
        const auto named = [&](const char* name) {
            auto* found = item(narrowPanel, QString::fromLatin1(name));
            CHECK(found != nullptr);
            return found;
        };
        // Menu items live in the menu's popup; they are found as the menu's children.
        const auto menuItem = [&](const char* name) {
            auto* found = narrowPanel->findChild<QQuickItem*>(QString::fromLatin1(name));
            CHECK(found != nullptr);
            return found;
        };
        auto* menu = narrowPanel->findChild<QObject*>(QStringLiteral("watchMoreMenu"));
        CHECK(menu != nullptr);
        // The expand box and the panel's `expanded` are one state: a host that
        // opens the list sees the box ticked, and unticking it closes the list.
        {
            auto* toggle = named("watchlistExpanded");
            CHECK(!narrowPanel->property("expanded").toBool() && !toggle->property("checked").toBool());
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
        // A control of the toolbar: shown, inside the panel, inside the row.
        const auto inBar = [&](const char* name) {
            auto* control = named(name);
            CHECK(control->isVisible());
            const QRectF box = rectangle(control), bar = rectangle(toolbar), whole = rectangle(narrowPanel);
            CHECK(box.left() >= whole.left() - 1 && box.right() <= whole.right() + 1);
            CHECK(box.top() >= bar.top() - 1 && box.bottom() <= bar.bottom() + 1);
            return control;
        };
        // From wide to narrow "Every" gives way first, then Read once (into
        // ⋯); the rest stays one row. The status is a line under it, below the row.
        {
            bool everySeen[2] = {}, readOnceSeen[2] = {};
            for (int width = 900; width >= 320; width -= 10) {
                narrowWindow->resize(width, 240);
                settle();
                for (const auto* name : {"watchlistExpanded", "startWatchButton", "watchInterval", "watchMoreButton"})
                    inBar(name);
                const bool every = named("watchEvery")->isVisible();
                const bool readOnce = named("refreshWatchButton")->isVisible();
                CHECK(!every || readOnce);
                if (every) inBar("watchEvery");
                if (readOnce) inBar("refreshWatchButton");
                everySeen[every] = readOnceSeen[readOnce] = true;
                auto* status = named("watchStatusLabel");
                CHECK(status->isVisible() && rectangle(status).top() >= rectangle(toolbar).bottom() - 1);
                CHECK(rectangle(status).right() <= rectangle(narrowPanel).right() + 1);
            }
            CHECK(everySeen[0] && everySeen[1] && readOnceSeen[0] && readOnceSeen[1]);
            narrowWindow->resize(320, 240);
            settle();
            CHECK(named("watchStatusLabel")->property("text").toString() == QStringLiteral("Ready"));
        }
        const qreal collapsedHeight = narrowPanel->property("implicitHeight").toReal();
        click(narrowWindow, named("watchlistExpanded"));
        CHECK(narrowPanel->property("implicitHeight").toReal() >= collapsedHeight + 120);
        click(narrowWindow, named("watchlistExpanded"));
        CHECK(narrowPanel->property("implicitHeight").toReal() <= collapsedHeight + 1);
        // ⋯ opens under its button with what the row has no room for: here
        // Read once, then Watch the selected Field, the read modes and Clear.
        click(narrowWindow, named("watchMoreButton"));
        CHECK(menu->property("visible").toBool());
        CHECK(menuItem("refreshWatchItem")->isVisible() && !menuItem("refreshWatchItem")->isEnabled());
        CHECK(menuItem("addWatchItem")->isVisible() && menuItem("addWatchItem")->isEnabled());
        CHECK(menuItem("watchModeReadMany")->property("checked").toBool());
        CHECK(!menuItem("watchModeFieldRead")->property("checked").toBool());
        CHECK(!menuItem("clearWatchItem")->isEnabled());
        // The menu stays inside the window, however small the dock.
        CHECK(rectangle(menuItem("addWatchItem")).top() >= 0 &&
              rectangle(menuItem("addWatchItem")).right() <= narrowWindow->width() + 1);
        click(narrowWindow, menuItem("addWatchItem"));
        CHECK(!menu->property("visible").toBool());
        CHECK(narrowProvider->property("watchCount").toInt() == 1);
        // Adding opens the list through `expanded`, so the box shows it.
        CHECK(narrowPanel->property("expanded").toBool() && named("watchlistExpanded")->property("checked").toBool());
        CHECK(QMetaObject::invokeMethod(menuItem("refreshWatchItem"), "click"));
        settle();
        CHECK(narrowProvider->property("reads").toInt() == 1);
        // The read modes: one is always chosen; choosing it again keeps it.
        CHECK(QMetaObject::invokeMethod(menuItem("watchModeFieldRead"), "click"));
        settle();
        CHECK(narrowProvider->property("watchIndividual").toBool());
        CHECK(menuItem("watchModeFieldRead")->property("checked").toBool() &&
              !menuItem("watchModeReadMany")->property("checked").toBool());
        CHECK(QMetaObject::invokeMethod(menuItem("watchModeFieldRead"), "click"));
        settle();
        CHECK(narrowProvider->property("watchIndividual").toBool() &&
              menuItem("watchModeFieldRead")->property("checked").toBool());
        CHECK(QMetaObject::invokeMethod(menuItem("watchModeReadMany"), "click"));
        settle();
        CHECK(!narrowProvider->property("watchIndividual").toBool() &&
              menuItem("watchModeReadMany")->property("checked").toBool());
        // Without ReadMany Fields are read one by one and there is no choice.
        CHECK(narrowProvider->setProperty("watchReadManySupported", false));
        settle();
        click(narrowWindow, named("watchMoreButton"));
        CHECK(menu->property("visible").toBool());
        CHECK(!menuItem("watchModeReadMany")->isVisible());
        CHECK(menuItem("watchModeFieldRead")->property("checked").toBool() &&
              !menuItem("watchModeFieldRead")->isEnabled());
        QTest::keyClick(narrowWindow, Qt::Key_Escape);
        settle();
        CHECK(!menu->property("visible").toBool());
        CHECK(narrowProvider->setProperty("watchReadManySupported", true));
        settle();
        // The interval: the usual steps, then Other…; Continuous only where 0
        // is accepted; a stored value outside the steps is offered as it is.
        {
            auto* interval = named("watchInterval");
            auto* input = named("watchIntervalInput");
            const auto count = [&] { return interval->property("count").toInt(); };
            const auto index = [&] { return interval->property("currentIndex").toInt(); };
            const auto ms = [&] { return narrowProvider->property("watchIntervalMs").toInt(); };
            QString last;
            CHECK(count() == 9 && index() == 0);
            CHECK(QMetaObject::invokeMethod(interval, "textAt", Q_RETURN_ARG(QString, last), Q_ARG(int, 8)) &&
                  last == QStringLiteral("Other…"));
            CHECK(narrowProvider->setProperty("watchIntervalMinimumMs", 0));
            settle();
            CHECK(count() == 10 && index() == 1);
            CHECK(narrowProvider->setProperty("watchIntervalMinimumMs", 1));
            CHECK(narrowProvider->setProperty("watchIntervalMs", 1234));
            settle();
            CHECK(count() == 10 && index() == 4);
            CHECK(interval->property("currentText").toString().endsWith(QStringLiteral(" s")));
            CHECK(QMetaObject::invokeMethod(interval, "activated", Q_ARG(int, 3)));
            settle();
            CHECK(ms() == 1000 && count() == 9 && index() == 3);
            // Other… puts a field for milliseconds in the list's place, the
            // interval in it selected; Enter sets what is typed.
            const auto other = [&] {
                CHECK(QMetaObject::invokeMethod(interval, "activated", Q_ARG(int, count() - 1)));
                settle();
                CHECK(input->isVisible() && input->hasActiveFocus() && !interval->isVisible());
                inBar("watchIntervalInput");
            };
            // The first time through the list's own popup, as a person picks it.
            {
                click(narrowWindow, interval);
                auto* popup = interval->property("popup").value<QObject*>();
                CHECK(popup && popup->property("visible").toBool());
                auto* choices = qobject_cast<QQuickItem*>(popup->property("contentItem").value<QObject*>());
                CHECK(choices && QMetaObject::invokeMethod(choices, "positionViewAtEnd"));
                settle();
                QQuickItem* otherChoice = nullptr;
                CHECK(QMetaObject::invokeMethod(choices, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, otherChoice),
                                                Q_ARG(int, count() - 1)) && otherChoice);
                click(narrowWindow, otherChoice);
                CHECK(input->isVisible() && input->hasActiveFocus() && !interval->isVisible());
            }
            QTest::keyClick(narrowWindow, '3');
            QTest::keyClick(narrowWindow, '7');
            QTest::keyClick(narrowWindow, Qt::Key_Return);
            settle();
            CHECK(ms() == 37 && !input->isVisible() && interval->isVisible());
            CHECK(index() == 0 && interval->property("currentText").toString() == QStringLiteral("37 ms"));
            // Below the provider's minimum Enter takes nothing; Esc goes back
            // to the list, which shows the interval as it was, not Other….
            other();
            QTest::keyClick(narrowWindow, '0');
            QTest::keyClick(narrowWindow, Qt::Key_Return);
            settle();
            CHECK(input->isVisible() && ms() == 37);
            QTest::keyClick(narrowWindow, Qt::Key_Escape);
            settle();
            CHECK(!input->isVisible() && interval->isVisible() && ms() == 37);
            CHECK(index() == 0 && interval->property("currentText").toString() == QStringLiteral("37 ms"));
            // Left by a click elsewhere, the field lets go of its focus: the
            // panel, a focus scope, never gives the keys back to it hidden.
            other();
            narrowList->forceActiveFocus(Qt::MouseFocusReason);
            settle();
            CHECK(!input->isVisible() && interval->isVisible() && ms() == 37);
            narrowPanel->forceActiveFocus(Qt::OtherFocusReason);
            settle();
            CHECK(narrowWindow->activeFocusItem() != input);
            CHECK(narrowProvider->setProperty("watchIntervalMs", 100));
            settle();
        }
        // While polling the list, the mode and the interval keep still.
        click(narrowWindow, named("startWatchButton"));
        CHECK(narrowProvider->property("watchActive").toBool());
        CHECK(named("startWatchButton")->property("text").toString() == QStringLiteral("Stop"));
        CHECK(!named("watchInterval")->isEnabled() && !menuItem("clearWatchItem")->isEnabled() &&
              !menuItem("addWatchItem")->isEnabled() && !menuItem("watchModeReadMany")->isEnabled());
        click(narrowWindow, named("startWatchButton"));
        CHECK(!narrowProvider->property("watchActive").toBool());
        CHECK(narrowProvider->property("starts").toInt() == 1 && narrowProvider->property("stops").toInt() == 1);
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
        // 65 Fields: the rows scroll under a toolbar that stays where it is,
        // and the wheel never reads or polls.
        CHECK(QMetaObject::invokeMethod(narrowProvider, "populate", Q_ARG(QVariant, 65)));
        settle();
        CHECK(narrowProvider->property("watchCount").toInt() == 65);
        auto* narrowBar = named("watchlistScrollBar");
        CHECK(narrowBar->isVisible() && rectangle(narrowBar).right() <= rectangle(narrowPanel).right() + 1);
        const QRectF barBefore = rectangle(toolbar);
        const qreal topBefore = narrowList->property("contentY").toReal();
        wheelAt(narrowWindow, rectangle(narrowList).center(), -360);
        CHECK(narrowList->property("contentY").toReal() > topBefore);
        CHECK(rectangle(toolbar) == barBefore);
        inBar("startWatchButton");
        CHECK(narrowProvider->property("starts").toInt() == 1 && narrowProvider->property("reads").toInt() == 1);
        // Narrow rows take two lines: the name, then the value and the status,
        // which wrap rather than run out of the list; × is always there.
        std::function<QQuickItem*(QQuickItem*, int)> rowAt = [&](QQuickItem* root, int index) -> QQuickItem* {
            if (root->objectName() == QStringLiteral("watchlistRow") && root->property("index").toInt() == index)
                return root;
            for (auto* child : root->childItems())
                if (auto* found = rowAt(child, index))
                    return found;
            return nullptr;
        };
        {
            CHECK(!narrowPanel->property("tableRows").toBool());
            CHECK(QMetaObject::invokeMethod(narrowList, "positionViewAtEnd"));
            settle();
            // Two lines, each a row high: the name and ×, then the value and
            // the status in a word, the provider's own word kept for the tooltip.
            const qreal lineHeight = std::round(std::max<qreal>(10.0, QGuiApplication::font().pointSizeF()) *
                                                QGuiApplication::primaryScreen()->logicalDotsPerInch() / 72.0 * 1.65);
            auto* lastRow = rowAt(narrowList, 64);
            CHECK(lastRow != nullptr && qAbs(lastRow->height() - 2 * lineHeight) < 0.5);
            CHECK(lastRow->property("status").toString() ==
                  QStringLiteral("SubmissionRefusedBecauseTheDeviceIsTemporarilyUnavailable"));
            auto* longStatus = item(lastRow, QStringLiteral("watchRowStatus"));
            CHECK(longStatus && longStatus->property("text").toString() == QStringLiteral("Refused"));
            CHECK(longStatus->property("paintedHeight").toReal() <= lineHeight);
            auto* lastRemove = item(lastRow, QStringLiteral("removeWatchButton"));
            auto* lastValue = item(lastRow, QStringLiteral("watchValueText"));
            CHECK(lastRemove && lastValue && rectangle(lastRemove).bottom() <= rectangle(lastValue).top() + 1);
            for (const auto* name : {"watchValueText", "watchRowStatus", "removeWatchButton"}) {
                auto* control = item(lastRow, QString::fromLatin1(name));
                CHECK(control && control->isVisible());
                CHECK(control->property("paintedWidth").isValid() == false ||
                      control->property("paintedWidth").toReal() <= control->width() + 1);
                CHECK(rectangle(control).left() >= rectangle(narrowList).left() - 1);
                CHECK(rectangle(control).right() <= rectangle(narrowList).right() + 1);
                CHECK(!rectangle(control).intersects(rectangle(narrowBar)));
            }
            CHECK(narrowWindow->grabWindow().save(QString::fromUtf8(argv[1]) + QStringLiteral("/narrow-watchlist.png")));
        }
        // The keyboard: Tab goes from ⋯ to the rows, the arrows move the
        // current row, Delete removes it and the list keeps the focus.
        {
            CHECK(QMetaObject::invokeMethod(narrowList, "positionViewAtBeginning"));
            settle();
            named("watchMoreButton")->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(narrowWindow, Qt::Key_Tab);
            settle();
            CHECK(narrowList->hasActiveFocus() && narrowList->property("currentIndex").toInt() == 0);
            QTest::keyClick(narrowWindow, Qt::Key_Down);
            settle();
            CHECK(narrowList->property("currentIndex").toInt() == 1);
            QTest::keyClick(narrowWindow, Qt::Key_Delete);
            settle();
            CHECK(narrowProvider->property("watchCount").toInt() == 64 && narrowList->hasActiveFocus());
            CHECK(QMetaObject::invokeMethod(narrowProvider, "populate", Q_ARG(QVariant, 65)));
            settle();
        }
        // A wide dock shows one row per Field under column titles: the value
        // right-aligned, a good reading as a dot, a problem as its status
        // word cut to one line, and × on the row under the pointer or the
        // current one.
        {
            narrowWindow->resize(720, 360);
            settle();
            CHECK(narrowPanel->property("tableRows").toBool());
            CHECK(QMetaObject::invokeMethod(narrowList, "positionViewAtBeginning"));
            settle();
            auto* model = narrowProvider->property("watchlist").value<QObject*>();
            CHECK(model && QMetaObject::invokeMethod(model, "setProperty", Q_ARG(int, 0),
                                                     Q_ARG(QString, QStringLiteral("status")),
                                                     Q_ARG(QVariant, QStringLiteral("Ok"))));
            settle();
            auto* columns = named("watchColumns");
            auto* valueTitle = named("watchColumnValue");
            auto* statusTitle = named("watchColumnStatus");
            auto* okRow = rowAt(narrowList, 0);
            auto* failedRow = rowAt(narrowList, 1);
            CHECK(columns->isVisible() && okRow && failedRow);
            // A one-line row is Type's row: the body (the application font, at
            // least 10 pt) in pixels x 1.65.
            const qreal bodyPixels = std::max<qreal>(10.0, QGuiApplication::font().pointSizeF()) *
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
                // The overlay scrollbar never covers ×.
                CHECK(narrowBar->isVisible() && !rectangle(rowRemove).intersects(rectangle(narrowBar)));
                CHECK(rectangle(rowRemove).top() >= rectangle(row).top() - 1);
                CHECK(rectangle(rowRemove).bottom() <= rectangle(row).bottom() + 1);
            }
            // × on the current row only, while nothing is under the pointer.
            QTest::mouseMove(narrowWindow, rectangle(toolbar).center().toPoint());
            narrowList->setProperty("currentIndex", 0);
            narrowList->forceActiveFocus(Qt::TabFocusReason);
            settle();
            CHECK(item(okRow, QStringLiteral("removeWatchButton"))->isVisible());
            CHECK(!item(failedRow, QStringLiteral("removeWatchButton"))->isVisible());
            auto* okStatus = item(okRow, QStringLiteral("watchRowStatus"));
            auto* failedStatus = item(failedRow, QStringLiteral("watchRowStatus"));
            CHECK(okStatus && !okStatus->isVisible());
            CHECK(failedStatus && failedStatus->isVisible());
            // A writable row is written in place: double-click the value or
            // press F2, type, Enter writes; Esc or clicking elsewhere cancels;
            // a read-only row offers nothing, and neither does a provider
            // without permission.
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
                CHECK(!editor->isVisible() && okValue->isVisible() && narrowList->hasActiveFocus());
                CHECK(narrowProvider->property("writes").toStringList() == QStringList{QStringLiteral("0:42")});
                auto* outcome = named("watchWriteOutcome");
                CHECK(outcome->property("text").toString() == QStringLiteral("Writing measure.Field0"));
                QTest::keyClick(narrowWindow, Qt::Key_F2);
                settle();
                CHECK(editor->isVisible() && editor->hasActiveFocus());
                QTest::keyClick(narrowWindow, Qt::Key_Escape);
                settle();
                CHECK(!editor->isVisible() && narrowList->hasActiveFocus());
                doubleClick(okValue);
                CHECK(editor->isVisible());
                named("watchInterval")->forceActiveFocus();
                settle();
                CHECK(!editor->isVisible());
                click(narrowWindow, failedRow);
                {
                    // The list or its current row has the keys, never the hidden editor.
                    auto* focused = narrowWindow->activeFocusItem();
                    CHECK(focused && focused != editor && narrowList->hasActiveFocus() &&
                          (focused == narrowList || focused->objectName() == QStringLiteral("watchlistRow")));
                }
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
            CHECK(failedStatus->property("text").toString() == QStringLiteral("Refused"));
            CHECK(qAbs(rectangle(failedStatus).left() - rectangle(statusTitle).left()) <= 1);
            CHECK(narrowWindow->grabWindow().save(QString::fromUtf8(argv[1]) + QStringLiteral("/wide-watchlist.png")));
            click(narrowWindow, named("watchMoreButton"));
            CHECK(menu->property("visible").toBool() && !menuItem("refreshWatchItem")->isVisible());
            CHECK(narrowWindow->grabWindow().save(QString::fromUtf8(argv[1]) + QStringLiteral("/watch-menu.png")));
            QTest::keyClick(narrowWindow, Qt::Key_Escape);
            settle();
            CHECK(QMetaObject::invokeMethod(narrowList, "positionViewAtEnd"));
            settle();
            auto* lastRow = rowAt(narrowList, 64);
            auto* wideLongStatus = lastRow ? item(lastRow, QStringLiteral("watchRowStatus")) : nullptr;
            CHECK(wideLongStatus && wideLongStatus->property("text").toString() == QStringLiteral("Refused") &&
                  !wideLongStatus->property("truncated").toBool());
            CHECK(wideLongStatus->property("paintedHeight").toReal() < typeRow);
            // A host that docks the list for good hides the expand box.
            auto* expandBox = named("watchlistExpanded");
            CHECK(expandBox->isVisible());
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
            // Watching opens the list through `expanded`, so a host that
            // closes it later also unticks the box.
            CHECK(QMetaObject::invokeMethod(menuItem("addWatchItem"), "click"));
            settle();
            CHECK(narrowPanel->property("expanded").toBool() && expandBox->property("checked").toBool());
            CHECK(narrowPanel->setProperty("expanded", false));
            settle();
            CHECK(!expandBox->property("checked").toBool());
            CHECK(narrowProvider->property("starts").toInt() == 1 && narrowProvider->property("reads").toInt() == 1);
            // Clear asks first; Esc keeps the list, Remove all empties it.
            CHECK(narrowPanel->setProperty("expanded", true));
            settle();
            auto* clearDialog = narrowPanel->findChild<QObject*>(QStringLiteral("clearWatchDialog"));
            CHECK(clearDialog != nullptr);
            CHECK(QMetaObject::invokeMethod(menuItem("clearWatchItem"), "click"));
            settle();
            CHECK(clearDialog->property("visible").toBool());
            auto* clearCancel = item(narrowWindow->contentItem(), QStringLiteral("clearWatchCancel"));
            CHECK(clearCancel && clearCancel->hasActiveFocus());
            CHECK(clearDialog->property("title").toString().contains(QStringLiteral("66")));
            CHECK(narrowWindow->grabWindow().save(QString::fromUtf8(argv[1]) + QStringLiteral("/watch-clear.png")));
            QTest::keyClick(narrowWindow, Qt::Key_Escape);
            settle();
            CHECK(!clearDialog->property("visible").toBool() && narrowProvider->property("watchCount").toInt() == 66);
            CHECK(QMetaObject::invokeMethod(menuItem("clearWatchItem"), "click"));
            settle();
            click(narrowWindow, item(narrowWindow->contentItem(), QStringLiteral("clearWatchConfirm")));
            CHECK(!clearDialog->property("visible").toBool() && narrowProvider->property("watchCount").toInt() == 0);
            auto* empty = named("watchEmpty");
            CHECK(empty->isVisible() && empty->property("text").toString().contains(QStringLiteral("Watch the selected Field")));
        }
        // Hidden and shown again, then narrowed, a dock keeps its toolbar in view.
        {
            engine.load(QUrl(QStringLiteral("qrc:/Narrow.qml")));
            auto* lifecycleWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
            CHECK(lifecycleWindow && lifecycleWindow != narrowWindow);
            settle();
            auto* lifecyclePanel = item(lifecycleWindow->contentItem(), QStringLiteral("narrowWatch"));
            CHECK(lifecyclePanel);
            auto* lifecycleToggle = item(lifecyclePanel, QStringLiteral("watchlistExpanded"));
            auto* lifecycleStart = item(lifecyclePanel, QStringLiteral("startWatchButton"));
            CHECK(lifecycleToggle && lifecycleStart);
            lifecycleWindow->setVisible(false);
            QTest::qWait(200);
            lifecycleWindow->setVisible(true);
            CHECK(lifecycleToggle->setProperty("checked", true));
            lifecycleWindow->resize(320, 240);
            settle();
            QTest::qWait(300);
            for (auto* control : {lifecycleToggle, lifecycleStart}) {
                CHECK(rectangle(control).top() >= 0);
                CHECK(rectangle(control).bottom() <= lifecycleWindow->height());
                CHECK(rectangle(control).right() <= lifecycleWindow->width());
            }
            CHECK(lifecycleWindow->grabWindow().save(QString::fromUtf8(argv[1]) + QStringLiteral("/narrow-reopened.png")));
            // A window closed with its menu open leaves nothing behind.
            click(lifecycleWindow, item(lifecyclePanel, QStringLiteral("watchMoreButton")));
            delete lifecycleWindow;
            QTest::qWait(300);
        }
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
        CHECK(searchWindow && searchWindow != narrowWindow);
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
