// Three independent installed QML panels over one actual native client/FIFO. MIT.
#include "Fixture.hpp"
#include "../helpers/UndoKinds.hpp"
#include <Telemetry/UX/WatchKey.hpp>
#include <Telemetry/UX/WatchTraces.hpp>
#include <Telemetry/UX/WatchLeaves.hpp>
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

static std::vector<std::byte> selectedDraftPayload(bool reselect)
{
	Harness h;
	Provider provider{h.binding.get()};
	provider.setAllowMutations(true);
	CHECK(!provider.selectionRequiresDiscard());
	select(provider, "Config");
	CHECK(provider.refresh());
	h.pump();
	CHECK(provider.beginEdit() && !provider.selectionRequiresDiscard());
	CHECK(edit(provider, "limit", "77") && provider.selectionRequiresDiscard());
	const auto selected = provider.selectedEndpointIndex();
	const auto packets = h.storage->packets;
	QSignalSpy resets(provider.values(), &QAbstractItemModel::modelReset);
	CHECK(resets.isValid());
	if (reselect)
		CHECK(provider.selectEndpoint(selected));
	CHECK(provider.selectedEndpointIndex() == selected && provider.editing() &&
	      provider.canApply() && provider.selectionRequiresDiscard());
	CHECK(provider.values()->data(member(provider.values(), "limit"),
	                              ValueModel::EditorTextRole).toString() == "77");
	CHECK(resets.isEmpty() && h.storage->packets == packets &&
	      h.storage->owner.pending() == 0 && device.writes == 0);
	CHECK(provider.apply());
	const auto packet = h.storage->offered();
	const std::vector<std::byte> payload(packet.bytes.begin(), packet.bytes.end());
	h.storage->execute(packet);
	h.pump();
	CHECK(device.writes == 1 && device.config.limit == 77 && !provider.selectionRequiresDiscard());
	return payload;
}

static void selectionDiscardContract()
{
	// The entire actual encoded Write packet stays identical to the control,
	// including untouched U64/S64/F32/F64 values, after revealing the current Field.
	const auto control = selectedDraftPayload(false);
	CHECK(selectedDraftPayload(true) == control);
	Harness h;
	Provider provider{h.binding.get()};
	provider.setAllowMutations(true);
	select(provider, "Config");
	CHECK(provider.refresh());
	h.pump();
	CHECK(provider.beginEdit() && !provider.selectionRequiresDiscard());
	const auto selected = provider.selectedEndpointIndex();
	const auto packets = h.storage->packets;
	CHECK(!edit(provider, "limit", "not-a-number"));
	CHECK(provider.selectionRequiresDiscard() && provider.editing() && !provider.canApply());
	QSignalSpy resets(provider.values(), &QAbstractItemModel::modelReset);
	CHECK(provider.selectEndpoint(selected));
	CHECK(provider.selectionRequiresDiscard() && provider.editing() && !provider.canApply() &&
	      resets.isEmpty() && provider.selectedEndpointIndex() == selected);
	CHECK(h.storage->packets == packets && h.storage->owner.pending() == 0);
	CHECK(edit(provider, "limit", "1") && !provider.selectionRequiresDiscard());
	provider.cancelEdit();
	CHECK(!provider.selectionRequiresDiscard());
	select(provider, "Reset");
	CHECK(!provider.selectionRequiresDiscard()); // Void Command has no user input.
	select(provider, "Inspect");
	CHECK(!provider.selectionRequiresDiscard()); // Canonical-zero request is untouched.
	CHECK(!edit(provider, "multiplier", "4294967296") && provider.selectionRequiresDiscard());
	const auto request = provider.selectedEndpointIndex();
	CHECK(provider.selectEndpoint(request) && provider.selectionRequiresDiscard() &&
	      !provider.canCallService());
	CHECK(edit(provider, "multiplier", "4") && provider.selectionRequiresDiscard());
	CHECK(provider.selectEndpoint(request));
	CHECK(provider.canCallService() && provider.selectionRequiresDiscard() &&
	      provider.values()->data(member(provider.values(), "multiplier"),
	                              ValueModel::EditorTextRole).toString() == "4");
	CHECK(h.storage->packets == packets && h.storage->owner.pending() == 0 &&
	      device.commands == 0 && device.services == 0);
	for (const bool service : {false, true}) {
		select(provider, service ? "Inspect" : "Reset");
		const auto target = provider.selectedEndpointIndex();
		provider.cancelEdit();
		CHECK(!provider.editing() && !provider.selectionRequiresDiscard());
		CHECK(service ? !provider.canCallService() : !provider.canExecute());
		CHECK(provider.selectEndpoint(target));
		CHECK(service ? provider.canCallService() : provider.canExecute());
		CHECK(!provider.selectionRequiresDiscard());
	}
	CHECK(h.storage->packets == packets && h.storage->owner.pending() == 0 &&
	      device.commands == 0 && device.services == 0);
}

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
	const auto selected = a.selectedEndpointIndex();
	const auto serial = endpoint(a, "Serial");
	CHECK(a.watchCandidate(serial) && a.watchCandidate(fieldIndex) && !a.watchCandidate(commandIndex));
	const auto beforeBulk = h.storage->packets;
	const auto rejected = a.addWatchMany({QVariant::fromValue(serial),
	                                      QVariant::fromValue(endpoint(b, "Serial"))});
	CHECK(rejected.value("status") == "invalid" && a.watchCount() == 0);
	CHECK(a.addWatchMany({QVariant::fromValue(serial)}).value("status") == "refused");
	CHECK(a.selectedEndpointIndex() == selected && a.editing());
	CHECK(a.values()->data(member(a.values(), "limit"), ValueModel::EditorTextRole).toString() ==
	      QStringLiteral("77"));
	a.refreshWatch();
	CHECK(h.storage->packets == beforeBulk && h.storage->owner.pending() == 0);
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
		// The same index may be stale after another panel changes the device.
		// Explicit selection reparses its initial arguments without device I/O.
		const auto packets = h.storage->packets;
		CHECK(selected.selectEndpoint(endpoint(selected, service ? "Inspect" : "Reset")));
		CHECK(service ? selected.canCallService() : selected.canExecute());
		CHECK(!selected.selectionRequiresDiscard());
		CHECK(h.storage->packets == packets && h.storage->owner.pending() == 0 &&
		      device.commands == 1 && device.services == 0);
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
	const bool found = until([&] {
		result = visibleItem(scope, QString::fromLatin1(name), QString::fromLatin1(scalarName));
		return result && result->width() > 0 && result->height() > 0;
	});
	// Name what is missing: a check number alone does not say which control.
	if (!found)
		std::fprintf(stderr, "No visible control %s %s\n", name, scalarName);
	CHECK(found);
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
	// A control that has just appeared (Apply, with the draft) is placed by its
	// layouts' next polish; an offscreen window may not have drawn it yet.
	for (auto* ancestor = &item; ancestor; ancestor = ancestor->parentItem())
		ancestor->ensurePolished();
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
	// Before any read Details shows the Field's definition from the schema,
	// never an empty table: rows, and still no read.
	for (auto* panel : {configPanel, valuePanel}) {
		auto* definitions = control(panel, "metadataTree");
		CHECK(until([&] { return definitions->property("rows").toInt() > 0; }));
	}
	CHECK(h.storage->owner.pending() == 0 && device.reads == 0);
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
	// No Edit button: the cell is the editor, and the draft opens with the
	// first change (BA-4).
	CHECK(!visibleItem(configPanel, QStringLiteral("editButton")) && !config.editing());
	CHECK(!visibleItem(configPanel, QStringLiteral("applyButton")));
	click(*window, *control(configPanel, "valueText", "limit"), configPanel);
	auto* editor = control(configPanel, "scalarEditor", "limit");
	CHECK(until([&] {
		return editor->hasActiveFocus();
	}));
	CHECK(!config.editing());
	QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClick(window, Qt::Key_Backspace);
	for (const char letter : QByteArray{"101"})
		QTest::keyClick(window, letter);
	CHECK(editor->property("text").toString() == QStringLiteral("101"));
	CHECK(config.editing() && !value.editing());
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
	// A Service that takes nothing says so in a caption, with no empty table.
	CHECK(control(configPanel, "noArgumentsLabel")->property("text").toString() == QStringLiteral("No arguments."));
	// Before a call the Response heading says how to fill it, on its own line.
	CHECK(control(configPanel, "emptyResponseLabel")->property("text").toString() ==
	      QStringLiteral("Call to see the response."));
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

// A host whose owner runs only when it has work learns of each request a
// provider puts in the owner's queue from submitted(): once per accepted
// submission, with the request already queued, and never for a refusal.
// QtConnectionBinding wakes the Qt session on it (SDK-2b).
static void submittedOncePerAcceptedRequest()
{
	Harness h;
	Provider provider{h.binding.get()};
	provider.setAllowMutations(true);
	int signalled = 0;
	std::size_t queuedAtSignal = 0;
	QObject::connect(h.binding.get(), &Binding::submitted, h.binding.get(), [&] {
		++signalled;
		queuedAtSignal = std::size_t(h.storage->owner.pending());
	});
	select(provider, "Config");
	CHECK(provider.refresh());
	CHECK(signalled == 1 && queuedAtSignal == 1);
	CHECK(!provider.refresh() && signalled == 1); // still reading: refused, nothing queued
	h.pump();
	CHECK(provider.beginEdit() && edit(provider, "limit", "7"));
	CHECK(provider.apply() && signalled == 2 && queuedAtSignal == 1);
	h.pump(); // the read back is the Workbench's own, not a second submission
	CHECK(signalled == 2 && device.writes == 1 && device.config.limit == 7);
	select(provider, "Reset");
	CHECK(provider.executeCommand() && signalled == 3 && queuedAtSignal == 1);
	h.pump();
	select(provider, "Ping");
	CHECK(provider.callService() && signalled == 4 && queuedAtSignal == 1);
	h.pump();
	CHECK(device.commands == 1 && device.services == 1);
	// Changes not allowed: refused before the queue, so nothing to wake.
	h.binding->setMutationsAllowed(false);
	select(provider, "Reset");
	CHECK(!provider.executeCommand() && signalled == 4 && h.storage->owner.pending() == 0);
}

// Without a Watchlist source nothing can be watched yet: no box, no reason,
// nothing saved and nothing restored. The S3-A headers stand on their own.
void watchWithoutSource()
{
	Harness h;
	Provider provider{h.binding.get()};
	const auto config = endpoint(provider, "Config");
	CHECK(config.isValid() && provider.watchState(config) == -1 && provider.watchRefusal(config).isEmpty());
	CHECK(provider.watchTally(config).isEmpty() && provider.watchBindings().isEmpty());
	CHECK(provider.addWatchBindings({QVariantMap{{"group", "device"}, {"name", "Config"}, {"member", "gain"}}})
	          .value("status") == "refused");
	static_assert(telemetry::ux::watch_key::member(1, 0) == (quint64{1} << 32 | 1));
	telemetry::ux::WatchLeaves leaves;
	leaves.reset(h.storage->schema);
	const auto type = h.storage->schema.endpoints(telemetry::descriptor::Category::Field)[0].typeId;
	CHECK(leaves.count(type) == 306 && leaves.ordinal(type, u"samples[3]") == 9u);
}

// The live Plot's steps (UR-1), on a provider's own traces: every plot kind
// held to UndoKinds' property - done, undone and redone the same, across the
// provider losing its device and getting it back, and across the Watchlist
// naming other paths, as after another descriptor; curves come back in
// their places with their colours. A provider gives its traces its history.
// restore() records nothing.
static void plotUndoKinds()
{
	using telemetry::ux::UndoHistory;
	using telemetry::ux::WatchTraces;
	Harness h;
	Provider provider{h.binding.get()};
	UndoHistory history;
	qint64 now = 0;
	history.setClock([&now] { return now; });
	auto* traces = qobject_cast<WatchTraces*>(provider.watchTraces());
	CHECK(traces && traces->history() == nullptr);
	provider.setHistory(&history);
	CHECK(traces->history() == &history);
	const QString gain = QStringLiteral("device.Config.gain"), serial = QStringLiteral("device.Serial"),
	              limit = QStringLiteral("device.Config.limit");
	const auto state = [traces] {
		QVariantMap colours;
		for (const auto& path : traces->plotted())
			colours.insert(path, traces->colorIndex(path));
		auto saved = traces->capture();
		saved.insert(QStringLiteral("colours"), colours);
		return QVariant(saved);
	};
	const auto start = [&] {
		if (!provider.connected())
			provider.setBinding(h.binding.get());
		CHECK(traces->restore({{QStringLiteral("plotted"), QStringList{gain, serial}}}));
		// Every curve's look plain, then gain's own.
		for (const auto& path : {gain, serial, limit, QStringLiteral("device.Config.mode")}) {
			CHECK(traces->setAppearance(path, QString{}, 1, 0));
			traces->setPoints(path, 0);
		}
		CHECK(traces->setAppearance(gain, QStringLiteral("#3366cc"), 2, 0.5));
		history.clear();
		now += 10000;
	};
	const auto away = [&] {
		provider.setBinding(nullptr);
		events();
		CHECK(!provider.connected());
	};
	const auto back = [&] {
		provider.setBinding(h.binding.get());
		events();
		CHECK(provider.connected());
	};
	// The Watchlist now names other paths: the curves keep theirs.
	quint64 membership = 100;
	const auto otherPaths = [&] {
		traces->setPaths(++membership, {{7, QStringLiteral("other.Value")}});
	};
	const auto done = [](const QString& outcome, const QString&) { return outcome == QStringLiteral("done"); };
	std::vector<undo_kinds::Case> cases;
	const auto add = [&](const char* kind, const char* name, std::function<void()> act) {
		cases.push_back({QString::fromLatin1(kind), QString::fromLatin1(name), &history, start, std::move(act), state,
		                 away, back, otherPaths, done});
	};
	add("plot.curves", "plot a curve", [&] { CHECK(traces->plot(limit).isEmpty()); });
	add("plot.curves", "remove a curve", [&] { traces->unplot(gain); });
	add("plot.curves", "remove all curves", [&] { traces->unplotMany(traces->plotted()); });
	add("plot.curves", "plot several", [&] {
		CHECK(traces->plotMany({limit, gain, QStringLiteral("device.Config.mode")}).value("added").toInt() == 2);
	});
	add("plot.look", "change a curve", [&] { CHECK(traces->setAppearance(serial, QStringLiteral("#aa0000"), 10, -1)); });
	add("plot.look", "change a curve's points twice", [&] {
		traces->setPoints(gain, 1);
		now += 500;
		traces->setPoints(gain, 2);
	});
	add("plot.points", "show the points", [&] { traces->setShowPoints(true); });
	const auto report = undo_kinds::run(QStringLiteral("dynamic_qml_configurator"), cases);
	CHECK(report.cases == 7 && report.disconnects == 7 && report.descriptors == 7);

	// The words, and a curve back in its place with its colour.
	start();
	CHECK(traces->plot(limit).isEmpty() && history.undoText() == QStringLiteral("Plot device.Config.limit"));
	const auto colour = traces->colorIndex(gain);
	traces->unplot(gain);
	CHECK(history.undoText() == QStringLiteral("Remove device.Config.gain from the Plot"));
	// A curve never plotted takes the first free colour: the one gain left.
	CHECK(traces->plot(QStringLiteral("device.Config.fresh")).isEmpty() &&
	      traces->colorIndex(QStringLiteral("device.Config.fresh")) == colour);
	CHECK(history.undo() && history.undo() && traces->plotted() == QStringList({gain, serial, limit}) &&
	      traces->colorIndex(gain) == colour);
	traces->unplotMany(traces->plotted());
	CHECK(history.undoText() == QStringLiteral("Remove all curves from the Plot") && history.undo() &&
	      traces->plotted() == QStringList({gain, serial, limit}));
	now += 10000;
	traces->setPoints(serial, 1);
	CHECK(history.undoText() == QStringLiteral("Change the points of device.Serial"));
	now += 500;
	traces->setPoints(serial, 0);
	CHECK(history.undoText() != QStringLiteral("Change the points of device.Serial"));
	CHECK(traces->setAppearance(serial, QStringLiteral("#00aa00"), 1, 0) &&
	      history.undoText() == QStringLiteral("Change the curve of device.Serial"));
	traces->setShowPoints(true);
	CHECK(history.undoText() == QStringLiteral("Show measurement points"));
	traces->setShowPoints(false);
	CHECK(history.undoText() == QStringLiteral("Hide measurement points"));
	// A project's Plot put back records nothing.
	const auto steps = history.count();
	CHECK(traces->restore({{QStringLiteral("plotted"), QStringList{serial}}, {QStringLiteral("showPoints"), true}}) &&
	      history.count() == steps);
	// A curve taken off since: its plot's undo finds nothing and goes.
	history.clear();
	CHECK(traces->plot(gain).isEmpty());
	CHECK(traces->restore({{QStringLiteral("plotted"), QStringList{serial}}}));
	QString said;
	QObject observer;
	QObject::connect(&history, &UndoHistory::done, &observer,
	                 [&](int, const QString& text, bool undone, const QString& note, const QString& outcome) {
		                 said = history.sentence(text, undone, note, outcome);
	                 });
	CHECK(history.undo() && history.count() == 0 &&
	      said == QStringLiteral("Skipped: Plot device.Config.gain; device.Config.gain is no longer on the Plot."));
	// Without a history its traces record nothing either.
	provider.setHistory(nullptr);
	CHECK(traces->history() == nullptr && traces->plot(limit).isEmpty() && history.count() == 0);
}

// The central Undo's history itself (UR-1), on a target of the check's own: a
// map of keys to values. A step is recorded once its change is made; undo puts
// its states before back, redo its states after. Steps of one merge key within
// two seconds merge, and one back where it started goes. A blocked step keeps
// its words; a skipped one goes; a dead target is skipped. Never more than
// the limit. An action on the device is never a step and clears nothing.
static void undoHistoryCore()
{
	using telemetry::ux::UndoFunctions;
	using telemetry::ux::UndoHistory;
	using telemetry::ux::UndoOutcome;
	using telemetry::ux::UndoPart;
	QVariantMap held;
	QString blocked;
	int applied = 0;
	const auto apply = [&](const QString&, const QVariantMap& state, const QStringList& keys) {
		++applied;
		int present = 0;
		for (const auto& key : keys) {
			if (state.contains(key)) {
				held.insert(key, state.value(key));
				++present;
			} else {
				present += held.remove(key) > 0 ? 1 : 0;
			}
		}
		return present ? UndoOutcome{} : UndoOutcome{UndoOutcome::Kind::Skipped, QStringLiteral("nothing is there")};
	};
	UndoFunctions target(apply, [&blocked] { return blocked; });
	// One part, made apart from the call that records it.
	const auto parts = [](QObject* on, const char* kind, QVariantMap before, QVariantMap after) {
		return QVector<UndoPart>{UndoPart{on, QString::fromLatin1(kind), std::move(before), std::move(after)}};
	};
	UndoHistory history;
	qint64 now = 0;
	history.setClock([&now] { return now; });
	QSignalSpy changed(&history, &UndoHistory::changed), recorded(&history, &UndoHistory::recorded),
	    done(&history, &UndoHistory::done);
	CHECK(!history.canUndo() && !history.canRedo() && history.undoText().isEmpty() && history.undoSerial() == 0 &&
	      history.limit() == 100 && history.clean() && history.count() == 0 && !history.undo() && !history.redo());
	const auto kinds = UndoHistory::kinds();
	CHECK(kinds.contains(QStringLiteral("watch.rows")) && kinds.contains(QStringLiteral("watch.format")) &&
	      kinds.contains(QStringLiteral("file.bytes")));
	// Set a key: the change made, then recorded, which applies nothing.
	const auto set = [&](const QString& key, const QVariant& value, const QString& merge = {}) {
		QVariantMap before, after{{key, value}};
		if (held.contains(key))
			before.insert(key, held.value(key));
		held.insert(key, value);
		return history.record(QStringLiteral("Set %1").arg(key), parts(&target, "watch.rows", before, after),
		                      merge);
	};
	const int first = set(QStringLiteral("a"), 1);
	CHECK(first > 0 && applied == 0 && history.undoSerial() == first && history.undoText() == QStringLiteral("Set a") &&
	      recorded.count() == 1 && recorded.last().at(0).toInt() == first && changed.count() >= 1 && !history.clean());
	now += 5000;
	const int second = set(QStringLiteral("b"), 2);
	CHECK(second > first && history.count() == 2);
	CHECK(history.undo() && held == QVariantMap{{QStringLiteral("a"), 1}} && history.redoSerial() == second &&
	      history.undoSerial() == first && done.last().at(0).toInt() == second && done.last().at(2).toBool() &&
	      done.last().at(4).toString() == QStringLiteral("done"));
	CHECK(history.sentence(done.last().at(1).toString(), true, {}, QStringLiteral("done")) == QStringLiteral("Undone: Set b"));
	CHECK(history.redo() && held.value(QStringLiteral("b")) == 2 && !done.last().at(2).toBool() &&
	      history.sentence(QStringLiteral("Set b"), false, {}, QStringLiteral("done")) == QStringLiteral("Redone: Set b"));
	// A step that changes nothing is not kept; neither is a kind not declared,
	// nor a target that is not one - both said as warnings.
	CHECK(history.record(QStringLiteral("Nothing"), parts(&target, "watch.rows", {}, {})) == 0);
	const auto warned = warnings;
	QObject plain;
	const QVariantMap x{{QStringLiteral("x"), 1}};
	CHECK(history.record(QStringLiteral("Odd"), parts(&target, "no.such.kind", {}, x)) == 0);
	CHECK(history.record(QStringLiteral("Odd"), parts(&plain, "watch.rows", {}, x)) == 0);
	CHECK(warnings == warned + 2 && history.count() == 2);
	warnings = warned;
	// Merged within two seconds of the same key: the earliest before, the
	// latest after, under the newest serial and words.
	now += 5000;
	const int third = set(QStringLiteral("c"), 1, QStringLiteral("c"));
	now += 1000;
	const int merged = set(QStringLiteral("c"), 2, QStringLiteral("c"));
	CHECK(merged > third && history.count() == 3 && history.undoSerial() == merged);
	CHECK(history.undo() && !held.contains(QStringLiteral("c")) && history.redo() && held.value(QStringLiteral("c")) == 2);
	// Back where it started, the merged step goes.
	now += 5000;
	CHECK(set(QStringLiteral("d"), 1, QStringLiteral("d")) > 0 && history.count() == 4);
	now += 500;
	held.remove(QStringLiteral("d"));
	const QVariantMap d1{{QStringLiteral("d"), 1}};
	const int back = history.record(QStringLiteral("Unset d"), parts(&target, "watch.rows", d1, {}), QStringLiteral("d"));
	CHECK(back == 0 && history.count() == 3 && history.undoSerial() == merged);
	// Past the window, or another key: a step of its own.
	now += 2500;
	CHECK(set(QStringLiteral("c"), 3, QStringLiteral("c")) > 0 && history.count() == 4);
	CHECK(set(QStringLiteral("e"), 1, QStringLiteral("c2")) > 0 && history.count() == 5);
	// Blocked: the step keeps its words, greyed, and says why.
	blocked = QStringLiteral("Stop polling to change the Watchlist.");
	history.refresh();
	CHECK(!history.canUndo() && history.undoText() == QStringLiteral("Set e") && history.blockedReason() == blocked &&
	      !history.undo() && held.contains(QStringLiteral("e")));
	blocked.clear();
	history.refresh();
	CHECK(history.canUndo() && history.blockedReason().isEmpty());
	// Skipped: nothing it names is there; it goes, and says so.
	held.remove(QStringLiteral("e"));
	const auto steps = history.count();
	CHECK(history.undo() && history.count() == steps - 1 && done.last().at(4).toString() == QStringLiteral("skipped") &&
	      done.last().at(3).toString() == QStringLiteral("nothing is there"));
	CHECK(history.sentence(QStringLiteral("Set e"), true, QStringLiteral("nothing is there"), QStringLiteral("skipped")) ==
	      QStringLiteral("Skipped: Set e; nothing is there."));
	CHECK(history.sentence(QStringLiteral("Set e"), true, QStringLiteral("e is missing"), QStringLiteral("partial")) ==
	      QStringLiteral("Undone: Set e. e is missing."));
	// An action on the device: words kept for whatever answers Ctrl+Z, no
	// step, nothing cleared; the next step, or an acknowledgement, ends them.
	const auto before = history.count();
	history.noteIrreversible(QStringLiteral("The write to device.Gain is not undone; write 1.5 to go back."));
	CHECK(history.irreversibleNote().startsWith(QStringLiteral("The write")) && history.count() == before && history.canUndo());
	history.acknowledgeIrreversible();
	CHECK(history.irreversibleNote().isEmpty());
	history.noteIrreversible(QStringLiteral("Undo does not take back what is sent to the device."));
	now += 5000;
	CHECK(set(QStringLiteral("f"), 1) > 0 && history.irreversibleNote().isEmpty());
	// Clean: what was saved; an undo past it is no longer.
	history.setClean();
	CHECK(history.clean() && history.undo() && !history.clean() && history.redo() && history.clean());
	// A target gone: its steps are skipped and go.
	{
		UndoFunctions gone(apply, [] { return QString{}; });
		now += 5000;
		held.insert(QStringLiteral("g"), 1);
		const QVariantMap g1{{QStringLiteral("g"), 1}};
		CHECK(history.record(QStringLiteral("Set g"), parts(&gone, "watch.rows", {}, g1)) > 0);
	}
	const auto withGone = history.count();
	CHECK(history.canUndo() && history.undo() && history.count() == withGone - 1 &&
	      done.last().at(4).toString() == QStringLiteral("skipped") && held.contains(QStringLiteral("g")));
	// The limit: the oldest steps go first; setting it drops every step.
	history.setLimit(3);
	CHECK(history.limit() == 3 && history.count() == 0);
	for (int i = 0; i < 5; ++i) {
		now += 5000;
		CHECK(set(QStringLiteral("h%1").arg(i), i) > 0);
	}
	CHECK(history.count() == 3 && history.undoText() == QStringLiteral("Set h4"));
	history.clear();
	CHECK(history.count() == 0 && !history.canUndo() && !history.canRedo());
	// A host's own kind is declared once, then recorded.
	CHECK(!UndoHistory::kinds().contains(QStringLiteral("check.kind")));
	UndoHistory::declareKind(QStringLiteral("check.kind"));
	CHECK(UndoHistory::kinds().contains(QStringLiteral("check.kind")));
	const QVariantMap o1{{QStringLiteral("o"), 1}};
	CHECK(history.record(QStringLiteral("Own"), parts(&target, "check.kind", {}, o1)) > 0);

	// A window of 0: merged for as long as the key stays the same, however
	// long apart - an edited cell until it is left; a window of the caller's
	// own, its length.
	{
		UndoHistory cells;
		qint64 at = 0;
		cells.setClock([&at] { return at; });
		const auto put = [&](const QString& key, const QVariant& value, const QString& merge, qint64 window) {
			QVariantMap before, after{{key, value}};
			if (held.contains(key))
				before.insert(key, held.value(key));
			held.insert(key, value);
			return cells.record(QStringLiteral("Edit %1").arg(key), parts(&target, "watch.rows", before, after), merge,
			                    window);
		};
		held.remove(QStringLiteral("cell"));
		const int cell = put(QStringLiteral("cell"), 1, QStringLiteral("cell/1"), 0);
		at += 60000;
		CHECK(put(QStringLiteral("cell"), 2, QStringLiteral("cell/1"), 0) > cell && cells.count() == 1);
		at += 600000;
		CHECK(put(QStringLiteral("cell"), 3, QStringLiteral("cell/1"), 0) > 0 && cells.count() == 1);
		// Left and edited again: another key, another step.
		at += 100;
		CHECK(put(QStringLiteral("cell"), 4, QStringLiteral("cell/2"), 0) > 0 && cells.count() == 2);
		CHECK(cells.undo() && held.value(QStringLiteral("cell")) == 3 && cells.undo() &&
		      !held.contains(QStringLiteral("cell")) && cells.redo() && held.value(QStringLiteral("cell")) == 3);
		CHECK(cells.redo() && held.value(QStringLiteral("cell")) == 4);
		at += 100;
		CHECK(put(QStringLiteral("w"), 1, QStringLiteral("w"), 10000) > 0 && cells.count() == 3);
		at += 9000;
		CHECK(put(QStringLiteral("w"), 2, QStringLiteral("w"), 10000) > 0 && cells.count() == 3);
		at += 11000;
		CHECK(put(QStringLiteral("w"), 3, QStringLiteral("w"), 10000) > 0 && cells.count() == 4);
	}

	// A provider takes a history and says so; without a Watchlist source it
	// has nothing to record.
	Harness h;
	Provider provider{h.binding.get()};
	QSignalSpy assigned(&provider, &Provider::historyChanged);
	CHECK(provider.history() == nullptr);
	provider.setHistory(&history);
	provider.setHistory(&history);
	CHECK(provider.history() == &history && assigned.count() == 1);
	const auto kept = history.count();
	provider.clearWatch();
	CHECK(history.count() == kept);
	provider.setHistory(nullptr);
	CHECK(provider.history() == nullptr && assigned.count() == 2);
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
	if (selectedCase.isEmpty() || selectedCase == QStringLiteral("discard"))
		selectionDiscardContract();
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
	if (selectedCase.isEmpty() || selectedCase == QStringLiteral("watch"))
		watchWithoutSource();
	if (selectedCase.isEmpty() || selectedCase == QStringLiteral("submitted"))
		submittedOncePerAcceptedRequest();
	// Before "history", which declares a kind of the check's own that no
	// UndoKinds case proves.
	if (selectedCase.isEmpty() || selectedCase == QStringLiteral("plot"))
		plotUndoKinds();
	if (selectedCase.isEmpty() || selectedCase == QStringLiteral("history"))
		undoHistoryCore();
	CHECK(warnings == 0);
	if (application.arguments().contains(QStringLiteral("--application-leak-control")))
		injectedApplicationLeak();
	std::printf("Dynamic QML provider checks=%u warnings=%u\n", checks, warnings);
	return 0;
}
