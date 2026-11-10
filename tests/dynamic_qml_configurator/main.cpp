// Three independent installed QML panels over one actual native client/FIFO. MIT.
#include "Fixture.hpp"
#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <stdexcept>

using namespace dynamic_qml_fixture;

static void independentFieldsAndRefusal()
{
	Harness h;
	Provider a{h.binding.get()}, b{h.binding.get()}, c{h.binding.get()};
	CHECK(a.connected() && b.connected() && c.connected());
	CHECK(h.storage->owner.pending() == 0 && device.reads == 0);
	CHECK(!a.selectedEndpointIndex().isValid());
	const auto fieldIndex = endpoint(a, "Config");
	const auto commandIndex = endpoint(a, "Reset");
	const auto serviceIndex = endpoint(a, "Inspect");
	CHECK(fieldIndex.isValid() && commandIndex.isValid() && serviceIndex.isValid());
	CHECK(a.selectEndpoint(commandIndex) && a.selectedEndpointIndex() == commandIndex);
	CHECK(a.selectEndpoint(serviceIndex) && a.selectedEndpointIndex() == serviceIndex);
	select(a, "Config");
	select(b, "Config");
	select(c, "Serial");
	CHECK(a.selectedEndpointIndex() == fieldIndex);
	CHECK(b.selectedEndpointIndex() == endpoint(b, "Config"));
	CHECK(c.selectedEndpointIndex() == endpoint(c, "Serial"));
	CHECK(!b.selectEndpoint(fieldIndex)); // A foreign model index cannot change selection.
	CHECK(b.selectedEndpointIndex() == endpoint(b, "Config"));
	CHECK(h.storage->owner.pending() == 0);
	CHECK(a.refresh() && b.refresh() && c.refresh());
	CHECK(!a.selectEndpoint(commandIndex) && a.selectedEndpointIndex() == fieldIndex);
	CHECK(h.storage->owner.pending() == 3);
	h.pump();
	CHECK(device.reads == 3 && h.storage->packets == 3);
	CHECK(h.storage->requestIds.size() == 3 &&
	      h.storage->requestIds[0] < h.storage->requestIds[1] &&
	      h.storage->requestIds[1] < h.storage->requestIds[2]);
	CHECK(a.selection() == b.selection() && a.selection() != c.selection());
	CHECK(scalar(a.values(), "serial") == QStringLiteral("18446744073709551615"));
	CHECK(scalar(a.values(), "offset") == QStringLiteral("-9223372036854775808"));
	CHECK(scalar(a.values(), "mode").contains(QStringLiteral("-123")));
	a.setAllowMutations(true);
	b.setAllowMutations(true);
	CHECK(a.beginEdit() && b.beginEdit());
	CHECK(edit(a, "limit", "77") && edit(b, "limit", "22"));
	CHECK(a.values()->data(member(a.values(), "limit"), ValueModel::EditorTextRole).toString() ==
	      QStringLiteral("77"));
	CHECK(b.values()->data(member(b.values(), "limit"), ValueModel::EditorTextRole).toString() ==
	      QStringLiteral("22"));
	const auto field = h.storage->owner.bindField(0);
	CHECK(field);
	for (int i = 0; i < 8; ++i)
		CHECK(h.storage->owner.readField(field.endpoint, [](const ins::ReadResult&) noexcept {
		}));
	CHECK(h.storage->owner.pending() == 8);
	const auto packets = h.storage->packets, writes = device.writes;
	CHECK(!a.apply());
	CHECK(a.editing() && a.canApply() && !a.busy() && device.writes == writes);
	CHECK(a.values()->data(member(a.values(), "limit"), ValueModel::EditorTextRole).toString() ==
	      QStringLiteral("77"));
	CHECK(b.editing() && b.canApply() && b.values()->rowCount() != 0);
	CHECK(h.storage->packets == packets);
	h.pump();
	CHECK(a.apply());
	CHECK(!b.canApply() && b.values()->rowCount() == 0 && c.values()->rowCount() == 0);
	h.pump();
	CHECK(device.writes == writes + 1 && device.config.limit == 77);
	CHECK(a.writeOutcome().contains(QStringLiteral("confirmed")));
	CHECK(scalar(a.values(), "limit") == QStringLiteral("77"));
	CHECK(!b.busy() && b.connected() && c.connected());
	CHECK(b.refresh() && c.refresh());
	h.pump();
	CHECK(scalar(b.values(), "limit") == QStringLiteral("77"));
}

static void exactAndOwnedValues()
{
	constexpr std::array<std::uint32_t, 6> f32{0x7fc12345U, 0xffc23456U, 0x7f812345U,
	                                           0xff823456U, 0,           0x80000000U};
	constexpr std::array<std::uint64_t, 6> f64{0x7ff8123456789abcULL,
	                                           0xfff823456789abcdULL,
	                                           0x7ff0123456789abcULL,
	                                           0xfff023456789abcdULL,
	                                           0,
	                                           0x8000000000000000ULL};
	for (std::size_t i = 0; i < f32.size(); ++i) {
		Harness h;
		device.config.gain = std::bit_cast<float>(f32[i]);
		device.config.precise = std::bit_cast<double>(f64[i]);
		Provider provider{h.binding.get()};
		provider.setAllowMutations(true);
		select(provider, "Config");
		CHECK(provider.refresh());
		h.pump();
		const auto oldIndex = member(provider.values(), "serial");
		CHECK(oldIndex.isValid());
		CHECK(provider.beginEdit());
		CHECK(!edit(provider, "serial", "18446744073709551616") && !provider.canApply());
		CHECK(edit(provider, "serial", "9007199254740993"));
		CHECK(edit(provider, "offset", "-9223372036854775808"));
		CHECK(edit(provider, "limit", "101"));
		const auto packets = h.storage->packets;
		CHECK(provider.apply());
		h.pump();
		CHECK(h.storage->packets == packets + 2 && device.config.limit == 100);
		CHECK(device.config.serial == UINT64_C(9007199254740993) &&
		      device.config.offset == INT64_MIN);
		CHECK(std::bit_cast<std::uint32_t>(device.config.gain) == f32[i]);
		CHECK(std::bit_cast<std::uint64_t>(device.config.precise) == f64[i]);
		CHECK(!provider.values()->data(oldIndex).isValid());
		CHECK(scalar(provider.values(), "serial") == QStringLiteral("9007199254740993"));
	}
	Harness h;
	device.config.samples[129] = 31337;
	Provider provider{h.binding.get()};
	provider.setAllowMutations(true);
	select(provider, "Inspect");
	CHECK(edit(provider, "multiplier", "4"));
	CHECK(provider.callService());
	h.pump();
	CHECK(provider.responses()->rowCount() == 1 && !provider.serviceResponseEndpoint().isEmpty());
	const auto serial = member(provider.responses(), "serial");
	CHECK(serial.isValid());
	const auto expected = provider.responses()->data(serial, ValueModel::ValueRole);
	h.storage->rx.fill(std::byte{0xa5});
	h.storage->reply.fill(std::byte{0x5a});
	h.storage->scratch.fill(std::byte{0x77});
	device.config = {};
	CHECK(provider.responses()->data(serial, ValueModel::ValueRole) == expected);
	const auto samples = member(provider.responses(), "samples");
	CHECK(samples.isValid());
	CHECK(provider.responses()->canFetchMore(samples));
	provider.responses()->fetchMore(samples);
	CHECK(provider.responses()->rowCount(samples) == 128);
	provider.responses()->fetchMore(samples);
	CHECK(provider.responses()->rowCount(samples) == 256);
	CHECK(provider.responses()
	          ->data(provider.responses()->index(129, 0, samples), ValueModel::ValueRole)
	          .toString() == QStringLiteral("31337"));
	select(provider, "Ping");
	CHECK(provider.callService());
	h.pump();
	CHECK(provider.responses()->rowCount() == 0 && provider.serviceResponseEndpoint().isEmpty());
	select(provider, "Empty");
	CHECK(provider.callService());
	h.pump();
	CHECK(provider.responses()->rowCount() == 1 && !provider.serviceResponseEndpoint().isEmpty());
	CHECK(provider.responses()->rowCount(provider.responses()->index(0, 0)) == 0);
}

static void invalidatedRequestKeepsCategory()
{
	for (const bool service : {false, true}) {
		Harness h;
		Provider selected{h.binding.get()}, peer{h.binding.get()};
		selected.setAllowMutations(true);
		peer.setAllowMutations(true);
		select(selected, "Config");
		CHECK(selected.refresh());
		h.pump();
		CHECK(selected.values()->rowCount() == 1);
		select(selected, service ? "Inspect" : "Reset");
		CHECK(service ? selected.serviceSelected() : selected.commandSelected());
		CHECK(!selected.canRefresh());
		select(peer, "Reset");
		CHECK(peer.executeCommand());
		const auto reads = device.reads;
		const auto pending = h.storage->owner.pending();
		CHECK(service ? selected.serviceSelected() : selected.commandSelected());
		CHECK(service ? !selected.commandSelected() : !selected.serviceSelected());
		CHECK(!selected.canRefresh() && !selected.refresh());
		CHECK(!selected.canExecute() && !selected.canCallService());
		CHECK(device.reads == reads && h.storage->owner.pending() == pending);
		h.pump();
		CHECK(!selected.canRefresh() && !selected.refresh());
		CHECK(device.reads == reads && h.storage->owner.pending() == 0);
	}
}

static void modelNotificationDeletion()
{
	for (const bool deleteProvider : {true, false}) {
		Harness h;
		auto targetStorage = std::make_shared<Backing>(18);
		auto* target = new Binding(targetStorage->owner, targetStorage);
		QPointer<Binding> aliveTarget(target);
		auto* provider = new Provider(h.binding.get());
		QPointer<Provider> alive(provider);
		bool deleted = false;
		QObject context;
		QObject::connect(provider->values(), &QAbstractItemModel::modelReset, &context, [&] {
			if (deleted)
				return;
			deleted = true;
			if (deleteProvider)
				delete provider;
			else
				delete target;
		});
		provider->setBinding(target);
		CHECK(deleted);
		if (deleteProvider)
			CHECK(!alive);
		else {
			CHECK(alive && !alive->connected() && !aliveTarget);
			delete provider;
		}
		if (aliveTarget) {
			aliveTarget->retire();
			CHECK(aliveTarget->drained());
			delete target;
		}
		h.binding->poll();
		CHECK(h.binding->drained());
	}
	Harness h;
	auto* provider = new Provider(h.binding.get());
	QPointer<Provider> alive(provider);
	QObject context;
	QObject::connect(provider, &Provider::readFinished, &context, [&](bool published) {
		CHECK(published);
		delete provider;
	});
	select(*provider, "Config");
	CHECK(provider->refresh());
	h.pump();
	CHECK(!alive);
	Provider peer{h.binding.get()};
	select(peer, "Config");
	CHECK(peer.refresh());
	h.pump();
	CHECK(peer.values()->rowCount() == 1 && peer.connected());
}

static constexpr const char* threePanels = R"qml(
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import Telemetry.UX
Window {
    width: 1800; height: 900; visible: true
    RowLayout {
        anchors.fill: parent
        Configurator { objectName: "panelA"; Layout.fillWidth: true; Layout.fillHeight: true; watchlistVisible: false; provider: providerA }
        Loader {
            objectName: "middle"; active: true; Layout.fillWidth: true; Layout.fillHeight: true
            sourceComponent: Component { ServicePanel { objectName: "panelB"; provider: providerB } }
        }
        FieldEditor { objectName: "panelC"; Layout.fillWidth: true; Layout.fillHeight: true; provider: providerC }
    }
}
)qml";

template<class Predicate>
static bool until(Predicate predicate)
{
	QElapsedTimer elapsed;
	elapsed.start();
	while (!predicate() && elapsed.elapsed() < 5000) {
		events();
		QTest::qWait(1);
	}
	return predicate();
}

static QQuickItem* visibleItem(QQuickItem* root, const QString& name,
                               const QString& scalarName = {})
{
	if (root->isVisible() && root->objectName() == name &&
	    (scalarName.isEmpty() || root->property("scalarName").toString() == scalarName))
		return root;
	for (auto* child : root->childItems())
		if (auto* found = visibleItem(child, name, scalarName))
			return found;
	return nullptr;
}

static QQuickItem* control(QQuickItem* scope, const char* name, const char* scalarName = "")
{
	QQuickItem* result = nullptr;
	CHECK(until([&] {
		result = visibleItem(scope, QString::fromLatin1(name), QString::fromLatin1(scalarName));
		return result && result->width() > 0 && result->height() > 0;
	}));
	return result;
}

static QQuickItem* endpointRow(QQuickItem* root, const QString& endpointName)
{
	if (root->isVisible() && root->objectName() == QStringLiteral("endpointRow") &&
	    root->property("name").toString() == endpointName &&
	    root->property("isEndpoint").toBool())
		return root;
	for (auto* child : root->childItems())
		if (auto* found = endpointRow(child, endpointName))
			return found;
	return nullptr;
}

static void typedSelectionInQml()
{
	Harness h;
	Provider p{h.binding.get()};
	QQmlEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("realProvider"), &p);
	QQmlComponent component{&engine};
	component.setData(R"qml(import QtQuick
import QtQuick.Controls
import Telemetry.UX
ApplicationWindow { width: 500; height: 900; visible: true
    EndpointTreePanel { anchors.fill: parent; provider: realProvider }
})qml", QUrl{QStringLiteral("qrc:/Selection.qml")});
	if (component.isError())
		std::fprintf(stderr, "%s\n", qPrintable(component.errorString()));
	std::unique_ptr<QObject> root{component.create()};
	CHECK(root);
	auto* window = qobject_cast<QQuickWindow*>(root.get());
	CHECK(window);
	QQuickItem* field = nullptr;
	QQuickItem* command = nullptr;
	CHECK(until([&] {
		field = endpointRow(window->contentItem(), QStringLiteral("Config"));
		command = endpointRow(window->contentItem(), QStringLiteral("Serial"));
		return field && command;
	}));
	CHECK(p.selectEndpoint(endpoint(p, "Config")));
	CHECK(until([&] { return field->property("highlighted").toBool(); }));
	CHECK(!command->property("highlighted").toBool());
	CHECK(p.refresh());
	CHECK(!p.selectEndpoint(endpoint(p, "Serial")));
	CHECK(field->property("highlighted").toBool() && !command->property("highlighted").toBool());
	h.pump();
	CHECK(p.selectEndpoint(endpoint(p, "Serial")));
	CHECK(until([&] { return command->property("highlighted").toBool(); }));
	CHECK(!field->property("highlighted").toBool());
	h.binding->retire();
	h.storage->owner.invalidate();
	h.storage->owner.process(h.storage->now);
	h.binding->poll();
	events();
	CHECK(!p.selectedEndpointIndex().isValid());
}

static bool inside(QQuickItem& item, QQuickItem& scope)
{
	const auto bounds = item.mapRectToItem(&scope, QRectF{0, 0, item.width(), item.height()});
	return bounds.left() >= -0.5 && bounds.top() >= -0.5 && bounds.right() <= scope.width() + 0.5 &&
	       bounds.bottom() <= scope.height() + 0.5;
}

static void click(QQuickWindow& window, QQuickItem& item, QQuickItem* scope = nullptr)
{
	CHECK(item.isVisible() && item.isEnabled());
	events();
	CHECK(inside(item, *window.contentItem()));
	auto* owner = scope ? scope : item.parentItem();
	CHECK(owner && inside(item, *owner));
	const auto point = item.mapToScene(QPointF{item.width() / 2, item.height() / 2}).toPoint();
	CHECK(point.x() >= 0 && point.x() < window.width() && point.y() >= 0 &&
	      point.y() < window.height());
	QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point);
	events();
}

static void placeholderGeometry(QQuickItem* scope, const char* name, const char* treeName)
{
	auto* label = control(scope, name);
	auto* viewport = label->parentItem();
	CHECK(viewport && viewport->objectName() == QString::fromLatin1(treeName));
	CHECK(!label->property("text").toString().isEmpty());
	QRectF bounds;
	const bool within = until([&] {
		bounds = label->mapRectToItem(viewport, QRectF{0, 0, label->width(), label->height()});
		return viewport->width() > 0 && viewport->height() > 0 && bounds.left() >= -0.5 &&
		       bounds.top() >= -0.5 && bounds.right() <= viewport->width() + 0.5 &&
		       bounds.bottom() <= viewport->height() + 0.5;
	});
	std::fprintf(stderr, "Dynamic QML example %s: viewport=%.1fx%.1f label=%.1f,%.1f %.1fx%.1f\n",
	             name, double(viewport->width()), double(viewport->height()), double(bounds.x()),
	             double(bounds.y()), double(bounds.width()), double(bounds.height()));
	CHECK(within);
}

static void actualExampleInputJourney()
{
	Harness h;
	Provider config{h.binding.get()}, value{h.binding.get()};
	select(config, "Config");
	select(value, "Config");
	QQmlEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("configProvider"), &config);
	engine.rootContext()->setContextProperty(QStringLiteral("valueProvider"), &value);
	QQmlComponent component{&engine, QUrl::fromLocalFile(QStringLiteral(DYNAMIC_QML_EXAMPLE_MAIN))};
	if (component.isError())
		std::fprintf(stderr, "%s\n", qPrintable(component.errorString()));
	std::unique_ptr<QObject> root{component.create()};
	CHECK(root);
	auto* window = qobject_cast<QQuickWindow*>(root.get());
	CHECK(window);
	CHECK(window->width() == 1400 && window->height() == 820);
	auto* configPanel = control(window->contentItem(), "configPanel");
	auto* valuePanel = control(window->contentItem(), "valuePanel");
	CHECK(until([&] {
		return configPanel->width() >= 800 && valuePanel->width() >= 480 &&
		       valuePanel->width() <= 520.5;
	}));
	auto* configPermission = control(window->contentItem(), "configMutationsCheck");
	auto* valuePermission = control(window->contentItem(), "valueMutationsCheck");
	CHECK(!config.allowMutations() && !value.allowMutations());
	CHECK(!configPermission->property("checked").toBool() &&
	      !valuePermission->property("checked").toBool());
	CHECK(h.storage->owner.pending() == 0 && device.reads == 0);
	placeholderGeometry(configPanel, "emptyValueLabel", "valueTree");
	placeholderGeometry(valuePanel, "emptyValueLabel", "valueTree");
	click(*window, *configPermission);
	CHECK(config.allowMutations() && !value.allowMutations());
	CHECK(configPermission->property("checked").toBool() &&
	      !valuePermission->property("checked").toBool());
	click(*window, *control(valuePanel, "refreshButton"), valuePanel);
	h.pump();
	CHECK(value.values()->rowCount() == 1 && !value.canEdit());
	click(*window, *control(configPanel, "refreshButton"), configPanel);
	h.pump();
	std::fprintf(
	    stderr,
	    "Dynamic QML actual input afterRefresh: allowed=%u canEdit=%u rows=%d peerRows=%d reads=%u status=%s\n",
	    config.allowMutations(), config.canEdit(), config.values()->rowCount(),
	    value.values()->rowCount(), device.reads, qPrintable(config.statusText()));
	CHECK(config.canEdit() && value.values()->rowCount() == 1);
	click(*window, *control(configPanel, "editButton"), configPanel);
	CHECK(config.editing() && !value.editing());
	auto* editor = control(configPanel, "scalarEditor", "limit");
	click(*window, *editor, configPanel);
	CHECK(until([&] {
		return editor->hasActiveFocus();
	}));
	QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClick(window, Qt::Key_Backspace);
	for (const char letter : QByteArray{"101"})
		QTest::keyClick(window, letter);
	CHECK(editor->property("text").toString() == QStringLiteral("101"));
	CHECK(editor->hasActiveFocus() && config.canApply()); // No Enter or signal invocation.
	const auto packets = h.storage->packets, reads = device.reads, writes = device.writes;
	QSignalSpy completion(&config, SIGNAL(applyFinished(bool)));
	CHECK(completion.isValid());
	click(*window, *control(configPanel, "applyButton"), configPanel);
	CHECK(config.busy() && value.values()->rowCount() == 0 && !value.canEdit());
	h.pump();
	CHECK(device.writes == writes + 1 && device.reads == reads + 1);
	CHECK(h.storage->packets == packets + 2 && device.config.limit == 100);
	CHECK(scalar(config.values(), "limit") == QStringLiteral("100"));
	CHECK(config.writeOutcome().contains(QStringLiteral("confirmed")));
	CHECK(completion.size() == 1 && completion.front().front().toBool());
	CHECK(value.values()->rowCount() == 0 && !value.allowMutations());
	click(*window, *configPermission);
	CHECK(!config.allowMutations() && !config.canEdit());
	click(*window, *valuePermission);
	CHECK(value.allowMutations() && !config.allowMutations());
	click(*window, *control(valuePanel, "refreshButton"), valuePanel);
	h.pump();
	CHECK(value.canEdit() && value.values()->rowCount() == 1);
	select(config, "Ping");
	events();
	placeholderGeometry(configPanel, "emptyValueLabel", "valueTree");
	placeholderGeometry(configPanel, "emptyResponseLabel", "responseTree");
	CHECK(!config.canCallService());
	root.reset();
	events();
	CHECK(warnings == 0);
	std::printf(
	    "Dynamic QML actual example optIn=isolated writes=1 readbacks=1 geometry=valid warnings=0\n");
}

static void threePanelsRetirementAndReconnect()
{
	Harness h;
	Provider a{h.binding.get()}, c{h.binding.get()};
	auto* b = new Provider(h.binding.get());
	a.setAllowMutations(true);
	b->setAllowMutations(true);
	QQmlEngine engine;
	QObject::connect(&engine, &QQmlEngine::warnings, &engine, [](const QList<QQmlError>& errors) {
		for (const auto& error : errors)
			std::fprintf(stderr, "%s\n", qPrintable(error.toString()));
	});
	engine.rootContext()->setContextProperty(QStringLiteral("providerA"), &a);
	engine.rootContext()->setContextProperty(QStringLiteral("providerB"), b);
	engine.rootContext()->setContextProperty(QStringLiteral("providerC"), &c);
	QQmlComponent component{&engine};
	component.setData(threePanels, QUrl{QStringLiteral("qrc:/ThreeDynamicPanels.qml")});
	if (component.isError())
		std::fprintf(stderr, "%s\n", qPrintable(component.errorString()));
	std::unique_ptr<QObject> window{component.create()};
	CHECK(window);
	events();
	QTest::qWait(50);
	auto* panelA = window->findChild<QObject*>(QStringLiteral("panelA"));
	CHECK(panelA);
	CHECK(window->findChild<QObject*>(QStringLiteral("panelB")));
	CHECK(window->findChild<QObject*>(QStringLiteral("panelC")));
	CHECK(!window->findChild<QObject*>(QStringLiteral("watchlistPane")));
	CHECK(panelA->setProperty("watchlistVisible", true));
	events();
	QTest::qWait(20);
	CHECK(!window->findChild<QObject*>(QStringLiteral("watchlistPane")));
	CHECK(panelA->setProperty("watchlistVisible", false));
	QSignalSpy facts(h.binding.get(), SIGNAL(operationFinished(QVariantMap)));
	CHECK(facts.isValid());
	select(a, "Config");
	CHECK(a.refresh());
	select(*b, "Inspect");
	CHECK(edit(*b, "multiplier", "7"));
	CHECK(b->callService());
	select(c, "Serial");
	CHECK(c.refresh());
	CHECK(h.storage->owner.pending() == 3 && a.busy() && b->busy() && c.busy());
	h.storage->execute(h.storage->offered());
	h.binding->poll();
	events();
	CHECK(!a.busy() && a.values()->rowCount() == 0); // Service mutation retired its older read.
	const auto servicePacket = h.storage->offered();
	const auto serviceBytes = h.storage->execute(servicePacket, false);
	CHECK(device.services == 1);
	auto* middle = window->findChild<QObject*>(QStringLiteral("middle"));
	CHECK(middle);
	CHECK(middle->setProperty("active", false));
	events();
	CHECK(!window->findChild<QObject*>(QStringLiteral("panelB")));
	QPointer<Provider> removed(b);
	delete b;
	CHECK(!removed);
	CHECK(!h.binding->drained());
	CHECK(h.storage->owner.onReply(cl::Input{h.storage->reply}.first(serviceBytes),
	                               servicePacket.session, servicePacket.requestId));
	h.storage->owner.process(h.storage->now);
	h.binding->poll();
	events();
	h.pump();
	CHECK(a.connected() && c.connected() && !a.busy() && !c.busy());
	CHECK(device.services == 1 && h.storage->packets == 3);
	CHECK(c.values()->rowCount() == 1);
	bool keptServiceFact = false;
	for (const auto& entry : facts) {
		const auto value = entry.front().toMap();
		if (value.value(QStringLiteral("operation")).toUInt() == unsigned(cl::Operation::Service)) {
			CHECK(value.value(QStringLiteral("succeeded")).toBool());
			CHECK(value.value(QStringLiteral("panelRetired")).toBool());
			CHECK(!value.value(QStringLiteral("published")).toBool());
			keptServiceFact = true;
		}
	}
	CHECK(keptServiceFact);
	CHECK(a.refresh());
	h.pump();
	CHECK(a.values()->rowCount() == 1);
	const auto oldEndpoint = endpoint(a, "Config"), oldValue = member(a.values(), "serial");
	const auto packets = h.storage->packets, services = device.services;
	h.binding->retire();
	h.storage->owner.invalidate();
	h.storage->owner.process(h.storage->now);
	h.binding->poll();
	events();
	CHECK(!a.connected() && !c.connected() && a.values()->rowCount() == 0 &&
	      c.values()->rowCount() == 0);
	CHECK(!a.selectedEndpointIndex().isValid() && !c.selectedEndpointIndex().isValid());
	CHECK(!a.values()->data(oldValue).isValid() && !a.endpoints()->data(oldEndpoint).isValid());
	CHECK(!a.refresh() && !c.refresh());
	CHECK(h.storage->owner.confirmQuiescence({17, true, true, true}));
	CHECK(h.storage->owner.admit(h.storage->schema, cl::Input{h.storage->bytes}.first(64), 18) ==
	      cl::Status::Ok);
	Binding replacement{h.storage->owner, h.storage};
	replacement.setMutationsAllowed(true);
	a.setBinding(&replacement);
	c.setBinding(&replacement);
	events();
	CHECK(a.connected() && c.connected() && !a.allowMutations() && !c.allowMutations());
	CHECK(a.selection().isEmpty() && c.selection().isEmpty());
	CHECK(!a.selectedEndpointIndex().isValid() && !c.selectedEndpointIndex().isValid());
	CHECK(h.storage->packets == packets && device.services == services);
	CHECK(!a.selectEndpoint(oldEndpoint));
	select(a, "Config");
	select(c, "Serial");
	CHECK(a.refresh() && c.refresh());
	h.storage->pump(replacement);
	CHECK(a.values()->rowCount() == 1 && c.values()->rowCount() == 1 &&
	      device.services == services);
	window.reset();
	events();
	replacement.retire();
	h.storage->owner.invalidate();
	h.storage->owner.process(h.storage->now);
	replacement.poll();
	CHECK(replacement.drained());
	CHECK(warnings == 0);
}

static void uncertainCommandFacts()
{
	Harness h;
	auto* provider = new Provider(h.binding.get());
	provider->setAllowMutations(true);
	select(*provider, "Reset");
	CHECK(provider->executeCommand());
	const auto packet = h.storage->offered();
	const auto size = h.storage->execute(packet, false);
	CHECK(device.commands == 1);
	QSignalSpy facts(h.binding.get(), SIGNAL(operationFinished(QVariantMap)));
	CHECK(facts.isValid());
	delete provider;
	h.binding->retire();
	h.storage->owner.invalidate();
	// Revocation forbids new handoff but still permits a correlated passive reply
	// until the existing deadline. Only deadline loss makes this fact uncertain.
	h.storage->now += 30001;
	h.storage->owner.process(h.storage->now);
	h.binding->poll();
	CHECK(h.binding->drained() && device.commands == 1 && facts.size() == 1);
	const auto value = facts.front().front().toMap();
	CHECK(value.value(QStringLiteral("status")).toUInt() == unsigned(cl::Status::OutcomeUnknown));
	CHECK(value.value(QStringLiteral("panelRetired")).toBool());
	CHECK(!h.storage->owner.onReply(cl::Input{h.storage->reply}.first(size), packet.session,
	                                packet.requestId));
	CHECK(h.storage->owner.confirmQuiescence({17, true, true, true}));
	CHECK(h.storage->owner.admit(h.storage->schema, cl::Input{h.storage->bytes}.first(64), 18) ==
	      cl::Status::Ok);
	CHECK(!h.storage->owner.nextPacket() && device.commands == 1);
	CHECK(!h.storage->owner.onReply(cl::Input{h.storage->reply}.first(size), packet.session,
	                                packet.requestId));
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void injectedApplicationLeak()
{
	void* memory = std::malloc(123);
	CHECK(memory);
	std::memset(memory, 0x42, 123);
#if defined(__GNUC__)
	asm volatile("" : : "r"(memory) : "memory");
#endif
}

int main(int argc, char** argv)
{
	QGuiApplication application(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
			++warnings;
		std::fprintf(stderr, "%s\n", qPrintable(message));
	});
	const auto arguments = application.arguments();
	const auto caseAt = arguments.indexOf(QStringLiteral("--case"));
	const auto selectedCase = caseAt >= 0 ? arguments.value(caseAt + 1) : QString{};
	if (selectedCase.isEmpty() || selectedCase == QStringLiteral("independent"))
		independentFieldsAndRefusal();
	if (selectedCase.isEmpty() || selectedCase == QStringLiteral("exact"))
		exactAndOwnedValues();
	if (selectedCase.isEmpty() || selectedCase == QStringLiteral("category"))
		invalidatedRequestKeepsCategory();
	if (selectedCase.isEmpty() || selectedCase == QStringLiteral("deletion"))
		modelNotificationDeletion();
	if (selectedCase.isEmpty() || selectedCase == QStringLiteral("selection"))
		typedSelectionInQml();
	if (selectedCase.isEmpty() || selectedCase == QStringLiteral("panels"))
		threePanelsRetirementAndReconnect();
	if (selectedCase.isEmpty() || selectedCase == QStringLiteral("example"))
		actualExampleInputJourney();
	if (selectedCase.isEmpty() || selectedCase == QStringLiteral("unknown"))
		uncertainCommandFacts();
	CHECK(warnings == 0);
	if (application.arguments().contains(QStringLiteral("--application-leak-control")))
		injectedApplicationLeak();
	std::printf("Dynamic QML provider checks=%u warnings=%u\n", checks, warnings);
	return 0;
}
