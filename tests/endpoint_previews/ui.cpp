// Real local TCP, installed providers and the shipped QML editors. MIT.
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <Telemetry/UX/EndpointPreviews.hpp>
#include <integration/qt/ConnectionSession.hpp>
#include "TcpDemoServer.hpp"
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QPersistentModelIndex>
#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace ux = telemetry::ux;
namespace host = telemetry::integration::qt;
namespace {
unsigned checks = 0, warnings = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { \
    std::fprintf(stderr, "Endpoint preview UI check %u line %d: %s\n", checks, __LINE__, #__VA_ARGS__); \
    std::abort(); } } while (false)

bool await(const std::function<bool()>& ready, int limit = 10000) {
	QElapsedTimer timer; timer.start();
	do {
		QCoreApplication::processEvents();
		if (ready()) return true;
		QTest::qWait(1);
	} while (timer.elapsed() < limit);
	return ready();
}
void settle(int milliseconds = 80) { QTest::qWait(milliseconds); }

QQuickItem* visual(QQuickItem* root, const QString& objectName,
                   const QString& property = {}, const QString& expected = {}, bool shown = true) {
	if ((!shown || root->isVisible()) && root->objectName() == objectName &&
	    (property.isEmpty() || root->property(property.toUtf8()).toString() == expected)) return root;
	for (auto* child : root->childItems())
		if (auto* found = visual(child, objectName, property, expected, shown)) return found;
	return nullptr;
}
QQuickItem* control(QQuickItem* root, const char* name,
                    const char* property = nullptr, const QString& expected = {}) {
	const QPointer<QQuickItem> alive(root);
	QQuickItem* found = nullptr;
	const bool available = await([&] {
		if (!alive) return false;
		found = visual(alive, QString::fromLatin1(name), property ? QString::fromLatin1(property) : QString{}, expected);
		return found && found->width() > 0 && found->height() > 0;
	});
	if (!available) std::fprintf(stderr, "Missing visible control %s (%s=%s) under %s\n", name,
	    property ? property : "", qPrintable(expected), alive ? qPrintable(alive->objectName()) : "destroyed delegate");
	CHECK(available);
	return found;
}
void click(QQuickWindow& window, QQuickItem* item) {
	CHECK(item);
	QList<QQuickItem*> ancestors;
	for (auto* p = item; p; p = p->parentItem()) ancestors.prepend(p);
	for (auto* p : ancestors) p->ensurePolished();
	const auto point = item->mapToScene({item->width() / 2, item->height() / 2}).toPoint();
	CHECK(item->isVisible() && item->isEnabled());
	CHECK(point.x() >= 0 && point.x() < window.width());
	CHECK(point.y() >= 0 && point.y() < window.height());
	QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point);
	settle();
}
void type(QQuickWindow& window, QQuickItem* field, const QString& text) {
	click(window, field);
	QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClick(&window, Qt::Key_Backspace);
	for (const auto letter : text.toLatin1()) QTest::keyClick(&window, letter);
	settle();
}
QModelIndex endpoint(QAbstractItemModel* model, const QString& name, const QModelIndex& parent = {}) {
	for (int row = 0; row < model->rowCount(parent); ++row) {
		const auto index = model->index(row, 0, parent);
		if (model->data(index, ux::EndpointModel::IsEndpointRole).toBool() &&
		    model->data(index, ux::EndpointModel::NameRole).toString() == name) return index;
		if (auto found = endpoint(model, name, index); found.isValid()) return found;
	}
	return {};
}
QModelIndex member(QAbstractItemModel* model, const QString& name) {
	const auto root = model->index(0, 0);
	if (model->canFetchMore(root)) model->fetchMore(root);
	for (int row = 0; row < model->rowCount(root); ++row) {
		const auto index = model->index(row, 0, root);
		if (model->data(index, ux::ValueModel::NameRole).toString() == name) return index;
	}
	return {};
}
QString value(ux::DynamicConfiguratorProvider& provider, const char* name, int role = ux::ValueModel::EditorTextRole) {
	const auto index = member(provider.values(), QString::fromLatin1(name)); CHECK(index.isValid());
	return provider.values()->data(index, role).toString();
}
void geometry(QQuickWindow& window, QQuickItem* item, int minimumHeight = 1) {
	CHECK(item && item->isVisible());
	CHECK(std::isfinite(item->width()) && item->width() > 0);
	CHECK(std::isfinite(item->height()) && item->height() >= minimumHeight);
	const auto area = item->mapRectToScene(item->boundingRect());
	CHECK(area.left() >= -1 && area.right() <= window.width() + 1);
	CHECK(area.top() >= -1 && area.bottom() <= window.height() + 1);
}

class Window {
public:
	QQmlApplicationEngine engine;
	QQuickWindow* window = nullptr;
	QQuickItem *treePanel = nullptr, *details = nullptr;
	QString screenshots;
	explicit Window(ux::DynamicConfiguratorProvider& provider, QString captures)
	    : screenshots(std::move(captures)) {
		engine.rootContext()->setContextProperty(QStringLiteral("previewProvider"), &provider);
		engine.loadData(R"QML(
import QtQuick
import QtQuick.Controls
import Telemetry.UX
ApplicationWindow {
    id: hostWindow
    width: 880; height: 820; visible: true
    title: "Telemetry endpoint preview acceptance"
    property string page: "tree"
    EndpointTreePanel {
        id: navigation; objectName: "previewNavigation"
        anchors.fill: parent; visible: hostWindow.page === "tree"
        provider: previewProvider; previewValues: false
    }
    EndpointPanel {
        objectName: "previewDetails"
        anchors.fill: parent; visible: hostWindow.page === "details"
        provider: previewProvider
    }
}
)QML", QUrl(QStringLiteral("qrc:/endpoint-preview-ui.qml")));
		CHECK(engine.rootObjects().size() == 1);
		window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
		CHECK(window && QTest::qWaitForWindowExposed(window));
		treePanel = control(window->contentItem(), "previewNavigation");
		details = visual(window->contentItem(), QStringLiteral("previewDetails"), {}, {}, false);
		CHECK(details);
	}
	void page(const char* name, int width = 880, int height = 820) {
		CHECK(window->setProperty("page", QString::fromLatin1(name)));
		window->resize(width, height); settle();
		CHECK((QString::fromLatin1(name) == "tree") == treePanel->isVisible());
		CHECK((QString::fromLatin1(name) == "details") == details->isVisible());
	}
	void capture(const char* name) {
		if (screenshots.isEmpty()) return;
		CHECK(QDir().mkpath(screenshots));
		settle(120);
		const auto image = window->grabWindow(); CHECK(!image.isNull());
		const auto path = QDir(screenshots).filePath(QString::fromLatin1(name) + QStringLiteral(".png"));
		CHECK(image.save(path));
		std::printf("Endpoint preview capture: %s\n", qPrintable(path));
	}
	QQuickItem* valueRow(const char* name) {
		// Selection/reset queues the view's forceLayout; old delegates with the
		// same member names can remain visible until that turn has completed.
		// Real input arrives after that turn too. Never retain those old rows.
		settle();
		return control(details, "valueRow", "name", QString::fromLatin1(name));
	}
	void choose(const char* name, int index, const char* expectedLabel) {
		auto* choice = control(valueRow(name), "valueChoice");
		click(*window, choice);
		QTest::keyClick(window, Qt::Key_Home);
		for (int n = 0; n < index; ++n) QTest::keyClick(window, Qt::Key_Down);
		QTest::keyClick(window, Qt::Key_Return); settle();
		CHECK(await([&] { return choice->property("currentText").toString() == QString::fromLatin1(expectedLabel); }));
	}
	void enter(const char* name, const char* text) {
		type(*window, control(valueRow(name), "scalarEditor"), QString::fromLatin1(text));
	}
};

void editorBounds(Window& ui) {
	for (const auto& name : {"Gain", "Counter", "Offset", "Enabled", "OperatingMode"}) {
		auto* row = ui.valueRow(name);
		auto* input = control(row, (QString::fromLatin1(name) == "Enabled" ||
		    QString::fromLatin1(name) == "OperatingMode") ? "valueChoice" : "scalarEditor");
		const auto area = input->mapRectToItem(row, input->boundingRect());
		CHECK(area.top() >= -1 && area.bottom() <= row->height() + 1);
		CHECK(area.left() >= -1 && area.right() <= row->width() + 1);
		geometry(*ui.window, input);
	}
}

void metadataBeforeRead(Window& ui, ux::DynamicConfiguratorProvider& provider,
                        inspector_qml::TcpDemoServer& peer) {
	CHECK(peer.fieldReads() == 0 && peer.nativeCommands() == 0 && peer.nativeServices() == 0);
	CHECK(!ui.treePanel->property("previewValues").toBool());
	click(*ui.window, control(ui.treePanel, "endpointRow", "name", QStringLiteral("Config")));
	CHECK(provider.selection().endsWith(QStringLiteral("Config")));
	CHECK(peer.fieldReads() == 0 && provider.values()->rowCount() == 0);
	CHECK(provider.selectionInfo().value(QStringLiteral("typeText")).toString().contains(QStringLiteral("struct")));
	CHECK(provider.selectionInfo().value(QStringLiteral("wireBytes")).toUInt() > 8);
	CHECK(provider.selectionInfo().value(QStringLiteral("readable")).toBool());
	ui.page("details", 320, 820);
	auto* tree = control(ui.details, "metadataTree");
	CHECK(QMetaObject::invokeMethod(tree, "expand", Q_ARG(int, 0)));
	CHECK(await([&] { return visual(tree, QStringLiteral("endpointRow"), QStringLiteral("name"), QStringLiteral("Gain")); }));
	CHECK(control(tree, "endpointRow", "name", QStringLiteral("Gain"))->property("typeText").toString() == "f32");
	const auto root = provider.definitions()->index(0, 0, provider.definitionRootIndex());
	CHECK(root.isValid());
	if (provider.definitions()->canFetchMore(root)) provider.definitions()->fetchMore(root);
	CHECK(provider.definitions()->rowCount(root) >= 8);
	for (int n = 0; n < provider.definitions()->rowCount(root); ++n) {
		const auto child = provider.definitions()->index(n, 0, root);
		CHECK(child.isValid());
		CHECK(!provider.definitions()->data(child, ux::EndpointModel::NameRole).toString().isEmpty());
		CHECK(!provider.definitions()->data(child, ux::EndpointModel::TypeTextRole).toString().isEmpty());
		CHECK(!provider.definitions()->data(child, ux::EndpointModel::IsEndpointRole).toBool());
	}
	geometry(*ui.window, tree, 200);
	geometry(*ui.window, control(ui.details, "endpointSelectionInfo"));
	geometry(*ui.window, control(ui.details, "definitionUnreadNote"));
	for (const auto& name : {"Config", "Gain", "Counter"}) {
		auto* row = control(tree, "endpointRow", "name", QString::fromLatin1(name));
		std::printf("Metadata geometry %s width=%.1f height=%.1f implicit=%.1f twoLines=%d\n", name,
		    row->width(), row->height(), row->implicitHeight(), row->property("twoLines").toBool());
	}
	CHECK(peer.fieldReads() == 0 && provider.values()->rowCount() == 0);
	ui.capture("details-unread-320");
	ui.page("details", 480, 820); geometry(*ui.window, tree, 200); ui.capture("details-unread-480");
}

void previewVisibility(Window& ui, ux::DynamicConfiguratorProvider& provider,
                       host::ConnectionSession& session, inspector_qml::TcpDemoServer& peer) {
	ui.page("tree", 480, 820);
	CHECK(ui.treePanel->setProperty("previewValues", true));
	settle(750);
	// The normal writable fixture contains only one aggregate Field. Its
	// collapsed row is metadata, never a request for its getter.
	CHECK(peer.fieldReads() == 0 && !session.busy());
	auto* config = control(ui.treePanel, "endpointRow", "name", QStringLiteral("Config"));
	CHECK(config->property("previewAggregate").toBool());
	CHECK(config->property("shownValue").toString().isEmpty());
	CHECK(config->property("stateText").toString() == "Expand to preview");
	auto* tree = control(ui.treePanel, "endpointTree");
	CHECK(QMetaObject::invokeMethod(tree, "expand", Q_ARG(int, config->property("row").toInt())));
	const bool firstPreview = await([&] { return peer.fieldReads() == 1 && !session.busy(); });
	if (!firstPreview) {
		auto* currentConfig = control(ui.treePanel, "endpointRow", "name", QStringLiteral("Config"));
		std::fprintf(stderr, "Preview demand: reads=%llu busy=%d active=%d supported=%d rows=%d..%d config row=%d expanded=%d eligible=%d state=%s\n",
		    static_cast<unsigned long long>(peer.fieldReads()), session.busy(), ui.treePanel->property("previewDemandActive").toBool(),
		    provider.previewsSupported(), tree->property("topRow").toInt(), tree->property("bottomRow").toInt(),
		    currentConfig->property("row").toInt(), currentConfig->property("expanded").toBool(), currentConfig->property("previewEligible").toBool(),
		    qPrintable(currentConfig->property("previewState").toString()));
	}
	CHECK(firstPreview);
	CHECK(await([&] {
		auto* counter = visual(tree, QStringLiteral("endpointRow"), QStringLiteral("name"), QStringLiteral("Counter"));
		return counter && counter->property("previewValue").toString() == "18446744073709551615";
	}));
	for (const auto& name : {"Gain", "Counter", "Offset", "Enabled", "OperatingMode"}) {
		auto* row = control(tree, "endpointRow", "name", QString::fromLatin1(name));
		CHECK(!row->property("typeText").toString().isEmpty());
		CHECK(row->property("previewChild").toBool());
		CHECK(row->property("previewState").toString() == "Current");
		CHECK(!row->property("previewValue").toString().isEmpty());
		geometry(*ui.window, row);
	}
	CHECK(provider.values()->rowCount() == 0); // previews never publish a Details snapshot.
	ui.capture("tree-preview-480");
	ui.page("tree", 320, 820);
	CHECK(control(tree, "endpointRow", "name", QStringLiteral("Counter"))->property("twoLines").toBool());
	ui.capture("tree-preview-320");
	ui.page("details", 480, 820);
	CHECK(await([&] { return !session.busy(); }));
	const auto hiddenReads = peer.fieldReads(); settle(5600);
	CHECK(peer.fieldReads() == hiddenReads); // hidden panes have no lease, including TTL expiry.
	ui.page("tree", 480, 820);
	CHECK(await([&] { return peer.fieldReads() > hiddenReads && !session.busy(); }));
	auto* search = control(ui.treePanel, "endpointSearch");
	type(*ui.window, search, QStringLiteral("Config"));
	CHECK(await([&] { return !ui.treePanel->property("searchRunning").toBool() && !session.busy(); }));
	const auto searchedReads = peer.fieldReads(); settle(5600);
	CHECK(peer.fieldReads() == searchedReads); // metadata search has cleared visible preview demand.
	click(*ui.window, search);
	QTest::keyClick(ui.window, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClick(ui.window, Qt::Key_Backspace);
	CHECK(await([&] { return peer.fieldReads() > searchedReads && !session.busy(); }));
	CHECK(ui.treePanel->setProperty("previewValues", false));
	ui.page("details", 880, 820);
}

void editorsAndActualCalls(Window& ui, ux::DynamicConfiguratorProvider& provider,
                           host::ConnectionSession& session, inspector_qml::TcpDemoServer& peer) {
	click(*ui.window, control(ui.details, "refreshButton"));
	CHECK(await([&] { return !provider.busy() && provider.values()->rowCount() == 1; }));
	click(*ui.window, control(ui.details, "editButton"));
	CHECK(provider.editing());
	ui.choose("Enabled", 0, "false (0)");
	CHECK(value(provider, "Enabled") == "false");
	ui.enter("Gain", "0.1");
	CHECK(control(ui.valueRow("Gain"), "scalarEditor")->property("text").toString() == "0.1");
	CHECK(value(provider, "Gain").startsWith(QStringLiteral("0.100000001")));
	const auto canonicalGain = value(provider, "Gain");
	auto* caption = control(ui.valueRow("Gain"), "valueEncodedCaption");
	CHECK(caption->property("text").toString().startsWith(QStringLiteral("Encoded: 0.100000001")));
	CHECK(caption->property("text").toString().endsWith(QStringLiteral("(f32)")));
	const auto draftSelection = provider.selectedEndpointIndex();
	const auto readBefore = peer.fieldReads();
	ui.page("tree", 480, 820); CHECK(ui.treePanel->setProperty("previewValues", true));
	CHECK(await([&] { return peer.fieldReads() > readBefore && !session.busy(); }, 8000));
	CHECK(provider.editing() && provider.selectedEndpointIndex() == draftSelection);
	CHECK(value(provider, "Gain") == canonicalGain && value(provider, "Enabled") == "false");
	CHECK(ui.treePanel->setProperty("previewValues", false)); ui.page("details", 480, 820);
	ui.capture("details-edit-480");
	ui.page("details", 320, 820);
	geometry(*ui.window, control(ui.valueRow("Gain"), "valueEncodedCaption"));
	editorBounds(ui);
	ui.capture("details-edit-320");
	ui.page("details", 880, 820);
	const auto writes = peer.nativeWrites();
	std::puts("Endpoint editor journey: Apply false + f32");
	click(*ui.window, control(ui.details, "applyButton"));
	CHECK(await([&] { return !provider.busy() && peer.nativeWrites() == writes + 1; }));
	CHECK(value(provider, "Enabled", ux::ValueModel::ValueRole) == "false");
	CHECK(!provider.editing());
	std::puts("Endpoint editor journey: Apply true");
	click(*ui.window, control(ui.details, "editButton"));
	ui.choose("Enabled", 1, "true (1)"); CHECK(value(provider, "Enabled") == "true");
	click(*ui.window, control(ui.details, "applyButton"));
	CHECK(await([&] { return !provider.busy() && peer.nativeWrites() == writes + 2; }));
	CHECK(value(provider, "Enabled", ux::ValueModel::ValueRole) == "true");
	std::puts("Endpoint editor journey: Command");
	CHECK(provider.selectEndpoint(endpoint(provider.endpoints(), QStringLiteral("Execute"))));
	CHECK(provider.commandSelected());
	ui.choose("Enabled", 1, "true (1)"); CHECK(value(provider, "Enabled") == "true");
	const auto commands = peer.nativeCommands();
	click(*ui.window, control(ui.details, "executeButton"));
	CHECK(await([&] { return !provider.busy() && peer.nativeCommands() == commands + 1; }));
	CHECK(peer.executedCommands() == 1 && !provider.commandOutcome().isEmpty());
	CHECK(control(ui.details, "commandOutcomeLabel"));
	std::puts("Endpoint editor journey: Service");
	CHECK(provider.selectEndpoint(endpoint(provider.endpoints(), QStringLiteral("Transform"))));
	CHECK(provider.serviceSelected());
	ui.enter("Gain", "0.1");
	ui.choose("Enabled", 0, "false (0)");
	// Canonical zero is unnamed in this enum. Select Idle (-7), then explicitly
	// enter another numeric code through the real ComboBox/manual-code control.
	ui.choose("OperatingMode", 1, "Idle (-7)");
	CHECK(value(provider, "OperatingMode") == "-7");
	auto* enumChoice = control(ui.valueRow("OperatingMode"), "valueChoice");
	click(*ui.window, enumChoice);
	QTest::keyClick(ui.window, Qt::Key_End); QTest::keyClick(ui.window, Qt::Key_Return); settle();
	ui.enter("OperatingMode", "-123"); CHECK(value(provider, "OperatingMode") == "-123");
	const auto services = peer.nativeServices();
	click(*ui.window, control(ui.details, "callServiceButton"));
	CHECK(await([&] { return !provider.busy() && peer.nativeServices() == services + 1 && provider.responses()->rowCount() == 1; }));
	auto* requestTree = control(ui.details, "valueTree");
	auto* responseTree = control(ui.details, "responseTree");
	geometry(*ui.window, requestTree, 180); geometry(*ui.window, responseTree, 180);
	CHECK(await([&] { return visual(responseTree, QStringLiteral("responseRow"), QStringLiteral("name"), QStringLiteral("Enabled")); }));
	CHECK(control(responseTree, "responseRow", "name", QStringLiteral("Enabled"))->property("value").toString() == "false");
	CHECK(control(responseTree, "responseRow", "name", QStringLiteral("OperatingMode"))->property("value").toString() == "-123 (unknown)");
	CHECK(control(responseTree, "responseRow", "name", QStringLiteral("Gain"))->property("value").toString().startsWith(QStringLiteral("0.100000001")));
	CHECK(!visual(ui.details, QStringLiteral("commandOutcomeLabel")));
	CHECK(control(ui.details, "serviceOutcomeLabel")); ui.capture("service-request-response-880");
	CHECK(provider.selectEndpoint(endpoint(provider.endpoints(), QStringLiteral("Config"))));
	settle();
	CHECK(!visual(ui.details, QStringLiteral("responseTree")));
	CHECK(!visual(ui.details, QStringLiteral("serviceOutcomeLabel")));
	CHECK(!visual(ui.details, QStringLiteral("commandOutcomeLabel")));
	CHECK(!provider.serviceOutcome().isEmpty() && !provider.commandOutcome().isEmpty());
	const auto finalCommands = peer.nativeCommands(), finalServices = peer.nativeServices();
	session.disconnect(); settle(200);
	CHECK(peer.nativeCommands() == finalCommands && peer.nativeServices() == finalServices);
}

void leasesWithoutQmlObservers()
{
	// Use the real public Session/binding factory so private cache attachment is
	// exercised honestly. No window/modelReset observer releases these leases.
	inspector_qml::TcpDemoServer peer;
	peer.setWritableDemo(true);
	CHECK(peer.listen());
	host::ConnectionSession session;
	ux::QtConnectionBinding binding(&session);
	auto* first = binding.createProvider();
	auto* second = binding.createProvider();
	CHECK(first && second);
	CHECK(session.connectTcpReference(QStringLiteral("127.0.0.1"), peer.port()));
	CHECK(await([&] { return session.ready() && first->connected() && second->connected(); }));
	const auto storage = session.storage(); CHECK(storage);
	auto* cache = session.findChild<ux::EndpointPreviews*>(); CHECK(cache);
	const auto fields = storage->schema.endpoints(telemetry::descriptor::Category::Field);
	CHECK(fields.size() == 1 && fields[0].id == 0);
	const auto* type = storage->schema.type(fields[0].typeId); CHECK(type);
	QList<quint32> reads;
	ux::EndpointPreviewSource::Completion completion;
	ux::EndpointPreviewSource source;
	source.backing = storage;
	source.current = [storage] { return storage->schema.valid(); };
	source.schema = [storage] { return storage->schema; };
	source.session = [storage] { return storage->inspector.session(); };
	source.busy = [&completion] { return bool(completion); };
	source.read = [&reads, &completion](quint32 id, auto done) {
		CHECK(id == 0);
		CHECK(!completion);
		reads.append(id);
		completion = std::move(done);
		return telemetry::client::Submission{telemetry::client::Status::Ok, {}};
	};
	// Replace only the test seam, retaining actual parsed Session storage.
	// Invalidation refreshes TTL but preserves the global spacing budget.
	cache->setSource(std::move(source));
	const auto issue = [&](int count) {
		CHECK(await([&] { cache->poll(); return reads.size() == count && cache->pending(); }));
	};
	const auto finish = [&] {
		CHECK(completion);
		*completion = ux::EndpointPreviewResult{telemetry::client::Status::Ok, true,
		    QByteArray(int(type->wireBytes), '\0'), {}};
		completion.reset(); cache->poll(); CHECK(!cache->pending());
	};
	const auto quiet = [&](int count) {
		settle(ux::EndpointPreviews::spacingMs + 30); cache->poll();
		CHECK(reads.size() == count && !cache->pending());
	};
	QObject firstView, secondView;
	CHECK(first->setPreviewDemand(&firstView, {QVariant::fromValue(endpoint(first->endpoints(), "Config"))}));
	CHECK(second->setPreviewDemand(&secondView, {QVariant::fromValue(endpoint(second->endpoints(), "Config"))}));
	issue(1);
	first->setBinding(nullptr);
	CHECK(!first->connected() && second->connected() && cache->pending());
	finish(); // accepted work drains for the surviving view.
	cache->invalidateValues(); issue(2); finish();
	CHECK(reads.size() == 2); // retiring first did not erase second's lease.
	second->setBinding(nullptr);
	CHECK(!second->connected());
	cache->invalidateValues(); quiet(2);
	CHECK(reads.size() == 2); // neither retired provider retained hidden demand.

	// A binding can retire while the native owner remains admitted. Its stale
	// sync path must release demand too; the cache source intentionally stays live.
	auto* stale = binding.createProvider(); CHECK(stale && stale->connected());
	ux::DynamicConfiguratorBinding shadow(storage->inspector, storage);
	stale->setBinding(&shadow);
	QObject staleView;
	CHECK(stale->setPreviewDemand(&staleView, {QVariant::fromValue(endpoint(stale->endpoints(), "Config"))}));
	cache->invalidateValues(); issue(3);
	shadow.retire(); shadow.poll();
	CHECK(!stale->connected() && cache->current());
	finish();
	cache->invalidateValues(); quiet(3);
	CHECK(reads.size() == 3);

	// clearDemand publishes synchronously. Rebinding remains safe when that
	// publication deletes this provider before its retirement call resumes.
	auto* doomed = binding.createProvider(); CHECK(doomed && doomed->connected());
	QObject doomedView, observer;
	CHECK(doomed->setPreviewDemand(&doomedView, {QVariant::fromValue(endpoint(doomed->endpoints(), "Config"))}));
	QPointer<ux::DynamicConfiguratorProvider> alive(doomed);
	bool destroy = true;
	QObject::connect(cache, &ux::EndpointPreviews::changed, &observer, [&] {
		if (destroy) { destroy = false; delete doomed; }
	});
	doomed->setBinding(nullptr);
	CHECK(!alive);
	cache->invalidateValues(); quiet(3);
	CHECK(reads.size() == 3 && peer.fieldReads() == 0);
	session.disconnect();
}
} // namespace

int main(int argc, char** argv) {
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	qInstallMessageHandler(+[](QtMsgType type, const QMessageLogContext&, const QString& message) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
			++warnings; std::fprintf(stderr, "Endpoint preview QML warning: %s\n", qPrintable(message));
		}
	});
	inspector_qml::TcpDemoServer peer; peer.setWritableDemo(true); CHECK(peer.listen());
	host::ConnectionSession session; ux::QtConnectionBinding binding(&session);
	binding.setMutationsAllowed(true);
	auto* provider = binding.createProvider(); CHECK(provider);
	CHECK(session.connectTcpReference(QStringLiteral("127.0.0.1"), peer.port()));
	CHECK(await([&] { return session.ready() && provider->connected(); }));
	provider->setAllowMutations(true);
	Window ui(*provider, argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString{});
	metadataBeforeRead(ui, *provider, peer);
	previewVisibility(ui, *provider, session, peer);
	editorsAndActualCalls(ui, *provider, session, peer);
	leasesWithoutQmlObservers();
	CHECK(warnings == 0);
	std::printf("Endpoint preview UI checks=%u warnings=%u\n", checks, warnings);
}
