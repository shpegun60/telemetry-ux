// Real endpoint/value models over parsed metadata and the native Encoded client. MIT.
#include <Telemetry/UX/EndpointPreviews.hpp>
#include <Telemetry/UX/WatchlistModel.hpp>
#include <Telemetry/UX/DynamicWatchlist.hpp>
#include <Telemetry/UX/WatchKey.hpp>
#include <Telemetry/UX/WatchLeaves.hpp>
#include "../dynamic_qml_configurator/Fixture.hpp"
#include <integration/read_many/Workbench.hpp>
#include <QtCore/QPersistentModelIndex>
#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>
#include <QtTest/QAbstractItemModelTester>
#include <QtTest/QSignalSpy>
#include <charconv>
#include <functional>
#include <utility>

namespace ux = telemetry::ux;
namespace f = dynamic_qml_fixture;
namespace cl = telemetry::client;
namespace rt = telemetry::runtime;
namespace en = telemetry::integration::encoded;
namespace ins = telemetry::integration::inspector;

namespace local {
enum class Small : std::int8_t { Negative = -1, Positive = 2 };
struct Device {
	bool flag() const noexcept { return true; }
	Small mode() const noexcept { return Small::Negative; }
	std::array<f::Empty, 4097> empty() const noexcept { return {}; }
};
inline Device device;
inline constexpr telemetry::FieldTable fields{
    telemetry::field<&Device::flag>("Flag", device),
    telemetry::field<&Device::mode>("Mode", device),
    telemetry::field<&Device::empty>("ManyEmpty", device)};
inline constexpr telemetry::FieldCatalogTable catalogs{telemetry::group("local", fields)};
inline constexpr telemetry::ServiceCatalogTable<> services{};
inline constexpr telemetry::Model model{catalogs, telemetry::emptyCommands, services};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();

struct Backing {
	std::array<std::byte, descriptorBytes.size()> bytes = descriptorBytes;
	std::array<rt::RuntimeType, 64> types{};
	std::array<rt::RuntimeMember, 16> members{};
	std::array<rt::RuntimeEnumEntry, 16> enums{};
	std::array<rt::RuntimeCatalog, 8> groups{};
	std::array<rt::RuntimeEndpoint, 16> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, groups, endpoints};
	rt::RuntimeSchema schema;
	std::uint32_t now = 0;
	cl::Clock clock{this, +[](void* self) noexcept { return static_cast<Backing*>(self)->now; }};
	std::array<std::byte, 16384> tx{}, rx{}, reply{}, scratch{};
	telemetry::Workspace dispatchWorkspace{scratch};
	en::EncodedBackend backend{tx, rx, clock};
	ux::Inspector inspector{backend, clock};
	unsigned packets = 0;
	Backing() {
		const auto parsed = rt::RuntimeSchema::parse(bytes, workspace);
		CHECK(parsed);
		schema = parsed.schema;
		CHECK(inspector.admit(schema, cl::Input{bytes}.first(64), 17) == cl::Status::Ok);
	}
	~Backing() {
		inspector.close(); inspector.process(now); CHECK(inspector.drained());
	}
	void read(quint32 id) {
		const auto binding = inspector.bindField(id); CHECK(binding);
		unsigned completed = 0;
		CHECK(inspector.readField(binding.endpoint, [&](const auto& result) noexcept {
			CHECK(result.operation.succeeded() && result.published); ++completed;
		}));
		inspector.process(now);
		const auto packet = inspector.nextPacket(); CHECK(packet);
		CHECK(inspector.prepareHandoff(packet.session, packet.requestId));
		CHECK(inspector.onSendAccepted(packet.session, packet.requestId));
		const auto result = en::dispatchEncoded(model.view(), dispatchWorkspace, packet.session,
		                                       packet.bytes, reply);
		CHECK(result && inspector.onReply(cl::Input{reply}.first(result.written),
		                                   packet.session, packet.requestId));
		inspector.process(now); CHECK(completed == 1); ++packets;
	}
};
} // namespace local

namespace {
const char* currentScenario = "startup";
QModelIndex endpoint(ux::EndpointModel& model, int category, quint32 id,
                     const QModelIndex& parent = {}) {
	for (int n = 0; n < model.rowCount(parent); ++n) {
		const auto index = model.index(n, 0, parent);
		const auto item = model.endpoint(index);
		if (item.value("category").toInt() == category && item.value("id").toUInt() == id && !item.isEmpty())
			return index;
		if (const auto nested = endpoint(model, category, id, index); nested.isValid()) return nested;
	}
	return {};
}
QModelIndex child(ux::EndpointModel& model, const QModelIndex& parent, const QString& name) {
	while (model.canFetchMore(parent)) model.fetchMore(parent);
	for (int n = 0; n < model.rowCount(parent); ++n) {
		const auto index = model.index(n, 0, parent);
		if (model.data(index, ux::EndpointModel::NameRole).toString() == name) return index;
	}
	return {};
}
template<class T> QByteArray encode(const T& value) {
	QByteArray bytes(qsizetype(telemetry::wireSize<T>), '\0');
	CHECK(telemetry::encode(value, {reinterpret_cast<std::byte*>(bytes.data()), std::size_t(bytes.size())}) ==
	      telemetry::CodecStatus::Ok);
	return bytes;
}
struct Source {
	rt::RuntimeSchema schema;
	qint64 now = 0;
	QList<quint32> calls;
	ux::EndpointPreviewSource::Completion done;
	ux::EndpointPreviewSource source() {
		ux::EndpointPreviewSource source;
		source.current = [this] { return schema.valid(); };
		source.schema = [this] { return schema; };
		source.session = [] { return quint32{17}; };
		source.busy = [this] { return bool(done); };
		source.read = [this](quint32 id, auto completion) {
			CHECK(!done); calls.append(id); done = std::move(completion);
			return cl::Submission{cl::Status::Ok, {}};
		};
		return source;
	}
	void finish(QByteArray bytes) {
		CHECK(done); *done = ux::EndpointPreviewResult{cl::Status::Ok, true, std::move(bytes), {}};
		done.reset();
	}
};

void metadataAndLocalChildren() {
	auto storage = std::make_shared<f::Backing>();
	Source source; source.schema = storage->schema;
	ux::EndpointPreviews previews(nullptr, [&] { return source.now; });
	previews.setSource(source.source());
	ux::EndpointModel model;
	QAbstractItemModelTester tester{&model, QAbstractItemModelTester::FailureReportingMode::Fatal};
	tester.setUseFetchMore(false);
	model.setPreviews(&previews); model.rebuild(storage->owner);
	const auto config = endpoint(model, 1, 0);
	const auto command = endpoint(model, 2, 0);
	const auto service = endpoint(model, 3, 0);
	CHECK(config.isValid() && command.isValid() && service.isValid());
	// One tree of groups: every top row is a group, and each endpoint's parent.
	for (int row = 0; row < model.rowCount(); ++row)
		CHECK(model.isGroup(model.index(row, 0)));
	CHECK(model.isGroup(model.parent(config)) && model.isGroup(model.parent(command)) &&
	      model.isGroup(model.parent(service)) && !model.parent(model.parent(config)).isValid());
	CHECK(!model.isGroup({}) && !model.isGroup(config) && !model.isGroup(command) && !model.isGroup(service));
	ux::EndpointModel foreign;
	foreign.rebuild(storage->owner);
	CHECK(!model.isGroup(foreign.index(0, 0)));
	CHECK(model.data(config, ux::EndpointModel::TypeTextRole).toString() == "struct (7)");
	CHECK(model.data(config, ux::EndpointModel::WireBytesRole).toUInt() == telemetry::wireSize<f::Config>);
	CHECK(model.data(config, ux::EndpointModel::AccessTextRole).toString() == "Read / Write");
	// A Command reads as its arguments: none here.
	CHECK(model.data(command, ux::EndpointModel::TypeTextRole).toString() == "()");
	CHECK(model.data(command, ux::EndpointModel::AccessTextRole).toString() == "Execute");
	CHECK(model.data(service, ux::EndpointModel::ResponseTypeTextRole).toString() == "struct (7)");
	// The row in words, its signature included, never an internal id.
	CHECK(model.data(service, ux::EndpointModel::DetailRole).toString().startsWith("Service") &&
	      model.data(service, ux::EndpointModel::DetailRole).toString().contains(" -> ") &&
	      !model.data(service, ux::EndpointModel::DetailRole).toString().contains("#"));
	CHECK(model.endpoint(service).value("responseWireBytes").toUInt() == telemetry::wireSize<f::Config>);
	CHECK(model.previewDemandField(config).isValid() && model.previewDemandField(config).toUInt() == 0);
	CHECK(!model.previewDemandField(command).isValid() && !model.previewDemandField(service).isValid());
	CHECK(model.hasChildren(config) && model.rowCount(config) == 0);
	const auto serial = child(model, config, QStringLiteral("serial"));
	const auto samples = child(model, config, QStringLiteral("samples"));
	CHECK(serial.isValid() && samples.isValid() && model.rowCount(config) == 7);
	CHECK(!model.isGroup(serial) && !model.isGroup(samples));
	CHECK(model.data(serial, ux::EndpointModel::TypeTextRole).toString() == "u64");
	CHECK(model.data(serial, ux::EndpointModel::PreviewChildRole).toBool());
	CHECK(model.endpoint(serial).isEmpty() && !model.data(serial, ux::EndpointModel::IsEndpointRole).toBool());
	// A value reads and writes as its Field (the owner, 20 November 2026):
	// the Device tree marks rw on every row of a Field.
	CHECK(model.data(serial, ux::EndpointModel::ReadableRole).toBool() &&
	      model.data(serial, ux::EndpointModel::WritableRole).toBool());
	CHECK(model.data(serial, ux::EndpointModel::KindRole).toString() == "value" &&
	      model.data(samples, ux::EndpointModel::KindRole).toString() == "array" &&
	      model.data(config, ux::EndpointModel::KindRole).toString() == "struct");
	CHECK(model.previewDemandField(serial).isValid() && model.previewDemandField(serial).toUInt() == 0);
	const auto firstRange = child(model, samples, QStringLiteral("[0 … 255]"));
	const auto secondRange = child(model, samples, QStringLiteral("[256 … 299]"));
	CHECK(firstRange.isValid() && secondRange.isValid() && model.rowCount(samples) == 2);
	CHECK(!model.isGroup(firstRange) && !model.isGroup(secondRange));
	model.fetchMore(firstRange); CHECK(model.rowCount(firstRange) == 128 && model.canFetchMore(firstRange));
	model.fetchMore(firstRange); CHECK(model.rowCount(firstRange) == 256 && !model.canFetchMore(firstRange));
	const auto last = child(model, secondRange, QStringLiteral("[299]")); CHECK(last.isValid());
	CHECK(source.calls.isEmpty() && storage->packets == 0 && storage->owner.pending() == 0);
	CHECK(model.previewOnlyChange({ux::EndpointModel::PreviewValueRole, ux::EndpointModel::PreviewAgeMsRole}));
	CHECK(!model.previewOnlyChange({}) && !model.previewOnlyChange({ux::EndpointModel::NameRole}));

	f::Config value; value.samples[299] = 42;
	QObject visibleView;
	CHECK(previews.setDemand(&visibleView, {0}));
	QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
	QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
	QPersistentModelIndex kept{last};
	previews.poll(); CHECK(source.calls == QList<quint32>{0});
	QByteArray bytes = encode(value); source.finish(bytes); previews.poll(); bytes.fill(char(0x5a));
	CHECK(model.data(serial, ux::EndpointModel::PreviewValueRole).toString() == "18446744073709551615");
	CHECK(model.data(last, ux::EndpointModel::PreviewValueRole).toString() == "42");
	CHECK(model.data(last, ux::EndpointModel::PreviewStateRole).toString() == "Current");
	CHECK(kept.isValid() && resets.isEmpty() && !changes.isEmpty());
	for (const auto& change : changes) CHECK(model.previewOnlyChange(change[2].value<QList<int>>()));
	source.now = 1000; previews.poll();
	CHECK(model.data(last, ux::EndpointModel::PreviewAgeMsRole).toLongLong() == 1000);
	CHECK(source.calls.size() == 1 && storage->packets == 0);
	previews.clearDemand(&visibleView); source.now = 10000; previews.poll();
	CHECK(source.calls.size() == 1);
	previews.invalidateValues();
	CHECK(model.data(last, ux::EndpointModel::PreviewValueRole).toString().isEmpty() && kept.isValid());
	// Source publication can precede a provider rebuild; metadata is reapplied.
	model.rebuild(storage->owner);
	CHECK(!kept.isValid());
	CHECK(model.data(endpoint(model, 1, 1), ux::EndpointModel::TypeTextRole).toString() == "u64");
	const auto old = endpoint(model, 1, 0);
	storage->owner.invalidate();
	for (auto role : model.roleNames().keys()) CHECK(!model.data(old, role).isValid());
	CHECK(!model.previewDemandField(old).isValid() && model.endpoint(old).isEmpty());
	CHECK(!model.isGroup(model.index(0, 0)) && !model.isGroup(model.parent(old)));
}

std::vector<std::byte> draftAfterPreview(bool preview) {
	f::Harness h;
	f::Provider provider{h.binding.get()}; provider.setAllowMutations(true);
	f::select(provider, "Config"); CHECK(provider.refresh()); h.pump(); CHECK(provider.beginEdit());
	CHECK(f::edit(provider, "gain", "4.25"));
	const auto selected = provider.selectedEndpointIndex();
	const auto packets = h.storage->packets;
	if (preview) {
		ux::EndpointModel model; Source source; source.schema = h.storage->schema;
		ux::EndpointPreviews previews(nullptr, [&] { return source.now; });
		model.setPreviews(&previews); previews.setSource(source.source()); model.rebuild(h.storage->owner);
		const auto field = endpoint(model, 1, 0);
		CHECK(child(model, field, QStringLiteral("gain")).isValid());
		const auto binding = h.storage->owner.bindField(1); CHECK(binding);
		unsigned completed = 0;
		CHECK(telemetry::integration::read_many::readFieldValue(h.storage->owner, binding.endpoint,
		    [&](const auto& result) noexcept { CHECK(result.operation.succeeded() && result.hasValue()); ++completed; }));
		h.storage->execute(h.storage->offered()); h.pump(); CHECK(completed == 1);
		CHECK(provider.selectedEndpointIndex() == selected && provider.editing());
		CHECK(f::scalar(provider.values(), "gain") == "4.25");
		CHECK(h.storage->packets == packets + 1);
	}
	CHECK(provider.apply()); const auto packet = h.storage->offered();
	// Ignore request-header sequence; compare the full actual canonical Write DTO.
	const std::vector<std::byte> payload(packet.bytes.begin() + 32, packet.bytes.end());
	h.storage->execute(packet); h.pump();
	CHECK(f::device.config.gain == 4.25f && f::device.writes == 1);
	return payload;
}

void editorMetadataAndPrecision() {
	f::Harness h; f::Provider provider{h.binding.get()}; provider.setAllowMutations(true);
	auto* values = qobject_cast<ux::ValueModel*>(provider.values()); CHECK(values);
	values->setSchemaMetadata(h.storage->schema); // supplied before the first snapshot.
	f::select(provider, "Config"); CHECK(provider.refresh()); h.pump(); CHECK(provider.beginEdit());
	const auto mode = f::member(values, "mode"), gain = f::member(values, "gain");
	CHECK(values->data(mode, ux::ValueModel::EditorKindRole).toString() == "enum");
	const auto choices = values->data(mode, ux::ValueModel::EditorChoicesRole).toList();
	CHECK(choices.size() == 2);
	CHECK(choices[0].toMap().value("label") == "Reverse (-7)" && choices[0].toMap().value("text") == "-7");
	CHECK(choices[1].toMap().value("label") == "Forward (12)" && choices[1].toMap().value("text") == "12");
	CHECK(values->data(mode, ux::ValueModel::EditorTextRole) == "-123");
	CHECK(!values->data(mode, ux::ValueModel::EditorChoicesTruncatedRole).toBool());
	CHECK(values->data(gain, ux::ValueModel::TypeRole) == "f32" &&
	      values->data(gain, ux::ValueModel::TypeTextRole) == "f32");
	CHECK(f::edit(provider, "gain", "0.1"));
	CHECK(values->data(gain, ux::ValueModel::CanonicalTextRole) == "0.100000001");
	CHECK(f::edit(provider, "precise", "0.1"));
	CHECK(values->data(f::member(values, "precise"), ux::ValueModel::CanonicalTextRole) == "0.10000000000000001");
	CHECK(values->data(f::member(values, "serial"), ux::ValueModel::CanonicalTextRole) == "18446744073709551615");
	CHECK(values->data(f::member(values, "offset"), ux::ValueModel::CanonicalTextRole) == "-9223372036854775808");
	const auto packets = h.storage->packets;
	CHECK(!f::edit(provider, "gain", "1e999") && !f::edit(provider, "gain", "1e-999"));
	CHECK(!f::edit(provider, "serial", "18446744073709551616"));
	CHECK(values->data(gain, ux::ValueModel::CanonicalTextRole) == "0.100000001");
	CHECK(h.storage->packets == packets && f::device.writes == 0);
	CHECK(f::edit(provider, "gain", "1e-30") && f::edit(provider, "gain", "3e30"));
	CHECK(f::edit(provider, "mode", "-1")); // an unnamed code is not a closed enum choice set.
	CHECK(values->data(mode, ux::ValueModel::CanonicalTextRole) == "-1");
}

void definitionsBeforeRead() {
	f::Harness h;
	f::Provider provider{h.binding.get()};
	auto* endpoints = qobject_cast<ux::EndpointModel*>(provider.endpoints()); CHECK(endpoints);
	Source source; source.schema = h.storage->schema;
	ux::EndpointPreviews previews(nullptr, [&] { return source.now; });
	previews.setSource(source.source()); endpoints->setPreviews(&previews);
	auto* definitions = provider.definitions(); CHECK(definitions);
	QAbstractItemModelTester tester{definitions, QAbstractItemModelTester::FailureReportingMode::Fatal};
	tester.setUseFetchMore(false);
	f::select(provider, "Config");
	const auto info = provider.selectionInfo();
	CHECK(info.value("id").toUInt() == 0 && info.value("typeText") == "struct (7)");
	CHECK(info.value("wireBytes").toUInt() == telemetry::wireSize<f::Config>);
	const auto root = provider.definitionRootIndex();
	CHECK(root.isValid() && definitions->rowCount(root) == 1);
	const auto field = definitions->index(0, 0, root);
	CHECK(definitions->data(field, ux::EndpointModel::NameRole) == "Config");
	CHECK(definitions->canFetchMore(field)); definitions->fetchMore(field);
	CHECK(definitions->rowCount(field) == 7);
	bool gain = false, serial = false;
	for (int n = 0; n < definitions->rowCount(field); ++n) {
		const auto index = definitions->index(n, 0, field);
		const auto name = definitions->data(index, ux::EndpointModel::NameRole).toString();
		if (name == "gain") { gain = true; CHECK(definitions->data(index, ux::EndpointModel::TypeTextRole) == "f32"); }
		if (name == "serial") { serial = true; CHECK(definitions->data(index, ux::EndpointModel::TypeTextRole) == "u64"); }
		CHECK(definitions->data(index, ux::EndpointModel::PreviewValueRole).toString().isEmpty());
		CHECK(!definitions->data(index, ux::EndpointModel::IsEndpointRole).toBool());
	}
	CHECK(gain && serial && provider.values()->rowCount() == 0 && !provider.editing());
	f::select(provider, "Serial");
	const auto scalarRoot = provider.definitionRootIndex();
	CHECK(provider.selectionInfo().value("typeText") == "u64");
	CHECK(definitions->rowCount(scalarRoot) == 1);
	CHECK(!definitions->hasChildren(definitions->index(0, 0, scalarRoot)));
	f::select(provider, "Reset");
	CHECK(provider.selectionInfo().value("typeText") == "()");
	f::select(provider, "Inspect");
	CHECK(provider.selectionInfo().value("responseTypeText") == "struct (7)");
	CHECK(h.storage->packets == 0 && h.storage->owner.pending() == 0 && source.calls.isEmpty());
	CHECK(f::device.reads == 0 && f::device.writes == 0 && f::device.commands == 0 && f::device.services == 0);
}

void smallSignedBooleanAndExpansionBound() {
	auto storage = std::make_unique<local::Backing>();
	ux::WatchlistModel watched;
	bool current = true;
	watched.bind([&] { return current; });
	watched.setSchemaMetadata(storage->schema);
	CHECK(watched.add(0, "Flag") && watched.add(1, "Mode"));
	const auto watchedFlag = watched.index(0, 0), watchedMode = watched.index(1, 0);
	CHECK(watched.data(watchedFlag, ux::WatchlistModel::EditorKindRole) == "bool" &&
	      watched.data(watchedMode, ux::WatchlistModel::EditorKindRole) == "enum");
	CHECK(watched.data(watchedFlag, ux::WatchlistModel::CategoricalRole).toBool() &&
	      watched.data(watchedMode, ux::WatchlistModel::CategoricalRole).toBool());
	CHECK(watched.data(watchedMode, ux::WatchlistModel::EditorTextRole).toString().isEmpty() &&
	      storage->packets == 0); // Metadata/choices never require a getter.
	const auto* booleanField = storage->schema.findField("local", "Flag"); CHECK(booleanField);
	for (const auto flag : {false, true}) {
		const auto bytes = encode(flag);
		const auto value = storage->schema.view(booleanField->typeId,
		    {reinterpret_cast<const std::byte*>(bytes.constData()), std::size_t(bytes.size())});
		CHECK(value && ux::WatchlistModel::formatValue(value.value) == (flag ? "true (1)" : "false (0)"));
	}
	ux::ValueModel values; values.setSchemaMetadata(storage->schema);
	storage->read(0); values.resetSnapshot(storage->inspector);
	const auto flag = values.index(0, 0);
	CHECK(values.data(flag, ux::ValueModel::EditorKindRole) == "bool");
	const auto booleanChoices = values.data(flag, ux::ValueModel::EditorChoicesRole).toList();
	CHECK(booleanChoices.size() == 2 && booleanChoices[0].toMap().value("label") == "false (0)" &&
	      booleanChoices[0].toMap().value("text") == "false" &&
	      booleanChoices[1].toMap().value("label") == "true (1)" &&
	      booleanChoices[1].toMap().value("text") == "true");
	storage->read(1); values.resetSnapshot(storage->inspector);
	const auto choices = values.data(values.index(0, 0), ux::ValueModel::EditorChoicesRole).toList();
	CHECK(choices.size() == 2 && choices[0].toMap().value("label") == "Negative (-1)" &&
	      choices[0].toMap().value("text") == "-1");
	CHECK(watched.data(watchedMode, ux::WatchlistModel::EditorChoicesRole).toList() == choices);
	CHECK(watched.data(watchedFlag, ux::WatchlistModel::EditorChoicesRole).toList() == booleanChoices);
	// The row's own code first when no choice is it, said as the row says it,
	// and every code in hex when the row shows hex (WL-8).
	{
		ux::WatchlistModel own;
		own.bind([] { return true; });
		own.setSchemaMetadata(storage->schema);
		CHECK(own.add(1, "Mode"));
		const auto row = own.index(0, 0);
		const auto* modeField = storage->schema.findField("local", "Mode"); CHECK(modeField);
		ux::WatchValueBits five;
		five.type = {modeField->typeId, ux::WatchScalar::S8, 1, true};
		const std::array<std::byte, 1> code{std::byte{5}};
		CHECK(five.assign(code.data(), 1));
		CHECK(own.setResult(0, "Ok", "Unknown (5)", ux::WatchSample::ofSigned(5), five));
		auto mine = own.data(row, ux::WatchlistModel::EditorChoicesRole).toList();
		CHECK(mine.size() == 3 && mine[0].toMap().value("label") == "Unknown (5)" && mine[0].toMap().value("text") == "5" &&
		      mine[1].toMap().value("label") == "Negative (-1)");
		CHECK(own.data(row, ux::WatchlistModel::ShownTextRole) == "Unknown (5)");
		CHECK(own.setRowFormat(0, ux::WatchNumberFormat::Hex));
		mine = own.data(row, ux::WatchlistModel::EditorChoicesRole).toList();
		CHECK(mine.size() == 3 && mine[0].toMap().value("label") == "Unknown (0x05)" && mine[0].toMap().value("text") == "5" &&
		      mine[1].toMap().value("label") == "Negative (0xFF)" && mine[1].toMap().value("text") == "-1" &&
		      mine[2].toMap().value("label") == "Positive (0x02)");
		CHECK(own.setRowFormat(0, std::nullopt));
		const std::array<std::byte, 1> negative{std::byte{0xff}};
		CHECK(five.assign(negative.data(), 1));
		CHECK(own.setResult(0, "Ok", "Negative (-1)", ux::WatchSample::ofSigned(-1), five));
		CHECK(own.data(row, ux::WatchlistModel::EditorChoicesRole).toList() == choices);
	}
	// Both editors own the copied descriptor labels; none retain an enum-name borrow.
	storage->enums[0].name = "Replacement";
	CHECK(watched.data(watchedMode, ux::WatchlistModel::EditorChoicesRole).toList() == choices &&
	      values.data(values.index(0, 0), ux::ValueModel::EditorChoicesRole).toList() == choices);
	current = false;
	CHECK(!watched.data(watchedMode, ux::WatchlistModel::EditorChoicesRole).isValid());
	auto rebound = std::make_unique<local::Backing>();
	rebound->enums[0].name = "New session name";
	watched.bind([&] { return rebound->schema.valid(); }); // same Field ID, different admission storage.
	CHECK(watched.data(watchedMode, ux::WatchlistModel::EditorChoicesRole).toList().isEmpty() &&
	      watched.data(watchedMode, ux::WatchlistModel::TypeTextRole).toString().isEmpty());
	Source source; source.schema = storage->schema;
	ux::EndpointPreviews previews(nullptr, [&] { return source.now; }); previews.setSource(source.source());
	ux::EndpointModel model; model.setPreviews(&previews); model.rebuild(storage->inspector);
	QAbstractItemModelTester tester{&model, QAbstractItemModelTester::FailureReportingMode::Fatal};
	tester.setUseFetchMore(false);
	const auto field = endpoint(model, 1, 2);
	while (model.canFetchMore(field)) model.fetchMore(field);
	CHECK(model.rowCount(field) == 17);
	int descendants = model.rowCount(field); bool limited = false;
	for (int n = 0; n < model.rowCount(field); ++n) {
		const auto range = model.index(n, 0, field);
		while (model.canFetchMore(range)) model.fetchMore(range);
		descendants += model.rowCount(range);
		if (model.data(range, ux::EndpointModel::PreviewStateRole) == "Open Details") {
			limited = true; CHECK(!model.data(range, ux::EndpointModel::PreviewReasonRole).toString().isEmpty());
		}
	}
	CHECK(descendants == 4096 && limited && source.calls.isEmpty() && storage->packets == 2);
}

void publicationLifetimeAndWrongSource() {
	auto storage = std::make_shared<f::Backing>(); Source source; source.schema = storage->schema;
	ux::EndpointPreviews previews(nullptr, [&] { return source.now; }); previews.setSource(source.source());
	{
		auto model = std::make_unique<ux::EndpointModel>(); model->setPreviews(&previews); model->rebuild(storage->owner);
		const auto config = endpoint(*model, 1, 0);
		QObject::connect(model.get(), &QAbstractItemModel::rowsInserted, &previews, [&] { model.reset(); });
		model->fetchMore(config); CHECK(!model);
	}
	{
		auto model = std::make_unique<ux::EndpointModel>(); model->setPreviews(&previews); model->rebuild(storage->owner);
		QObject::connect(model.get(), &QAbstractItemModel::dataChanged, &previews, [&] { model.reset(); });
		previews.invalidateValues(); CHECK(!model);
	}
	ux::EndpointModel model; model.setPreviews(&previews); model.rebuild(storage->owner);
	const auto config = endpoint(model, 1, 0);
	auto unrelated = std::make_shared<f::Backing>(18);
	source.schema = unrelated->schema; previews.setSource(source.source());
	CHECK(!model.previewDemandField(config).isValid());
	CHECK(model.data(config, ux::EndpointModel::PreviewStateRole) == "Stale");
	CHECK(model.data(config, ux::EndpointModel::PreviewValueRole).toString().isEmpty());
	CHECK(!model.canFetchMore(config) && source.calls.isEmpty());
	model.rebuild(unrelated->owner);
	CHECK(model.previewDemandField(endpoint(model, 1, 0)).isValid());
}

void schemaSourceReplacement() {
	// Both descriptors have a scalar Field 1; their declared types differ:
	// the local source has an enum, the other source a u64. No device I/O.
	auto first = std::make_unique<local::Backing>();
	auto replacement = std::make_shared<f::Backing>();
	const auto source = [](const rt::RuntimeSchema& schema) {
		ux::WatchlistSource result;
		result.current = [schema] { return schema.valid(); };
		result.fieldBytes = [](quint32 id) -> std::optional<unsigned> {
			return id == 1 ? std::optional<unsigned>{8} : std::nullopt;
		};
		result.schema = [schema] { return schema; };
		return result;
	};
	QObject context;
	ux::DynamicWatchlist watch(&context);
	const auto next = source(replacement->schema);
	auto original = source(first->schema);
	original.schema = [&] {
		watch.setSource(next);
		return first->schema;
	};
	watch.setSource(std::move(original));
	CHECK(watch.add(1, "Scalar"));
	CHECK(watch.model()->index(0, 0).data(ux::WatchlistModel::EditorKindRole) == "text" &&
	      watch.model()->index(0, 0).data(ux::WatchlistModel::TypeTextRole) == "u64");
	bool replaceOnClear = false;
	original = source(first->schema);
	original.schema = [&] {
		if (replaceOnClear) watch.setSource(next);
		return first->schema;
	};
	watch.setSource(std::move(original));
	// The first source's row is kept, without a device (WL-4c); this check is
	// about the metadata of the row added next, alone.
	CHECK(watch.count() == 1);
	watch.clear();
	CHECK(watch.add(1, "Mode"));
	CHECK(watch.model()->index(0, 0).data(ux::WatchlistModel::EditorKindRole) == "enum");
	replaceOnClear = true;
	watch.clear();
	CHECK(watch.count() == 0 && watch.add(1, "Scalar"));
	CHECK(watch.model()->index(0, 0).data(ux::WatchlistModel::EditorKindRole) == "text" &&
	      watch.model()->index(0, 0).data(ux::WatchlistModel::TypeTextRole) == "u64");
	CHECK(first->packets == 0 && replacement->packets == 0);
}
} // namespace

// A Field's values numbered as the Watchlist keys them, and the tree's rows
// carrying their path and leaf range: device.Config holds 306 leaves, serial
// first and samples[299] last.
void watchLeavesAndPaths() {
	static_assert(ux::watch_key::whole(7) == 7 && ux::watch_key::isWhole(7));
	static_assert(ux::watch_key::field(ux::watch_key::member(7, 0)) == 7 &&
	              ux::watch_key::ordinal(ux::watch_key::member(7, 262142)) == 262142 &&
	              !ux::watch_key::isWhole(ux::watch_key::member(7, 0)));
	static_assert(ux::watch_key::member(0xffffffffu, 262142) < (quint64{1} << 51));
	auto storage = std::make_shared<f::Backing>();
	const auto& schema = storage->schema;
	ux::WatchLeaves leaves;
	CHECK(leaves.count(0) == 0 && !leaves.locate(0, 0) && !leaves.matches(schema));
	leaves.reset(schema);
	CHECK(leaves.matches(schema));
	const auto fields = schema.endpoints(telemetry::descriptor::Category::Field);
	const auto services = schema.endpoints(telemetry::descriptor::Category::Service);
	CHECK(fields.size() == 2 && !services.empty());
	const auto config = fields[0].typeId, serial = fields[1].typeId;
	const auto members = schema.members(config);
	CHECK(members.size() == 7);
	const auto samples = members[6].typeId;
	CHECK(leaves.count(config) == 306 && leaves.count(serial) == 1 && leaves.count(samples) == 300);
	CHECK(leaves.kind(config) == ux::WatchLeaves::Kind::Values);
	CHECK(leaves.kind(samples) == ux::WatchLeaves::Kind::Elements);
	CHECK(leaves.kind(services[0].typeId) == ux::WatchLeaves::Kind::Members);
	const auto gain = leaves.locate(config, 2);
	CHECK(gain && gain->suffix == ".gain" && gain->member() == "gain" && gain->depth == 1 &&
	      !gain->steps[0].element && gain->steps[0].index == 2 && gain->offset == members[2].wireOffset);
	const auto sample = leaves.locate(config, 300);
	CHECK(sample && sample->suffix == ".samples[294]" && sample->member() == "samples[294]" &&
	      sample->depth == 2 && sample->steps[1].element && sample->steps[1].index == 294 &&
	      sample->offset == members[6].wireOffset + 294 * 2 && sample->type == schema.type(samples)->relatedTypeId);
	const auto whole = leaves.locate(serial, 0);
	CHECK(whole && whole->suffix.isEmpty() && whole->depth == 0 && whole->offset == 0);
	CHECK(!leaves.locate(config, 306) && !leaves.locate(serial, 1));
	CHECK(leaves.ordinal(config, u"mode") == 5u && leaves.ordinal(config, u"samples[3]") == 9u);
	CHECK(leaves.ordinal(config, u"samples[299]") == 305u && leaves.ordinal(samples, u"[7]") == 7u);
	for (const auto bad : {u"samples", u"samples[300]", u".mode", u"nope", u"mode[0]", u"samples[3", u"samples[]",
	                       u"samples[3].x", u"gain.", u"samples.[3]", u"[0]"})
		CHECK(!leaves.ordinal(config, bad));
	bool roundTrip = true;
	for (quint32 n = 0; n < 306; ++n) {
		const auto leaf = leaves.locate(config, n);
		roundTrip = roundTrip && leaf && leaves.ordinal(config, leaf->member()) == n;
	}
	CHECK(roundTrip);
	// A path names a range of values (WL-4b): a leaf, or all of a struct's or
	// an array's; or says in words why not.
	{
		const auto path = QStringLiteral("device/Config");
		const auto all = leaves.range(config, u"", path);
		CHECK(all.error.isEmpty() && all.first == 0 && all.count == 306);
		const auto run = leaves.range(config, u"samples", path);
		CHECK(run.error.isEmpty() && run.first == 6 && run.count == 300);
		const auto one = leaves.range(config, u"samples[3]", path);
		CHECK(one.error.isEmpty() && one.first == 9 && one.count == 1);
		CHECK(leaves.range(config, u"mode", path).first == 5 && leaves.range(config, u"mode", path).count == 1);
		CHECK(leaves.range(samples, u"[7]", QStringLiteral("data/Samples")).first == 7);
		CHECK(leaves.range(config, u"gian", path).error == "device/Config has no member gian. Did you mean gain?");
		CHECK(leaves.range(config, u"zzzzzz", path).error == "device/Config has no member zzzzzz.");
		CHECK(leaves.range(config, u"samples[300]", path).error ==
		      "device/Config.samples has 300 elements, [0] to [299].");
		CHECK(leaves.range(config, u"mode[0]", path).error == "device/Config.mode is not an array.");
		CHECK(leaves.range(config, u"samples[3].x", path).error ==
		      "device/Config.samples[3] is not a struct, so it has no member x.");
		for (const auto bad : {u"samples[3", u"samples[]", u"gain.", u"samples.[3]", u".mode", u"samples[x]"})
			CHECK(!leaves.range(config, bad, path).error.isEmpty() && leaves.range(config, bad, path).count == 0);
		bool ranges = true;
		for (quint32 n = 0; n < 306; ++n) {
			const auto leaf = leaves.locate(config, n);
			const auto at = leaf ? leaves.range(config, leaf->member(), path) : ux::WatchLeaves::Range{};
			ranges = ranges && at.error.isEmpty() && at.first == n && at.count == 1;
		}
		CHECK(ranges);
	}

	Source source; source.schema = schema;
	ux::EndpointPreviews previews(nullptr, [&] { return source.now; });
	previews.setSource(source.source());
	ux::EndpointModel model;
	QAbstractItemModelTester tester{&model, QAbstractItemModelTester::FailureReportingMode::Fatal};
	model.setPreviews(&previews); model.rebuild(storage->owner);
	const auto field = endpoint(model, 1, 0);
	const auto scalar = endpoint(model, 1, 1);
	const auto command = endpoint(model, 2, 0);
	const auto range = [&](const QModelIndex& index) {
		return std::pair{model.data(index, ux::EndpointModel::WatchLeafFirstRole).toUInt(),
		                 model.data(index, ux::EndpointModel::WatchLeafCountRole).toUInt()};
	};
	CHECK(model.pathOf(model.parent(field)) == "device" && range(model.parent(field)) == std::pair{0u, 0u});
	CHECK(model.pathOf(field) == "device/Config" && range(field) == std::pair{0u, 306u});
	CHECK(model.pathOf(scalar) == "device/Serial" && range(scalar) == std::pair{0u, 1u});
	CHECK(model.pathOf(command) == "device/Reset" && range(command) == std::pair{0u, 0u});
	CHECK(model.data(field, ux::EndpointModel::PathRole).toString() == "device/Config" && model.pathOf({}).isEmpty());
	const auto gainRow = child(model, field, QStringLiteral("gain"));
	const auto modeRow = child(model, field, QStringLiteral("mode"));
	const auto samplesRow = child(model, field, QStringLiteral("samples"));
	CHECK(model.pathOf(gainRow) == "device/Config.gain" && range(gainRow) == std::pair{2u, 1u});
	CHECK(model.pathOf(modeRow) == "device/Config.mode" && range(modeRow) == std::pair{5u, 1u});
	CHECK(model.pathOf(samplesRow) == "device/Config.samples" && range(samplesRow) == std::pair{6u, 300u});
	const auto firstRange = child(model, samplesRow, QStringLiteral("[0 … 255]"));
	const auto secondRange = child(model, samplesRow, QStringLiteral("[256 … 299]"));
	CHECK(model.pathOf(firstRange) == "device/Config.samples[0 … 255]" && range(firstRange) == std::pair{6u, 256u});
	CHECK(model.pathOf(secondRange) == "device/Config.samples[256 … 299]" && range(secondRange) == std::pair{262u, 44u});
	const auto third = child(model, firstRange, QStringLiteral("[3]"));
	const auto last = child(model, secondRange, QStringLiteral("[299]"));
	CHECK(model.pathOf(third) == "device/Config.samples[3]" && range(third) == std::pair{9u, 1u});
	CHECK(model.pathOf(last) == "device/Config.samples[299]" && range(last) == std::pair{305u, 1u});
	const auto lastLeaf = leaves.locate(config, 305);
	CHECK(lastLeaf && "device/Config" + lastLeaf->suffix == model.pathOf(last));
}

int main(int argc, char** argv) {
	// QAbstractItemModelTester validates GUI roles (including QPixmap), even
	// though this executable never creates a window or sends native input.
	if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
	QGuiApplication app(argc, argv);
	qInstallMessageHandler(+[](QtMsgType type, const QMessageLogContext&, const QString& message) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
			++f::warnings; std::fprintf(stderr, "Qt preview model warning in %s (check %u): %s\n",
			                           currentScenario, f::checks, qPrintable(message));
		}
	});
	currentScenario = "metadata/local children";
	metadataAndLocalChildren();
	currentScenario = "watch leaves and paths";
	watchLeavesAndPaths();
	currentScenario = "draft/preview FIFO";
	CHECK(draftAfterPreview(false) == draftAfterPreview(true));
	currentScenario = "editor precision";
	editorMetadataAndPrecision();
	currentScenario = "definitions before Read";
	definitionsBeforeRead();
	currentScenario = "signed enum/bool/expansion bound";
	smallSignedBooleanAndExpansionBound();
	currentScenario = "publication lifetime/source identity";
	publicationLifetimeAndWrongSource();
	currentScenario = "metadata callback source replacement";
	schemaSourceReplacement();
	CHECK(f::warnings == 0);
	std::printf("Endpoint preview model checks=%u warnings=%u Qt=%s\n", f::checks, f::warnings, qVersion());
}
