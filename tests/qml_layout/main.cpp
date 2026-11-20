// Layout only: real disconnected SDK Session, no transport or device requests. MIT.
#include <Telemetry/UX/QmlTypes.hpp>
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
	// Table delegates retain a logical width beyond a narrow dock. Activate
	// the visible Name cell; the centre of the whole row may hit a neighbour.
	if (target->objectName() == QStringLiteral("endpointRow") && target->property("tableMode").toBool()) {
		target = item(target, QStringLiteral("endpointRowName"));
		CHECK(target);
	}
	auto visible = rectangle(target);
	for (auto* parent = target->parentItem(); parent; parent = parent->parentItem())
		if (parent->clip()) visible = visible.intersected(rectangle(parent));
	visible = visible.intersected(QRectF(QPointF{}, window->size()));
	if (!target->isEnabled() || visible.isEmpty())
		std::fprintf(stderr, "Unreachable native click: %s visible=%.1f,%.1f %.1fx%.1f\n",
		    qPrintable(target->objectName()), visible.x(), visible.y(), visible.width(), visible.height());
	CHECK(target->isEnabled() && !visible.isEmpty());
	QTest::mouseClick(
	    window, Qt::LeftButton, Qt::NoModifier,
	    visible.center().toPoint());
	settle();
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	// The Device tree shows its rows through the module's C++ types.
	telemetry::ux::registerQmlTypes();
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
			auto* valueTree = item(view, QStringLiteral("valueTree"));
			auto* values = item(view, QStringLiteral("emptyEndpointLabel"));
			// Details' one scroll fills its area; the empty sentence is one
			// line at its top (Details layout §6).
			auto* details = item(view, QStringLiteral("endpointBodyScroll"));
			auto* watchToggle = item(view, QStringLiteral("watchlistExpanded"));
			auto* batchToggle = item(view, QStringLiteral("batchToggle"));
			CHECK(tree && valueTree && values && details && watchToggle && batchToggle);
			CHECK(rectangle(values).top() - rectangle(details).top() <= 12 && values->height() < 60);
			// This real Session is disconnected and has no selection. Its primary
			// Details viewport is the empty state, not an invisible value table.
			CHECK(provider->selection().isEmpty() && !valueTree->isVisible() && values->isVisible());
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
			CHECK(details->height() >= 200);
			CHECK(window->grabWindow().save(
			    QString::fromUtf8(argv[1]) +
			    QStringLiteral("/%1x%2-collapsed.png").arg(size.width()).arg(size.height())));
			watchToggle->setProperty("checked", true);
			batchToggle->setProperty("checked", true);
			settle();
			CHECK(watchPanel->property("implicitHeight").toReal() >= collapsedWatchHeight + 120);
			std::printf("Layout %dx%d expanded endpoint=%.1f values=%.1f\n", size.width(),
			            size.height(), tree->height(), details->height());
			CHECK(tree->height() >= 200);
			CHECK(details->height() >= 200);
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
			// The right pane is Details over the selected item (BA-4a).
			auto* pane = item(view, QStringLiteral("batchItemPane"));
			CHECK(footer && batch && pane);
			CHECK(footer->height() >= 34);
			CHECK(rectangle(footer).bottom() <= rectangle(view).bottom() + 1);
			CHECK(rectangle(batch).bottom() <= rectangle(footer).top());
			CHECK(pane->height() >= 100);
			CHECK(rectangle(pane).top() >= rectangle(batch).top());
			CHECK(rectangle(pane).bottom() <= rectangle(batch).bottom());
			auto* empty = item(pane, QStringLiteral("emptyEndpointLabel"));
			CHECK(empty && empty->property("text").toString() == QStringLiteral("Choose an item to see its values here."));
			CHECK(rectangle(empty).top() >= rectangle(pane).top() && rectangle(empty).bottom() <= rectangle(pane).bottom() + 1);
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
		auto* cancel = item(statePanel, QStringLiteral("cancelEditButton"));
		auto* apply = item(statePanel, QStringLiteral("applyButton"));
		auto* refresh = item(statePanel, QStringLiteral("refreshButton"));
		auto* command = item(statePanel, QStringLiteral("executeButton"));
		auto* service = item(statePanel, QStringLiteral("callServiceButton"));
		auto* values = item(statePanel, QStringLiteral("valueTree"));
		CHECK(title && actions && cancel && apply && refresh && command && service && values);
		// There is no Edit button: the cells are the editor (BA-4).
		CHECK(!item(statePanel, QStringLiteral("editButton")));
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
				// The title and the actions share the row while the title keeps
				// six characters (Details layout §4); the actions go under it
				// only below that. Never over each other.
				const bool oneRow = std::abs(rectangle(title).center().y() - rectangle(actions).center().y()) < 2;
				CHECK(oneRow ? rectangle(title).right() <= rectangle(actions).left() + 1
				             : rectangle(title).bottom() <= rectangle(actions).top() + 1);
				// The title and the value tree stay inside too: a fixed minimum
				// width on the tree once pushed the whole panel past a 300 px dock.
				CHECK(rectangle(title).left() >= left - 1 && rectangle(title).right() <= right + 1);
				CHECK(rectangle(values).right() <= right + 1);
				QList<QQuickItem*> visibleButtons;
				for (auto* button : {cancel, apply, refresh, command, service})
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
		CHECK(refresh->isVisible() && !cancel->isVisible() &&
		      !apply->isVisible() && !command->isVisible() && !service->isVisible());
		checkGeometry();
		// A draft exists from the first change: Cancel and Apply come, Read goes.
		CHECK(stateProvider->setProperty("editing", true));
		settle();
		CHECK(stateProvider->property("edits").toInt() == 0);
		CHECK(cancel->isVisible() && apply->isVisible() && !refresh->isVisible());
		checkGeometry();
		CHECK(stateWindow->grabWindow().save(QString::fromUtf8(argv[1]) +
		                                     QStringLiteral("/endpoint-edit-280.png")));
		click(stateWindow, apply);
		CHECK(stateProvider->property("applies").toInt() == 1);
		click(stateWindow, cancel);
		CHECK(stateProvider->property("cancels").toInt() == 1 && !stateProvider->property("editing").toBool());
		CHECK(stateProvider->setProperty("commandSelected", true));
		settle();
		CHECK(command->isVisible() && !service->isVisible() && !refresh->isVisible());
		checkGeometry();
		click(stateWindow, command);
		CHECK(stateProvider->property("commands").toInt() == 1);
		CHECK(stateProvider->setProperty("commandSelected", false));
		CHECK(stateProvider->setProperty("serviceSelected", true));
		settle();
		CHECK(service->isVisible() && !command->isVisible() && !refresh->isVisible());
		checkGeometry();
		click(stateWindow, service);
		CHECK(stateProvider->property("services").toInt() == 1);
		// The family's rows (BA-4·0b): one row tall, striped, Name, Value and
		// Type at one x, nothing sideways, Type gone before Name is cut, and the
		// header's line 2 and status line under the path.
		{
			auto* readValues = stateWindow->findChild<QObject*>(QStringLiteral("readValues"));
			CHECK(readValues && stateProvider->setProperty("serviceSelected", false) &&
			      stateProvider->setProperty("commandSelected", false) &&
			      stateProvider->setProperty("editing", false) &&
			      stateProvider->setProperty("values", QVariant::fromValue(readValues)));
			for (const int width : {480, 360, 280}) {
				stateWindow->setWidth(width);
				settle();
				auto* tree = item(statePanel, QStringLiteral("valueTree"));
				CHECK(tree && tree->property("rows").toInt() == 4);
				CHECK(tree->property("contentWidth").toReal() <= tree->width() + 0.75);
				QList<QQuickItem*> rows;
				for (int row = 0; row < 4; ++row) {
					QQuickItem* at = nullptr;
					CHECK(QMetaObject::invokeMethod(tree, "itemAtCell", Q_RETURN_ARG(QQuickItem*, at),
					                                Q_ARG(QPoint, QPoint(0, row))));
					rows.append(at);
					CHECK(rows.last());
				}
				const auto rowHeight = rows.first()->height();
				CHECK(rowHeight >= 20 && rowHeight <= 24);
				const auto stripe = [](QQuickItem* row) {
					auto* background = qvariant_cast<QQuickItem*>(row->property("background"));
					return background ? background->property("color").value<QColor>() : QColor();
				};
				for (auto* row : rows) {
					CHECK(std::abs(row->height() - rowHeight) < 0.5);
					auto* name = item(row, QStringLiteral("detailCellName"));
					auto* value = item(row, QStringLiteral("valueCell"));
					auto* type = item(row, QStringLiteral("detailCellType"));
					CHECK(name && value && type);
					// Value and Type at one x on every row; Type hides before
					// Name is narrower than 96 px at the default body.
					const auto valueX = value->mapToScene(QPointF()).x();
					CHECK(std::abs(valueX - item(rows.first(), QStringLiteral("valueCell"))->mapToScene(QPointF()).x()) < 0.5);
					CHECK(!type->isVisible() || name->width() >= 96 - 1);
					CHECK(name->mapToScene(QPointF(name->width(), 0)).x() <= valueX + 0.5);
					CHECK(!type->isVisible() || value->mapToScene(QPointF(value->width(), 0)).x() <=
					                                type->mapToScene(QPointF()).x() + 0.5);
				}
				CHECK(stripe(rows[1]) == QColor(QStringLiteral("#151d27")) && stripe(rows[3]) == stripe(rows[1]));
				CHECK(stripe(rows[0]).alpha() == 0 || stripe(rows[0]) != stripe(rows[1]));
				auto* line2 = item(statePanel, QStringLiteral("endpointSelectionInfo"));
				auto* status = item(statePanel, QStringLiteral("endpointStatusLine"));
				CHECK(line2 && line2->property("text").toString() == QStringLiteral("Field"));
				CHECK(status && status->property("text").toString() == QStringLiteral("Read"));
				CHECK(item(statePanel, QStringLiteral("endpointProgress")));
				CHECK(stateWindow->grabWindow().save(QString::fromUtf8(argv[1]) +
				                                     QStringLiteral("/details-family-%1.png").arg(width)));
			}
			CHECK(stateProvider->setProperty("editing", true));
			settle();
			CHECK(item(statePanel, QStringLiteral("endpointStatusLine"))->property("text").toString() ==
			      QStringLiteral("Edited, not applied"));
		}
		// A Service's two request values over its response in a narrow, short
		// dock (the Batch item's pane): the Request takes its rows' whole
		// height and the Response starts below the last one.
		{
			auto* requestValues = stateWindow->findChild<QObject*>(QStringLiteral("requestValues"));
			auto* responseValues = stateWindow->findChild<QObject*>(QStringLiteral("responseValues"));
			CHECK(requestValues && responseValues);
			CHECK(stateProvider->setProperty("editing", false) && stateProvider->setProperty("commandSelected", false) &&
			      stateProvider->setProperty("serviceSelected", true) &&
			      stateProvider->setProperty("values", QVariant::fromValue(requestValues)) &&
			      stateProvider->setProperty("responses", QVariant::fromValue(responseValues)));
			for (const int height : {640, 420, 340}) {
				stateWindow->setWidth(300);
				stateWindow->setHeight(height);
				settle();
				auto* tree = item(statePanel, QStringLiteral("valueTree"));
				auto* heading = item(statePanel, QStringLiteral("responseTableHeader"));
				CHECK(tree && heading && heading->isVisible() && tree->property("rows").toInt() == 2);
				QQuickItem* last = nullptr;
				CHECK(QMetaObject::invokeMethod(tree, "itemAtCell", Q_RETURN_ARG(QQuickItem*, last), Q_ARG(QPoint, QPoint(0, 1))));
				CHECK(last && last->isVisible());
				const auto lastBottom = last->mapToScene(QPointF(0, last->height())).y();
				CHECK(tree->mapToScene(QPointF(0, tree->height())).y() >= lastBottom - 0.5);
				CHECK(heading->mapToScene(QPointF()).y() >= lastBottom - 0.5);
				CHECK(stateWindow->grabWindow().save(QString::fromUtf8(argv[1]) +
				                                     QStringLiteral("/service-request-narrow-%1.png").arg(height)));
			}
			// The pane of the Batch's item scrolls: in a dock too short for both
			// tables nothing is clipped or overlaps, and the Response's last
			// row can be scrolled into view.
			CHECK(statePanel->setProperty("scrolls", true));
			stateWindow->setWidth(300);
			// The one stack from the top is compact: 200 px holds the header
			// and the Request, not the Response.
			stateWindow->setHeight(200);
			settle();
			auto* body = item(statePanel, QStringLiteral("endpointBodyScroll"));
			CHECK(body && body->property("contentHeight").toReal() > body->height() + 1 && body->property("interactive").toBool());
			auto* requestTree = item(statePanel, QStringLiteral("valueTree"));
			auto* responseTree = item(statePanel, QStringLiteral("responseTree"));
			auto* responseHeading = item(statePanel, QStringLiteral("responseTableHeader"));
			CHECK(requestTree && responseTree && responseHeading && responseTree->isVisible());
			const auto bottomOf = [](QQuickItem* it) { return it->mapToScene(QPointF(0, it->height())).y(); };
			const auto topOf = [](QQuickItem* it) { return it->mapToScene(QPointF()).y(); };
			// The tables do not overlap one another, whatever the scroll.
			CHECK(bottomOf(requestTree) <= topOf(responseHeading) + 0.5);
			QQuickItem* lastResponse = nullptr;
			CHECK(QMetaObject::invokeMethod(responseTree, "itemAtCell", Q_RETURN_ARG(QQuickItem*, lastResponse), Q_ARG(QPoint, QPoint(0, 1))));
			CHECK(lastResponse && responseTree->property("contentHeight").toReal() <= responseTree->height() + 1);
			body->setProperty("contentY", 0);
			settle();
			const auto panelBottom = rectangle(statePanel).bottom();
			CHECK(bottomOf(lastResponse) > panelBottom);
			body->setProperty("contentY", body->property("contentHeight").toReal() - body->height());
			settle();
			CHECK(bottomOf(lastResponse) <= panelBottom + 0.5 && topOf(lastResponse) >= rectangle(statePanel).top());
			CHECK(stateWindow->grabWindow().save(QString::fromUtf8(argv[1]) + QStringLiteral("/service-pane-scrolled.png")));
			// A tall pane has nothing to scroll.
			stateWindow->setHeight(900);
			settle();
			CHECK(!body->property("interactive").toBool());
			CHECK(statePanel->setProperty("scrolls", false));
			stateWindow->setHeight(640);
			settle();
		}
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
        // Find only filters the tree; nothing is selected or sent until the
        // user steps to a match with Enter, which is an explicit activation.
        auto* searchInput = item(separateTreePanel, QStringLiteral("endpointSearch"));
        auto* searchStatus = item(separateTreePanel, QStringLiteral("endpointSearchStatus"));
        CHECK(searchInput && searchStatus);
        const auto beforeSearch = shared->property("selections").toInt();
        CHECK(searchInput->setProperty("text", QStringLiteral("voltage")));
        QTest::qWait(250);
        CHECK(searchStatus->property("text").toString() == QStringLiteral("2 matches"));
        CHECK(shared->property("selections").toInt() == beforeSearch);
        CHECK(keyboardTree->property("rows").toInt() == 2);
        searchInput->forceActiveFocus();
        settle();
        CHECK(shared->setProperty("refuseSelection", true));
        QTest::keyClick(separateWindow, Qt::Key_Return);
        settle();
        CHECK(shared->property("selections").toInt() == beforeSearch);
        CHECK(searchStatus->property("text").toString() == QStringLiteral("1 of 2 matches"));
        CHECK(shared->setProperty("refuseSelection", false));
        QTest::keyClick(separateWindow, Qt::Key_Return);
        settle();
        CHECK(shared->property("selections").toInt() == beforeSearch + 1);
        CHECK(searchStatus->property("text").toString() == QStringLiteral("2 of 2 matches"));
        QTest::keyClick(separateWindow, Qt::Key_Return, Qt::ShiftModifier);
        settle();
        CHECK(shared->property("selections").toInt() == beforeSearch + 2);
        CHECK(selected->property("text").toString() == QStringLiteral("Field voltage"));
        CHECK(searchInput->setProperty("text", QStringLiteral("not-present")));
        QTest::qWait(250);
        CHECK(searchStatus->property("text").toString() == QStringLiteral("Nothing matches “not-present”."));
        CHECK(searchInput->setProperty("text", QString()));
        QTest::qWait(250);
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
        // Narrow rows stay one line (WL-2a): the columns give way in the row
        // family's order and the status word becomes a mark with the word in
        // its tooltip; × is always there.
        std::function<QQuickItem*(QQuickItem*, int)> rowAt = [&](QQuickItem* root, int index) -> QQuickItem* {
            if (root->objectName() == QStringLiteral("watchlistRow") && root->property("index").toInt() == index)
                return root;
            for (auto* child : root->childItems())
                if (auto* found = rowAt(child, index))
                    return found;
            return nullptr;
        };
        {
            CHECK(QMetaObject::invokeMethod(narrowList, "positionViewAtEnd"));
            settle();
            // One line a row high: the name, the value, the status as a mark
            // (its word, and the provider's own, in tooltips), and ×.
            const qreal lineHeight = std::round(std::max<qreal>(10.0, QGuiApplication::font().pointSizeF()) *
                                                QGuiApplication::primaryScreen()->logicalDotsPerInch() / 72.0 * 1.65);
            auto* lastRow = rowAt(narrowList, 64);
            CHECK(lastRow != nullptr && qAbs(lastRow->height() - lineHeight) < 0.5);
            CHECK(lastRow->property("status").toString() ==
                  QStringLiteral("SubmissionRefusedBecauseTheDeviceIsTemporarilyUnavailable"));
            CHECK(narrowPanel->property("statusGlyph").toBool() &&
                  lastRow->property("statusText").toString() == QStringLiteral("Refused"));
            CHECK(!item(lastRow, QStringLiteral("watchRowStatus"))->isVisible());
            auto* lastRemove = item(lastRow, QStringLiteral("removeWatchButton"));
            auto* lastValue = item(lastRow, QStringLiteral("watchValueText"));
            CHECK(lastRemove && lastValue && rectangle(lastRemove).left() >= rectangle(lastValue).right() - 1 &&
                  qAbs(rectangle(lastRemove).center().y() - rectangle(lastValue).center().y()) <= 1);
            // A number is never cut: the value column is as wide as it.
            CHECK(!lastValue->property("truncated").toBool());
            // × shows on the row under the pointer or the current one (A1).
            CHECK(!lastRemove->isVisible());
            for (const auto* name : {"watchValueText", "watchRowStatusGlyph"}) {
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
        // A wide dock shows every column under its title: the value
        // right-aligned, a good reading as the value alone, a problem as its
        // status word, and × on the row under the pointer or the current one.
        {
            narrowWindow->resize(720, 360);
            settle();
            CHECK(!narrowPanel->property("statusGlyph").toBool());
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
            CHECK(okStatus && okStatus->property("text").toString().isEmpty());
            CHECK(failedStatus && failedStatus->isVisible());
            // A writable row is written in place: double-click the value or
            // press F2, type, Enter writes; Esc or clicking elsewhere cancels;
            // a read-only row offers nothing, and neither does a provider
            // without permission.
            {
                auto* okValue = item(okRow, QStringLiteral("watchValueText"));
                auto* editor = item(okRow, QStringLiteral("scalarEditor"));
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
                { auto* readOnlyEditor = item(failedRow, QStringLiteral("scalarEditor")); CHECK(!readOnlyEditor || !readOnlyEditor->isVisible()); }
                CHECK(narrowProvider->setProperty("canWriteWatch", false));
                settle();
                doubleClick(okValue);
                CHECK(!editor->isVisible());
                CHECK(narrowProvider->setProperty("canWriteWatch", true));
                CHECK(narrowProvider->setProperty("watchWriteOutcome", QString()));
                settle();
            }
            // No dot for a good reading (family A6): the value is the OK state.
            CHECK(!item(okRow, QStringLiteral("watchOkDot")) && !item(failedRow, QStringLiteral("watchOkDot")));
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
            // Clear empties it at once, never asked first (UR-1): Undo brings
            // the values back where the host gave a history; this one gave
            // none, so the line says it with no Undo.
            CHECK(narrowPanel->setProperty("expanded", true));
            settle();
            CHECK(QMetaObject::invokeMethod(menuItem("clearWatchItem"), "click"));
            settle();
            CHECK(!narrowPanel->findChild<QObject*>(QStringLiteral("clearWatchDialog")));
            CHECK(narrowProvider->property("watchCount").toInt() == 0);
            auto* cleared = named("watchNotice");
            CHECK(cleared && cleared->property("text").toString() == QStringLiteral("Cleared 66 values"));
            CHECK(narrowWindow->grabWindow().save(QString::fromUtf8(argv[1]) + QStringLiteral("/watch-clear.png")));
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
        auto* sourceTree = item(searchPanel, QStringLiteral("endpointTree"));
        auto* fake = searchPanel->property("provider").value<QObject*>();
        CHECK(searchPanel && query && status && sourceTree && fake);
        // The tree's own index of a row of the model: it shows the model
        // through its find filter.
        const auto shownIndex = [&](const QModelIndex& source) {
            QVariant result;
            CHECK(QMetaObject::invokeMethod(searchPanel, "shownOf", Q_RETURN_ARG(QVariant, result),
                                            Q_ARG(QVariant, QVariant::fromValue(source))));
            return result.value<QModelIndex>();
        };
        // Find filters the tree in place: a path typed with "/" or ".", in
        // any case, finds the row; nothing is selected or sent yet.
        CHECK(query->setProperty("text", QStringLiteral("ALPHA / voltage")));
        QTest::qWait(250);
        CHECK(status->property("text").toString() == QStringLiteral("1 match"));
        CHECK(sourceTree->property("rows").toInt() == 2 && fake->property("calls").toInt() == 0);
        auto* resultRow = rowItem(searchPanel, 1);
        CHECK(resultRow && resultRow->property("name").toString() == QStringLiteral("Voltage"));
        auto* resultName = item(resultRow, QStringLiteral("endpointRowName"));
        auto* resultBackground = resultRow->property("background").value<QObject*>();
        auto* treeBackground = item(searchPanel, QStringLiteral("endpointTreePanelBackground"));
        // No heading repeats the dock's title and no standing note takes rows;
        // the watch status line is reserved at the bottom, blank when idle.
        auto* watchStatus = item(searchPanel, QStringLiteral("endpointWatchSelectionStatus"));
        CHECK(resultName && resultBackground && treeBackground && watchStatus);
        CHECK(!item(searchPanel, QStringLiteral("endpointTreeTitle")) &&
              !item(searchPanel, QStringLiteral("endpointTreeHelp")) && watchStatus->isVisible());
        const auto treeColor = [&] { return treeBackground->property("color").value<QColor>(); };
        const auto textColor = [&] { return resultName->property("color").value<QColor>(); };
        const auto rowFill = [&] { return resultBackground->property("color").value<QColor>(); };
        // The matched run is marked, underlined; the rest of the name as at rest.
        CHECK(resultName->property("textFormat").toInt() == 4 /* Text.StyledText */);
        CHECK(resultName->property("text").toString().contains(QStringLiteral("<u>")));
        CHECK(!resultRow->property("highlighted").toBool());
        CHECK(textColor() == QColor(QStringLiteral("#e8edf5")));
        CHECK(contrastRatio(textColor(), treeColor()) >= 4.5);
        CHECK(contrastRatio(watchStatus->property("color").value<QColor>(), treeColor()) >= 4.5);
        CHECK(contrastRatio(status->property("color").value<QColor>(), treeColor()) >= 4.5);
        // Enter makes the match current and selected; in the focused tree the
        // selection takes the host's highlight.
        query->forceActiveFocus();
        settle();
        QTest::keyClick(searchWindow, Qt::Key_Return);
        settle();
        CHECK(fake->property("calls").toInt() == 1);
        CHECK(fake->property("selectedEndpointIndex").value<QModelIndex>() == alphaVoltage->index());
        CHECK(status->property("text").toString() == QStringLiteral("1 of 1 matches"));
        CHECK(resultRow->property("highlighted").toBool());
        sourceTree->forceActiveFocus(Qt::TabFocusReason);
        settle();
        CHECK(rowFill() == QColor(QStringLiteral("#215a68")));
        CHECK(textColor() == QColor(QStringLiteral("#ffffff")));
        CHECK(contrastRatio(textColor(), rowFill()) >= 4.5);
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
        CHECK(rowFill() == QColor(QStringLiteral("#c3e0ff")));
        CHECK(textColor() == QColor(QStringLiteral("#121212")));
        CHECK(contrastRatio(textColor(), rowFill()) >= 4.5);
        // Cleared, the tree is whole again, the selected row kept.
        CHECK(query->setProperty("text", QString()));
        QTest::qWait(250);
        // A light canvas can still use a dark selected-row highlight. The
        // keyboard cursor must use highlightedText on the selected tree row.
        CHECK(searchWindow->setProperty("testHighlight", QColor(QStringLiteral("#163860"))));
        CHECK(searchWindow->setProperty("testHighlightedText", QColor(QStringLiteral("#ffffff"))));
        settle();
        auto* selectedTreeRow = rowItem(searchPanel, 1);
        CHECK(selectedTreeRow && selectedTreeRow->property("highlighted").toBool());
        auto* selectedFocus = item(selectedTreeRow, QStringLiteral("endpointFocusIndicator"));
        auto* selectionModel = qobject_cast<QItemSelectionModel*>(sourceTree->property("selectionModel").value<QObject*>());
        CHECK(selectedFocus && selectionModel);
        selectionModel->setCurrentIndex(shownIndex(alphaVoltage->index()), QItemSelectionModel::NoUpdate);
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
        // A name is never read as markup: its run is marked, the rest escaped.
        searchFixture.append(alpha, QStringLiteral("<b>Current</b>"), true);
        CHECK(query->setProperty("text", QStringLiteral("<b>current")));
        QTest::qWait(250);
        CHECK(status->property("text").toString() == QStringLiteral("1 match"));
        auto* markupRow = rowItem(searchPanel, 1);
        CHECK(markupRow && markupRow->property("name").toString() == QStringLiteral("<b>Current</b>"));
        auto* markupName = item(markupRow, QStringLiteral("endpointRowName"));
        CHECK(markupName && markupName->property("textFormat").toInt() == 4);
        const auto markup = markupName->property("text").toString();
        CHECK(markup.contains(QStringLiteral("&lt;b&gt;")) && !markup.contains(QStringLiteral("<b>")));
        CHECK(!markupRow->property("highlighted").toBool());
        CHECK(markupName->property("color").value<QColor>() == QColor(QStringLiteral("#242424")));
        CHECK(contrastRatio(markupName->property("color").value<QColor>(), treeColor()) >= 4.5);
        // Reset invalidates indexes; the find stays and filters the new rows.
        CHECK(query->setProperty("text", QStringLiteral("alpha.voltage")));
        QTest::qWait(250);
        CHECK(status->property("text").toString() == QStringLiteral("1 match"));
        searchFixture.clear();
        settle();
        CHECK(status->property("text").toString() == QStringLiteral("Nothing matches “alpha.voltage”."));
        alpha = searchFixture.append(nullptr, QStringLiteral("Alpha"), false);
        alphaVoltage = searchFixture.append(alpha, QStringLiteral("Voltage"), true);
        QTest::qWait(250);
        CHECK(status->property("text").toString() == QStringLiteral("1 match") && fake->property("calls").toInt() == 1);
        query->forceActiveFocus();
        settle();
        QTest::keyClick(searchWindow, Qt::Key_Return);
        settle();
        CHECK(fake->property("calls").toInt() == 2);
        CHECK(fake->property("selectedEndpointIndex").value<QModelIndex>() == alphaVoltage->index());
        // Every match is counted, rows added later too: nothing limits a find.
        CHECK(query->setProperty("text", QStringLiteral("item")));
        auto* large = searchFixture.append(nullptr, QStringLiteral("Large"), false);
        for (int i = 0; i < 499; ++i)
            searchFixture.append(large, QStringLiteral("item %1").arg(i), true);
        QTest::qWait(250);
        CHECK(status->property("text").toString() == QStringLiteral("499 matches"));
        searchFixture.append(large, QStringLiteral("item 499"), true);
        searchFixture.append(large, QStringLiteral("item 500"), true);
        settle();
        CHECK(status->property("text").toString() == QStringLiteral("501 matches"));
        CHECK(fake->property("calls").toInt() == 2);
        delete searchWindow;
        QCoreApplication::processEvents();
        // 10,000 rows filtered at once stay quick.
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
        QElapsedTimer elapsed;
        elapsed.start();
        CHECK(query->setProperty("text", QStringLiteral("never-match")));
        CHECK(QMetaObject::invokeMethod(searchPanel, "applyFind"));
        const auto filtered = elapsed.elapsed();
        settle();
        std::printf("Find over 10000 nodes elapsed=%lldms\n", static_cast<long long>(filtered));
        CHECK(filtered < 2000);
        CHECK(status->property("text").toString() == QStringLiteral("Nothing matches “never-match”."));
        CHECK(query->setProperty("text", QStringLiteral("item 9999")));
        CHECK(QMetaObject::invokeMethod(searchPanel, "applyFind"));
        settle();
        CHECK(status->property("text").toString() == QStringLiteral("1 match"));
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
