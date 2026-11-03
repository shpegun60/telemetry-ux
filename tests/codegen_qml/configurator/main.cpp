#include "Harness.hpp"
namespace cf = codegen_fixture;
namespace ex = client_example;
namespace qa = device_api::qml;
using static_qml_fixture::Harness;
using static_qml_fixture::bytes;
using static_qml_fixture::check;
using static_qml_fixture::checks;
using static_qml_fixture::events;
static unsigned warnings = 0;

class SeedMutator final : public QObject {
	Q_OBJECT
public:
	char* raw = nullptr;
	qsizetype rawSize = 0;
	unsigned calls = 0;

	Q_INVOKABLE void touch()
	{
		++calls;
		std::fill_n(raw, rawSize, char(0xa5));
	}
};

static void preservedValues()
{
	constexpr std::array f32{0x7fc12345u, 0xffc54321u, 0x7f812345u, 0xff854321u, 0u, 0x80000000u};
	constexpr std::array<std::uint64_t, 6> f64{0x7ff8123456789abcULL,
	                                           0xfff8abcdef012345ULL,
	                                           0x7ff0123456789abcULL,
	                                           0xfff0abcdef012345ULL,
	                                           0,
	                                           0x8000000000000000ULL};
	for (std::size_t index = 0; index < f32.size(); ++index) {
		ex::device.config = ex::initial;
		ex::device.config.gain = std::bit_cast<float>(f32[index]);
		ex::device.config.precise = std::bit_cast<double>(f64[index]);
		const auto original = bytes(ex::device.config);
		Harness h;
		const auto& field = h.endpoint(1, QStringLiteral("Config"));
		bool atomic = false;
		QObject::connect(h.api.device(), &qa::Group_0Api::configChanged, &h.api, [&] {
			if (h.api.device()->configValid())
				atomic = field.cachedValid(&h.api) && field.cachedPayload(&h.api) == original;
		});
		const auto read = h.await(field.read(&h.api));
		check(read.value("success").toBool() && atomic, "value and raw cache published atomically");
		check(read.value("canonicalPayload").toByteArray() == original,
		      "result retains exact checked raw bytes");
		check(read.value("session").toUInt() == h.api.session(), "originating result session");
		QVariantMap edited = field.cached(&h.api).toMap();
		check(edited.value("serial").toString() == QStringLiteral("18446744073709551615"),
		      "exact U64 in projected value");
		check(edited.value("offset").toString() == QStringLiteral("-9223372036854775808"),
		      "exact S64 in projected value");
		edited["enabled"] = false;
		QString error;
		check(field.validate(edited, error), "generated QVariant validator accepts native draft");
		const auto write =
		    h.await(field.writePreserving(&h.api, edited, field.cachedPayload(&h.api)));
		check(write.value("success").toBool(), "preserving FieldWrite completed");
		auto expected = original;
		expected[28] = 0; // Independent Config wire order: U64,S64,F32,F64,Bool.
		check(h.lastPayload == expected,
		      "unmodified qNaN/sNaN signs, payloads and signed zero retained");
		check(bytes(ex::device.config) == expected,
		      "real model receives the full exact canonical draft");
		check(!field.cachedValid(&h.api) && field.cachedPayload(&h.api).isEmpty(),
		      "successful write retires both caches");
		check(h.await(field.read(&h.api)).value("success").toBool(), "separate confirmed readback");
		edited = field.cached(&h.api).toMap();
		edited["gain"] = 4.25;
		edited["precise"] = -2.5;
		check(h.await(field.writePreserving(&h.api, edited, field.cachedPayload(&h.api)))
		          .value("success")
		          .toBool(),
		      "explicit floating edit completed");
		check(ex::device.config.gain == 4.25f && ex::device.config.precise == -2.5,
		      "explicit finite values replace original floating bytes");
		check(h.packets == 4 && h.delivered == 4,
		      "one FIFO operation and completion per explicit request");
	}
}

static void ownershipAndLifetime()
{
	ex::device.config = ex::initial;
	ex::device.config.gain = std::bit_cast<float>(0x7f812345u);
	ex::device.config.precise = std::bit_cast<double>(0xfff0123456789abcULL);
	Harness h;
	const auto& field = h.endpoint(1, QStringLiteral("Config"));
	check(h.await(field.read(&h.api)).value("success").toBool(), "seed ownership initial read");
	const auto original = field.cachedPayload(&h.api);
	QByteArray backing(original.constData(), original.size());
	QByteArray seed = QByteArray::fromRawData(backing.constData(), backing.size());
	QJSEngine engine;
	SeedMutator mutator;
	mutator.raw = backing.data();
	mutator.rawSize = backing.size();
	engine.globalObject().setProperty("mutator", engine.newQObject(&mutator));
	h.api.setInputEngine(&engine);
	const auto object = engine.evaluate(QStringLiteral(
	    "({get serial(){mutator.touch();return '18446744073709551615';},offset:'-9223372036854775808',gain:NaN,precise:NaN,enabled:true,mode:-2,samples:[1,256,65535]})"));
	const auto token = field.writePreserving(&h.api, QVariant::fromValue(object), seed);
	check(mutator.calls == 1 && seed != original,
	      "QJS getter really mutates borrowed seed during conversion");
	check(h.await(token).value("success").toBool() && h.lastPayload == original,
	      "owned seed precedes reentrant input snapshot");
	check(h.await(field.read(&h.api)).value("success").toBool(), "raw cache refreshed");
	const auto before = h.packets;
	const auto retained = field.cachedPayload(&h.api);
	const auto invalid = h.await(
	    field.writePreserving(&h.api, field.cached(&h.api), QByteArray(retained.size(), '\0')));
	// All-zero seeds are valid for this DTO, so reject malformed Bool specifically.
	check(invalid.value("success").toBool(),
	      "canonical seed is not required to match cached device value");
	check(h.packets == before + 1, "explicit canonical seed still uses the ordinary FIFO");
	check(h.await(field.read(&h.api)).value("success").toBool(), "validation read");
	auto malformed = field.cachedPayload(&h.api);
	malformed[28] = 2;
	const auto count = h.packets;
	const auto refused = h.await(field.writePreserving(&h.api, field.cached(&h.api), malformed));
	check(!refused.value("success").toBool() &&
	          refused.value("errorKind") == QStringLiteral("Validation"),
	      "malformed canonical seed refused before admission");
	check(h.packets == count && field.cachedValid(&h.api),
	      "validation refusal preserves caches and performs no I/O");
	const auto oldSession = h.api.session();
	const auto staleToken = field.read(&h.api);
	h.exchange(false);
	h.api.resetSession();
	events();
	const auto completed = h.await(staleToken);
	check(completed.value("success").toBool() && completed.value("session").toUInt() == oldSession,
	      "late checked operation evidence remains truthful");
	check(!field.cachedValid(&h.api) && field.cachedPayload(&h.api).isEmpty(),
	      "late result cannot repopulate a retired cache");
	check(!h.binding.ready(&h.api), "binding observes admission retirement");
}

int main(int argc, char** argv)
{
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
		if (type == QtWarningMsg || type == QtCriticalMsg)
			++warnings;
		std::fprintf(stderr, "%s\n", message.toUtf8().constData());
	});
	QCoreApplication application(argc, argv);
	try {
		preservedValues();
		ownershipAndLifetime();
		Harness h;
		check(h.binding.api == &h.api && h.binding.types.size() > 12,
		      "generated typed owner and structural graph");
		const auto& failure = h.endpoint(3, QStringLiteral("Fail"));
		check(failure.invoke != nullptr && !failure.writePreserving,
		      "category-specific binding uses actual fail_2 layout");
		const auto failed = h.await(failure.invoke(&h.api, QVariant{}));
		check(!failed.value("success").toBool() && failed.value("endpointStatus").toInt() != 0,
		      "generated normalized Service callback reports real refusal");
		QObject wrong;
		check(!h.binding.ready(&wrong) && h.binding.session(&wrong) == 0,
		      "exact Api identity checked");
		check(h.endpoint(1, QStringLiteral("Config")).read(&wrong).isEmpty(),
		      "wrong Api cannot invoke a binding");
		check(warnings == 0, "no Qt warnings during native generated-cache workflows");
		std::printf("Static QML generated checks=%u\n", checks);
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}

#include "main.moc"
