// Controlled completions and ticks: no event-loop timing assumptions. MIT.
#include <Telemetry/UX/DynamicWatchlist.hpp>
#include <Telemetry/UX/ReadPortions.hpp>
#include <Telemetry/UX/TrendHistory.hpp>
#include <Telemetry/UX/WatchKey.hpp>
#include <Telemetry/UX/WatchFormat.hpp>
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <algorithm>
#include <QtCore/QCoreApplication>
#include <QtCore/QByteArray>
#include <QtCore/QEvent>
#include <QtTest/QAbstractItemModelTester>
#include <QtTest/QSignalSpy>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>
#include <utility>

namespace ux = telemetry::ux;
namespace cl = telemetry::client;
namespace rt = telemetry::runtime;

// A device whose Field 1 is a struct: device.Config holds 302 values, gain
// first, enabled second, then samples[0] to samples[299]. Field 0 is a
// scalar. Ids 2 to 99 stay the Peer's whole 8-byte Fields.
namespace members {
struct Config {
	float gain = 1.25f;
	bool enabled = true;
	std::array<std::uint16_t, 300> samples{};
};
struct Device {
	std::uint32_t counter() const noexcept { return 7; }
	Config config() const noexcept { return {}; }
};
inline Device device;
inline constexpr telemetry::FieldTable fields{
    telemetry::field<&Device::counter>("Counter", device),
    telemetry::field<&Device::config>("Config", device)};
inline constexpr telemetry::FieldCatalogTable catalogs{telemetry::group("device", fields)};
inline constexpr telemetry::ServiceCatalogTable<> services{};
inline constexpr telemetry::Model model{catalogs, telemetry::emptyCommands, services};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();
constexpr quint32 config = 1, leaves = 302;

struct Schema {
	std::array<std::byte, descriptorBytes.size()> bytes = descriptorBytes;
	std::array<rt::RuntimeType, 32> types{};
	std::array<rt::RuntimeMember, 32> members{};
	std::array<rt::RuntimeEnumEntry, 4> enums{};
	std::array<rt::RuntimeCatalog, 4> catalogs{};
	std::array<rt::RuntimeEndpoint, 8> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
	rt::RuntimeSchema schema;
	Schema()
	{
		const auto parsed = rt::RuntimeSchema::parse(bytes, workspace);
		if (!parsed) std::abort();
		schema = parsed.schema;
	}
};
} // namespace members

// Two large structs and a small one, for a Batch of struct Fields: Counter is
// Field 0, Left 1, Right 2 (605 bytes on the wire each), Small 3 (4 bytes).
namespace twin {
struct Small {
	std::uint16_t a = 1, b = 2;
};
struct Device {
	std::uint32_t counter() const noexcept { return 7; }
	members::Config left() const noexcept { return {}; }
	members::Config right() const noexcept { return {}; }
	Small small() const noexcept { return {}; }
};
inline Device device;
inline constexpr telemetry::FieldTable fields{
    telemetry::field<&Device::counter>("Counter", device), telemetry::field<&Device::left>("Left", device),
    telemetry::field<&Device::right>("Right", device), telemetry::field<&Device::small>("Small", device)};
inline constexpr telemetry::FieldCatalogTable catalogs{telemetry::group("device", fields)};
inline constexpr telemetry::ServiceCatalogTable<> services{};
inline constexpr telemetry::Model model{catalogs, telemetry::emptyCommands, services};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();
constexpr quint32 left = 1, right = 2, small = 3;

struct Schema {
	std::array<std::byte, descriptorBytes.size()> bytes = descriptorBytes;
	std::array<rt::RuntimeType, 32> types{};
	std::array<rt::RuntimeMember, 32> members{};
	std::array<rt::RuntimeEnumEntry, 4> enums{};
	std::array<rt::RuntimeCatalog, 4> catalogs{};
	std::array<rt::RuntimeEndpoint, 8> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
	rt::RuntimeSchema schema;
	Schema()
	{
		const auto parsed = rt::RuntimeSchema::parse(bytes, workspace);
		if (!parsed) std::abort();
		schema = parsed.schema;
	}
};
} // namespace twin

// Groups whose names hold dots (WL-8): power.Total, then power.meter.Voltage
// and power.meter.Current.
namespace dotted {
struct Device {
	std::uint32_t total() const noexcept { return 1; }
	std::uint32_t voltage() const noexcept { return 230; }
	std::uint32_t current() const noexcept { return 5; }
};
inline Device device;
inline constexpr telemetry::FieldTable power{telemetry::field<&Device::total>("Total", device)};
inline constexpr telemetry::FieldTable meter{telemetry::field<&Device::voltage>("Voltage", device),
                                             telemetry::field<&Device::current>("Current", device)};
inline constexpr telemetry::FieldCatalogTable catalogs{telemetry::group("power", power),
                                                       telemetry::group("power.meter", meter)};
inline constexpr telemetry::ServiceCatalogTable<> services{};
inline constexpr telemetry::Model model{catalogs, telemetry::emptyCommands, services};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();

struct Schema {
	std::array<std::byte, descriptorBytes.size()> bytes = descriptorBytes;
	std::array<rt::RuntimeType, 16> types{};
	std::array<rt::RuntimeMember, 8> members{};
	std::array<rt::RuntimeEnumEntry, 4> enums{};
	std::array<rt::RuntimeCatalog, 4> catalogs{};
	std::array<rt::RuntimeEndpoint, 8> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
	rt::RuntimeSchema schema;
	Schema()
	{
		const auto parsed = rt::RuntimeSchema::parse(bytes, workspace);
		if (!parsed) std::abort();
		schema = parsed.schema;
	}
};
} // namespace dotted

namespace {
unsigned checks = 0;
#define CHECK(...)                                                                                 \
	do {                                                                                           \
		++checks;                                                                                  \
		if (!(__VA_ARGS__)) {                                                                      \
			std::fprintf(stderr, "Dynamic Watchlist check %u line %d: %s\n", checks, __LINE__,     \
			             #__VA_ARGS__);                                                            \
			std::abort();                                                                          \
		}                                                                                          \
	} while (false)

void events()
{
	QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
}

ux::WatchValueBits bits(ux::WatchScalar scalar, quint8 width, quint64 number, bool enumeration = false)
{
	ux::WatchValueBits result;
	result.type = {1, scalar, width, enumeration};
	std::array<std::byte, 8> bytes{};
	for (unsigned i = 0; i < width; ++i) bytes[i] = std::byte((number >> (i * 8)) & 0xff);
	CHECK(result.assign(bytes.data(), width));
	bytes.fill(std::byte{0x55}); // The model must own the exact payload, including unplottable values.
	return result;
}

// Original, Dec and Hex from a value's exact bits (WL-3), and hex typed for
// a write turned into the value's own text.
void numberFormats()
{
	using ux::WatchNumberFormat;
	namespace wf = ux::watch_format;
	const auto all = [](const ux::WatchValueBits& value, const QHash<quint64, QString>* names = nullptr) {
		return QStringList{wf::text(value, WatchNumberFormat::Original, names),
		                   wf::text(value, WatchNumberFormat::Dec, names), wf::text(value, WatchNumberFormat::Hex, names)};
	};
	CHECK(all(bits(ux::WatchScalar::U16, 2, 230)) == QStringList({"230", "230", "0x00E6"}));
	CHECK(all(bits(ux::WatchScalar::S16, 2, 0xffff)) == QStringList({"-1", "-1", "0xFFFF"}));
	CHECK(all(bits(ux::WatchScalar::F32, 4, 0x3fc00000)) == QStringList({"1.5", "1.5", "0x3FC00000"}));
	CHECK(all(bits(ux::WatchScalar::F64, 8, 0x3ff8000000000000ULL)) == QStringList({"1.5", "1.5", "0x3FF8000000000000"}));
	// WL-9 B1: Original a float's shortest exact text, Dec every digit its bits
	// hold, Hex its bits; exact decimals stay short in Dec.
	CHECK(all(bits(ux::WatchScalar::F32, 4, 0x3dcccccd)) == QStringList({"0.1", "0.100000001", "0x3DCCCCCD"}));
	CHECK(all(bits(ux::WatchScalar::F32, 4, 0x4365deb8)) == QStringList({"229.87", "229.869995", "0x4365DEB8"}));
	CHECK(all(bits(ux::WatchScalar::F64, 8, 0x3fb999999999999aULL)) ==
	      QStringList({"0.1", "0.10000000000000001", "0x3FB999999999999A"}));
	CHECK(all(bits(ux::WatchScalar::F32, 4, 0x80000000)) == QStringList({"-0", "-0", "0x80000000"}));
	CHECK(all(bits(ux::WatchScalar::F32, 4, 0x7fc00000)) == QStringList({"NaN", "NaN", "0x7FC00000"}));
	CHECK(all(bits(ux::WatchScalar::F32, 4, 0x43660000)) == QStringList({"230", "230", "0x43660000"}));
	CHECK(all(bits(ux::WatchScalar::F64, 8, 0x8010000000000000ULL))[1] == "-2.2250738585072014e-308");
	// A bool or an enum: its word with its code in every form; Hex turns
	// only the code, at the type's width (a negative code is its bits).
	CHECK(all(bits(ux::WatchScalar::Boolean, 1, 1)) == QStringList({"true (1)", "true (1)", "true (0x01)"}));
	CHECK(all(bits(ux::WatchScalar::Boolean, 1, 0)) == QStringList({"false (0)", "false (0)", "false (0x00)"}));
	CHECK(all(bits(ux::WatchScalar::Boolean, 1, 2)) == QStringList({"true (2)", "true (2)", "true (0x02)"}));
	const QHash<quint64, QString> names{{2, "High"}, {0xff, "Off"}};
	CHECK(all(bits(ux::WatchScalar::U8, 1, 2, true), &names) == QStringList({"High (2)", "High (2)", "High (0x02)"}));
	CHECK(all(bits(ux::WatchScalar::S8, 1, 0xff, true), &names) == QStringList({"Off (-1)", "Off (-1)", "Off (0xFF)"}));
	CHECK(wf::text(bits(ux::WatchScalar::U8, 1, 7, true), WatchNumberFormat::Original, &names) == "Unknown (7)");
	CHECK(wf::text(bits(ux::WatchScalar::U8, 1, 7, true), WatchNumberFormat::Hex, &names) == "Unknown (0x07)");
	{
		const auto parts = wf::parts(bits(ux::WatchScalar::U8, 1, 2, true), WatchNumberFormat::Hex, &names);
		CHECK(parts.word == "High" && parts.code == "0x02");
		const auto number = wf::parts(bits(ux::WatchScalar::U16, 2, 230), WatchNumberFormat::Original);
		CHECK(number.word.isEmpty() && number.code == "230");
	}
	// Without the type's names, Original has nothing to add: the row keeps its text.
	CHECK(wf::text(bits(ux::WatchScalar::U8, 1, 2, true), WatchNumberFormat::Original).isEmpty());
	CHECK(wf::text(bits(ux::WatchScalar::U8, 1, 2, true), WatchNumberFormat::Hex) == "0x02");
	CHECK(wf::text({}, WatchNumberFormat::Hex).isEmpty());
	CHECK(wf::parse(u"hex") == WatchNumberFormat::Hex && !wf::parse(u"bin") && wf::name(WatchNumberFormat::Dec) == "dec");

	const auto type = [](ux::WatchScalar scalar, quint8 width, bool enumeration = false) {
		return ux::WatchValueType{1, scalar, width, enumeration};
	};
	const auto hex = [](const char* text, const ux::WatchValueType& of) { return wf::fromHex(QString::fromLatin1(text), of); };
	CHECK(hex("0x40000000", type(ux::WatchScalar::F32, 4)).canonical == "2");
	CHECK(hex("0X3ff8000000000000", type(ux::WatchScalar::F64, 8)).canonical == "1.5");
	CHECK(hex("0xFFFF", type(ux::WatchScalar::S16, 2)).canonical == "-1");
	CHECK(hex(" 0x0001 ", type(ux::WatchScalar::U8, 1)).canonical == "1");  // leading zeros are allowed
	CHECK(hex("0x1_F", type(ux::WatchScalar::U8, 1)).canonical == "31");
	CHECK(hex("0x02", type(ux::WatchScalar::U8, 1, true)).canonical == "2");
	CHECK(hex("0x01", type(ux::WatchScalar::Boolean, 1)).canonical == "true");
	CHECK(!hex("0x100", type(ux::WatchScalar::U8, 1)).error.isEmpty() && hex("0x100", type(ux::WatchScalar::U8, 1)).canonical.isEmpty());
	CHECK(hex("0x100", type(ux::WatchScalar::U8, 1)).error.contains("u8"));
	CHECK(!hex("0x02", type(ux::WatchScalar::Boolean, 1)).error.isEmpty());
	CHECK(!hex("0xZZ", type(ux::WatchScalar::U8, 1)).error.isEmpty());
	CHECK(!hex("0x", type(ux::WatchScalar::U8, 1)).error.isEmpty());
	CHECK(!hex("0x1", {}).error.isEmpty());
	CHECK(wf::isHex(u" 0x1") && !wf::isHex(u"12") && !wf::isHex(u"0"));
	// A float's bits come back exactly through the write's text; a NaN's
	// payload would not, so it is refused rather than changed.
	CHECK(hex("0x80000000", type(ux::WatchScalar::F32, 4)).canonical == "-0");
	CHECK(hex("0xFF800000", type(ux::WatchScalar::F32, 4)).canonical == "-inf");
	CHECK(!hex("0x7FC00001", type(ux::WatchScalar::F32, 4)).error.isEmpty() &&
	      hex("0x7FC00001", type(ux::WatchScalar::F32, 4)).canonical.isEmpty());
	CHECK(!hex("0x7FF0000000000001", type(ux::WatchScalar::F64, 8)).error.isEmpty());
	// A typed value as the bits it would write, or why not (WL-3b).
	const auto encode = [](const char* text, const ux::WatchValueType& of) {
		return wf::encode(QString::fromLatin1(text), of);
	};
	CHECK(wf::text(encode("2", type(ux::WatchScalar::F32, 4)).value, WatchNumberFormat::Hex) == "0x40000000");
	CHECK(wf::text(encode("0x3FC00000", type(ux::WatchScalar::F32, 4)).value, WatchNumberFormat::Dec) == "1.5");
	CHECK(wf::text(encode("-1", type(ux::WatchScalar::S16, 2)).value, WatchNumberFormat::Hex) == "0xFFFF");
	// A bool keeps its word in every format; Hex turns only its code (WL-9 B1).
	CHECK(wf::text(encode("true", type(ux::WatchScalar::Boolean, 1)).value, WatchNumberFormat::Hex) == "true (0x01)");
	CHECK(wf::range(type(ux::WatchScalar::U16, 2)) == "0 to 65535 (0x0000 to 0xFFFF)");
	CHECK(wf::range(type(ux::WatchScalar::S16, 2)) == "-32768 to 32767, or 0x0000 to 0xFFFF as bits");
	CHECK(wf::range(type(ux::WatchScalar::F32, 4)).isEmpty());
	// The range in words, the typed text not echoed (WL-9 C3).
	CHECK(encode("65536", type(ux::WatchScalar::U16, 2)).error == "u16 takes 0 to 65535 (0x0000 to 0xFFFF).");
	CHECK(encode("0x10000", type(ux::WatchScalar::U16, 2)).error == "u16 takes 0 to 65535 (0x0000 to 0xFFFF).");
	CHECK(encode("-1", type(ux::WatchScalar::U8, 1)).error == "u8 takes 0 to 255 (0x00 to 0xFF).");
	CHECK(encode("123455555555555", type(ux::WatchScalar::U32, 4)).error ==
	      "u32 takes 0 to 4294967295 (0x00000000 to 0xFFFFFFFF).");
	CHECK(encode("40000", type(ux::WatchScalar::S16, 2)).error ==
	      "i16 takes -32768 to 32767, or 0x0000 to 0xFFFF as bits.");
	// A row shown in hex hears its range in hex first (WL-8).
	CHECK(wf::encode(u"65536", type(ux::WatchScalar::U16, 2), WatchNumberFormat::Hex).error ==
	      "u16 takes 0x0000 to 0xFFFF (0 to 65535).");
	CHECK(wf::encode(u"0x10000", type(ux::WatchScalar::U16, 2), WatchNumberFormat::Hex).error ==
	      "u16 takes 0x0000 to 0xFFFF (0 to 65535).");
	CHECK(wf::fromHex(u"0x100", type(ux::WatchScalar::U8, 1), WatchNumberFormat::Hex).error ==
	      "u8 takes 0x00 to 0xFF (0 to 255).");
	CHECK(wf::encode(u"40000", type(ux::WatchScalar::S16, 2), WatchNumberFormat::Hex).error ==
	      "i16 takes 0x0000 to 0xFFFF as bits, or -32768 to 32767.");
	CHECK(wf::encode(u"65536", type(ux::WatchScalar::U16, 2), WatchNumberFormat::Dec).error.contains("takes 0 to 65535 (0x0000"));
	CHECK(wf::range(type(ux::WatchScalar::U16, 2), WatchNumberFormat::Hex) == "0x0000 to 0xFFFF (0 to 65535)");
	CHECK(encode("abc", type(ux::WatchScalar::U16, 2)).error == "“abc” is not a number for u16.");
	CHECK(encode("+7", type(ux::WatchScalar::U16, 2)).error == "“+7”: write it without the + sign.");
	CHECK(encode("1e3", type(ux::WatchScalar::U16, 2)).error == "“1e3”: u16 takes whole numbers only.");
	CHECK(encode("1e40", type(ux::WatchScalar::F32, 4)).error == "“1e40” is out of range for f32.");
	CHECK(encode("0x1FFFFFFFF", type(ux::WatchScalar::F32, 4)).error == "“0x1FFFFFFFF”: f32 takes at most 8 hex digits.");
	CHECK(encode("yes", type(ux::WatchScalar::Boolean, 1)).error == "Enter true or false (1 or 0).");
	CHECK(encode("", type(ux::WatchScalar::U16, 2)).error.isEmpty() && encode("", type(ux::WatchScalar::U16, 2)).value.size == 0);
	CHECK(!encode("1", {}).error.isEmpty());

	// The model: the Watchlist's form, a row's own, remembered by path.
	ux::WatchlistModel model;
	CHECK(model.add(ux::WatchEntry::of(1, "device/Gain")));
	CHECK(model.add(ux::WatchEntry::of(2, "device/Mode")));
	CHECK(model.setResult(0, "Ok", "1.5", ux::WatchSample::ofUnsigned(1), bits(ux::WatchScalar::F32, 4, 0x3fc00000)));
	CHECK(model.setResult(1, "Ok", "7", ux::WatchSample::ofUnsigned(7), bits(ux::WatchScalar::U16, 2, 7)));
	const auto shown = [&](int row) { return model.data(model.index(row, 0), ux::WatchlistModel::ShownTextRole).toString(); };
	const auto role = [&](int row, int r) { return model.data(model.index(row, 0), r); };
	CHECK(shown(0) == "1.5" && shown(1) == "7" && role(0, ux::WatchlistModel::FormatRole).toString().isEmpty());
	QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
	model.setFormat(WatchNumberFormat::Hex);
	CHECK(changed.count() == 1 && shown(0) == "0x3FC00000" && shown(1) == "0x0007");
	CHECK(role(0, ux::WatchlistModel::TextRole) == "1.5");  // the recording's text stays
	CHECK(role(1, ux::WatchlistModel::EditorTextRole) == "0x0007");  // a number's editor opens in hex
	CHECK(model.setRowFormat(0, WatchNumberFormat::Original) && shown(0) == "1.5" && shown(1) == "0x0007");
	CHECK(role(0, ux::WatchlistModel::FormatRole) == "original" && role(0, ux::WatchlistModel::EditorTextRole) == "1.5");
	CHECK(model.pathFormats().value("device/Gain") == WatchNumberFormat::Original && model.pathFormats().size() == 1);
	CHECK(model.valueType(0).scalar == ux::WatchScalar::F32 && !model.valueType(9).valid());
	// Removed and added again, the value comes back in its own form.
	const auto removed = model.removeIds({1});
	CHECK(removed.size() == 1);
	CHECK(model.add(ux::WatchEntry::of(1, "device/Gain")) && model.rowFormat(1) == WatchNumberFormat::Original);
	CHECK(model.setRowFormat(1, std::nullopt) && model.pathFormats().isEmpty());
	CHECK(model.setPathFormats({{"device/Mode", WatchNumberFormat::Dec}}));
	CHECK(shown(0) == "7" && model.rowFormat(0) == WatchNumberFormat::Dec && !model.rowFormat(1));
	// Not read yet: no form to show.
	CHECK(shown(1).isEmpty());

	// Past formatLimit the paths not watched now give way, the longest unset
	// first (WL-3c); a watched row's format stays.
	// Set from the highest name down, so the oldest is the last by name; an
	// Undo's put-back in between keeps when each was set.
	ux::WatchlistModel bounded;
	CHECK(bounded.add(ux::WatchEntry::of(1, "kept.Watched")) && bounded.setRowFormat(0, WatchNumberFormat::Hex));
	const auto path = [](int at) { return QStringLiteral("old.P%1").arg(at, 4, 10, QLatin1Char('0')); };
	bool every = true;
	for (int at = ux::WatchlistModel::formatLimit; at > 0 && every; --at) {
		const quint64 id = quint64(at) + 1;
		every = bounded.add(ux::WatchEntry::of(id, path(at))) && bounded.setRowFormat(1, WatchNumberFormat::Dec) &&
		        bounded.removeIds({id}).size() == 1;
		if (at == 2048)
			every = every && bounded.setPathFormats(bounded.pathFormats());
	}
	CHECK(every);
	const auto kept = bounded.pathFormats();
	CHECK(kept.size() == ux::WatchlistModel::formatLimit && kept.value("kept.Watched") == WatchNumberFormat::Hex);
	CHECK(!kept.contains(path(ux::WatchlistModel::formatLimit)) && kept.contains(path(ux::WatchlistModel::formatLimit - 1)) &&
	      kept.contains(path(1)));
}

void typedEditors()
{
	ux::WatchlistModel model;
	bool current = true;
	model.bind([&] { return current; });
	CHECK(model.add(0, QStringLiteral("Typed")));
	const auto row = model.index(0, 0);
	const auto data = [&](int role) { return model.data(row, role); };
	CHECK(data(ux::WatchlistModel::EditorTextRole).toString().isEmpty());
	CHECK(!data(ux::WatchlistModel::CategoricalRole).toBool());
	// Legacy sources may omit bytes; their existing sample contract is unchanged.
	CHECK(model.setResult(0, "Ok", "legacy display", ux::WatchSample::ofSigned(12)));
	CHECK(data(ux::WatchlistModel::SampleValueRole).toDouble() == 12 &&
	      data(ux::WatchlistModel::EditorTextRole).toString().isEmpty() &&
	      !data(ux::WatchlistModel::CategoricalRole).toBool());
	CHECK(model.setResult(0, "Ok", "true (display decoration)", ux::WatchSample::ofUnsigned(1),
	                      bits(ux::WatchScalar::Boolean, 1, 1)));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "true" &&
	      data(ux::WatchlistModel::EditorKindRole) == "bool" &&
	      data(ux::WatchlistModel::CategoricalRole).toBool());
	const auto boolean = data(ux::WatchlistModel::EditorChoicesRole).toList();
	CHECK(boolean.size() == 2 && boolean[0].toMap().value("label") == "false (0)" &&
	      boolean[0].toMap().value("text") == "false" &&
	      boolean[1].toMap().value("label") == "true (1)" &&
	      boolean[1].toMap().value("text") == "true");
	CHECK(model.setResult(0, "Ok", "false", ux::WatchSample::ofUnsigned(0),
	                      bits(ux::WatchScalar::Boolean, 1, 0)));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "false");
	CHECK(model.setResult(0, "Ok", "rounded display must not be parsed", {},
	                      bits(ux::WatchScalar::U64, 8, UINT64_MAX)));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "18446744073709551615" &&
	      !data(ux::WatchlistModel::SampleValueRole).isValid() &&
	      !data(ux::WatchlistModel::CategoricalRole).toBool());
	CHECK(model.setResult(0, "Ok", "rounded display", {},
	                      bits(ux::WatchScalar::S64, 8, UINT64_C(0x8000000000000000))));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "-9223372036854775808");
	CHECK(model.setResult(0, "Ok", "Negative (-1)", ux::WatchSample::ofSigned(-1),
	                      bits(ux::WatchScalar::S8, 1, 0xff, true)));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "-1" &&
	      data(ux::WatchlistModel::EditorKindRole) == "enum" &&
	      data(ux::WatchlistModel::CategoricalRole).toBool() &&
	      data(ux::WatchlistModel::EditorChoicesRole).toList().isEmpty());
	CHECK(model.setResult(0, "Ok", "unknown enum", {}, bits(ux::WatchScalar::U64, 8, UINT64_MAX, true)));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "18446744073709551615" &&
	      data(ux::WatchlistModel::CategoricalRole).toBool() &&
	      !data(ux::WatchlistModel::SampleExactRole).toBool());
	CHECK(model.setResult(0, "Ok", "0.1", ux::WatchSample::ofFloating(0.1f),
	                      bits(ux::WatchScalar::F32, 4, 0x3dcccccd)));
	// Original opens a float in its shortest exact text (WL-9 B1); it reads
	// back to the same bits.
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "0.1" &&
	      !data(ux::WatchlistModel::CategoricalRole).toBool());
	CHECK(model.setResult(0, "Ok", "negative zero", ux::WatchSample::ofFloating(-0.0),
	                      bits(ux::WatchScalar::F64, 8, UINT64_C(0x8000000000000000))));
	CHECK(data(ux::WatchlistModel::EditorTextRole) == "-0");
	CHECK(model.setResult(0, "Failed", "No reading", {}));
	CHECK(data(ux::WatchlistModel::EditorTextRole).toString().isEmpty());
	CHECK(model.setResult(0, "Ok", "large enum", {}, bits(ux::WatchScalar::U64, 8, UINT64_MAX, true)));
	current = false;
	CHECK(data(ux::WatchlistModel::EditorTextRole).toString().isEmpty() &&
	      !data(ux::WatchlistModel::EditorChoicesRole).isValid() &&
	      !data(ux::WatchlistModel::CategoricalRole).toBool());
}

QString text(ux::DynamicWatchlist& watch, int row)
{
	auto* model = watch.model();
	return model->data(model->index(row, 0), ux::WatchlistModel::TextRole).toString();
}

struct Peer {
	bool current = true, busy = false, supported = true, inCallback = false;
	ux::WatchlistSource::Limits limits{32, 130, 514};
	std::vector<std::vector<std::uint32_t>> calls;
	ux::WatchlistSource::Completion completion;
	std::vector<std::uint32_t> pending;
	std::shared_ptr<int> lease = std::make_shared<int>(0);
	// Set to read struct members: the schema and the member reads asked.
	std::shared_ptr<members::Schema> schema;
	std::vector<std::pair<quint32, std::vector<std::uint32_t>>> memberCalls;
	quint32 memberField = 0;
	// Set for the twin schema; batchLimits set: the device has Batch.
	std::shared_ptr<twin::Schema> twin;
	// Set for the dotted schema: groups whose names hold dots.
	std::shared_ptr<dotted::Schema> dotted;
	std::optional<ux::WatchlistSource::Limits> batchLimits;
	QHash<quint32, unsigned> valueSizes{{twin::left, 605}, {twin::right, 605}, {twin::small, 4}};
	using Fields = std::vector<std::pair<quint32, std::vector<std::uint32_t>>>;
	std::vector<Fields> batchCalls;
	Fields batchPending;

	ux::WatchlistSource source()
	{
		ux::WatchlistSource source;
		source.storage = lease;
		source.current = [this] {
			return current;
		};
		source.busy = [this] {
			return busy;
		};
		source.capability = [this]() -> std::optional<ux::WatchlistSource::Limits> {
			return supported ? std::optional{limits} : std::nullopt;
		};
		// The dotted schema's Field ids carry their catalog: every one is held.
		source.fieldBytes = [this](quint32 id) -> std::optional<unsigned> {
			return id < 100 || dotted ? std::optional<unsigned>{8} : std::nullopt;
		};
		source.readMany = [this](std::span<const std::uint32_t> ids,
		                         ux::WatchlistSource::Completion done) {
			return submit(ids, std::move(done));
		};
		source.readField = [this](quint32 id, ux::WatchlistSource::Completion done) {
			return submit(std::span<const std::uint32_t>{&id, 1}, std::move(done));
		};
		if (batchLimits) {
			source.batchCapability = [this] { return batchLimits; };
			source.valueBytes = [this](quint32 id) -> std::optional<unsigned> {
				const auto at = valueSizes.constFind(id);
				return at == valueSizes.cend() ? std::nullopt : std::optional<unsigned>{*at};
			};
			source.readBatch = [this](std::span<const ux::WatchlistSource::BatchField> fields,
			                          ux::WatchlistSource::Completion done) -> cl::Submission {
				CHECK(current && !busy && !completion && fields.size() >= 2 && fields.size() <= 32);
				batchPending.clear();
				for (const auto& field : fields) {
					CHECK(!field.leaves.empty() && std::is_sorted(field.leaves.begin(), field.leaves.end()));
					batchPending.push_back({field.field, {field.leaves.begin(), field.leaves.end()}});
				}
				batchCalls.push_back(batchPending);
				completion = std::move(done);
				busy = true;
				return {cl::Status::Ok, {}};
			};
		}
		if (schema || twin || dotted) {
			source.schema = [this] { return schema ? schema->schema : twin ? twin->schema : dotted->schema; };
			source.readMembers = [this](quint32 field, std::span<const std::uint32_t> leaves,
			                            ux::WatchlistSource::Completion done) -> cl::Submission {
				CHECK(current && !busy && !completion && !leaves.empty() && leaves.size() <= std::size_t(ux::watchlistRowLimit));
				CHECK(std::is_sorted(leaves.begin(), leaves.end()));
				memberField = field;
				pending.assign(leaves.begin(), leaves.end());
				memberCalls.push_back({field, pending});
				completion = std::move(done);
				busy = true;
				return {cl::Status::Ok, {}};
			};
		}
		return source;
	}

	// Answers a member read: each value is its own ordinal as a u16. skew
	// answers the first value as another leaf, as a confused device would.
	void finishMembers(cl::Status status = cl::Status::Ok, bool skew = false)
	{
		CHECK(busy && bool(completion) && memberField != 0);
		ux::WatchlistCompletion result;
		result.status = status;
		result.native.value = status == cl::Status::Ok ? 0 : 2;
		result.completed = ux::WatchTimestamp::now();
		result.count = int(pending.size());
		result.members.resize(pending.size());
		for (std::size_t i = 0; i < pending.size(); ++i) {
			auto& row = result.members[i];
			row.id = memberField;
			row.leaf = pending[i] + 1;
			row.itemStatus = row.valueStatus = status == cl::Status::Ok ? 0 : 2;
			row.value.type = {7, ux::WatchScalar::U16, 2, false};
			const std::array<std::byte, 2> bytes{std::byte(pending[i] & 0xff), std::byte(pending[i] >> 8)};
			CHECK(row.value.assign(bytes.data(), 2));
			row.status = QStringLiteral("Ok");
			row.text = QString::number(pending[i]);
			row.sample = ux::WatchSample::ofUnsigned(pending[i]);
		}
		if (skew)
			result.members[0].leaf += 1;
		auto done = std::exchange(completion, {});
		busy = false;
		memberField = 0;
		*done = std::move(result);
	}

	// Answers a Batch: each value is its own ordinal as a u16. The Field at
	// refuse is refused by the device (endpoint 3); skew answers its first
	// value as another leaf.
	void finishBatch(cl::Status status = cl::Status::Ok, int refuse = -1, bool skew = false)
	{
		CHECK(busy && bool(completion) && !batchPending.empty());
		ux::WatchlistCompletion result;
		result.status = status;
		result.native.client = qint32(status);
		result.completed = ux::WatchTimestamp::now();
		for (std::size_t i = 0; i < batchPending.size(); ++i) {
			const auto& [field, leaves] = batchPending[i];
			ux::WatchlistCompletion::Part part;
			part.field = field;
			part.first = int(result.members.size());
			part.count = int(leaves.size());
			const bool refused = int(i) == refuse;
			part.native.endpoint = refused ? 3 : 0;
			part.native.value = refused ? -1 : 0;
			result.parts.push_back(part);
			for (const auto leaf : leaves) {
				ux::WatchlistCompletion::Row row;
				row.id = field;
				row.leaf = leaf + 1;
				row.value.type = {7, ux::WatchScalar::U16, 2, false};
				row.itemStatus = refused ? 3 : 0;
				row.valueStatus = refused ? -1 : 0;
				if (refused) {
					row.status = QStringLiteral("Field refused (dispatch 0, endpoint 3)");
				} else {
					const std::array<std::byte, 2> bytes{std::byte(leaf & 0xff), std::byte(leaf >> 8)};
					CHECK(row.value.assign(bytes.data(), 2));
					row.status = QStringLiteral("Ok");
					row.text = QString::number(leaf);
					row.sample = ux::WatchSample::ofUnsigned(leaf);
				}
				result.members.push_back(std::move(row));
			}
		}
		result.count = int(result.members.size());
		if (skew)
			result.members[0].leaf += 1;
		auto done = std::exchange(completion, {});
		busy = false;
		batchPending.clear();
		*done = std::move(result);
	}

	cl::Submission submit(std::span<const std::uint32_t> ids, ux::WatchlistSource::Completion done)
	{
		CHECK(current && !busy && !completion && !ids.empty() && ids.size() <= 32);
		pending.assign(ids.begin(), ids.end());
		calls.push_back(pending);
		completion = std::move(done);
		busy = true;
		return {cl::Status::Ok, {}};
	}

	void finish(cl::Status status = cl::Status::Ok, bool malformed = false)
	{
		CHECK(busy && bool(completion) && memberField == 0);
		inCallback = true;
		ux::WatchlistCompletion result;
		result.status = status;
		result.native.client = qint32(status);
		result.native.value = status == cl::Status::Ok ? 0 : 2;
		result.completed = ux::WatchTimestamp::now();
		result.count = int(pending.size());
		for (int row = 0; row < result.count; ++row) {
			result.rows[row].id = pending[row];
			result.rows[row].status = QStringLiteral("Ok");
			auto& exact = result.rows[row];
			exact.itemStatus = status == cl::Status::Ok ? 0 : -1;
			exact.valueStatus = status == cl::Status::Ok ? 0 : 2;
			const auto id = pending[std::size_t(row)];
			exact.value.type = {id, id == 2 ? ux::WatchScalar::S64 : id == 3 ? ux::WatchScalar::S16
			    : id == 4 || id == 5 ? ux::WatchScalar::F32 : id == 6 ? ux::WatchScalar::F64 : ux::WatchScalar::U64,
			    quint8(id == 3 ? 2 : id == 4 || id == 5 ? 4 : 8), id == 3};
			const quint64 bits = id == 1 ? UINT64_MAX : id == 2 || id == 6 ? UINT64_C(0x8000000000000000)
			    : id == 3 ? UINT64_C(0xff85) : id == 4 ? UINT64_C(0x7fc12345)
			    : id == 5 ? UINT64_C(0x80000000) : id;
			std::array<std::byte, 8> borrowed{};
			for (unsigned i = 0; i != borrowed.size(); ++i) borrowed[i] = std::byte((bits >> (8 * i)) & 0xff);
			if (status == cl::Status::Ok) CHECK(exact.value.assign(borrowed.data(), exact.value.type.width));
			borrowed.fill(std::byte{0x55}); // RX borrow expires before the scheduler sees it.
			result.rows[row].text = pending[row] == 1   ? QStringLiteral("18446744073709551615")
			                        : pending[row] == 2 ? QStringLiteral("-9223372036854775808")
			                        : pending[row] == 3 ? QStringLiteral("Unknown (-123)")
			                                            : QString::number(pending[row]);
			// The typed facts a real binding takes from the same value (sampleOf).
			result.rows[row].sample =
			    pending[row] == 1   ? ux::WatchSample::ofUnsigned(18446744073709551615ULL)
			    : pending[row] == 2 ? ux::WatchSample::ofSigned(std::numeric_limits<long long>::min())
			    : pending[row] == 3 ? ux::WatchSample::ofSigned(-123)
			                        : ux::WatchSample::ofUnsigned(pending[row]);
		}
		if (malformed)
			result.rows[0].id = 999;
		auto done = std::exchange(completion, {});
		busy = false;
		*done = std::move(result); // Deliberately no watch.poll() while callback active.
		inCallback = false;
	}
};

void fill(ux::DynamicWatchlist& watch, int count)
{
	for (int id = 1; id <= count; ++id)
		CHECK(watch.add(quint32(id), QStringLiteral("catalog.Field%1").arg(id)));
}

// The device went and the rows stayed (WL-4c): count rows, none bound to it.
bool unbound(ux::DynamicWatchlist& watch, int count)
{
	const auto* model = qobject_cast<ux::WatchlistModel*>(watch.model());
	return model && watch.count() == count && !model->hasBound() && watch.held().isEmpty();
}

void bulkPublication()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	QSignalSpy inserted(watch.model(), &QAbstractItemModel::rowsInserted);
	QVector<QPair<quint32, QString>> fields;
	for (quint32 i = 1; i <= 65; ++i)
		fields.push_back({i, QStringLiteral("field%1").arg(i)});
	fields.push_back({1, QStringLiteral("duplicate")});
	fields.push_back({100, QStringLiteral("unsupported")});
	const auto result = watch.addMany(fields);
	CHECK(result.value("status") == "ok" && result.value("added").toInt() == 65);
	CHECK(result.value("duplicate").toInt() == 1 && result.value("unsupported").toInt() == 1);
	CHECK(watch.count() == 65 && inserted.count() == 1 && peer.calls.empty());
	CHECK(watch.begin(false));
	for (int size : {32, 32, 1}) {
		CHECK(peer.pending.size() == size_t(size));
		peer.finish(); watch.poll(); events();
	}
	CHECK(!watch.active());
	CHECK(frames.size() == 3 && frames[0].count == 32 && frames[1].count == 32 && frames[2].count == 1);
	for (std::size_t i = 0; i != frames.size(); ++i) {
		CHECK(frames[i].sequence == qint64(i + 1) && frames[i].cycle == frames[0].cycle && frames[i].portion == qint64(i));
		CHECK(frames[i].phase == ux::WatchCompletionPhase::Current && frames[i].exact);
	}
	CHECK(frames[0].rows[0].id == 1 && frames[1].rows[0].id == 33 && frames[2].rows[0].id == 65);

	QObject otherOwner;
	Peer other;
	ux::DynamicWatchlist stale(&otherOwner);
	auto source = other.source();
	source.fieldBytes = [&other](quint32) -> std::optional<unsigned> {
		other.current = false;
		return 4;
	};
	stale.setSource(std::move(source));
	CHECK(stale.addMany({{1, "one"}}).value("status") == "stale");
	CHECK(stale.count() == 0 && other.calls.empty());

	QObject retireOwner;
	Peer retirePeer;
	ux::DynamicWatchlist retiring(&retireOwner);
	retiring.setSource(retirePeer.source());
	QObject::connect(retiring.model(), &QAbstractItemModel::rowsAboutToBeInserted, &retireOwner,
	                 [&] { retiring.retire(); });
	CHECK(retiring.addMany({{1, "one"}, {2, "two"}}).value("added").toInt() == 2);
	events();
	CHECK(unbound(retiring, 2) && retirePeer.calls.empty());

	QPointer<QObject> doomed = new QObject;
	QObject observer;
	Peer deathPeer;
	ux::DynamicWatchlist orphaned(doomed);
	orphaned.setSource(deathPeer.source());
	QObject::connect(orphaned.model(), &QAbstractItemModel::rowsAboutToBeInserted, &observer,
	                 [&] { delete doomed.data(); });
	const auto destruction = orphaned.addMany({{1, "one"}});
	CHECK(!doomed && destruction.value("added").toInt() == 1 && deathPeer.calls.empty());
}

// One Watchlist holds at most watchlistRowLimit Fields. Past it nothing more is
// added, in bulk or one at a time, and the bulk result counts the rest as
// "capacity".
void rowLimit()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	auto source = peer.source();
	source.fieldBytes = [](quint32) -> std::optional<unsigned> { return 4; };
	watch.setSource(std::move(source));
	QVector<QPair<quint32, QString>> fields;
	for (quint32 i = 1; i <= quint32(ux::watchlistRowLimit) + 5; ++i)
		fields.push_back({i, QStringLiteral("field%1").arg(i)});
	const auto result = watch.addMany(fields);
	CHECK(result.value("status") == "ok");
	CHECK(result.value("added").toInt() == ux::watchlistRowLimit);
	CHECK(result.value("capacity").toInt() == 5);
	CHECK(watch.count() == ux::watchlistRowLimit);
	const auto beyond = quint32(ux::watchlistRowLimit) + 1000;
	CHECK(!watch.canAdd(beyond) && !watch.add(beyond, QStringLiteral("over")));
	const auto over = watch.addMany({{beyond + 1, QStringLiteral("over")}});
	CHECK(over.value("added").toInt() == 0 && over.value("capacity").toInt() == 1);
	CHECK(watch.count() == ux::watchlistRowLimit && peer.calls.empty());
}

// After a write while polling, the Watchlist reads again without waiting for
// its timer. A Field this cycle already read, or had in flight while the write
// waited, is read first; one not read yet waits for its turn. With no Field
// named (a Command, Service or Batch) the next cycle follows this one at once,
// and between cycles a wake starts the next one now.
void wakeAfterWrites()
{
	const auto step = [](ux::DynamicWatchlist& watch, Peer& peer) {
		peer.finish();
		watch.poll();
		for (int i = 0; i < 4; ++i)
			events();
	};
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 65);
	watch.setInterval(60000); // the timer never fires during this test
	CHECK(watch.begin(true));
	CHECK(peer.pending.size() == 32 && peer.pending.front() == 1);
	step(watch, peer);
	CHECK(peer.pending.size() == 32 && peer.pending.front() == 33); // in flight
	watch.wake(5u);  // read earlier in this cycle
	watch.wake(40u); // in flight: read before the write
	watch.wake(65u); // not read yet: its turn comes after the write
	watch.wake(5u);  // named twice, read once
	step(watch, peer);
	CHECK(peer.pending == std::vector<std::uint32_t>{5});
	step(watch, peer);
	CHECK(peer.pending == std::vector<std::uint32_t>{40});
	CHECK(text(watch, 4) == QStringLiteral("5"));
	step(watch, peer);
	CHECK(peer.pending == std::vector<std::uint32_t>{65});
	step(watch, peer);
	CHECK(!peer.busy && !watch.pending()); // complete; the 60 s timer has not fired
	const auto calls = peer.calls.size();
	watch.wake();
	for (int i = 0; i < 4; ++i)
		events();
	CHECK(peer.calls.size() == calls + 1 && peer.pending.front() == 1);
	step(watch, peer); // 33..64 in flight
	watch.wake();      // a Command: whatever it changed is read in the next cycle
	step(watch, peer); // 65
	CHECK(peer.pending == std::vector<std::uint32_t>{65});
	step(watch, peer); // complete, and the next cycle follows at once
	CHECK(peer.busy && peer.pending.size() == 32 && peer.pending.front() == 1);
	step(watch, peer);
	step(watch, peer);
	step(watch, peer);
	CHECK(!peer.busy); // no further cycle without a new wake or tick
	watch.stop();
	// Not polling: a wake reads nothing.
	const auto stopped = peer.calls.size();
	watch.wake(5u);
	watch.wake();
	for (int i = 0; i < 4; ++i)
		events();
	CHECK(peer.calls.size() == stopped);
}

// Interval 0 reads continuously, at low priority: each cycle follows the
// last without the timer, and nothing is sent while other work is in flight.
void continuousLowPriority()
{
	const auto settle = [] {
		for (int i = 0; i < 4; ++i)
			events();
	};
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 40);
	watch.setInterval(0);
	CHECK(watch.interval() == 0);
	watch.setInterval(-1);
	CHECK(watch.interval() == 0);
	CHECK(watch.begin(true));
	CHECK(peer.pending.size() == 32);
	peer.finish();
	watch.poll();
	settle();
	CHECK(peer.pending.size() == 8 && peer.pending.front() == 33);
	peer.finish();
	peer.busy = true; // another panel's request is in flight
	watch.poll();
	settle();
	CHECK(peer.calls.size() == 2); // nothing sent beside it
	peer.busy = false;
	watch.poll(); // its completion progresses the session
	settle();
	CHECK(peer.calls.size() == 3 && peer.pending.size() == 32 && peer.pending.front() == 1);
	watch.stop();
	peer.finish();
	watch.poll();
	settle();
	CHECK(!watch.active() && peer.calls.size() == 3);
}

// A tick that finds the session busy (another panel's request) starts the
// cycle anyway and sends when the session is free, instead of being skipped;
// a wake between cycles does the same. Stop leaves no rerun behind.
void busySessionTicks()
{
	const auto settle = [] {
		for (int i = 0; i < 4; ++i)
			events();
	};
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 3);
	watch.setInterval(60000);
	CHECK(watch.begin(true));
	peer.finish();
	watch.poll();
	settle();
	CHECK(!peer.busy && !watch.pending()); // first cycle complete
	peer.busy = true;                      // another panel's request
	watch.tick();
	CHECK(!watch.pending() && peer.calls.size() == 1);
	peer.busy = false;
	watch.poll(); // that request's completion progresses the session
	settle();
	CHECK(peer.calls.size() == 2 && peer.busy); // the cycle went out
	peer.finish();
	watch.poll();
	settle();
	peer.busy = true; // a write from another panel is still queued
	watch.dataChanged();
	settle();
	CHECK(peer.calls.size() == 2);
	peer.busy = false;
	watch.poll();
	settle();
	CHECK(peer.calls.size() == 3); // read again after the write, not after a minute
	watch.wake(); // a Command during this cycle: a rerun is due
	watch.stop();
	peer.finish();
	watch.poll();
	settle();
	CHECK(watch.begin(true));
	peer.finish();
	watch.poll();
	settle();
	CHECK(!peer.busy && peer.calls.size() == 4); // no leftover rerun after the restart
	watch.stop();
}

// Start while a Read once runs turns that cycle into the first one of polling
// rather than being refused; the timer then repeats it.
void startDuringReadOnce()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 3);
	CHECK(watch.begin(false) && peer.calls.size() == 1);
	CHECK(watch.begin(true) && watch.active());
	peer.finish();
	watch.poll();
	for (int i = 0; i < 4; ++i)
		events();
	CHECK(watch.active() && !watch.pending()); // polling, between cycles
	watch.tick();
	CHECK(peer.calls.size() == 2 && watch.pending());
	watch.stop();
}

void trendCompletions()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	CHECK(watch.add(4, QStringLiteral("Field4")));
	ux::TrendHistory trend;
	trend.setSource(watch.model());
	trend.setRow(0);
	for (int attempt = 0; attempt < 2; ++attempt) {
		CHECK(watch.begin(false));
		peer.finish();
		watch.poll(); events();
	}
	CHECK(trend.points().size() == 2);
	CHECK(!trend.points().last().toMap().value("breakBefore").toBool());
	CHECK(trend.points().last().toMap().value("value").toDouble() == 4.0);
	const auto* model = watch.model();
	const auto first = model->index(0, 0);
	CHECK(model->data(first, ux::WatchlistModel::SampleValueRole).toDouble() == 4.0);
	CHECK(model->data(first, ux::WatchlistModel::SampleExactRole).toBool());
	CHECK(model->data(first, ux::WatchlistModel::StatusCodeRole).toInt() ==
	      int(ux::WatchStatus::Ok));
	watch.setIndividual(true);
	CHECK(watch.begin(false));
	peer.finish(); watch.poll(); events();
	CHECK(trend.points().size() == 3 && peer.calls.size() == 3);
	static_cast<ux::WatchlistModel*>(watch.model())->invalidateValues();
	CHECK(watch.model()->data(watch.model()->index(0, 0), ux::WatchlistModel::SampleTextRole).toString().isEmpty());
	CHECK(watch.begin(false));
	peer.finish(); watch.poll(); events();
	CHECK(trend.points().size() == 4 && trend.points().last().toMap().value("breakBefore").toBool());
}

void chunks()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	QAbstractItemModelTester tester(watch.model(),
	                                QAbstractItemModelTester::FailureReportingMode::Fatal);
	fill(watch, 65);
	CHECK(watch.count() == 65 && peer.calls.empty());
	CHECK(!watch.add(1, QStringLiteral("duplicate")) && !watch.canAdd(100));
	CHECK(watch.begin(false) && watch.active() && watch.pending());
	QSignalSpy changed(watch.model(), &QAbstractItemModel::dataChanged);
	for (int batch = 0; batch < 3; ++batch) {
		CHECK(peer.calls.size() == std::size_t(batch + 1));
		CHECK(peer.calls.back().size() == std::size_t(batch == 2 ? 1 : 32));
		for (int tick = 0; tick < 50; ++tick)
			watch.tick();
		CHECK(peer.calls.size() == std::size_t(batch + 1));
		// The read mode and the interval wait for the cycle (rows may change
		// meanwhile, fix 8; removeDuringRead and clearWhilePolling check that).
		watch.setIndividual(true);
		watch.setInterval(1);
		CHECK(watch.count() == 65 && !watch.individual() && watch.interval() == 1000);
		const auto before = changed.count();
		peer.finish();
		CHECK(watch.pending() &&
		      changed.count() == before); // No Qt publication in native callback.
		watch.poll();
		CHECK(!watch.pending());
		events();
	}
	CHECK(!watch.active() && watch.status().contains(QStringLiteral("complete")));
	CHECK(text(watch, 0) == QStringLiteral("18446744073709551615"));
	CHECK(text(watch, 1) == QStringLiteral("-9223372036854775808"));
	CHECK(text(watch, 2) == QStringLiteral("Unknown (-123)"));
	CHECK(text(watch, 64) == QStringLiteral("65"));
	CHECK(peer.calls[0].front() == 1 && peer.calls[1].front() == 33 && peer.calls[2].front() == 65);
	watch.setInterval(1);
	CHECK(watch.interval() == 1);
	watch.setInterval(60000);
	CHECK(watch.interval() == 60000);
	watch.setInterval(60001);
	CHECK(watch.interval() == 60000);
	watch.setInterval(-1);
	CHECK(watch.interval() == 60000);
	watch.setInterval(0); // continuous
	CHECK(watch.interval() == 0);
}

void budgetsAndFallback()
{
	QObject owner;
	Peer peer;
	peer.limits = {32, 10, 34}; // Both request and reply permit only two U64 fields.
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 5);
	CHECK(watch.begin(false));
	for (int count : {2, 2, 1}) {
		CHECK(peer.pending.size() == std::size_t(count));
		peer.finish();
		watch.poll();
		events();
	}
	CHECK(!watch.active() && peer.calls.size() == 3);
	peer.supported = false;
	CHECK(!watch.supported() && !watch.individual());
	CHECK(!watch.begin(false) && peer.calls.size() == 3); // No automatic fan-out.
	watch.setIndividual(true);
	CHECK(watch.individual());
	CHECK(watch.begin(false));
	for (int i = 0; i < 5; ++i) {
		CHECK(peer.pending.size() == 1);
		peer.finish();
		watch.poll();
		events();
	}
	CHECK(!watch.active() && peer.calls.size() == 8);
	watch.setIndividual(false);
	peer.supported = true;
	peer.limits.replyBytes = 17;
	CHECK(!watch.begin(false) && peer.calls.size() == 8);
	CHECK(watch.status().contains(QStringLiteral("byte budget")));
}

void drainAndStale()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 65);
	CHECK(watch.begin(true));
	std::weak_ptr<int> lease = peer.lease;
	watch.stop();
	CHECK(watch.active() && watch.pending());
	// Rows change while the read drains (fix 8); the read mode does not.
	watch.remove(0);
	CHECK(watch.count() == 64);
	watch.clear();
	watch.setIndividual(true);
	CHECK(watch.count() == 0 && !watch.individual());
	peer.finish();
	watch.poll();
	events();
	CHECK(!watch.active() && peer.calls.size() == 1);
	fill(watch, 65);
	CHECK(text(watch, 0).isEmpty());
	CHECK(watch.begin(false));
	peer.current = false;
	CHECK(text(watch, 0).isEmpty()); // Guard works before the owner publishes reset.
	watch.retire();
	CHECK(unbound(watch, 65) && watch.pending() && watch.active());
	peer.lease.reset();
	CHECK(!lease.expired());
	peer.finish();
	watch.poll();
	events();
	CHECK(!watch.active() && peer.calls.size() == 2);
	Peer fresh;
	watch.setSource(fresh.source());
	fill(watch, 2);
	CHECK(lease.expired());
	CHECK(watch.begin(false));
	fresh.finish(cl::Status::Ok, true);
	watch.poll();
	events();
	CHECK(!watch.active() && text(watch, 0).isEmpty() && text(watch, 1).isEmpty());
}

void schedulingAndReentrancy()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	bool blocked = false;
	watch.setNotify(
	    [] {
	    },
	    [&] {
		    return blocked;
	    });
	watch.setSource(peer.source());
	fill(watch, 1);
	CHECK(watch.begin(true));
	peer.finish();
	watch.poll();
	events();
	CHECK(watch.active() && !watch.pending());
	// Ticks while the session is busy start one cycle that waits: nothing
	// accumulates, and it goes out on the session's next progress once neither
	// the session nor this panel holds it.
	peer.busy = true;
	for (int i = 0; i < 100; ++i)
		watch.tick();
	CHECK(peer.calls.size() == 1 && !watch.pending());
	peer.busy = false;
	blocked = true;
	watch.poll();
	events();
	CHECK(peer.calls.size() == 1);
	blocked = false;
	watch.poll();
	events();
	CHECK(peer.calls.size() == 2 && watch.pending());
	bool attempted = false;
	QObject::connect(watch.model(), &QAbstractItemModel::dataChanged, &owner, [&] {
		attempted = true;
		watch.clear();
		watch.remove(0);
		watch.setIndividual(true);
		CHECK(watch.count() == 1 && !watch.individual());
	});
	peer.finish();
	watch.poll();
	events();
	CHECK(attempted && peer.calls.size() == 2);
	watch.stop();
	CHECK(!watch.active());
	CHECK(watch.begin(false));
	{
		auto removed = std::make_unique<ux::DynamicWatchlist>(&owner);
		removed->setSource(peer.source());
	}
	peer.finish();
	watch.poll();
	events();
	CHECK(!watch.active());
	CHECK(watch.begin(false));
	bool retired = false;
	QObject::connect(watch.model(), &QAbstractItemModel::dataChanged, &owner, [&] {
		if (std::exchange(retired, true))
			return;  // unbinding the kept rows publishes too (WL-4c)
		peer.current = false;
		watch.retire(); // Defer the structural reset until model publication returns.
	});
	peer.finish();
	watch.poll();
	events();
	CHECK(retired && watch.count() > 0 && unbound(watch, watch.count()) && !watch.active());
}

void replaceSourceDuringPublication()
{
	QObject owner;
	Peer old, fresh;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(old.source());
	fill(watch, 2);
	CHECK(watch.begin(false));
	bool replaced = false;
	QObject::connect(watch.model(), &QAbstractItemModel::dataChanged, &owner, [&] {
		if (replaced)
			return;
		replaced = true;
		watch.setSource(fresh.source());
		CHECK(text(watch, 0).isEmpty() && text(watch, 1).isEmpty());
		CHECK(watch.active()); // Reset/attachment must settle after model publication.
	});
	old.finish();
	watch.poll();
	CHECK(replaced && text(watch, 0).isEmpty() && text(watch, 1).isEmpty());
	CHECK(fresh.calls.empty());
	events();
	CHECK(!watch.active() && unbound(watch, 2));
	watch.clear();
	fill(watch, 1);
	CHECK(watch.begin(false));
	fresh.finish();
	watch.poll();
	events();
	CHECK(!watch.active() && fresh.calls.size() == 1);
	CHECK(text(watch, 0) == QStringLiteral("18446744073709551615"));
}

QByteArray raw(const ux::WatchValueBits& value)
{
	return QByteArray(reinterpret_cast<const char*>(value.bytes.data()), value.size);
}

void exactCompletionFrames()
{
	QObject owner;
	Peer old, fresh;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(old.source());
	fill(watch, 6);
	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) {
		CHECK(!old.inCallback && !fresh.inCallback);
		frames.push_back(frame);
	});
	CHECK(watch.begin(false));
	old.finish();
	CHECK(frames.empty()); // filling the native completion slot publishes nothing
	watch.poll(); events();
	CHECK(frames.size() == 1 && frames[0].count == 6 && frames[0].exact && frames[0].correlated);
	CHECK(frames[0].phase == ux::WatchCompletionPhase::Current && frames[0].operation == ux::WatchOperation::ReadMany);
	CHECK(frames[0].sequence == 1 && frames[0].cycle == 1 && frames[0].portion == 0 && frames[0].sourceBinding > 0);
	CHECK(frames[0].completed.monotonicNs >= frames[0].startedNs);
	CHECK(raw(frames[0].rows[0].value) == QByteArray::fromHex("ffffffffffffffff"));
	CHECK(raw(frames[0].rows[1].value) == QByteArray::fromHex("0000000000000080"));
	CHECK(frames[0].rows[2].value.type.enumeration && frames[0].rows[2].value.type.scalar == ux::WatchScalar::S16 &&
	      raw(frames[0].rows[2].value) == QByteArray::fromHex("85ff"));
	CHECK(raw(frames[0].rows[3].value) == QByteArray::fromHex("4523c17f"));
	CHECK(raw(frames[0].rows[4].value) == QByteArray::fromHex("00000080"));
	CHECK(raw(frames[0].rows[5].value) == QByteArray::fromHex("0000000000000080"));
	watch.setIndividual(true);
	CHECK(watch.begin(false));
	for (int i = 0; i != 6; ++i) { old.finish(); watch.poll(); events(); }
	CHECK(frames.size() == 7 && !watch.active());
	for (int i = 0; i != 6; ++i) {
		const auto& frame = frames[std::size_t(i + 1)];
		CHECK(frame.operation == ux::WatchOperation::FieldRead && frame.count == 1 && frame.rows[0].id == quint32(i + 1));
		CHECK(frame.sequence == i + 2 && frame.cycle == 2 && frame.portion == i && frame.exact);
	}
	watch.setIndividual(false);
	CHECK(watch.begin(false));
	const auto slot = old.completion;
	old.finish(cl::Status::Timeout);
	(*slot)->count = 0; // a real aggregate failure is allowed to return no items
	watch.poll(); events();
	CHECK(frames.size() == 8 && frames.back().phase == ux::WatchCompletionPhase::Failed && frames.back().count == 6);
	CHECK(frames.back().native.client == qint32(cl::Status::Timeout) && !frames.back().correlated);
	for (int i = 0; i != 6; ++i) CHECK(frames.back().rows[std::size_t(i)].id == quint32(i + 1) &&
	    frames.back().rows[std::size_t(i)].value.size == 0 && frames.back().rows[std::size_t(i)].value.type.valid());
	CHECK(watch.begin(false));
	watch.stop();
	old.finish(); watch.poll(); events();
	CHECK(frames.size() == 9 && frames.back().phase == ux::WatchCompletionPhase::Drained && !watch.active());
	CHECK(watch.begin(false));
	watch.setSource(fresh.source());
	old.finish(); watch.poll(); events();
	CHECK(frames.size() == 10 && frames.back().phase == ux::WatchCompletionPhase::Drained && unbound(watch, 6));
	const auto oldBinding = frames.back().sourceBinding;
	watch.clear();
	fill(watch, 1);
	CHECK(watch.begin(false));
	fresh.finish(); watch.poll(); events();
	CHECK(frames.size() == 11 && frames.back().phase == ux::WatchCompletionPhase::Current && frames.back().sourceBinding > oldBinding);
	CHECK(watch.begin(false));
	const auto unavailable = fresh.completion;
	fresh.finish();
	(*unavailable)->rows[0].itemStatus = 3;
	(*unavailable)->rows[0].valueStatus = 2;
	(*unavailable)->rows[0].value.size = 0;
	(*unavailable)->rows[0].status = QStringLiteral("Unavailable");
	(*unavailable)->rows[0].text.clear();
	(*unavailable)->rows[0].sample = {};
	watch.poll(); events();
	CHECK(frames.size() == 12 && frames.back().phase == ux::WatchCompletionPhase::Current && frames.back().exact);
	CHECK(frames.back().rows[0].itemStatus == 3 && frames.back().rows[0].valueStatus == 2 && frames.back().rows[0].value.size == 0);
	CHECK(watch.begin(false));
	fresh.finish(cl::Status::Ok, true); watch.poll(); events();
	CHECK(frames.size() == 13 && frames.back().phase == ux::WatchCompletionPhase::Failed && !frames.back().correlated);
	CHECK(frames.back().rows[0].id == 1 && frames.back().rows[0].value.size == 0);
	// One direct bounded recorder can halt the next cycle on backpressure. It
	// cannot recursively consume or start another request from this publication.
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) {
		frames.push_back(frame);
		watch.stop();
		CHECK(!watch.begin(true));
		watch.poll();
	});
	CHECK(watch.begin(true));
	fresh.finish(); watch.poll(); events();
	CHECK(frames.size() == 14 && !watch.active() && fresh.calls.size() == 4);
	const auto frameCount = frames.size();
	watch.poll(); events();
	CHECK(frames.size() == frameCount);
	// Destruction in a public terminal observer cannot continue with row writes.
	auto doomedOwner = std::make_unique<QObject>();
	auto doomed = std::make_unique<ux::DynamicWatchlist>(doomedOwner.get());
	Peer peer;
	doomed->setSource(peer.source()); fill(*doomed, 1);
	int destroyedFrames = 0;
	doomed->setCompletionObserver([&](const ux::WatchFrame&) {
		++destroyedFrames;
		doomedOwner.reset();
		doomed.reset();
	});
	CHECK(doomed->begin(false)); peer.finish(); doomed->poll();
	CHECK(!doomedOwner && !doomed && destroyedFrames == 1 && peer.calls.size() == 1);
}
} // namespace

// A Watchlist entry as a host names it; its type and write scope are the
// Watchlist's to fill in.
ux::WatchEntry entryOf(ux::WatchKey key, const QString& name, const QString& member = {})
{
	ux::WatchEntry entry;
	entry.key = key;
	entry.name = name;
	entry.member = member;
	return entry;
}

// The Config values of the members fixture, as the Device tree would name them.
QVector<ux::WatchEntry> configEntries(std::initializer_list<quint32> ordinals)
{
	QVector<ux::WatchEntry> entries;
	for (const auto ordinal : ordinals)
		entries.push_back(entryOf(ux::watch_key::member(members::config, ordinal),
		                          QStringLiteral("device/Config #%1").arg(ordinal)));
	return entries;
}

// One cycle's reads: whole Fields together in ReadMany portions, every value
// of one struct in one read, wherever its rows are, in ascending order.
// Rows for paths a device lacks, and a Watchlist kept across a disconnect (WL-4c).
void missingRows()
{
	QObject owner;
	Peer peer;
	peer.schema = std::make_shared<members::Schema>();
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	auto* model = qobject_cast<ux::WatchlistModel*>(watch.model());
	CHECK(model);
	const auto nope = watch.missingEntry(QStringLiteral("device/Nope"));
	CHECK(ux::watch_key::isUnresolved(nope.key) && nope.name == "device/Nope");
	const auto enabled = watch.resolvePath(QStringLiteral("device/Config.enabled")).entries;
	const auto gain = watch.resolvePath(QStringLiteral("device/Config.gain")).entries;
	CHECK(enabled.size() == 1 && gain.size() == 1);
	CHECK(watch.addEntries({enabled.first(), nope, gain.first()}).value("added").toInt() == 3);
	CHECK(model->isUnbound(1) && model->data(model->index(1, 0), ux::WatchlistModel::StatusRole) == "Missing");
	CHECK(model->data(model->index(1, 0), ux::WatchlistModel::AccessRole).toString().isEmpty());
	// Never held, never read; saved by its path.
	CHECK(!watch.held().contains(nope.key) && watch.held().size() == 2 && model->hasBound());
	CHECK(watch.binding(1) == QVariantMap({{"group", "device"}, {"name", "Nope"}}));
	CHECK(watch.begin(false));
	peer.finishMembers(); watch.poll(); events();
	CHECK(!watch.active());
	const auto read = model->data(model->index(2, 0), ux::WatchlistModel::TextRole).toString();
	CHECK(!read.isEmpty());
	// The device goes: every row stays, with its path and its last value, stale.
	QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
	watch.retire(true);
	events();
	CHECK(unbound(watch, 3) && changed.count() == 1);
	// With no device a row says nothing of devices, even one the last lacked.
	CHECK(model->data(model->index(1, 0), ux::WatchlistModel::StatusRole).toString().isEmpty());
	CHECK(model->data(model->index(2, 0), ux::WatchlistModel::NameRole) == "device/Config.gain");
	CHECK(model->data(model->index(2, 0), ux::WatchlistModel::StatusRole) == "Stale");
	CHECK(model->data(model->index(2, 0), ux::WatchlistModel::TextRole) == read);
	// While it is away, polling unbinds nothing again and says nothing.
	watch.poll(); events();
	watch.retire(true); events();
	CHECK(changed.count() == 1);
	// It comes back: the rows it has are found again in place; the other stays.
	watch.setSource(peer.source());
	events();
	CHECK(model->count() == 3 && !model->isUnbound(0) && model->isUnbound(1) && !model->isUnbound(2));
	CHECK(model->key(0) == enabled.first().key && model->key(2) == gain.first().key);
	CHECK(model->data(model->index(1, 0), ux::WatchlistModel::StatusRole) == "Missing");
	CHECK(model->data(model->index(2, 0), ux::WatchlistModel::StatusRole) == "Stale");
	CHECK(model->data(model->index(2, 0), ux::WatchlistModel::TextRole) == read);
	CHECK(model->data(model->index(2, 0), ux::WatchlistModel::AccessRole).toString().size() == 2);
	// A row typed for a whole struct becomes its values, in its place.
	CHECK(watch.addEntries({watch.missingEntry(QStringLiteral("device/Config"))}).value("added").toInt() == 1);
	watch.retire(true);
	events();
	watch.setSource(peer.source());
	events();
	// enabled, Nope, gain, then Config's 300 samples (gain and enabled are watched).
	CHECK(model->count() == int(members::leaves) + 1 && model->isUnbound(1) && !model->isUnbound(3));
	CHECK(model->data(model->index(3, 0), ux::WatchlistModel::NameRole) == "device/Config.samples[0]");
	CHECK(model->data(model->index(model->count() - 1, 0), ux::WatchlistModel::NameRole) == "device/Config.samples[299]");
	// Only rows not on this device: Start has nothing to read and says so.
	watch.clear();
	CHECK(watch.addEntries({watch.missingEntry(QStringLiteral("device/Nope"))}).value("added").toInt() == 1);
	const auto calls = peer.calls.size();
	CHECK(!watch.begin(true) && !watch.begin(false));
	events();
	CHECK(peer.calls.size() == calls && !watch.pending() && !watch.active());
	CHECK(watch.status() == "Nothing to read: none of these paths is on this device.");
	// A stop invalidates readings, never what a row without a device says.
	watch.retire(false);
	events();
	CHECK(model->data(model->index(0, 0), ux::WatchlistModel::StatusRole) == "Missing");
	// A struct kept whole above its own member takes every value, the member's
	// too: the member's row goes, its value is in the struct's place.
	const auto reattach = [&] {
		watch.retire(true);
		events();
		watch.setSource(peer.source());
		events();
	};
	watch.clear();
	CHECK(watch.addEntries({watch.missingEntry(QStringLiteral("device/Config")),
	                        watch.missingEntry(QStringLiteral("device/Config.gain"))})
	          .value("added").toInt() == 2);
	reattach();
	CHECK(model->count() == int(members::leaves) && !model->isUnbound(0) && model->hasBound());
	CHECK(model->data(model->index(0, 0), ux::WatchlistModel::NameRole) == "device/Config.gain");
	// Not enough room for the struct's values: it claims none, says why, and the
	// member below keeps its own value.
	watch.clear();
	QVector<ux::WatchEntry> crowd;
	// The Watchlist all but full: room for the struct's row and the member's,
	// not for the struct's values.
	constexpr int pads = ux::watchlistRowLimit - 2;
	for (int i = 0; i < pads; ++i)
		crowd.push_back(watch.missingEntry(QStringLiteral("pad.P%1").arg(i)));
	crowd.push_back(watch.missingEntry(QStringLiteral("device/Config")));
	crowd.push_back(watch.missingEntry(QStringLiteral("device/Config.gain")));
	CHECK(watch.addEntries(crowd).value("added").toInt() == pads + 2);
	reattach();
	CHECK(model->count() == pads + 2 && model->isUnbound(pads) && !model->isUnbound(pads + 1));
	CHECK(model->data(model->index(pads, 0), ux::WatchlistModel::StatusRole) == "Unwatchable");
	CHECK(model->data(model->index(pads, 0), ux::WatchlistModel::WriteRefusalRole).toString().contains("do not fit"));
	CHECK(model->data(model->index(pads + 1, 0), ux::WatchlistModel::NameRole) == "device/Config.gain");
}

// A typed path as the rows it adds, and what may come next (WL-4b).
void pathResolution()
{
	QObject owner;
	Peer peer;
	peer.schema = std::make_shared<members::Schema>();
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	auto r = watch.resolvePath(QStringLiteral(" device/Config.gain "));
	CHECK(r.error.isEmpty() && r.entries.size() == 1 && r.entries[0].name == "device/Config.gain" && r.already == 0);
	CHECK(watch.resolvePath("device/Config.samples").entries.size() == 300);
	CHECK(watch.resolvePath("device/Config").entries.size() == int(members::leaves));
	r = watch.resolvePath("device/Config.samples[3]");
	CHECK(r.entries.size() == 1 && r.entries[0].name == "device/Config.samples[3]" &&
	      r.entries[0].key == ux::watch_key::member(members::config, 5));
	r = watch.resolvePath("device/Counter");
	CHECK(r.entries.size() == 1 && ux::watch_key::isWhole(r.entries[0].key) && r.entries[0].name == "device/Counter");
	CHECK(watch.resolvePath("device/Config.gian").error == "device/Config has no member gian. Did you mean gain?");
	CHECK(watch.resolvePath("device/Config.samples[300]").error ==
	      "device/Config.samples has 300 elements, [0] to [299].");
	r = watch.resolvePath("device/Nope");
	CHECK(r.missing && r.entries.isEmpty() && r.error == "device/Nope is not on this device.");
	for (const auto bad : {"nope", "device.", ".Config", "device/Config."}) {
		r = watch.resolvePath(QString::fromLatin1(bad));
		CHECK(!r.missing && r.entries.isEmpty() && !r.error.isEmpty());
	}
	CHECK(!watch.resolvePath("device/Counter.x").error.isEmpty());
	// Watched already: nothing to add, and which row has it.
	CHECK(watch.addEntries(watch.resolvePath("device/Config.gain").entries).value("added").toInt() == 1);
	r = watch.resolvePath("device/Config.gain");
	CHECK(r.entries.isEmpty() && r.already == 1 && r.firstWatched == ux::watch_key::member(members::config, 0));
	r = watch.resolvePath("device/Config");
	CHECK(r.entries.size() == int(members::leaves) - 1 && r.already == 1);

	// Candidates: groups, Fields, members, elements; then the word anywhere.
	const auto paths = [&](const char* text) {
		QStringList out;
		for (const auto& c : watch.pathCandidates(QString::fromLatin1(text)))
			out << c.path;
		return out;
	};
	auto c = watch.pathCandidates("dev");
	CHECK(c.size() == 1 && c[0].path == "device/" && c[0].container && c[0].values == 2);
	c = watch.pathCandidates("device/C");
	CHECK(c.size() == 2 && c[0].path == "device/Counter" && !c[0].container && c[0].values == 1);
	CHECK(c[1].path == "device/Config" && c[1].container && c[1].typeText == "struct (3)" &&
	      c[1].values == members::leaves && !c[1].watched);
	CHECK(paths("device/Config.") ==
	      QStringList({"device/Config.gain", "device/Config.enabled", "device/Config.samples"}));
	CHECK(paths("device/Config.sa") == QStringList({"device/Config.samples"}));
	c = watch.pathCandidates("device/Config.samples[");
	CHECK(c.size() == 1 && c[0].path == QStringLiteral("device/Config.samples[0 … 299]") && c[0].typeText == "u16");
	CHECK(paths("gain").contains("device/Config.gain"));
	CHECK(watch.pathCandidates("device/Config.samples[", 0).isEmpty());
}

// A group whose name holds dots ("power.meter"): in the one grammar its dot is
// escaped ("power\.meter/Voltage"); an older dotted key is read with the
// longest group it starts with, then the Field (WL-8, the review's 15.1).
void dottedGroups()
{
	QObject owner;
	Peer peer;
	peer.dotted = std::make_shared<dotted::Schema>();
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	const auto paths = [&](const char* text) {
		QStringList out;
		for (const auto& c : watch.pathCandidates(QString::fromLatin1(text)))
			out << c.path;
		return out;
	};
	auto r = watch.resolvePath(QStringLiteral("power\\.meter/Voltage"));
	CHECK(r.error.isEmpty() && r.entries.size() == 1 && r.entries[0].name == "power\\.meter/Voltage" &&
	      ux::watch_key::isWhole(r.entries[0].key));
	const auto voltage = r.entries[0].key;
	// The older key names the same value, written in the one grammar.
	r = watch.resolvePath(QStringLiteral("power.meter.Voltage"));
	CHECK(r.error.isEmpty() && r.entries.size() == 1 && r.entries[0].key == voltage &&
	      r.entries[0].name == "power\\.meter/Voltage");
	r = watch.resolvePath(QStringLiteral("power/Total"));
	CHECK(r.error.isEmpty() && r.entries.size() == 1 && ux::watch_key::isWhole(r.entries[0].key) &&
	      r.entries[0].key != voltage);
	r = watch.resolvePath(QStringLiteral("power.Total"));
	CHECK(r.error.isEmpty() && r.entries.size() == 1 && r.entries[0].name == "power/Total");
	r = watch.resolvePath(QStringLiteral("power\\.meter/Nope"));
	CHECK(r.missing && r.error == "power\\.meter/Nope is not on this device.");
	r = watch.resolvePath(QStringLiteral("power.meter.Nope"));
	CHECK(r.missing && r.error == "power.meter.Nope is not on this device.");
	// One segment at a time: the groups that go on from what is typed,
	// escaped, then a group's Fields.
	CHECK(paths("pow") == QStringList({"power/", "power\\.meter/"}));
	CHECK(paths("power/") == QStringList({"power/Total"}));
	CHECK(paths("power\\.") == QStringList({"power\\.meter/"}));
	CHECK(paths("power\\.meter/") == QStringList({"power\\.meter/Voltage", "power\\.meter/Current"}));
	CHECK(paths("power\\.meter/C") == QStringList({"power\\.meter/Current"}));
	// An older dotted start reads as the "/" path it begins.
	CHECK(paths("power.meter.") == QStringList({"power\\.meter/Voltage", "power\\.meter/Current"}));
	// Added by the path, saved by its group and name, as before.
	CHECK(watch.addEntries(watch.resolvePath("power\\.meter/Current").entries).value("added").toInt() == 1);
	CHECK(watch.binding(0) == QVariantMap({{"group", "power.meter"}, {"name", "Current"}}));
	// Kept without the Field (an older key), saved by the same group.
	CHECK(watch.addEntries({watch.missingEntry("power.meter.Frequency")}).value("added").toInt() == 1);
	watch.rebindRows();
	CHECK(watch.binding(1) == QVariantMap({{"group", "power.meter"}, {"name", "Frequency"}}));
}

void memberPlan()
{
	QObject owner;
	Peer peer;
	peer.schema = std::make_shared<members::Schema>();
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	auto* model = watch.model();
	QVector<ux::WatchEntry> rows{entryOf(ux::watch_key::whole(2), "device/Two")};
	rows += configEntries({5});
	rows.push_back(entryOf(ux::watch_key::whole(3), "device/Three"));
	rows += configEntries({0});
	rows.push_back(entryOf(ux::watch_key::whole(4), "device/Four"));
	// Refused: past the last value, a scalar Field's "member", a wrong member text.
	rows += configEntries({members::leaves});
	rows.push_back(entryOf(ux::watch_key::member(0, 0), "device/Counter"));
	rows.push_back(entryOf(ux::watch_key::member(members::config, 1), "device/Config.enabled", "gain"));
	rows += configEntries({5});
	const auto added = watch.addEntries(rows);
	CHECK(added.value("status") == "ok" && added.value("added").toInt() == 5);
	CHECK(added.value("unsupported").toInt() == 3 && added.value("duplicate").toInt() == 1);
	CHECK(watch.held().contains(ux::watch_key::member(members::config, 5)) && watch.held().contains(2));
	const auto memberRow = model->index(1, 0);
	CHECK(model->data(memberRow, ux::WatchlistModel::MemberRole).toString() == "samples[3]");
	CHECK(model->data(memberRow, ux::WatchlistModel::WatchKeyRole).toULongLong() ==
	      ux::watch_key::member(members::config, 5));
	CHECK(model->data(memberRow, ux::WatchlistModel::EndpointIdRole).toUInt() == members::config);
	CHECK(model->data(memberRow, ux::WatchlistModel::TypeTextRole).toString() == "u16");
	CHECK(model->data(model->index(3, 0), ux::WatchlistModel::TypeTextRole).toString() == "f32");
	CHECK(model->data(model->index(0, 0), ux::WatchlistModel::MemberRole).toString().isEmpty());
	// Each step's type for the tree's nodes: the Field's, then down to the value's.
	CHECK(model->data(memberRow, ux::WatchlistModel::PathTypesRole).toStringList() ==
	      QStringList({"struct (3)", "u16[300]", "u16"}));
	CHECK(model->data(model->index(3, 0), ux::WatchlistModel::PathTypesRole).toStringList() ==
	      QStringList({"struct (3)", "f32"}));
	CHECK(model->data(model->index(0, 0), ux::WatchlistModel::PathTypesRole).toStringList().size() <= 1);  // the Peer's whole Fields are not in its schema

	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	CHECK(watch.begin(false));
	CHECK(peer.calls.size() == 1 && peer.calls[0] == std::vector<std::uint32_t>{2, 3, 4});
	CHECK(watch.status() == "Reading values 1-3 of 5");
	peer.finish(); watch.poll(); events();
	CHECK(peer.memberCalls.size() == 1 && peer.memberCalls[0].first == members::config &&
	      peer.memberCalls[0].second == std::vector<std::uint32_t>{0, 5});
	peer.finishMembers(); watch.poll(); events();
	CHECK(!watch.active() && watch.status() == "ReadMany complete - 5 values");
	CHECK(model->data(model->index(1, 0), ux::WatchlistModel::TextRole).toString() == "5");
	CHECK(model->data(model->index(3, 0), ux::WatchlistModel::TextRole).toString() == "0");
	CHECK(model->data(model->index(3, 0), ux::WatchlistModel::SampleValueRole).toDouble() == 0.0);
	CHECK(frames.size() == 2 && frames[1].operation == ux::WatchOperation::FieldRead && frames[1].count == 2);
	CHECK(frames[1].rows[0].id == members::config && frames[1].rows[0].leaf == 1 && frames[1].rows[1].leaf == 6);
	CHECK(frames[0].rows[0].leaf == 0 && frames[1].phase == ux::WatchCompletionPhase::Current && frames[1].exact);
	CHECK(frames[1].sequence == frames[0].sequence + 1 && frames[1].portion == 1);

	// Individual mode: one FieldRead per whole Field, the struct still one read.
	peer.calls.clear(); peer.memberCalls.clear();
	watch.setIndividual(true);
	CHECK(watch.begin(false));
	for (const quint32 id : {2u, 3u, 4u}) {
		CHECK(peer.calls.size() == 1 + std::size_t(id - 2) && peer.calls.back() == std::vector<std::uint32_t>{id});
		if (id == 2) {
			// The struct's read comes where its first row is: after Field 2.
			peer.finish(); watch.poll(); events();
			CHECK(peer.memberCalls.size() == 1);
			peer.finishMembers(); watch.poll(); events();
		} else {
			peer.finish(); watch.poll(); events();
		}
	}
	CHECK(!watch.active() && watch.status() == "Individual FieldRead complete - 5 values");

	// Taken out by key, put back in place by path (UR-1): kept by its path,
	// then found on the device - the schema has device.Config.gain, not the
	// Peer's whole Field 3, which stays a row that says so. (This fixture
	// names its rows "device/Config #0"; a path is what is put back.)
	const auto removed = watch.removeMany({ux::watch_key::member(members::config, 0), 3});
	CHECK(removed.size() == 2 && removed[0].row == 2 && removed[1].row == 3 && watch.count() == 3);
	CHECK(removed[1].entry.member == "gain" && removed[1].entry.type != 0);
	CHECK(watch.putBack({{removed[1].row, QStringLiteral("device/Config.gain")}, {removed[0].row, removed[0].entry.name}}) &&
	      watch.count() == 5);
	watch.rebindRows();
	CHECK(model->data(model->index(3, 0), ux::WatchlistModel::MemberRole).toString() == "gain" &&
	      model->data(model->index(3, 0), ux::WatchlistModel::WatchKeyRole).toULongLong() ==
	          ux::watch_key::member(members::config, 0));
	CHECK(model->data(model->index(2, 0), ux::WatchlistModel::NameRole).toString() == "device/Three" &&
	      model->data(model->index(2, 0), ux::WatchlistModel::StatusRole).toString() == "Missing");
	// Past the room: refused, nothing put back.
	QVector<ux::DynamicWatchlist::PathRow> tooMany;
	for (int at = 0; at <= ux::WatchlistModel::maxRows - watch.count(); ++at)
		tooMany.push_back({at, QStringLiteral("device/X%1").arg(at)});
	CHECK(!watch.putBack(tooMany) && watch.count() == 5);
	// With no device, rows go back as a disconnect keeps them: by path,
	// saying nothing of devices, at their places cut to the rows there.
	watch.retire();
	events();
	CHECK(watch.putBack({{99, QStringLiteral("device/Config.enabled")}, {0, QStringLiteral("device/Five")}}) &&
	      watch.count() == 7);
	CHECK(model->data(model->index(0, 0), ux::WatchlistModel::NameRole).toString() == "device/Five" &&
	      model->data(model->index(6, 0), ux::WatchlistModel::NameRole).toString() == "device/Config.enabled" &&
	      model->data(model->index(6, 0), ux::WatchlistModel::StatusRole).toString().isEmpty() &&
	      ux::watch_key::isUnresolved(model->data(model->index(6, 0), ux::WatchlistModel::WatchKeyRole).toULongLong()));
}

// All 302 values of one struct: one read, published as ten frames of at most
// 32 rows with consecutive sequences, the same cycle and portion.
void memberFanOut()
{
	QObject owner;
	Peer peer;
	peer.schema = std::make_shared<members::Schema>();
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	QVector<ux::WatchEntry> all;
	for (quint32 n = 0; n < members::leaves; ++n)
		all += configEntries({n});
	CHECK(watch.addEntries(all).value("added").toInt() == int(members::leaves));
	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	CHECK(watch.begin(false));
	CHECK(peer.calls.empty() && peer.memberCalls.size() == 1 && peer.memberCalls[0].second.size() == members::leaves);
	CHECK(watch.status() == "Reading values 1-302 of 302");
	peer.finishMembers(); watch.poll(); events();
	CHECK(!watch.active() && peer.memberCalls.size() == 1 && frames.size() == 10);
	for (std::size_t k = 0; k != frames.size(); ++k) {
		const auto& frame = frames[k];
		CHECK(frame.sequence == frames[0].sequence + qint64(k) && frame.cycle == frames[0].cycle &&
		      frame.portion == 0 && frame.startedNs == frames[0].startedNs);
		CHECK(frame.operation == ux::WatchOperation::FieldRead && frame.phase == ux::WatchCompletionPhase::Current &&
		      frame.correlated && frame.exact && !frame.urgent);
		CHECK(frame.count == (k < 9 ? 32 : 14));
		for (int i = 0; i < frame.count; ++i)
			CHECK(frame.rows[std::size_t(i)].id == members::config &&
			      frame.rows[std::size_t(i)].leaf == quint32(k * 32 + std::size_t(i) + 1));
	}
	auto* model = watch.model();
	CHECK(model->data(model->index(301, 0), ux::WatchlistModel::TextRole).toString() == "301");
	CHECK(model->data(model->index(301, 0), ux::WatchlistModel::MemberRole).toString() == "samples[299]");
	CHECK(model->data(model->index(0, 0), ux::WatchlistModel::StatusRole).toString() == "Ok");
	// A second cycle numbers its frames on.
	CHECK(watch.begin(false));
	peer.finishMembers(); watch.poll(); events();
	CHECK(frames.size() == 20 && frames[10].sequence == frames[9].sequence + 1 && frames[10].cycle == frames[9].cycle + 1);
}

// A struct written while polling is read again before the cycle goes on, as
// one read of its watched values; a struct not yet reached is not.
void memberUrgent()
{
	QObject owner;
	Peer peer;
	peer.schema = std::make_shared<members::Schema>();
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	auto rows = configEntries({1, 0});
	rows.push_back(entryOf(ux::watch_key::whole(2), "device/Two"));
	CHECK(watch.addEntries(rows).value("added").toInt() == 3);
	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	CHECK(watch.begin(true));
	CHECK(peer.memberCalls.size() == 1 && peer.memberCalls[0].second == std::vector<std::uint32_t>{0, 1});
	watch.wake(2);               // not reached yet: read in its turn
	watch.wake(members::config); // in flight: sent before the write, so read again
	watch.wake(members::config); // once is enough
	peer.finishMembers(); watch.poll(); events();
	CHECK(peer.calls.empty() && peer.memberCalls.size() == 2 &&
	      peer.memberCalls[1].second == std::vector<std::uint32_t>{0, 1});
	CHECK(watch.status() == "Reading a written Field again");
	peer.finishMembers(); watch.poll(); events();
	CHECK(peer.calls.size() == 1 && peer.calls[0] == std::vector<std::uint32_t>{2} && peer.memberCalls.size() == 2);
	CHECK(frames.size() == 2 && !frames[0].urgent && frames[1].urgent &&
	      frames[1].phase == ux::WatchCompletionPhase::Current);
	CHECK(frames[1].count == 2 && frames[1].rows[0].leaf == 1 && frames[1].rows[1].leaf == 2);
	peer.finish(); watch.poll(); events();
	CHECK(frames.size() == 3 && !frames[2].urgent && !peer.busy);
	watch.stop();
	events();
}

// What stops a cycle at a struct's read: no member reads on this connection,
// a reply that names other values, a failed read.
void memberFailure()
{
	{
		QObject owner;
		Peer peer;
		peer.schema = std::make_shared<members::Schema>();
		auto source = peer.source();
		source.readMembers = {};
		ux::DynamicWatchlist watch(&owner);
		watch.setSource(std::move(source));
		const auto added = watch.addEntries(configEntries({0, 1}));
		CHECK(added.value("added").toInt() == 0 && added.value("unsupported").toInt() == 2);
	}
	for (const bool skew : {true, false}) {
		QObject owner;
		Peer peer;
		peer.schema = std::make_shared<members::Schema>();
		ux::DynamicWatchlist watch(&owner);
		watch.setSource(peer.source());
		auto rows = configEntries({0, 1});
		rows.push_back(entryOf(ux::watch_key::whole(2), "device/Two"));
		CHECK(watch.addEntries(rows).value("added").toInt() == 3);
		std::vector<ux::WatchFrame> frames;
		watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
		CHECK(watch.begin(true));
		peer.finishMembers(skew ? cl::Status::Ok : cl::Status::CodecError, skew);
		watch.poll(); events();
		CHECK(!watch.active() && peer.calls.empty() && watch.status().startsWith("Watch read failed"));
		CHECK(frames.size() == 1 && frames[0].phase == ux::WatchCompletionPhase::Failed);
		CHECK(frames[0].correlated == !skew);
		auto* model = watch.model();
		CHECK(model->data(model->index(0, 0), ux::WatchlistModel::StatusRole).toString() == "Stale");
	}
}

// Fix 8: the Watchlist takes changes while it polls. A change enters the
// list at once; the read in flight finishes as it was asked and is applied
// to the rows still there; the plan follows at the next portion.
void removeDuringRead()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 3);
	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	CHECK(watch.begin(true));
	const auto asked = watch.membership();
	const auto removed = watch.removeMany({2});
	CHECK(removed.size() == 1 && watch.count() == 2 && watch.active() && watch.membership() != asked);
	peer.finish(); watch.poll(); events();
	CHECK(frames.size() == 1 && frames[0].phase == ux::WatchCompletionPhase::Current && frames[0].correlated &&
	      frames[0].count == 3 && frames[0].membership == asked);
	CHECK(text(watch, 0) == "18446744073709551615" && text(watch, 1) == "Unknown (-123)");
	CHECK(watch.active() && !watch.status().startsWith("Watch read failed"));
	// The next cycle reads the rows there now.
	watch.tick();
	CHECK(peer.calls.size() == 2 && peer.calls[1] == std::vector<std::uint32_t>{1, 3});
	peer.finish(); watch.poll(); events();
	CHECK(frames.size() == 2 && frames[1].membership == watch.membership());
	watch.stop();
	events();
}

void addDuringCycle()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 40);
	std::vector<ux::WatchFrame> frames;
	watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
	CHECK(watch.begin(true));
	CHECK(peer.calls.size() == 1 && peer.calls[0].size() == 32);
	CHECK(watch.add(50, QStringLiteral("catalog.Field50")) && watch.count() == 41);
	// The new row waits for the next portion boundary; its value is empty.
	CHECK(text(watch, 40).isEmpty());
	peer.finish(); watch.poll(); events();
	// A new cycle over the rows shown now: it starts again from the first row.
	CHECK(peer.calls.size() == 2 && peer.calls[1].size() == 32 && peer.calls[1][0] == 1);
	CHECK(frames.size() == 1 && frames[0].phase == ux::WatchCompletionPhase::Current);
	peer.finish(); watch.poll(); events();
	CHECK(peer.calls.size() == 3 && peer.calls[2].size() == 9 && peer.calls[2].back() == 50);
	peer.finish(); watch.poll(); events();
	CHECK(frames.size() == 3 && frames[2].cycle == frames[1].cycle && frames[1].cycle == frames[0].cycle + 1);
	CHECK(text(watch, 40) == "50");
	watch.stop();
	events();
}

void addBetweenCycles()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 2);
	CHECK(watch.begin(true));
	peer.finish(); watch.poll(); events();
	CHECK(peer.calls.size() == 1 && watch.active() && !watch.pending());
	// Polling between two cycles: an addition is read now, not at the tick.
	const auto added = watch.addEntries({entryOf(ux::watch_key::whole(3), QStringLiteral("catalog.Field3"))});
	CHECK(added.value("added").toInt() == 1);
	for (int step = 0; step < 4; ++step) events(); // notice, then start, then send
	CHECK(peer.calls.size() == 2 && peer.calls[1] == std::vector<std::uint32_t>{1, 2, 3});
	peer.finish(); watch.poll(); events();
	// A removal waits for the timer.
	watch.remove(0);
	events();
	CHECK(peer.calls.size() == 2 && watch.count() == 2);
	watch.tick();
	CHECK(peer.calls.size() == 3 && peer.calls[2] == std::vector<std::uint32_t>{2, 3});
	peer.finish(); watch.poll(); events();
	watch.stop();
	events();
}

void memberChangeDuringRead()
{
	QObject owner;
	Peer peer;
	peer.schema = std::make_shared<members::Schema>();
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	CHECK(watch.addEntries(configEntries({0, 1})).value("added").toInt() == 2);
	CHECK(watch.begin(true));
	CHECK(peer.memberCalls.size() == 1 && peer.memberCalls[0].second == std::vector<std::uint32_t>{0, 1});
	CHECK(watch.removeMany({ux::watch_key::member(members::config, 1)}).size() == 1);
	// A value added to the struct joins its read from the next cycle on.
	CHECK(watch.addEntries(configEntries({5})).value("added").toInt() == 1);
	peer.finishMembers(); watch.poll(); events();
	CHECK(text(watch, 0) == "0" && text(watch, 1).isEmpty() && !watch.status().startsWith("Watch read failed"));
	CHECK(peer.memberCalls.size() == 2 && peer.memberCalls[1].second == std::vector<std::uint32_t>{0, 5});
	peer.finishMembers(); watch.poll(); events();
	CHECK(text(watch, 1) == "5");
	watch.stop();
	events();
}

void clearWhilePolling()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 3);
	CHECK(watch.begin(true));
	watch.clear();
	CHECK(watch.count() == 0 && watch.active());
	peer.finish(); watch.poll(); events();
	// Nothing to read: polling idles without spinning and says so.
	CHECK(watch.active() && !watch.pending() && peer.calls.size() == 1);
	CHECK(watch.status() == "Nothing to read. Add a value, or Stop.");
	watch.tick();
	events();
	CHECK(peer.calls.size() == 1);
	CHECK(watch.add(7, QStringLiteral("catalog.Field7")));
	for (int step = 0; step < 4; ++step) events();
	CHECK(peer.calls.size() == 2 && peer.calls[1] == std::vector<std::uint32_t>{7});
	peer.finish(); watch.poll(); events();
	watch.stop();
	events();
}

void urgentAfterChange()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 40);
	CHECK(watch.begin(true));
	watch.wake(1);   // in flight: would be read again before the cycle goes on
	watch.remove(39);
	peer.finish(); watch.poll(); events();
	// The change replans the cycle, which reads every row: no separate read of 1.
	CHECK(peer.calls.size() == 2 && peer.calls[1].size() == 32 && peer.calls[1][0] == 1);
	peer.finish(); watch.poll(); events();
	CHECK(peer.calls.size() == 3 && peer.calls[2].size() == 7);
	peer.finish(); watch.poll(); events();
	watch.stop();
	events();
}

void readOnceReplans()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 40);
	CHECK(watch.begin(false));
	CHECK(watch.add(60, QStringLiteral("catalog.Field60")));
	peer.finish(); watch.poll(); events();
	// Read once reads the rows shown when it finishes, the added one too.
	CHECK(peer.calls.size() == 2 && peer.calls[1][0] == 1);
	peer.finish(); watch.poll(); events();
	CHECK(peer.calls.size() == 3 && peer.calls[2].back() == 60);
	peer.finish(); watch.poll(); events();
	CHECK(!watch.active() && text(watch, 40) == "60");
}

void changeInsideCompletion()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	fill(watch, 2);
	bool refused = false;
	watch.setCompletionObserver([&](const ux::WatchFrame&) {
		refused = !watch.add(9, QStringLiteral("catalog.Field9")) && watch.removeMany({1}).isEmpty() &&
		          watch.addEntries({entryOf(ux::watch_key::whole(8), QStringLiteral("catalog.Field8"))})
		                  .value("status") == "refused";
	});
	CHECK(watch.begin(false));
	peer.finish(); watch.poll(); events();
	CHECK(refused && watch.count() == 2);
}

// The plan on its own (ReadPortions.hpp): the portions, their order, the
// budgets, when the budget is asked, and where each failure stops it.
void readPortions()
{
	namespace rp = ux::read_portions;
	using Kind = rp::Portion::Kind;
	int asked = 0;
	rp::Source can;
	can.readMany = can.readField = can.readMembers = true;
	can.budget = [&asked] {
		++asked;
		return std::optional<rp::Budget>{rp::Budget{32, 130, 514}};
	};
	QHash<quint32, unsigned> sizes;
	can.fieldBytes = [&sizes](quint32 field) -> std::optional<unsigned> {
		const auto at = sizes.constFind(field);
		return at == sizes.cend() ? std::optional<unsigned>{4u} : std::optional<unsigned>{*at};
	};
	namespace key = ux::watch_key;
	// Nothing watched: no portion, the budget never asked.
	auto none = rp::plan({}, can);
	CHECK(none.portions.empty() && none.of.isEmpty() && !none.members && asked == 0);
	// 65 scalars: 32, 32 and 1, the budget asked once; an unresolved row is skipped.
	std::vector<ux::WatchKey> keys;
	for (quint32 field = 1; field <= 65; ++field)
		keys.push_back(key::whole(field));
	keys.insert(keys.begin() + 10, key::unresolved(3));
	auto many = rp::plan(keys, can);
	CHECK(asked == 1 && many.portions.size() == 3 && !many.members);
	CHECK(many.portions[0].kind == Kind::Many && many.portions[0].ids.size() == 32 && many.portions[1].ids.size() == 32 &&
	      many.portions[2].ids.size() == 1 && many.portions[2].ids[0] == 65);
	CHECK(many.of.value(1) == 0 && many.of.value(33) == 1 && many.of.value(65) == 2 && many.portions[0].rows() == 32);
	// Two 8-byte values among 4-byte ones: the reply stays inside 514 bytes,
	// so the items, not the bytes, close each portion.
	sizes = {{1, 8}, {2, 8}};
	CHECK(rp::plan(std::span{keys}.first(5), can).portions.size() == 1);
	CHECK(rp::plan(keys, can).portions.size() == 3);
	// Struct values: one member read a Field, wherever its rows are, leaves in order.
	keys = {key::member(7, 3), key::whole(1), key::member(7, 1), key::member(8, 0), key::whole(2)};
	sizes.clear();
	auto mixed = rp::plan(keys, can);
	CHECK(mixed.members && mixed.portions.size() == 3);
	CHECK(mixed.portions[0].kind == Kind::Members && mixed.portions[0].field == 7 &&
	      mixed.portions[0].leaves == std::vector<std::uint32_t>({1, 3}) && mixed.portions[0].rows() == 2);
	CHECK(mixed.portions[1].kind == Kind::Many && mixed.portions[1].ids == std::vector<std::uint32_t>({1, 2}));
	CHECK(mixed.portions[2].kind == Kind::Members && mixed.portions[2].field == 8 && mixed.of.value(8) == 2);
	// Individual: one FieldRead a whole Field, the budget not asked.
	asked = 0;
	can.individual = true;
	auto each = rp::plan(std::span{keys}.first(2), can);
	CHECK(asked == 0 && each.portions.size() == 2 && each.portions[1].kind == Kind::Field &&
	      each.portions[1].field == 1 && each.portions[1].rows() == 1);
	can.readField = false;
	auto noField = rp::plan(std::span{keys}.first(2), can);
	CHECK(noField.portions.size() == 2 && noField.portions[1].kind == Kind::Fail &&
	      noField.portions[1].failure == rp::Failure::NoFieldRead && noField.portions[1].rows() == 0);
	can.individual = false;
	can.readField = true;
	// No member read: the plan stops at the first value inside a Field.
	can.readMembers = false;
	auto noMembers = rp::plan(keys, can);
	CHECK(noMembers.members && noMembers.portions.size() == 1 && noMembers.portions[0].failure == rp::Failure::NoMembers);
	can.readMembers = true;
	// No ReadMany, or no budget: whole Fields can't be read together.
	can.readMany = false;
	CHECK(rp::plan(std::span{keys}.subspan(1, 1), can).portions.back().failure == rp::Failure::NoReadMany);
	can.readMany = true;
	const auto budget = can.budget;
	can.budget = [] { return std::optional<rp::Budget>{}; };
	CHECK(rp::plan(std::span{keys}.subspan(1, 1), can).portions.back().failure == rp::Failure::NoReadMany);
	can.budget = budget;
	// A stale or wide Field: what it would have joined is not sent either.
	keys = {key::whole(1), key::whole(2), key::whole(3)};
	sizes = {{3, 0}};
	auto stale = rp::plan(keys, can);
	CHECK(stale.portions.size() == 1 && stale.portions[0].failure == rp::Failure::NotScalar && stale.of.isEmpty());
	sizes = {{1, 16}};
	CHECK(rp::plan(keys, can).portions.front().failure == rp::Failure::NotScalar);
	// A scalar no portion can carry: a reply budget smaller than one item.
	can.budget = [] { return std::optional<rp::Budget>{rp::Budget{32, 130, 12}}; };
	sizes = {{1, 4}};
	CHECK(rp::plan(keys, can).portions.front().failure == rp::Failure::ExceedsReadMany);
	// The item budget: four a portion.
	can.budget = [] { return std::optional<rp::Budget>{rp::Budget{4, 130, 514}}; };
	sizes.clear();
	keys.clear();
	for (quint32 field = 1; field <= 9; ++field)
		keys.push_back(key::whole(field));
	auto four = rp::plan(keys, can);
	CHECK(four.portions.size() == 3 && four.portions[0].ids.size() == 4 && four.portions[2].ids.size() == 1);
	// The request budget: 2 + 4n <= 14 holds three.
	can.budget = [] { return std::optional<rp::Budget>{rp::Budget{32, 14, 514}}; };
	CHECK(rp::plan(keys, can).portions.front().ids.size() == 3);
	can.budget = budget;

	// Struct and array Fields in one Batch where the device has it (WL-6),
	// placed where the first of them is read; the budget asked once.
	int batchAsked = 0;
	rp::Budget batch{32, 4096, 4096};
	can.readBatch = true;
	can.batchBudget = [&] {
		++batchAsked;
		return std::optional<rp::Budget>{batch};
	};
	QHash<quint32, unsigned> structs{{7, 605}, {8, 4}, {9, 605}};
	can.valueBytes = [&structs](quint32 field) -> std::optional<unsigned> {
		const auto at = structs.constFind(field);
		return at == structs.cend() ? std::nullopt : std::optional<unsigned>{*at};
	};
	keys = {key::member(7, 3), key::whole(1), key::member(8, 0), key::member(7, 1), key::member(9, 2)};
	const auto fieldsOf = [](const rp::Portion& step) {
		std::vector<quint32> out;
		for (const auto& member : step.fields)
			out.push_back(member.field);
		return out;
	};
	auto batched = rp::plan(keys, can);
	CHECK(batchAsked == 1 && batched.portions.size() == 2 && batched.portions[0].kind == Kind::Batch &&
	      batched.portions[1].kind == Kind::Many);
	CHECK(fieldsOf(batched.portions[0]) == std::vector<quint32>({7, 8, 9}) &&
	      batched.portions[0].fields[0].leaves == std::vector<std::uint32_t>({1, 3}) && batched.portions[0].rows() == 4);
	CHECK(batched.of.value(7) == 0 && batched.of.value(9) == 0 && batched.of.value(1) == 1);
	// One struct: its own member read, the budget not asked.
	batchAsked = 0;
	auto single = rp::plan(std::span{keys}.first(2), can);
	CHECK(batchAsked == 0 && single.portions[0].kind == Kind::Members);
	// The reply budget: 2 + (12 + 605) + (12 + 4) fits 1000, the second large
	// struct starts a Batch of its own, which as one Field is a member read.
	batch = {32, 4096, 1000};
	auto byReply = rp::plan(keys, can);
	CHECK(byReply.portions.size() == 3 && fieldsOf(byReply.portions[0]) == std::vector<quint32>({7, 8}) &&
	      byReply.portions[2].kind == Kind::Members && byReply.portions[2].field == 9 && byReply.of.value(9) == 2);
	// Too large for any Batch: a member read of its own, and the small one alone too.
	batch = {32, 4096, 600};
	for (const auto& step : rp::plan(keys, can).portions)
		CHECK(step.kind != Kind::Batch);
	// The item and request budgets: two a Batch.
	batch = {2, 4096, 4096};
	auto byItems = rp::plan(keys, can);
	CHECK(fieldsOf(byItems.portions[0]) == std::vector<quint32>({7, 8}) && byItems.portions.back().kind == Kind::Members);
	batch = {32, 18, 4096};
	CHECK(fieldsOf(rp::plan(keys, can).portions[0]) == std::vector<quint32>({7, 8}));
	batch = {32, 4096, 4096};
	// A size the source does not know: that Field alone, the others together.
	structs.remove(8);
	auto unknown = rp::plan(keys, can);
	CHECK(fieldsOf(unknown.portions[0]) == std::vector<quint32>({7, 9}) && unknown.portions[2].kind == Kind::Members &&
	      unknown.portions[2].field == 8);
	structs.insert(8, 4);
	// Individual mode, or no Batch: member reads, as before.
	can.individual = true;
	for (const auto& step : rp::plan(keys, can).portions)
		CHECK(step.kind != Kind::Batch);
	can.individual = false;
	can.readBatch = false;
	for (const auto& step : rp::plan(keys, can).portions)
		CHECK(step.kind != Kind::Batch);
	can.readBatch = true;
	can.batchBudget = [] { return std::optional<rp::Budget>{}; };
	for (const auto& step : rp::plan(keys, can).portions)
		CHECK(step.kind != Kind::Batch);
}

QVector<ux::WatchEntry> twinEntries(quint32 field, std::initializer_list<quint32> ordinals)
{
	QVector<ux::WatchEntry> entries;
	for (const auto ordinal : ordinals) {
		ux::WatchEntry entry;
		entry.key = ux::watch_key::member(field, ordinal);
		entry.name = QStringLiteral("device/F%1 #%2").arg(field).arg(ordinal);
		entries.push_back(entry);
	}
	return entries;
}

// Several struct Fields in one Batch of FieldReads (WL-6): one exchange, a
// frame of its own for each Field with that item's outcome, a refused Field
// shown in its rows, a failed Batch stopping the cycle, Stop draining it, a
// written struct read again on its own, and the budgets and modes that keep
// member reads.
void batchMembers()
{
	const auto setUp = [](Peer& peer) {
		peer.twin = std::make_shared<twin::Schema>();
		peer.batchLimits = ux::WatchlistSource::Limits{32, 4096, 4096};
	};
	const auto rowsOf = [] {
		auto rows = twinEntries(twin::left, {0});
		rows += twinEntries(twin::small, {1});
		rows += twinEntries(twin::right, {0});
		rows += twinEntries(twin::left, {5});
		return rows;
	};
	using Fields = Peer::Fields;
	{
		QObject owner;
		Peer peer;
		setUp(peer);
		ux::DynamicWatchlist watch(&owner);
		watch.setSource(peer.source());
		CHECK(watch.addEntries(rowsOf()).value("added").toInt() == 4);
		std::vector<ux::WatchFrame> frames;
		watch.setCompletionObserver([&](const ux::WatchFrame& frame) { frames.push_back(frame); });
		CHECK(watch.begin(false));
		CHECK(peer.batchCalls.size() == 1 && peer.memberCalls.empty() && peer.calls.empty());
		CHECK(peer.batchCalls[0] == Fields({{twin::left, {0, 5}}, {twin::small, {1}}, {twin::right, {0}}}));
		CHECK(watch.status() == "Reading values 1-4 of 4");
		peer.finishBatch(); watch.poll(); events();
		CHECK(!watch.active() && watch.status() == "ReadMany complete - 4 values");
		CHECK(frames.size() == 3);
		for (std::size_t k = 0; k != frames.size(); ++k)
			CHECK(frames[k].operation == ux::WatchOperation::Batch && frames[k].phase == ux::WatchCompletionPhase::Current &&
			      frames[k].correlated && frames[k].exact && frames[k].portion == 0 &&
			      frames[k].cycle == frames[0].cycle && frames[k].sequence == frames[0].sequence + qint64(k));
		CHECK(frames[0].count == 2 && frames[0].rows[0].id == twin::left && frames[0].rows[0].leaf == 1 &&
		      frames[0].rows[1].leaf == 6 && frames[1].count == 1 && frames[1].rows[0].id == twin::small &&
		      frames[1].rows[0].leaf == 2 && frames[2].rows[0].id == twin::right);
		CHECK(text(watch, 0) == "0" && text(watch, 1) == "1" && text(watch, 2) == "0" && text(watch, 3) == "5");
		CHECK(watch.pace().batch == 1 && watch.pace().fieldRead == 0 && watch.pace().readMany == 0 &&
		      watch.pace().values == 4);

		// The device refuses one Field: its rows say so, the others are read,
		// its frame carries its own outcome, and the cycle goes on.
		frames.clear();
		CHECK(watch.begin(false));
		peer.finishBatch(cl::Status::Ok, 1); watch.poll(); events();
		auto* model = watch.model();
		CHECK(!watch.active() && watch.status() == "ReadMany complete - 4 values");
		CHECK(model->data(model->index(1, 0), ux::WatchlistModel::StatusRole).toString() ==
		      "Field refused (dispatch 0, endpoint 3)");
		CHECK(frames.size() == 3 && frames[1].native.endpoint == 3 && frames[0].native.endpoint == 0 &&
		      frames[1].phase == ux::WatchCompletionPhase::Current);

		// A Batch that failed as a whole, or answered other values, stops it.
		frames.clear();
		CHECK(watch.begin(false));
		peer.finishBatch(cl::Status::Timeout); watch.poll(); events();
		CHECK(!watch.active() && watch.status().startsWith("Watch read failed") && frames.size() == 3 &&
		      frames[0].phase == ux::WatchCompletionPhase::Failed);
		frames.clear();
		CHECK(watch.begin(false));
		peer.finishBatch(cl::Status::Ok, -1, true); watch.poll(); events();
		CHECK(!watch.active() && watch.status().startsWith("Watch read failed") && !frames[0].correlated &&
		      frames[0].phase == ux::WatchCompletionPhase::Failed);

		// Stopped while it is out: drained, every Field's frames, no row touched.
		frames.clear();
		CHECK(watch.begin(true));
		CHECK(peer.batchCalls.size() == 5);
		watch.stop();
		peer.finishBatch(); watch.poll(); events();
		CHECK(!watch.active() && frames.size() == 3 && frames[2].phase == ux::WatchCompletionPhase::Drained);

		// Written while the Batch is out: that struct alone is read again, as
		// one member read, before the cycle goes on.
		frames.clear();
		CHECK(watch.begin(true));
		watch.wake(twin::right);
		peer.finishBatch(); watch.poll(); events();
		CHECK(peer.memberCalls.size() == 1 && peer.memberCalls[0] == std::pair<quint32, std::vector<std::uint32_t>>{
		                                                                 twin::right, {0}});
		CHECK(watch.status() == "Reading a written Field again");
		peer.finishMembers(); watch.poll(); events();
		CHECK(frames.size() == 4 && frames[3].urgent && frames[3].operation == ux::WatchOperation::FieldRead);
		watch.stop();
		events();
	}
	// The reply budget splits them; Individual mode and a device without Batch
	// read each struct on its own.
	for (int mode = 0; mode < 3; ++mode) {
		QObject owner;
		Peer peer;
		setUp(peer);
		if (mode == 0)
			peer.batchLimits = ux::WatchlistSource::Limits{32, 4096, 1000};
		if (mode == 2)
			peer.batchLimits.reset();
		ux::DynamicWatchlist watch(&owner);
		watch.setSource(peer.source());
		watch.setIndividual(mode == 1);
		CHECK(watch.addEntries(rowsOf()).value("added").toInt() == 4);
		CHECK(watch.begin(false));
		if (mode == 0) {
			CHECK(peer.batchCalls.size() == 1 &&
			      peer.batchCalls[0] == Fields({{twin::left, {0, 5}}, {twin::small, {1}}}));
			peer.finishBatch(); watch.poll(); events();
			CHECK(peer.memberCalls.size() == 1 && peer.memberCalls[0].first == twin::right);
			peer.finishMembers(); watch.poll(); events();
		} else {
			for (const quint32 field : {twin::left, twin::small, twin::right}) {
				CHECK(peer.memberCalls.size() >= 1 && peer.memberCalls.back().first == field);
				peer.finishMembers(); watch.poll(); events();
			}
			CHECK(peer.batchCalls.empty() && peer.memberCalls.size() == 3);
		}
		CHECK(!watch.active() && watch.status().endsWith("complete - 4 values"));
	}
}

// The pace (WL-6) on a controlled clock: a Read once's own figures; polling
// within its interval; a slow cycle that skips the tick due during it and
// halves the pace at once; the pace back once the periods fit; and continuous
// reading, whose period is its cycle.
void paceOfPolling()
{
	QObject owner;
	Peer peer;
	ux::DynamicWatchlist watch(&owner);
	watch.setSource(peer.source());
	qint64 now = 1'000'000'000;
	watch.setClock([&now] { return now; });
	const auto ms = [&now](qint64 value) { now += value * 1'000'000; };
	// A completion queues the next step, which can queue another: settle.
	const auto settle = [] {
		for (int i = 0; i < 4; ++i)
			events();
	};
	const auto tick = [&] {
		watch.tick();
		settle();
	};
	const auto cycle = [&](qint64 took) {
		ms(took);
		peer.finish();
		watch.poll();
		settle();
	};
	fill(watch, 2);
	CHECK(watch.pace().cycles == 0);
	watch.setInterval(1000);
	CHECK(watch.begin(false));
	cycle(150);
	auto pace = watch.pace();
	CHECK(pace.cycles == 1 && !pace.polling && pace.routine && pace.values == 2 && pace.readMany == 1 &&
	      pace.fieldRead == 0 && pace.batch == 0 && pace.lastCycleMs == 150 && pace.achievedPeriodMs == 0 && !pace.behind);
	// Within the interval: a cycle of 200 ms, every second.
	CHECK(watch.begin(true));
	cycle(200);
	pace = watch.pace();
	CHECK(pace.cycles == 1 && pace.polling && pace.lastCycleMs == 200 && pace.achievedPeriodMs == 1000 &&
	      !pace.behind && pace.intervalMs == 1000);
	ms(800);
	tick();
	cycle(200);
	pace = watch.pace();
	CHECK(pace.cycles == 2 && pace.medianPeriodMs == 1000 && pace.achievedPeriodMs == 1000 && pace.skippedTicks == 0);
	// A cycle of 1.2 s: the tick due during it is skipped, so every 2 s.
	ms(800);
	tick();
	ms(1000);
	tick();
	cycle(200);
	pace = watch.pace();
	CHECK(pace.cycles == 3 && pace.lastCycleMs == 1200 && pace.skippedTicks == 1 && pace.achievedPeriodMs == 2000 &&
	      pace.behind && pace.medianCycleMs == 200);
	// Fast again: the periods fit, and it keeps up.
	ms(800);
	tick();
	cycle(200);
	pace = watch.pace();
	CHECK(pace.cycles == 4 && pace.achievedPeriodMs == 1000 && !pace.behind);
	watch.stop();
	events();
	// Stopped: the last figures stay; the status is no longer the cycle's.
	CHECK(watch.pace().cycles == 4 && !watch.pace().routine);
	// Continuous: each cycle starts as the last ends; its period is its cycle.
	watch.setInterval(0);
	CHECK(watch.begin(true));
	CHECK(watch.pace().cycles == 0);
	cycle(300);
	cycle(300);
	pace = watch.pace();
	CHECK(pace.cycles == 2 && pace.intervalMs == 0 && pace.medianPeriodMs == 300 && pace.achievedPeriodMs == 300 &&
	      !pace.behind);
	watch.stop();
	events();
	ms(1000);
	peer.finish();
	watch.poll();
	events();
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	readPortions();
	batchMembers();
	paceOfPolling();

	numberFormats();
	typedEditors();
	exactCompletionFrames();
	bulkPublication();
	rowLimit();
	wakeAfterWrites();
	continuousLowPriority();
	busySessionTicks();
	startDuringReadOnce();
	trendCompletions();
	chunks();
	budgetsAndFallback();
	drainAndStale();
	schedulingAndReentrancy();
	replaceSourceDuringPublication();
	pathResolution();
	dottedGroups();
	missingRows();
	memberPlan();
	memberFanOut();
	memberUrgent();
	memberFailure();
	removeDuringRead();
	addDuringCycle();
	addBetweenCycles();
	memberChangeDuringRead();
	clearWhilePolling();
	urgentAfterChange();
	readOnceReplans();
	changeInsideCompletion();
	std::printf("Dynamic Watchlist checks=%u chunks=32,32,1 member_frames=10 no_native_callback_signals=1\n",
	            checks);
}
