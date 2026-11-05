// Real generated API -> one DeviceClient FIFO -> native Model -> shared Qt view. MIT.
#include "../codegen_qml/configurator/Harness.hpp"
#include <Telemetry/UX/StaticConfiguratorProvider.hpp>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>
#include <cstdlib>
#include <cstring>

using namespace static_qml_fixture;
using Provider = telemetry::ux::StaticConfiguratorProvider;
static unsigned warnings = 0;

// A test-only counter-control: the narrow upstream Qt texture exception must
// never hide an application allocation. GNU/Clang keep this allocation alive
// through the barrier, but no pointer survives the function's return.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void injectedNonQtLeak()
{
	void* const memory = std::malloc(123);
	if (!memory)
		throw std::bad_alloc{};
	std::memset(memory, 0x42, 123);
#if defined(__GNUC__)
	asm volatile("" : : "r"(memory) : "memory");
#endif
}

static QModelIndex endpoint(Provider& provider, const QString& name)
{
	auto* model = provider.endpoints();
	for (int group = 0; group < model->rowCount(); ++group) {
		const auto parent = model->index(group, 0);
		for (int row = 0; row < model->rowCount(parent); ++row) {
			const auto index = model->index(row, 0, parent);
			if (model->data(index, Qt::UserRole + 1).toString() == name)
				return index;
		}
	}
	throw std::runtime_error("missing visible endpoint");
}

static QModelIndex value(Provider& provider, const QString& name)
{
	auto* model = provider.values();
	for (int row = 0; row < model->rowCount(); ++row) {
		const auto index = model->index(row, 0);
		if (model->data(index, Qt::UserRole + 1).toString() == name)
			return index;
	}
	throw std::runtime_error("missing visible member");
}

static void drain(Harness& h, Provider& provider)
{
	for (int step = 0; step < 60 && provider.busy(); ++step)
		h.exchange();
	events();
	check(!provider.busy(), "foreground request completes");
}

static void select(Provider& p, const char* name)
{
	p.selectEndpoint(endpoint(p, QString::fromLatin1(name)));
}

static void fields()
{
	constexpr std::array f32{0x7fc12345u, 0xffc54321u, 0x7f812345u, 0xff854321u, 0u, 0x80000000u};
	constexpr std::array<std::uint64_t, 6> f64{0x7ff8123456789abcULL,
	                                           0xfff8abcdef012345ULL,
	                                           0x7ff0123456789abcULL,
	                                           0xfff0abcdef012345ULL,
	                                           0,
	                                           0x8000000000000000ULL};
	for (std::size_t i = 0; i < f32.size(); ++i) {
		ex::device.config = ex::initial;
		ex::device.config.gain = std::bit_cast<float>(f32[i]);
		ex::device.config.precise = std::bit_cast<double>(f64[i]);
		Harness h;
		Provider p{h.binding};
		select(p, "Config");
		check(p.canRefresh() && !p.canEdit(), "read-only permission is default");
		p.refresh();
		drain(h, p);
		check(p.values()->data(value(p, "serial"), Qt::UserRole + 3).toString() ==
		          QStringLiteral("18446744073709551615"),
		      "U64 projection stays decimal text");
		check(p.values()->data(value(p, "offset"), Qt::UserRole + 3).toString() ==
		          QStringLiteral("-9223372036854775808"),
		      "S64 projection stays decimal text");
		check(p.values()->data(value(p, "mode"), Qt::UserRole + 3).toString() ==
		          QStringLiteral("-2"),
		      "unknown signed enum remains numeric");
		p.setAllowMutations(true);
		p.beginEdit();
		check(p.editing() && p.canApply(), "explicit mutation permission enables draft");
		const auto serial = value(p, "serial");
		check(!p.setScalarText(serial, QStringLiteral("18446744073709551616")) && !p.canApply(),
		      "overflow blocks mutation without changing draft");
		check(p.values()->data(serial, Qt::UserRole + 8).toString() ==
		          QStringLiteral("18446744073709551616"),
		      "invalid raw text retained");
		check(p.setScalarText(serial, QStringLiteral("18446744073709551615")),
		      "corrected exact U64 restores draft validity");
		check(p.setScalarText(value(p, "enabled"), QStringLiteral("false")), "typed Bool edit");
		const auto packets = h.packets;
		p.apply();
		drain(h, p);
		check(h.packets == packets + 2, "one Write then one distinct readback");
		check(p.writeOutcome().startsWith(QStringLiteral("Applied")) && !p.editing(),
		      "write fact published independently of readback");
		check(!ex::device.config.enabled &&
		          std::bit_cast<std::uint32_t>(ex::device.config.gain) == f32[i] &&
		          std::bit_cast<std::uint64_t>(ex::device.config.precise) == f64[i],
		      "full provider preserves untouched NaN and signed-zero bits");
		check(!p.values()->data(serial).isValid(), "old model indexes do not read new snapshot");
		h.api.resetSession();
		events();
		check(!p.connected() && p.values()->rowCount() == 0 &&
		          p.writeOutcome().contains(QStringLiteral("previous session")),
		      "disconnect retires values and labels historical Write");
		check(!p.allowMutations(), "actual generated session reset revokes mutation opt-in");
		p.setAllowMutations(true);
		check(!p.allowMutations(), "disconnected provider cannot preauthorize the next session");
		check(h.api.admitDescriptor(
		          {reinterpret_cast<const char*>(cf::descriptorBytes.data()), 64}),
		      "same generated API admits a fresh session");
		events();
		check(p.connected() && !p.allowMutations(), "reused provider requires a fresh opt-in");
	}
}

static void commandsAndServices()
{
	ex::device.config = ex::initial;
	Harness h;
	Provider p{h.binding};
	p.setAllowMutations(true);
	select(p, "Accept");
	check(!p.editing() && p.canExecute(),
	      "empty-structure Command has request but no Field edit mode");
	p.cancelEdit();
	check(p.canExecute(), "Field Cancel cannot strand Command request");
	p.executeCommand();
	drain(h, p);
	check(p.commandOutcome().startsWith(QStringLiteral("Accepted")) &&
	          !p.commandOutcome().contains(QStringLiteral("Executed")),
	      "Accepted remains distinct from Executed");
	const auto accepted = h.packets;
	events();
	h.adapter.process(h.now);
	check(h.packets == accepted, "no implicit Command replay");
	select(p, "Reset");
	check(p.canExecute(), "Void Command enabled");
	p.executeCommand();
	drain(h, p);
	check(p.commandOutcome().startsWith(QStringLiteral("Executed")), "Void Command executes");
	select(p, "Ping");
	check(p.canCallService(), "Void-request Service enabled");
	p.callService();
	drain(h, p);
	check(p.serviceOutcome().contains(QStringLiteral("no response value")) &&
	          p.serviceResponseEndpoint().isEmpty(),
	      "Void response has NoValue");
	select(p, "Good");
	p.callService();
	drain(h, p);
	check(p.serviceOutcome().contains(QStringLiteral("value available")) &&
	          !p.serviceResponseEndpoint().isEmpty() && p.responses()->rowCount() == 0,
	      "empty Struct response is a real zero-byte value");
	select(p, "Fail");
	p.callService();
	drain(h, p);
	check(p.serviceOutcome().startsWith(QStringLiteral("Busy")),
	      "reserved Qt name routes exact Service and human refusal");
	select(p, "Inspect");
	check(p.canCallService(), "Service DTO initialized through generated native type");
	p.callService();
	drain(h, p);
	check(p.serviceOutcome().startsWith(QStringLiteral("InvalidArgument")),
	      "application refusal remains separate from transport success");
	check(p.setScalarText(value(p, "multiplier"), QStringLiteral("4")), "Service argument edit");
	p.callService();
	drain(h, p);
	check(p.serviceOutcome().contains(QStringLiteral("value available")) &&
	          p.responses()->rowCount() == 2,
	      "Service response owned before callback returns");
	const auto responseIndex = p.responses()->index(1, 0);
	const auto text = p.responses()->data(responseIndex, Qt::UserRole + 3);
	h.response.fill(std::byte{0xa5});
	h.result.fill(std::byte{0x5a});
	ex::device.config = ex::initial;
	check(p.responses()->data(responseIndex, Qt::UserRole + 3) == text,
	      "response survives RX and native object overwrite");
}

static void responseMutationLifetime()
{
	for (const bool command : {false, true}) {
		for (const bool refuse : {false, true}) {
			ex::device.config = ex::initial;
			Harness h;
			Provider p{h.binding};
			p.setAllowMutations(true);
			select(p, "Inspect");
			check(p.setScalarText(value(p, "multiplier"), QStringLiteral("4")),
			      "prepare owned response before mutation");
			p.callService();
			drain(h, p);
			check(p.responses()->rowCount() == 2, "previous Service response published");
			const auto previous = p.serviceResponseEndpoint();
			const auto oldIndex = p.responses()->index(1, 0);
			select(p, command ? "Reset" : "Config");
			if (!command) {
				p.refresh();
				drain(h, p);
				p.beginEdit();
				check(p.setScalarText(value(p, "serial"), QStringLiteral("42")),
				      "prepare Field replacement");
			}
			const auto packets = h.packets;
			if (refuse) {
				check(!h.endpoint(1, QStringLiteral("Scalars")).read(&h.api).isEmpty(),
				      "fill FIFO read before rejected mutation");
				check(!h.endpoint(2, QStringLiteral("Reset")).invoke(&h.api, {}).isEmpty(),
				      "fill FIFO Command before rejected mutation");
				check(!h.endpoint(3, QStringLiteral("Ping")).invoke(&h.api, {}).isEmpty(),
				      "fill FIFO Service before rejected mutation");
				events();
			}
			if (command)
				p.executeCommand();
			else
				p.apply();
			check(p.busy() && p.responses()->rowCount() == 0 &&
			          !p.responses()->data(oldIndex).isValid(),
			      "pending generated mutation hides the previous response");
			if (refuse) {
				events();
				check(!p.busy() && p.responses()->rowCount() == 2 &&
				          p.serviceResponseEndpoint() == previous && h.packets == packets,
				      "asynchronous QueueFull preserves the previous response without I/O");
				h.adapter.close();
				events();
				h.api.resetSession();
				events();
				check(p.responses()->rowCount() == 0 && !p.responses()->data(oldIndex).isValid(),
				      "reset after refusal retires the restored response");
			} else {
				drain(h, p);
				check(p.responses()->rowCount() == 0 && p.serviceResponseEndpoint().isEmpty(),
				      "accepted Write or Command retires the previous Service response");
				check(command ? p.commandOutcome().startsWith(QStringLiteral("Executed")) :
				                  p.writeOutcome().startsWith(QStringLiteral("Applied")),
				      "mutation facts remain available after response retirement");
				check(h.packets == packets + (command ? 1u : 2u),
				      "only the explicit mutation and Field readback were sent");
			}
		}
	}
}

static void queueRefusal()
{
	Harness h;
	Provider p{h.binding};
	p.setAllowMutations(true);
	select(p, "Config");
	p.refresh();
	drain(h, p);
	p.beginEdit();
	check(p.setScalarText(value(p, "serial"), QStringLiteral("42")), "prepared QueueFull draft");
	const auto before = h.packets;
	check(!h.endpoint(1, QStringLiteral("Scalars")).read(&h.api).isEmpty(), "fill FIFO read");
	check(!h.endpoint(2, QStringLiteral("Reset")).invoke(&h.api, {}).isEmpty(),
	      "fill FIFO Command");
	check(!h.endpoint(3, QStringLiteral("Ping")).invoke(&h.api, {}).isEmpty(), "fill FIFO Service");
	events();
	p.apply();
	events();
	check(!p.busy() && p.editing() && p.canApply() && h.packets == before,
	      "QueueFull preserves draft with no send");
	check(p.values()->data(value(p, "serial"), Qt::UserRole + 8).toString() == QStringLiteral("42"),
	      "QueueFull retains exact editor text");
	h.adapter.close();
	events();
	h.api.resetSession();
	events();
}

static void unknownWrite()
{
	ex::device.config = ex::initial;
	Harness h;
	Provider p{h.binding};
	p.setAllowMutations(true);
	select(p, "Config");
	p.refresh();
	drain(h, p);
	p.beginEdit();
	check(p.setScalarText(value(p, "enabled"), QStringLiteral("false")), "uncertain write draft");
	p.apply();
	events();
	h.adapter.process(h.now);
	const auto packet = h.adapter.nextPacket();
	check(bool(packet), "offered Write");
	std::copy(packet.bytes.begin(), packet.bytes.end(), h.copied.begin());
	const auto frame = tc::Input{h.copied}.first(packet.bytes.size());
	check(h.adapter.prepareHandoff(packet.session, packet.requestId),
	      "authorized mutation boundary");
	const auto reply = encoded::dispatchEncoded(cf::model.view(), cf::workspace, h.api.session(),
	                                            frame, h.response);
	check(bool(reply) && !ex::device.config.enabled, "Model executed Write before lost ACK");
	const auto calls = ex::device.calls;
	h.now += 10000;
	h.adapter.process(h.now);
	events();
	check(!p.busy() && p.writeOutcome().contains(QStringLiteral("OutcomeUnknown")) &&
	          !p.canApply() && p.values()->rowCount() == 0,
	      "lost ACK reports uncertainty and retires value");
	for (int i = 0; i < 4; ++i) {
		h.adapter.process(++h.now);
		events();
		check(!h.adapter.nextPacket(), "uncertain mutation offers no replay packet");
	}
	check(ex::device.calls == calls, "no mutation replay after uncertain completion");
	check(!h.adapter.onReply(tc::Input{h.response}.first(reply.written), packet.session,
	                         packet.requestId),
	      "late ACK cannot become Success");
}

static QQuickItem* visual(QQuickItem* root, const QString& name, const QString& scalar = {})
{
	if (root->isVisible() && root->objectName() == name &&
	    (scalar.isEmpty() || root->property("scalarName").toString() == scalar))
		return root;
	for (auto* child : root->childItems())
		if (auto* item = visual(child, name, scalar))
			return item;
	return nullptr;
}

static void individualWatch()
{
	ex::device.config = ex::initial;
	Harness h;
	Provider p{h.binding};
	check(!p.watchReadManySupported() && p.watchIndividual(), "Static Watchlist explicitly uses Individual FieldRead");
	select(p, "Config");
	check(!p.addWatch(), "aggregate Field excluded from scalar Watchlist");
	const auto before = h.packets;
	for (const auto* name : {"Wide", "Signed", "Mode"}) {
		select(p, name);
		check(p.canAddWatch() && p.addWatch(), "add real generated scalar Field");
		check(!p.addWatch(), "duplicate watch membership refused");
	}
	check(p.watchCount() == 3 && h.packets == before, "selection and watch membership perform no I/O");
	p.refreshWatch();
	for (int attempt = 0; attempt < 30 && p.watchActive(); ++attempt) {
		h.exchange();
		events();
	}
	check(!p.watchActive() && h.packets == before + 3, "one Individual cycle uses exactly three existing FIFO requests");
	auto* model = p.watchlist();
	const auto text = [&](int row) { return model->data(model->index(row, 0), Qt::UserRole + 3).toString(); };
	check(text(0) == QStringLiteral("18446744073709551615") &&
	          text(1) == QStringLiteral("-9223372036854775808") && text(2) == QStringLiteral("-2"),
	      "Static Watchlist projects exact U64/S64/open enum cache values");
	p.startWatch();
	check(p.watchActive() && p.watchPending(), "explicit Start owns one pending Individual read");
	p.stopWatch();
	check(p.watchActive() && p.watchPending(), "Stop retains an accepted pending request until completion");
	const auto count = p.watchCount();
	p.clearWatch();
	check(p.watchCount() == count, "Stop does not unlock membership before drain");
	h.exchange();
	events();
	check(!p.watchActive() && h.packets == before + 4, "Stop prevents remaining Fields from submitting");
	const auto afterStop = h.packets;
	auto* timer = p.findChild<QTimer*>(QStringLiteral("staticConfiguratorWatchTimer"));
	check(timer && !timer->isActive(), "poll timer stopped explicitly");
	QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
	events();
	check(h.packets == afterStop && !p.busy(), "stopped timer notification cannot create work");
	h.api.resetSession();
	events();
	check(text(0).isEmpty() && model->data(model->index(0, 0), Qt::UserRole + 2).toString() == QStringLiteral("Stale"),
	      "session invalidation hides watch values before new admission");
}

static void qmlInput(const QString& image, bool standalone = false)
{
	ex::device.config = ex::initial;
	Harness h;
	Provider p{h.binding};
	p.setAllowMutations(true);
	select(p, "Config");
	p.refresh();
	drain(h, p);
	p.beginEdit();
	QQmlEngine engine;
	QQmlComponent component(&engine);
	const auto form = standalone ? QByteArrayLiteral("FieldEditor") : QByteArrayLiteral("Configurator");
	const auto options = standalone ? QByteArray{} : QByteArrayLiteral(";watchlistVisible:false");
	component.setData(
	    "import QtQuick\nimport QtQuick.Controls\nimport Telemetry.UX\nApplicationWindow { id:host; required property var deviceProvider; width:1200;height:900;visible:true; " + form + " { anchors.fill:parent; provider:host.deviceProvider" + options + " } }",
	    QUrl(QStringLiteral("qrc:/StaticWindow.qml")));
	for (unsigned attempt = 0; component.isLoading() && attempt < 100; ++attempt) {
		events();
		QTest::qWait(10);
	}
	check(component.isReady(), qPrintable(component.errorString()));
	std::unique_ptr<QObject> root(component.createWithInitialProperties(
	    {{QStringLiteral("deviceProvider"), QVariant::fromValue(&p)}}));
	check(bool(root), "create actual shared QML window");
	events();
	QTest::qWait(80);
	auto* window = qobject_cast<QQuickWindow*>(root.get());
	check(window != nullptr, "real QQuickWindow");
	auto* pane = root->findChild<QQuickItem*>(QStringLiteral("watchlistPane"));
	check(standalone ? pane == nullptr : pane && !pane->isVisible(), "standalone has no navigation or Watchlist pane");
	if (!standalone) {
		auto* watchMode = root->findChild<QObject*>(QStringLiteral("watchMode"));
		check(watchMode && watchMode->property("count").toInt() == 1 &&
		          watchMode->property("currentIndex").toInt() == 0,
		      "Static QML never advertises unsupported ReadMany mode");
	}
	auto* input =
	    visual(window->contentItem(), QStringLiteral("scalarEditor"), QStringLiteral("gain"));
	check(input != nullptr, "live QML scalar editor");
	const auto inputPoint =
	    input->mapToScene(QPointF(input->width() / 2, input->height() / 2)).toPoint();
	QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, inputPoint);
	QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
	for (const auto key : {Qt::Key_4, Qt::Key_Period, Qt::Key_2, Qt::Key_5})
		QTest::keyClick(window, key);
	check(input->property("text").toString() == QStringLiteral("4.25"),
	      "actual keyboard input retained before Enter");
	auto* apply = visual(window->contentItem(), QStringLiteral("applyButton"));
	check(apply && apply->isVisible() && apply->isEnabled(), "actual Apply ready");
	QTest::mouseClick(
	    window, Qt::LeftButton, Qt::NoModifier,
	    apply->mapToScene(QPointF(apply->width() / 2, apply->height() / 2)).toPoint());
	drain(h, p);
	check(ex::device.config.gain == 4.25f, "mouse Apply without Enter writes last input");
	events();
	QTest::qWait(40);
	check(window->grabWindow().save(image), "retain actual QML Field form screenshot");
	select(p, "Good");
	p.callService();
	drain(h, p);
	check(!p.serviceResponseEndpoint().isEmpty(), "successful Service retains its owned response history");
	select(p, "Config");
	events();
	QTest::qWait(30);
	auto* responseTree = root->findChild<QQuickItem*>(QStringLiteral("responseTree"));
	check(responseTree && responseTree->isVisible() == !standalone,
	      "standalone Field hides unrelated Service response; full view retains history");
	select(p, "Reset");
	events();
	QTest::qWait(30);
	auto* cancel = root->findChild<QQuickItem*>(QStringLiteral("cancelEditButton"));
	check(cancel && !cancel->isVisible() && !apply->isVisible(),
	      "Command UI contains no Field Cancel/Apply controls");
	window->close();
	root.reset();
	engine.collectGarbage();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	events();
	check(warnings == 0, "strict QML diagnostics stay clean");
}

static void qmlActionPanels()
{
	for (const auto* type : {"CommandPanel", "ServicePanel"}) {
		const bool command = std::strcmp(type, "CommandPanel") == 0;
		Harness h;
		Provider p{h.binding};
		p.setAllowMutations(true);
		select(p, "Config");
		const auto before = h.packets;
		QQmlEngine engine;
		QQmlComponent component(&engine);
		component.setData(
		    QByteArrayLiteral("import QtQuick\nimport QtQuick.Controls\nimport Telemetry.UX\nApplicationWindow { id:host; required property var deviceProvider; width:1000;height:700;visible:true; ") + type + " { objectName: \"standalone\"; anchors.fill:parent; provider:host.deviceProvider } }",
		    QUrl(QStringLiteral("qrc:/ActionPanel.qml")));
		check(component.isReady(), qPrintable(component.errorString()));
		std::unique_ptr<QObject> root(component.createWithInitialProperties(
		    {{QStringLiteral("deviceProvider"), QVariant::fromValue(&p)}}));
		check(bool(root), "installed standalone action panel creates");
		events();
		QTest::qWait(50);
		auto* window = qobject_cast<QQuickWindow*>(root.get());
		check(window != nullptr, "standalone action uses real window");
		auto* panel = root->findChild<QObject*>(QStringLiteral("standalone"));
		check(panel && !panel->property("selectionMatches").toBool(), "wrong endpoint category hidden");
		check(!visual(window->contentItem(), QStringLiteral("applyButton")) &&
		          !visual(window->contentItem(), QStringLiteral("executeButton")) &&
		          !visual(window->contentItem(), QStringLiteral("callServiceButton")),
		      "mismatched panel has no visible actions");
		check(h.packets == before && !p.busy(), "mounting panels performs no requests");
		select(p, command ? "Reset" : "Ping");
		events();
		QTest::qWait(50);
		auto* button = visual(window->contentItem(), command ? QStringLiteral("executeButton") : QStringLiteral("callServiceButton"));
		check(button && button->isEnabled(), "selected category enables its explicit action");
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
		                  button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
		drain(h, p);
		check(h.packets == before + 1, "one explicit click uses existing FIFO exactly once");
		check(command ? p.commandOutcome().startsWith(QStringLiteral("Executed")) :
		                  p.serviceOutcome().contains(QStringLiteral("no response value")),
		      "standalone action publishes actual endpoint result");
		window->close();
		root.reset();
		engine.collectGarbage();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		events();
		check(warnings == 0, "standalone action QML diagnostics clean");
	}
}

int main(int argc, char** argv)
{
	if (argc == 2 && std::strcmp(argv[1], "--qt-version") == 0) {
		std::puts(qVersion());
		return 0;
	}
	QGuiApplication app(argc, argv);
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
			++warnings;
			std::fprintf(stderr, "Qt: %s\n", qPrintable(message));
		}
	});
	try {
		fields();
		commandsAndServices();
		responseMutationLifetime();
		queueRefusal();
		unknownWrite();
		individualWatch();
		qmlInput(argc > 1 ? QString::fromLocal8Bit(argv[1])
		                  : QStringLiteral("static-configurator.png"));
		qmlInput((argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("static-configurator.png")) + QStringLiteral(".field.png"), true);
		qmlActionPanels();
		if (argc == 3 && std::strcmp(argv[2], "--inject-nonqt-leak") == 0)
			injectedNonQtLeak();
		std::printf("Static QML provider checks=%u warnings=%u\n", checks, warnings);
		std::fflush(stdout);
		return warnings ? 1 : 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
