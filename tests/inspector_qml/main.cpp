/*
 * @file main.cpp
 * @brief Real Encoded Inspector Qt model and controller lifecycle checks.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include "DemoPeer.hpp"
#include "InspectorController.hpp"
#include "Models.hpp"

#include <integration/encoded/Backend.hpp>
#include <QtCore/QElapsedTimer>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QPersistentModelIndex>
#include <QtCore/QTemporaryDir>
#include <QtGui/QGuiApplication>
#include <QtTest/QAbstractItemModelTester>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
namespace cl = telemetry::client;
namespace rt = telemetry::runtime;
namespace im = telemetry::integration::inspector;
namespace ui = inspector_qml;
unsigned checks = 0, reads = 0;

void check(bool passed, int line)
{
	++checks;
	if (!passed) {
		std::fprintf(stderr, "Inspector Qt check %u failed at line %d\n", checks, line);
		std::exit(1);
	}
}

#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)

template<class Predicate>
bool await(Predicate predicate, int timeout = 3000)
{
	QElapsedTimer elapsed;
	elapsed.start();
	while (!predicate() && elapsed.elapsed() < timeout) {
		QCoreApplication::processEvents();
		QTest::qWait(1);
	}
	return predicate();
}

QModelIndex endpoint(ui::EndpointModel& model, int category, std::uint32_t id,
                     const QModelIndex& parent = {})
{
	for (int row = 0; row < model.rowCount(parent); ++row) {
		const auto index = model.index(row, 0, parent);
		const auto metadata = model.endpoint(index);
		if (!metadata.isEmpty() && metadata.value("category").toInt() == category &&
		    metadata.value("id").toUInt() == id)
			return index;
		if (const auto found = endpoint(model, category, id, index); found.isValid())
			return found;
	}
	return {};
}

QModelIndex named(ui::ValueModel& model, const QModelIndex& parent, const QString& name)
{
	if (model.canFetchMore(parent))
		model.fetchMore(parent);
	for (int row = 0; row < model.rowCount(parent); ++row) {
		const auto index = model.index(row, 0, parent);
		if (model.data(index, ui::ValueModel::NameRole).toString() == name)
			return index;
	}
	return {};
}

QString value(ui::ValueModel& model, const QModelIndex& index)
{
	const auto data = model.data(index, ui::ValueModel::ValueRole);
	CHECK(data.metaType().id() == QMetaType::QString &&
	      model.data(index, Qt::DisplayRole) == model.data(index, ui::ValueModel::NameRole));
	return data.toString();
}

struct Harness {
	ui::DemoPeer peer;
	std::array<rt::RuntimeType, 128> types{};
	std::array<rt::RuntimeMember, 128> members{};
	std::array<rt::RuntimeEnumEntry, 32> enums{};
	std::array<rt::RuntimeCatalog, 32> catalogs{};
	std::array<rt::RuntimeEndpoint, 128> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
	rt::RuntimeSchema schema;
	std::uint32_t now = 0;
	cl::Clock clock{this, +[](void* object) noexcept {
		                return static_cast<Harness*>(object)->now;
	                }};
	std::array<std::byte, 64> tx{}, port{};
	std::array<std::byte, 65568> rx{}, response{};
	telemetry::integration::encoded::EncodedBackend backend{tx, rx, clock};
	ui::Inspector inspector{backend, clock};
	unsigned packets = 0;

	Harness()
	{
		const auto parsed = rt::RuntimeSchema::parse(peer.descriptor(), workspace);
		CHECK(parsed);
		schema = parsed.schema;
		CHECK(inspector.admit(schema, peer.descriptor().first(64), 1) == cl::Status::Ok);
	}

	~Harness()
	{
		inspector.close();
		inspector.process(now);
		CHECK(inspector.drained());
	}

	void read(std::uint32_t id)
	{
		const auto binding = inspector.bindField(id);
		CHECK(binding);
		unsigned completed = 0;
		CHECK(inspector.readField(binding.endpoint, [&](const auto& result) noexcept {
			CHECK(result.operation.succeeded() && result.published);
			++completed;
		}));
		inspector.process(now);
		auto packet = inspector.nextPacket();
		CHECK(packet && packet.bytes.size() == 32);
		CHECK(packet.bytes[6] == std::byte{4}); // FieldRead, never a write/command/service.
		std::copy(packet.bytes.begin(), packet.bytes.end(), port.begin());
		packet.bytes = cl::Input{port}.first(packet.bytes.size());
		CHECK(inspector.prepareHandoff(packet.session, packet.requestId));
		CHECK(inspector.onSendAccepted(packet.session, packet.requestId));
		const auto written = peer.dispatch(packet.bytes, response, packet.session);
		CHECK(written >= 32);
		CHECK(inspector.onReply(cl::Input{response}.first(written), packet.session,
		                        packet.requestId));
		inspector.process(now);
		CHECK(completed == 1 && inspector.pending() == 0);
		std::fill(response.begin(), response.end(), std::byte{0x5a});
		++packets;
		++reads;
	}
};

void modelChecks()
{
	auto fixture = std::make_unique<Harness>();
	ui::EndpointModel endpoints;
	ui::ValueModel values;
	QAbstractItemModelTester endpointTester{&endpoints,
	                                        QAbstractItemModelTester::FailureReportingMode::Fatal};
	QAbstractItemModelTester valueTester{&values,
	                                     QAbstractItemModelTester::FailureReportingMode::Fatal};
	valueTester.setUseFetchMore(false);
	QSignalSpy endpointResets{&endpoints, &QAbstractItemModel::modelReset};
	QSignalSpy valueResets{&values, &QAbstractItemModel::modelReset};
	QSignalSpy inserted{&values, &QAbstractItemModel::rowsInserted};
	CHECK(endpoints.roleNames().value(Qt::DisplayRole) == QByteArrayLiteral("display"));
	CHECK(values.roleNames().value(Qt::DisplayRole) == QByteArrayLiteral("display"));
	endpoints.rebuild(fixture->inspector);
	CHECK(endpointResets.count() == 1 && endpoints.rowCount() == 3);
	CHECK(endpoints.columnCount() == 1 && endpoints.endpoint({}).isEmpty());
	const std::array<std::uint32_t, 7> ids{0, 65536, 65537, 65538, 131072, 131073, 131074};
	for (const auto id : ids) {
		const auto index = endpoint(endpoints, 1, id);
		CHECK(index.isValid());
		const auto metadata = endpoints.endpoint(index);
		CHECK(metadata.value("readable").toBool());
		CHECK(!metadata.value("name").toString().isEmpty() &&
		      endpoints.data(index, Qt::DisplayRole) ==
		          endpoints.data(index, ui::EndpointModel::NameRole));
		CHECK(!metadata.value("path").toString().isEmpty());
		CHECK(!endpoints.parent(index).isValid() || endpoints.parent(index).model() == &endpoints);
	}
	CHECK(endpoint(endpoints, 2, 0).isValid());
	CHECK(endpoint(endpoints, 3, 0).isValid());
	CHECK(fixture->packets == 0 && !fixture->inspector.nextPacket());
	ui::EndpointModel foreign;
	foreign.rebuild(fixture->inspector);
	CHECK(endpoints.endpoint(endpoint(foreign, 1, 0)).isEmpty());
	const auto oldEndpoint = endpoint(endpoints, 1, 65536);
	QPersistentModelIndex persistentEndpoint{oldEndpoint};
	endpoints.rebuild(fixture->inspector);
	CHECK(!persistentEndpoint.isValid() && endpoints.endpoint(oldEndpoint).isEmpty());

	fixture->read(65536);
	values.resetSnapshot(fixture->inspector);
	CHECK(valueResets.count() == 1 && values.rowCount() == 1);
	CHECK(value(values, values.index(0, 0)) == QStringLiteral("18446744073709551615"));
	const auto oldValue = values.index(0, 0);
	QPersistentModelIndex persistentValue{oldValue};
	fixture->read(65537);
	values.resetSnapshot(fixture->inspector);
	CHECK(!persistentValue.isValid() && !values.data(oldValue).isValid());
	CHECK(value(values, values.index(0, 0)) == QStringLiteral("-9223372036854775808"));
	fixture->read(65538);
	values.resetSnapshot(fixture->inspector);
	CHECK(value(values, values.index(0, 0)) == QStringLiteral("-2 (unknown)"));

	fixture->read(131074);
	values.resetSnapshot(fixture->inspector);
	const auto root = values.index(0, 0);
	CHECK(values.hasChildren(root) && values.rowCount(root) == 0 && values.canFetchMore(root));
	const auto matrix = named(values, root, QStringLiteral("Matrix"));
	CHECK(matrix.isValid());
	const auto row = named(values, matrix, QStringLiteral("[1]"));
	CHECK(row.isValid());
	const auto cell = named(values, row, QStringLiteral("[2]"));
	CHECK(cell.isValid() && value(values, cell) == QStringLiteral("-6"));
	const auto voltages = named(values, root, QStringLiteral("Voltages"));
	const auto firstVoltage = named(values, voltages, QStringLiteral("[0]"));
	const auto ua = named(values, firstVoltage, QStringLiteral("Ua"));
	CHECK(ua.isValid() && value(values, ua) == QStringLiteral("1.25"));
	CHECK(value(values, named(values, root, QStringLiteral("Enabled"))) == QStringLiteral("true"));

	fixture->read(131073);
	values.resetSnapshot(fixture->inspector);
	CHECK(values.rowCount() == 1 && values.rowCount(values.index(0, 0)) == 0);
	CHECK(!values.hasChildren(values.index(0, 0)) && !values.canFetchMore(values.index(0, 0)));
	fixture->read(131072);
	values.resetSnapshot(fixture->inspector);
	const auto array = values.index(0, 0);
	CHECK(values.data(array, ui::ValueModel::ChildCountRole).toUInt() == 16384);
	CHECK(values.data(array, ui::ValueModel::LoadedCountRole).toInt() == 0);
	CHECK(values.rowCount(array) == 0 && values.canFetchMore(array));
	const int previousInsertions = inserted.count();
	values.fetchMore(array);
	CHECK(values.rowCount(array) == 128 && inserted.count() == previousInsertions + 1);
	CHECK(values.data(array, ui::ValueModel::LoadedCountRole).toInt() == 128);
	CHECK(inserted.last().at(1).toInt() == 0 && inserted.last().at(2).toInt() == 127);
	CHECK(value(values, values.index(127, 0, array)) == QStringLiteral("127"));
	while (values.canFetchMore(array)) {
		const auto before = values.rowCount(array);
		values.fetchMore(array);
		CHECK(values.rowCount(array) == before + 128);
	}
	CHECK(values.rowCount(array) == 16384);
	CHECK(value(values, values.index(16383, 0, array)) == QStringLiteral("16383"));
	CHECK(fixture->workspace.reset() == rt::Status::Ok);
	CHECK(!values.data(array).isValid() && !values.canFetchMore(array));
	CHECK(endpoints.endpoint(endpoint(endpoints, 1, 0)).isEmpty());
	values.clear();
	endpoints.clear();
	CHECK(values.rowCount() == 0 && endpoints.rowCount() == 0);
}

void endpointInvalidationChecks()
{
	for (const bool resetSchema : {false, true}) {
		auto fixture = std::make_unique<Harness>();
		ui::EndpointModel endpoints;
		endpoints.rebuild(fixture->inspector);
		QSignalSpy resets{&endpoints, &QAbstractItemModel::modelReset};
		const auto field = endpoint(endpoints, 1, 0);
		const std::array indexes{endpoints.index(0, 0), endpoints.parent(field), field};
		const auto roles = endpoints.roleNames().keys();
		for (const auto& index : indexes) {
			CHECK(index.isValid());
			for (const auto role : roles)
				CHECK(endpoints.data(index, role).isValid());
		}
		if (resetSchema)
			CHECK(fixture->workspace.reset() == rt::Status::Ok);
		else
			fixture->inspector.invalidate();
		CHECK(!fixture->inspector.isCurrent());
		// No pump, reset or request: stale metadata must already be inaccessible.
		for (const auto& index : indexes)
			for (const auto role : roles)
				CHECK(!endpoints.data(index, role).isValid());
		CHECK(endpoints.endpoint(field).isEmpty());
		CHECK(resets.isEmpty() && endpoints.rowCount() == 3 && fixture->packets == 0);
	}
}

void controllerChecks()
{
	ui::InspectorController controller;
	auto* endpoints = qobject_cast<ui::EndpointModel*>(controller.endpoints());
	auto* values = qobject_cast<ui::ValueModel*>(controller.values());
	CHECK(endpoints && values && !controller.connected() && !controller.busy());
	QAbstractItemModelTester endpointTester{endpoints,
	                                        QAbstractItemModelTester::FailureReportingMode::Fatal};
	QAbstractItemModelTester valueTester{values,
	                                     QAbstractItemModelTester::FailureReportingMode::Fatal};
	valueTester.setUseFetchMore(false);
	QSignalSpy finished{&controller, &ui::InspectorController::readFinished};
	QSignalSpy state{&controller, &ui::InspectorController::stateChanged};
	CHECK(controller.connectDemo() && controller.connected());
	CHECK(state.count() > 0 && endpoints->rowCount() == 3);
	CHECK(controller.selectEndpoint(endpoint(*endpoints, 2, 0)));
	CHECK(!controller.refresh() && !controller.canRefresh());
	CHECK(controller.selectEndpoint(endpoint(*endpoints, 3, 0)));
	CHECK(!controller.refresh() && !controller.canRefresh());
	CHECK(controller.inspector().pending() == 0 && !controller.busy());
	CHECK(controller.selectEndpoint(endpoint(*endpoints, 1, 65536)));
	CHECK(!controller.selection().isEmpty() && !controller.busy() && controller.canRefresh());
	CHECK(controller.inspector().pending() == 0 && finished.isEmpty());
	unsigned resetReentries = 0;
	const auto resetGuard =
	    QObject::connect(values, &QAbstractItemModel::modelAboutToBeReset, &controller, [&] {
		    ++resetReentries;
		    const auto session = controller.inspector().session();
		    controller.disconnectDevice();
		    CHECK(controller.connected() && controller.inspector().session() == session);
		    CHECK(!controller.connectDemo());
	    });
	CHECK(controller.refresh());
	CHECK(controller.busy() && values->rowCount() == 0);
	CHECK(await([&] {
		controller.pump();
		return !controller.busy();
	}));
	CHECK(finished.count() == 1 && finished.last().at(0).toBool());
	CHECK(value(*values, values->index(0, 0)) == QStringLiteral("18446744073709551615"));
	CHECK(resetReentries >= 2 && controller.connected());
	QObject::disconnect(resetGuard);
	++reads;

	const auto selected = controller.inspector().bindField(65536);
	CHECK(selected);
	std::array<cl::RequestHandle, 8> queued{};
	for (auto& handle : queued) {
		const auto result =
		    controller.inspector().readField(selected.endpoint, [](const auto&) noexcept {
		    });
		CHECK(result);
		handle = result.handle;
	}
	const QPersistentModelIndex preserved{values->index(0, 0)};
	const auto revisionBeforeRefusal = controller.inspector().snapshotInfo().revision;
	const auto selectionBeforeRefusal = controller.selection();
	CHECK(!values->data(preserved).isValid()); // The eight accepted reads already superseded it.
	CHECK(!controller.refresh());
	CHECK(preserved.isValid() && !values->data(preserved).isValid());
	CHECK(controller.inspector().snapshotInfo().revision == revisionBeforeRefusal);
	CHECK(controller.selection() == selectionBeforeRefusal);
	for (const auto handle : queued)
		CHECK(controller.inspector().cancel(handle));
	CHECK(await([&] {
		controller.pump();
		return controller.inspector().pending() == 0;
	}));
	CHECK(controller.selectEndpoint(endpoint(*endpoints, 1, 131072)));
	CHECK(controller.refresh());
	CHECK(await([&] {
		controller.pump();
		return !controller.busy();
	}));
	++reads;
	unsigned insertReentries = 0;
	const auto insertGuard =
	    QObject::connect(values, &QAbstractItemModel::rowsAboutToBeInserted, &controller, [&] {
		    ++insertReentries;
		    const auto session = controller.inspector().session();
		    controller.disconnectDevice();
		    CHECK(controller.connected() && controller.inspector().session() == session);
		    CHECK(!controller.connectDemo());
	    });
	unsigned dataReentries = 0;
	const auto dataGuard = QObject::connect(
	    values, &QAbstractItemModel::dataChanged, &controller,
	    [&](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
		    if (!roles.contains(ui::ValueModel::LoadedCountRole))
			    return;
		    ++dataReentries;
		    const auto session = controller.inspector().session();
		    controller.disconnectDevice();
		    CHECK(controller.connected() && controller.inspector().session() == session);
		    CHECK(!controller.connectDemo());
	    });
	const auto sampleRoot = values->index(0, 0);
	CHECK(values->canFetchMore(sampleRoot));
	values->fetchMore(sampleRoot);
	CHECK(insertReentries == 1 && values->rowCount(sampleRoot) == 128);
	CHECK(dataReentries == 1 &&
	      values->data(sampleRoot, ui::ValueModel::LoadedCountRole).toInt() == 128);
	CHECK(value(*values, values->index(127, 0, sampleRoot)) == QStringLiteral("127"));
	QObject::disconnect(insertGuard);
	QObject::disconnect(dataGuard);
	CHECK(controller.selectEndpoint(endpoint(*endpoints, 1, 65536)));

	controller.setReplyDelay(100);
	CHECK(controller.refresh());
	controller.pump();
	CHECK(controller.selectEndpoint(endpoint(*endpoints, 1, 65537)));
	const auto offsetSelection = controller.selection();
	CHECK(await([&] {
		controller.pump();
		return !controller.busy();
	}));
	CHECK(controller.selection() == offsetSelection && values->rowCount() == 0);
	controller.setReplyDelay(0);
	CHECK(controller.refresh());
	CHECK(await([&] {
		controller.pump();
		return !controller.busy();
	}));
	CHECK(value(*values, values->index(0, 0)) == QStringLiteral("-9223372036854775808"));
	++reads;

	controller.setReplyDelay(100);
	CHECK(controller.refresh());
	controller.pump();
	const auto oldSession = controller.inspector().session();
	CHECK(controller.connectDemo() && controller.inspector().session() != oldSession);
	CHECK(controller.selectEndpoint(endpoint(*endpoints, 1, 65536)));
	controller.setReplyDelay(0);
	CHECK(controller.refresh());
	CHECK(await([&] {
		controller.pump();
		return !controller.busy();
	}));
	QTest::qWait(150);
	controller.pump();
	CHECK(value(*values, values->index(0, 0)) == QStringLiteral("18446744073709551615"));
	++reads;
	controller.setReadTimeout(100);
	controller.setDropReplies(true);
	CHECK(controller.refresh());
	CHECK(await([&] {
		controller.pump();
		return !controller.busy();
	}));
	CHECK(!finished.last().at(0).toBool() && values->rowCount() == 0);
	CHECK(controller.inspector().recoveryRequired());
	controller.disconnectDevice();
	CHECK(!controller.connected() && !controller.busy() && values->rowCount() == 0);
	CHECK(controller.connectDemo());
	controller.setDropReplies(false);
	controller.setReadTimeout(1000);
	controller.setReplyDelay(100);
	CHECK(controller.selectEndpoint(endpoint(*endpoints, 1, 65536)));
	CHECK(controller.refresh());
	controller.pump();
	controller.disconnectDevice();
	const auto completionsAtDisconnect = finished.count();
	QTest::qWait(150);
	controller.pump();
	CHECK(!controller.connected() && !controller.busy() && values->rowCount() == 0);
	CHECK(finished.count() == completionsAtDisconnect);

	QTemporaryDir directory;
	CHECK(directory.isValid());
	QFile file{directory.filePath(QStringLiteral("descriptor with spaces.bin"))};
	CHECK(file.open(QIODevice::WriteOnly));
	const auto descriptor = controller.demoDescriptor();
	CHECK(file.write(descriptor) == descriptor.size());
	file.close();
	CHECK(controller.loadDescriptor(QUrl::fromLocalFile(file.fileName())));
	CHECK(!controller.connected() && endpoints->rowCount() == 3);
	bool nativePathLoaded = false;
	const auto nativePath = QDir::toNativeSeparators(file.fileName());
	CHECK(QMetaObject::invokeMethod(&controller, "openDescriptorPath", Qt::DirectConnection,
	                                Q_RETURN_ARG(bool, nativePathLoaded),
	                                Q_ARG(QString, nativePath)));
	CHECK(nativePathLoaded && !controller.connected() && endpoints->rowCount() == 3);
	CHECK(controller.selectEndpoint(endpoint(*endpoints, 1, 65536)));
	CHECK(!controller.refresh() && !controller.busy() && !controller.canRefresh() &&
	      controller.inspector().pending() == 0);
	CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
	CHECK(file.write("bad", 3) == 3);
	file.close();
	CHECK(!controller.loadDescriptor(QUrl::fromLocalFile(file.fileName())));
	CHECK(!controller.connected() && !controller.busy());
	CHECK(!controller.loadDescriptor(QUrl{QStringLiteral("https://example.invalid/schema.bin")}));
}

void workbenchChecks()
{
	ui::InspectorController controller;
	auto& endpoints = *qobject_cast<ui::EndpointModel*>(controller.endpoints());
	auto& values = *qobject_cast<ui::ValueModel*>(controller.values());
	QAbstractItemModelTester tester{&values, QAbstractItemModelTester::FailureReportingMode::Fatal};
	tester.setUseFetchMore(false);
	QSignalSpy readsFinished{&controller, &ui::InspectorController::readFinished};
	QSignalSpy applied{&controller, &ui::InspectorController::applyFinished};
	CHECK(controller.connectWritableDemo() && controller.writesEnabled() &&
	      controller.writeProfileSupported());
	const auto config = endpoint(endpoints, 1, 0);
	CHECK(config.isValid() && endpoints.endpoint(config).value("writable").toBool());
	CHECK(controller.selectEndpoint(config) && controller.refresh());
	CHECK(await([&] {
		return !controller.busy();
	}));
	CHECK(readsFinished.count() == 1 && readsFinished.last()[0].toBool());
	++reads;
	CHECK(controller.canEdit() && !controller.canApply() && controller.demoWriteCount() == 0);
	CHECK(controller.beginEdit() && controller.editing() && !controller.refresh());
	const auto root = values.index(0, 0);
	const auto verifyBits = [&](const QModelIndex& parent, bool focusUnchanged) {
		constexpr std::array<std::uint32_t, 6> f32{0x7fc12345u, 0xffc54321u, 0x7f812345u,
		                                           0xff854321u, 0x00000000u, 0x80000000u};
		constexpr std::array<std::uint64_t, 6> f64{
		    UINT64_C(0x7ff8123456789abc), UINT64_C(0xfff854321abcdef0),
		    UINT64_C(0x7ff0123456789abc), UINT64_C(0xfff054321abcdef0),
		    UINT64_C(0x0000000000000000), UINT64_C(0x8000000000000000)};
		const auto floats = named(values, parent, QStringLiteral("FloatBits"));
		const auto doubles = named(values, parent, QStringLiteral("DoubleBits"));
		CHECK(floats.isValid() && doubles.isValid());
		for (std::size_t i = 0; i < f32.size(); ++i) {
			const auto name = QStringLiteral("[%1]").arg(i);
			const auto single = named(values, floats, name), twice = named(values, doubles, name);
			CHECK(single.isValid() && twice.isValid());
			if (focusUnchanged) {
				CHECK(controller.setScalarText(
				    single, values.data(single, ui::ValueModel::EditorTextRole).toString()));
				CHECK(controller.setScalarText(
				    twice, values.data(twice, ui::ValueModel::EditorTextRole).toString()));
			}
			const auto singleNode = values.node(single), doubleNode = values.node(twice);
			CHECK(singleNode && doubleNode);
			const auto singleValue = controller.editing()
			                             ? controller.inspector().editScalar(singleNode.value)
			                             : controller.inspector().scalar(singleNode.value);
			const auto doubleValue = controller.editing()
			                             ? controller.inspector().editScalar(doubleNode.value)
			                             : controller.inspector().scalar(doubleNode.value);
			CHECK(singleValue && doubleValue);
			const auto* s = std::get_if<float>(&singleValue.value.value);
			const auto* d = std::get_if<double>(&doubleValue.value.value);
			CHECK(s && std::bit_cast<std::uint32_t>(*s) == f32[i]);
			CHECK(d && std::bit_cast<std::uint64_t>(*d) == f64[i]);
		}
	};
	verifyBits(root, true);
	CHECK(!controller.canApply());
	const auto gain = named(values, root, QStringLiteral("Gain"));
	const auto counter = named(values, root, QStringLiteral("Counter"));
	const auto offset = named(values, root, QStringLiteral("Offset"));
	const auto enabled = named(values, root, QStringLiteral("Enabled"));
	const auto mode = named(values, root, QStringLiteral("OperatingMode"));
	CHECK(gain.isValid() && counter.isValid() && offset.isValid() && enabled.isValid() &&
	      mode.isValid());
	CHECK(!controller.setScalarText(counter, QStringLiteral("18446744073709551616")));
	CHECK(!controller.setScalarText(offset, QStringLiteral("-9223372036854775809")));
	CHECK(!controller.setScalarText(enabled, QStringLiteral("2")));
	CHECK(!controller.setScalarText(gain, QStringLiteral("1.0 trailing")));
	CHECK(!controller.canApply() && controller.demoWriteCount() == 0);
	CHECK(value(values, counter) == QStringLiteral("18446744073709551615"));
	CHECK(controller.setScalarText(counter, QStringLiteral("18446744073709551614")));
	CHECK(controller.setScalarText(offset, QStringLiteral("-9223372036854775807")));
	CHECK(controller.setScalarText(enabled, QStringLiteral("false")));
	CHECK(controller.setScalarText(mode, QStringLiteral("-9")));
	const auto matrix = named(values, root, QStringLiteral("Matrix"));
	const auto row = named(values, matrix, QStringLiteral("[1]"));
	const auto element = named(values, row, QStringLiteral("[0]"));
	CHECK(element.isValid() && controller.setScalarText(element, QStringLiteral("-43")));
	CHECK(controller.setScalarText(gain, QStringLiteral("25")));
	CHECK(value(values, gain) == QStringLiteral("25"));
	CHECK(!controller.setScalarText(counter, QStringLiteral("18446744073709551616")));
	CHECK(!controller.canApply() && !controller.apply() && controller.demoWriteCount() == 0);
	CHECK(controller.setScalarText(counter, QStringLiteral("18446744073709551614")));
	CHECK(controller.demoWriteCount() == 0 && controller.canApply());
	const bool accepted = controller.apply();
	if (!accepted)
		std::fprintf(stderr, "Apply status: %s\n", qPrintable(controller.statusText()));
	CHECK(accepted && !controller.apply() && controller.busy() && values.rowCount() == 0);
	CHECK(await([&] {
		return !controller.busy();
	}));
	CHECK(applied.count() == 1 && applied.last()[0].toBool() && controller.demoWriteCount() == 1);
	CHECK(!controller.editing() && controller.writeOutcome().contains(QStringLiteral("confirmed")));
	const auto actual = values.index(0, 0);
	verifyBits(actual, false);
	CHECK(value(values, named(values, actual, QStringLiteral("Gain"))) == QStringLiteral("10"));
	CHECK(value(values, named(values, actual, QStringLiteral("Counter"))) ==
	      QStringLiteral("18446744073709551614"));
	CHECK(value(values, named(values, actual, QStringLiteral("Offset"))) ==
	      QStringLiteral("-9223372036854775807"));
	CHECK(value(values, named(values, actual, QStringLiteral("Enabled"))) ==
	      QStringLiteral("false"));
	CHECK(value(values, named(values, actual, QStringLiteral("OperatingMode"))) ==
	      QStringLiteral("-9 (unknown)"));
	const auto actualMatrix = named(values, actual, QStringLiteral("Matrix"));
	CHECK(value(values, named(values, named(values, actualMatrix, QStringLiteral("[1]")),
	                          QStringLiteral("[0]"))) == QStringLiteral("-43"));
	CHECK(!values.data(gain).isValid());
	CHECK(controller.beginEdit());
	CHECK(controller.setScalarText(named(values, values.index(0, 0), QStringLiteral("Gain")),
	                               QStringLiteral("4")));
	controller.cancelEdit();
	CHECK(!controller.editing() && controller.demoWriteCount() == 1);
	CHECK(value(values, named(values, values.index(0, 0), QStringLiteral("Gain"))) ==
	      QStringLiteral("10"));
	controller.setWritesEnabled(false);
	CHECK(!controller.canEdit() && !controller.beginEdit());
	controller.setWritesEnabled(true);
	CHECK(controller.beginEdit());
	CHECK(controller.setScalarText(named(values, values.index(0, 0), QStringLiteral("Gain")),
	                               QStringLiteral("5")));
	controller.setRejectWrites(true);
	CHECK(controller.apply());
	CHECK(await([&] {
		return !controller.busy();
	}));
	CHECK(applied.count() == 2 && !applied.last()[0].toBool() && controller.demoWriteCount() == 2);
	CHECK(controller.writeOutcome().contains(QStringLiteral("endpoint")) && values.rowCount() == 0);
	controller.setRejectWrites(false);
	CHECK(controller.refresh());
	CHECK(await([&] {
		return !controller.busy();
	}));
	++reads;
	CHECK(controller.beginEdit());
	CHECK(controller.setScalarText(named(values, values.index(0, 0), QStringLiteral("Gain")),
	                               QStringLiteral("6")));
	controller.setReadTimeout(80);
	controller.setDropReplies(true);
	CHECK(controller.apply());
	CHECK(await([&] {
		return !controller.busy();
	}));
	CHECK(applied.count() == 3 && !applied.last()[0].toBool());
	CHECK(controller.writeOutcome().contains(QStringLiteral("OutcomeUnknown")) &&
	      controller.demoWriteCount() == 3);
	QTest::qWait(120);
	controller.pump();
	CHECK(controller.demoWriteCount() == 3 && applied.count() == 3 && values.rowCount() == 0);
	CHECK(controller.connectDemo());
	CHECK(controller.selectEndpoint(endpoint(endpoints, 1, 0)));
	controller.setDropReplies(false);
	CHECK(controller.refresh());
	CHECK(await([&] {
		return !controller.busy();
	}));
	CHECK(!controller.canEdit() && !controller.beginEdit());
	++reads;
}

void disconnectApplyChecks()
{
	const auto draft = [](ui::InspectorController& controller) {
		CHECK(controller.connectWritableDemo());
		auto& endpoints = *qobject_cast<ui::EndpointModel*>(controller.endpoints());
		auto& values = *qobject_cast<ui::ValueModel*>(controller.values());
		CHECK(controller.selectEndpoint(endpoint(endpoints, 1, 0)) && controller.refresh());
		CHECK(await([&] {
			return !controller.busy();
		}));
		++reads;
		CHECK(controller.beginEdit());
		CHECK(controller.setScalarText(named(values, values.index(0, 0), QStringLiteral("Gain")),
		                               QStringLiteral("12")));
	};
	for (unsigned phase = 0; phase != 5; ++phase) {
		ui::InspectorController controller;
		draft(controller);
		QSignalSpy finished{&controller, &ui::InspectorController::applyFinished};
		CHECK(controller.apply() && controller.busy());
		// No event-loop progress between Apply and the explicitly chosen phase.
		if (phase == 1 || phase == 2) {
			const auto packet = controller.inspector().nextPacket();
			CHECK(packet && packet.mayExecute);
			if (phase == 2)
				CHECK(controller.inspector().prepareHandoff(packet.session, packet.requestId));
		} else if (phase >= 3) {
			controller.setDropReplies(phase == 3);
			controller.pump();
			CHECK(controller.demoWriteCount() == 1 && controller.busy());
			if (phase == 4)
				CHECK(controller.inspector().phase() ==
				      telemetry::integration::workbench::Phase::Refreshing);
		}
		const auto writes = controller.demoWriteCount();
		controller.disconnectDevice();
		CHECK(!controller.connected() && !controller.busy() && finished.count() == 1);
		CHECK(!finished.first()[0].toBool());
		const auto outcome = controller.writeOutcome();
		CHECK(!outcome.contains(QStringLiteral("Pending")));
		CHECK(outcome.contains(phase < 2   ? QStringLiteral("Cancelled")
		                       : phase < 4 ? QStringLiteral("OutcomeUnknown")
		                                   : QStringLiteral("Write applied")));
		if (phase == 4)
			CHECK(!outcome.contains(QStringLiteral("OutcomeUnknown")) &&
			      outcome.contains(QStringLiteral("unconfirmed")));
		controller.pump();
		controller.disconnectDevice();
		CHECK(finished.count() == 1 && controller.writeOutcome() == outcome);
		const char* names[]{"queued", "offered", "prepared", "executed", "write_confirmed"};
		std::printf("Inspector Apply disconnect phase=%s finished=%lld writes=%llu\n", names[phase],
		            static_cast<long long>(finished.count()),
		            static_cast<unsigned long long>(writes));
	}
	{
		ui::InspectorController controller;
		draft(controller);
		QSignalSpy finished{&controller, &ui::InspectorController::applyFinished};
		bool disconnected = false;
		QObject::connect(&controller, &ui::InspectorController::stateChanged, &controller, [&] {
			if (!disconnected && !controller.busy() &&
			    controller.writeOutcome().startsWith(QStringLiteral("Applied"))) {
				disconnected = true;
				controller.disconnectDevice();
			}
		});
		CHECK(controller.apply());
		controller.pump(); // WRITE acknowledged; Refresh queued/offered, not sent.
		CHECK(controller.busy() && finished.isEmpty());
		controller.pump(); // READ completes; stateChanged handler disconnects reentrantly.
		CHECK(disconnected && !controller.connected() && !controller.busy());
		CHECK(finished.count() == 1 && finished.first()[0].toBool());
		controller.pump();
		CHECK(finished.count() == 1);
	}
	{
		ui::InspectorController controller;
		draft(controller);
		QSignalSpy finished{&controller, &ui::InspectorController::applyFinished};
		bool disconnected = false;
		QObject::connect(&controller, &ui::InspectorController::stateChanged, &controller, [&] {
			if (!disconnected && controller.busy() &&
			    controller.writeOutcome().contains(QStringLiteral("Pending"))) {
				disconnected = true;
				controller.disconnectDevice();
			}
		});
		CHECK(controller.apply()); // Its synchronous notification retires a queued Write.
		CHECK(disconnected && !controller.busy() && !controller.connected());
		CHECK(finished.count() == 1 && !finished.first()[0].toBool());
		CHECK(controller.writeOutcome().contains(QStringLiteral("Cancelled")));
		controller.pump();
		CHECK(finished.count() == 1);
	}
}

void writeHistoryChecks()
{
	ui::InspectorController controller;
	auto& endpoints = *qobject_cast<ui::EndpointModel*>(controller.endpoints());
	auto& values = *qobject_cast<ui::ValueModel*>(controller.values());
	QSignalSpy finished{&controller, &ui::InspectorController::applyFinished};
	const auto draft = [&] {
		CHECK(controller.selectEndpoint(endpoint(endpoints, 1, 0)) && controller.refresh());
		CHECK(await([&] {
			return !controller.busy();
		}));
		++reads;
		CHECK(controller.beginEdit());
		CHECK(controller.setScalarText(named(values, values.index(0, 0), QStringLiteral("Gain")),
		                               QStringLiteral("9")));
	};
	const auto history = [&](quint32 origin, const QString& raw) {
		CHECK(controller.inspector().session() != origin);
		CHECK(controller.writeOutcome() ==
		      QStringLiteral("Previous session %1 (current session %2) · %3")
		          .arg(origin)
		          .arg(controller.inspector().session())
		          .arg(raw));
		CHECK(controller.writeOutcome().count(QStringLiteral("Previous session")) == 1);
	};
	CHECK(controller.connectWritableDemo());
	draft();
	CHECK(controller.apply());
	controller.pump();
	controller.pump();
	CHECK(finished.count() == 1 && finished.first()[0].toBool() && !controller.busy());
	const auto originalSession = controller.inspector().session();
	const auto confirmed = controller.writeOutcome();
	CHECK(confirmed.startsWith(QStringLiteral("Applied")) &&
	      !confirmed.contains(QStringLiteral("Previous session")));
	CHECK(controller.connectDemo());
	history(originalSession, confirmed);
	CHECK(controller.connectWritableDemo());
	history(originalSession, confirmed); // Repeated replacement cannot nest prefixes.
	const auto previous = controller.writeOutcome();
	CHECK(!controller.apply()); // Refused submission must not erase the previous result.
	CHECK(controller.writeOutcome() == previous && finished.count() == 1);
	draft();
	CHECK(controller.apply());
	CHECK(controller.writeOutcome().startsWith(QStringLiteral("Pending")) &&
	      !controller.writeOutcome().contains(QStringLiteral("Previous session")));
	const auto cancelledSession = controller.inspector().session();
	controller.disconnectDevice();
	CHECK(finished.count() == 2 && !finished.last()[0].toBool());
	const auto cancelled = controller.writeOutcome();
	CHECK(cancelled.contains(QStringLiteral("Cancelled")));
	CHECK(controller.connectDemo());
	history(cancelledSession, cancelled);
	CHECK(controller.connectWritableDemo());
	draft();
	CHECK(controller.apply());
	const auto packet = controller.inspector().nextPacket();
	CHECK(packet && controller.inspector().prepareHandoff(packet.session, packet.requestId));
	const auto unknownSession = controller.inspector().session();
	controller.disconnectDevice();
	CHECK(finished.count() == 3 && !finished.last()[0].toBool());
	const auto unknown = controller.writeOutcome();
	CHECK(unknown.contains(QStringLiteral("OutcomeUnknown")) &&
	      !unknown.contains(QStringLiteral("Previous session")));
	CHECK(controller.connectDemo());
	history(unknownSession, unknown);
	CHECK(finished.count() == 3 && !controller.busy());
	std::puts("Inspector write history: success=1 unknown=1 reset=1 refused=1 callbacks=3");
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication application{argc, argv};
	modelChecks();
	endpointInvalidationChecks();
	controllerChecks();
	workbenchChecks();
	disconnectApplyChecks();
	writeHistoryChecks();
	std::printf("Inspector Qt checks=%u reads=%u Qt=%s\n", checks, reads, qVersion());
	return 0;
}
