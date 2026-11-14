// Real endpoint/value models over parsed metadata and the native Encoded client. MIT.
#include <Telemetry/UX/EndpointPreviews.hpp>
#include <Telemetry/UX/WatchlistModel.hpp>
#include <Telemetry/UX/DynamicWatchlist.hpp>
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
	CHECK(model.isGroup(model.index(0, 0)) && model.isGroup(model.parent(config)));
	CHECK(model.isGroup(model.index(1, 0)) && model.isGroup(model.parent(command)));
	CHECK(model.isGroup(model.index(2, 0)) && model.isGroup(model.parent(service)));
	CHECK(!model.isGroup({}) && !model.isGroup(config) && !model.isGroup(command) && !model.isGroup(service));
	ux::EndpointModel foreign;
	foreign.rebuild(storage->owner);
	CHECK(!model.isGroup(foreign.index(0, 0)));
	CHECK(model.data(config, ux::EndpointModel::TypeTextRole).toString() == "struct, 7 members");
	CHECK(model.data(config, ux::EndpointModel::WireBytesRole).toUInt() == telemetry::wireSize<f::Config>);
	CHECK(model.data(config, ux::EndpointModel::AccessTextRole).toString() == "Read / Write");
	CHECK(model.data(command, ux::EndpointModel::TypeTextRole).toString() == "nothing");
	CHECK(model.data(command, ux::EndpointModel::AccessTextRole).toString() == "Execute");
	CHECK(model.data(service, ux::EndpointModel::ResponseTypeTextRole).toString() == "struct, 7 members");
	CHECK(model.data(service, ux::EndpointModel::DetailRole).toString().contains("Response:"));
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
	CHECK(!model.data(serial, ux::EndpointModel::ReadableRole).toBool() &&
	      !model.data(serial, ux::EndpointModel::WritableRole).toBool());
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
	CHECK(info.value("id").toUInt() == 0 && info.value("typeText") == "struct, 7 members");
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
	CHECK(provider.selectionInfo().value("typeText") == "nothing");
	f::select(provider, "Inspect");
	CHECK(provider.selectionInfo().value("responseTypeText") == "struct, 7 members");
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
