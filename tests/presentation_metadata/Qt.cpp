// Shared metadata vectors, real native execution and installed QML rendering. MIT.
#include "Fixture.hpp"
#include <Telemetry/UX/PresentationOverlay.hpp>
#include <Telemetry/UX/StaticConfiguratorProvider.hpp>
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

using namespace dynamic_qml_fixture;
using namespace telemetry::ux;
static unsigned cases = 0;

static QByteArray serialize(const QJsonValue& value)
{
	const auto wrapped = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
	return wrapped.mid(1, wrapped.size() - 2);
}

static QByteArray overlay(const QString& fingerprint, const QJsonArray& entries)
{
	return QJsonDocument(QJsonObject{{"format", "telemetry-presentation"},
	                                 {"version", 1},
	                                 {"fingerprint", fingerprint},
	                                 {"entries", entries}})
	    .toJson(QJsonDocument::Compact);
}

static QJsonObject entry(int category, quint32 id, const char* role, QJsonArray path,
                         QJsonObject hints)
{
	hints.insert("category", category);
	hints.insert("id", static_cast<double>(id));
	hints.insert("role", QLatin1String(role));
	hints.insert("path", path);
	return hints;
}

static void parser(const QString& fileName)
{
	QFile file(fileName);
	CHECK(file.open(QIODevice::ReadOnly));
	const auto vectors = QJsonDocument::fromJson(file.readAll()).object().value("cases").toArray();
	CHECK(!vectors.isEmpty());
	for (const auto& vector : vectors) {
		PresentationOverlay presentation;
		QString error;
		const auto test = vector.toObject();
		const bool valid = presentation.replace(serialize(test.value("input")), error);
		if (valid != test.value("valid").toBool())
			std::fprintf(stderr, "Presentation case: %s error=%s\n",
			             qPrintable(test.value("name").toString()), qPrintable(error));
		CHECK(valid == test.value("valid").toBool());
		CHECK(valid == error.isEmpty());
		++cases;
	}
	const QString fingerprint = QStringLiteral("0x0123456789abcdef");
	PresentationOverlay presentation;
	QString error;
	auto bytes = overlay(fingerprint, {entry(1, 4, "value", {0},
	                                         {{"label", "Friendly"},
	                                          {"unit", "V"},
	                                          {"minimum", "18446744073709551615"},
	                                          {"maximum", "-9223372036854775808"},
	                                          {"step", "0.001"},
	                                          {"help", "<b>plain</b>"}})});
	CHECK(presentation.replace(bytes, error));
	bytes.fill('x');
	const auto copy = presentation;
	CHECK(copy.hints(fingerprint, 1, 4, "value", {quint32{0}}).label == "Friendly");
	CHECK(copy.hints(fingerprint, 1, 4, "value", {true}).label.isEmpty());
	CHECK(copy.hints(fingerprint, 1, 4, "value", {0.0}).label.isEmpty());
	CHECK(copy.hints(fingerprint, 1, 4, "value", {QStringLiteral("0")}).label.isEmpty());
	CHECK(copy.hints(fingerprint, 1, 4, "value", {-1}).label.isEmpty());
	CHECK(copy.hints(fingerprint, 1, 4, "value", {qulonglong{UINT64_MAX}}).label.isEmpty());
	CHECK(copy.hints("0xffffffffffffffff", 1, 4, "value", {0}).label.isEmpty());
	CHECK(copy.hints(fingerprint, 2, 4, "value", {0}).label.isEmpty());
	CHECK(copy.hints(fingerprint, 1, 4, "request", {0}).label.isEmpty());
	CHECK(!presentation.replace("{", error));
	CHECK(presentation.hints(fingerprint, 1, 4, "value", {0}).label == "Friendly");
	CHECK(presentation.replace({}, error));
	CHECK(presentation.hints(fingerprint, 1, 4, "value", {0}).label.isEmpty());
	CHECK(copy.hints(fingerprint, 1, 4, "value", {0})
	          .displayDetail("raw", "U64")
	          .contains(QStringLiteral("18446744073709551615")));
}

struct StaticApi : QObject {
	unsigned issued = 0;
	bool ready = true;
	QVariant value = QStringLiteral("18446744073709551615");
};

static StaticConfiguratorBinding staticBinding(StaticApi& api)
{
	StaticConfiguratorBinding binding;
	binding.api = &api;
	binding.fingerprint = QStringLiteral("0x0123456789abcdef");
	binding.types.append(StaticType{});
	StaticType scalar;
	scalar.id = 1;
	scalar.kind = StaticTypeKind::Scalar;
	scalar.scalarCode = 8;
	binding.types.append(scalar);
	StaticEndpoint field;
	field.id = 9;
	field.category = 1;
	field.catalog = "measure";
	field.name = "RawCounter";
	field.valueType = 1;
	field.readable = field.writable = true;
	field.cached = +[](QObject* self) {
		return static_cast<StaticApi*>(self)->value;
	};
	field.cachedValid = +[](QObject*) {
		return true;
	};
	field.cachedPayload = +[](QObject*) {
		return QByteArray(8, '\0');
	};
	field.validate = +[](const QVariant& value, QString&) {
		bool ok = false;
		value.toString().toULongLong(&ok);
		return ok;
	};
	field.writePreserving = +[](QObject* self, const QVariant&, const QByteArray&) {
		++static_cast<StaticApi*>(self)->issued;
		return QStringLiteral("write");
	};
	binding.endpoints.append(field);
	binding.ready = +[](QObject* self) {
		return static_cast<StaticApi*>(self)->ready;
	};
	binding.session = +[](QObject*) {
		return quint32{1};
	};
	return binding;
}

static int role(QAbstractItemModel& model, const char* name)
{
	const auto names = model.roleNames();
	for (auto it = names.begin(); it != names.end(); ++it)
		if (it.value() == name)
			return it.key();
	return -1;
}

static void staticPresentation()
{
	StaticApi api;
	StaticConfiguratorProvider provider(staticBinding(api));
	provider.setAllowMutations(true);
	auto* endpoints = provider.endpoints();
	const auto index = endpoints->index(0, 0, endpoints->index(0, 0));
	provider.selectEndpoint(index);
	const auto metadata = overlay("0x0123456789abcdef", {entry(1, 9, "value", {},
	                                                           {{"label", "Total count"},
	                                                            {"unit", "ticks"},
	                                                            {"minimum", "100"},
	                                                            {"step", "10"},
	                                                            {"help", "Plain <b>text</b>"}})});
	QSignalSpy changed(provider.values(), &QAbstractItemModel::dataChanged);
	CHECK(provider.setPresentation(metadata));
	CHECK(provider.presentationError().isEmpty() && api.issued == 0);
	CHECK(!changed.isEmpty());
	CHECK(endpoints->data(index, role(*endpoints, "name")) == QStringLiteral("Total count"));
	CHECK(provider.selection().contains(QStringLiteral("RawCounter")));
	const auto value = provider.values()->index(0, 0);
	CHECK(provider.values()->data(value, role(*provider.values(), "value")) ==
	      QStringLiteral("18446744073709551615 ticks"));
	CHECK(provider.values()
	          ->data(value, role(*provider.values(), "detail"))
	          .toString()
	          .contains(QStringLiteral("Advisory only: minimum 100")));
	CHECK(provider.values()
	          ->data(value, role(*provider.values(), "detail"))
	          .toString()
	          .contains(QStringLiteral("Plain <b>text</b>")));
	provider.beginEdit();
	CHECK(provider.editing());
	const auto editIndex = provider.values()->index(0, 0);
	CHECK(provider.values()->data(editIndex, role(*provider.values(), "editorText")) ==
	      QStringLiteral("18446744073709551615"));
	CHECK(provider.setScalarText(editIndex, QStringLiteral("4")));
	CHECK(provider.canApply()); // Advisory minimum/step never become a second validator.
	CHECK(!provider.setPresentation("{}") && !provider.presentationError().isEmpty());
	CHECK(provider.values()->data(editIndex, role(*provider.values(), "editorText")) == "4");
	CHECK(api.issued == 0);
	provider.cancelEdit();
	CHECK(provider.setPresentation(
	    overlay("0xffffffffffffffff", {entry(1, 9, "value", {}, {{"label", "Wrong"}})})));
	CHECK(endpoints->data(index, role(*endpoints, "name")) == "RawCounter");
	CHECK(provider.setPresentation({}));
	CHECK(provider.presentationError().isEmpty());
}

static void dynamicPresentation()
{
	auto storage = std::make_shared<Backing>();
	const auto fingerprint =
	    QStringLiteral("0x%1").arg(storage->schema.fingerprint(), 16, 16, QLatin1Char('0'));
	Binding binding(storage->owner, storage, nullptr, fingerprint);
	binding.setMutationsAllowed(true);
	Provider first(&binding), second(&binding);
	select(first, "Config");
	select(second, "Config");
	const auto metadata = overlay(
	    fingerprint,
	    {entry(1, 0, "value", {"serial"}, {{"label", "Serial count"}, {"unit", "ticks"}}),
	     entry(1, 0, "value", {"offset"}, {{"unit", "ticks"}}),
	     entry(1, 0, "value", {"samples", 128}, {{"unit", "V"}}),
	     entry(1, 0, "value", {"limit"}, {{"minimum", "50"}, {"maximum", "80"}, {"step", "10"}}),
	     entry(3, 0, "request", {"multiplier"}, {{"label", "Multiply"}}),
	     entry(2, 0, "request", {}, {{"label", "Reset arguments"}}),
	     entry(3, 0, "request", {}, {{"label", "Inspect arguments"}}),
	     entry(3, 0, "response", {"serial"}, {{"unit", "replies"}})});
	CHECK(first.setPresentation(metadata));
	CHECK(storage->owner.pending() == 0 && storage->packets == 0);
	CHECK(first.refresh() && second.refresh());
	storage->pump(binding);
	CHECK(scalar(first.values(), "Serial count") == "18446744073709551615 ticks");
	CHECK(scalar(first.values(), "offset") == "-9223372036854775808 ticks");
	CHECK(scalar(second.values(), "serial") == "18446744073709551615");
	const auto samples = member(first.values(), "samples");
	first.values()->fetchMore(samples);
	CHECK(first.values()->rowCount(samples) == 128);
	first.values()->fetchMore(samples);
	CHECK(first.values()->rowCount(samples) == 256);
	CHECK(first.values()->data(first.values()->index(128, 0, samples), ValueModel::ValueRole) ==
	      "0 V");
	first.setAllowMutations(true);
	CHECK(first.beginEdit());
	CHECK(first.values()->data(member(first.values(), "Serial count"),
	                           ValueModel::EditorTextRole) == "18446744073709551615");
	CHECK(edit(first, "limit", "4") && first.canApply());
	CHECK(!first.setPresentation("{"));
	CHECK(first.values()->data(member(first.values(), "limit"), ValueModel::EditorTextRole) == "4");
	CHECK(storage->packets == 2);
	first.cancelEdit();
	QString expectedReset;
	const auto resetObserver =
	    QObject::connect(first.values(), &QAbstractItemModel::modelReset, &first, [&] {
		    if (!expectedReset.isEmpty() && first.values()->rowCount())
			    CHECK(first.values()->data(first.values()->index(0, 0), ValueModel::NameRole) ==
			          expectedReset);
	    });
	expectedReset = QStringLiteral("Reset arguments");
	select(first, "Reset arguments");
	expectedReset = QStringLiteral("Inspect arguments");
	select(first, "Inspect arguments");
	QObject::disconnect(resetObserver);
	CHECK(edit(first, "Multiply", "1"));
	CHECK(first.callService());
	storage->pump(binding);
	CHECK(scalar(first.responses(), "serial") == "18446744073709551615 replies");
	const auto retained = member(first.responses(), "serial");
	binding.retire();
	CHECK(first.responses()->data(retained, ValueModel::ValueRole).toString().isEmpty());
	storage->owner.close();
	storage->owner.process(storage->now);
	binding.poll();
	CHECK(binding.drained());
}

static bool containsText(QQuickItem* item, const QString& text)
{
	if (item->property("text").toString() == text)
		return true;
	for (auto* child : item->childItems())
		if (containsText(child, text))
			return true;
	return false;
}

static void qmlPresentation()
{
	StaticApi api;
	StaticConfiguratorProvider provider(staticBinding(api));
	provider.selectEndpoint(provider.endpoints()->index(0, 0, provider.endpoints()->index(0, 0)));
	CHECK(provider.setPresentation(overlay(
	    "0x0123456789abcdef",
	    {entry(1, 9, "value", {},
	           {{"label", "Total counter"}, {"unit", "ticks"}, {"help", "<b>plain help</b>"}})})));
	QQmlEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("presentationProvider"), &provider);
	QQmlComponent component(&engine);
	component.setData(R"(import QtQuick
import QtQuick.Window
import Telemetry.UX
Window { width: 900; height: 500; visible: true
    FieldEditor { anchors.fill: parent; provider: presentationProvider }
})",
	                  QUrl(QStringLiteral("qrc:/Presentation.qml")));
	for (const auto& error : component.errors())
		std::fprintf(stderr, "%s\n", qPrintable(error.toString()));
	CHECK(component.isReady());
	std::unique_ptr<QObject> object(component.create());
	auto* window = qobject_cast<QQuickWindow*>(object.get());
	CHECK(window);
	for (unsigned attempt = 0;
	     attempt < 100 &&
	     !containsText(window->contentItem(), QStringLiteral("18446744073709551615 ticks"));
	     ++attempt)
		QTest::qWait(5);
	CHECK(containsText(window->contentItem(), QStringLiteral("18446744073709551615 ticks")));
	CHECK(containsText(window->contentItem(), QStringLiteral("Total counter")));
	CHECK(api.issued == 0);
	window->close();
	events();
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void injectedApplicationLeak()
{
	auto* memory = new char[123];
#if defined(__GNUC__)
	asm volatile("" : : "r"(memory) : "memory");
#else
	(void)memory;
#endif
}

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	if (app.arguments().contains(QStringLiteral("--application-leak-control"))) {
		injectedApplicationLeak();
		return 0;
	}
	qInstallMessageHandler(+[](QtMsgType type, const QMessageLogContext&, const QString& message) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
			++warnings;
			std::fprintf(stderr, "%s\n", qPrintable(message));
		}
	});
	CHECK(argc > 1);
	parser(QString::fromLocal8Bit(argv[1]));
	staticPresentation();
	dynamicPresentation();
	qmlPresentation();
	CHECK(warnings == 0);
	std::printf("Presentation metadata checks=%u warnings=%u cases=%u\n", checks, warnings, cases);
	return 0;
}
