// Log snapshots and current problems, including a files-only TCP connection
// that is lost without needing same-peer recovery. No board. MIT.
#include <Telemetry/UX/EventLog.hpp>
#include <Telemetry/UX/SessionProblems.hpp>
#include <integration/tcp/Server.hpp>
#include <resource/BytesFile.hpp>
#include <telemetry/Telemetry.hpp>
#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>

namespace qt = telemetry::integration::qt;
namespace tcp = telemetry::integration::tcp;
using namespace telemetry::ux;

namespace {
unsigned checks = 0;
void check(bool okay, int line)
{
	++checks;
	if (!okay) {
		std::fprintf(stderr, "Event log check %u failed at line %d\n", checks, line);
		std::exit(1);
	}
}
#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)

bool await(const std::function<bool()>& done)
{
	QElapsedTimer deadline;
	deadline.start();
	QTimer wake;
	wake.setTimerType(Qt::PreciseTimer);
	wake.start(5);
	while (!done() && deadline.elapsed() < 15000)
		QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
	return done();
}

constexpr telemetry::FieldCatalogTable fields{};
constexpr telemetry::CommandCatalogTable commands{};
constexpr telemetry::ServiceCatalogTable services{};
constexpr telemetry::Model model{fields, commands, services};
const std::array<std::byte, 1> note{std::byte{42}};
const resource::BytesFile noteFile{note};
const auto files = resource::filesystem(resource::file("/note.bin", noteFile));

class Peer {
public:
	QTcpServer listener;
	QPointer<QTcpSocket> socket;
	std::unique_ptr<tcp::Server> server;
	alignas(32) std::array<std::byte, 1> scratch{};
	telemetry::Workspace workspace{scratch};
	std::array<std::byte, 4096> rx{}, tx{};
	QByteArray pending;

	Peer()
	{
		CHECK(listener.listen(QHostAddress::LocalHost, 0));
		QObject::connect(&listener, &QTcpServer::newConnection, &listener, [this] {
			socket = listener.nextPendingConnection();
			pending.clear();
			server = std::make_unique<tcp::Server>(model.view(), workspace, files.view(), rx, tx);
			QObject::connect(socket, &QTcpSocket::readyRead, &listener, [this] { read(); });
		});
	}

	~Peer()
	{
		if (socket) {
			QObject::disconnect(socket, nullptr, &listener, nullptr);
			socket->abort();
		}
	}

	void read()
	{
		pending += socket->readAll();
		while (!pending.isEmpty()) {
			const auto result = server->receive({reinterpret_cast<const std::byte*>(pending.constData()),
			                                     static_cast<std::size_t>(pending.size())});
			pending.remove(0, static_cast<qsizetype>(result.consumed));
			if (result.status == tcp::Status::NeedMore)
				break;
			const auto bytes = server->reply();
			socket->write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
			server->releaseReply();
		}
	}
};

void exportSnapshot()
{
	LogModel log;
	LogFilterModel shown(&log);
	{
		QAbstractItemModelTester tester(&log, QAbstractItemModelTester::FailureReportingMode::Fatal);
		log.append(Severity::Info, QStringLiteral("Connection"), QStringLiteral("Connected."));
		log.append(Severity::Warning, QStringLiteral("Device"), QStringLiteral("Keep this warning."));
	}
	shown.setMinimumSeverity(Severity::Warning);
	CHECK(shown.rowCount() == 1);
	const auto rows = shown.sourceRows();
	const auto captured = log.text(rows);
	CHECK(captured.contains(QStringLiteral("Keep this warning.")) && !captured.contains(QStringLiteral("Connected.")));
	// The save dialog stays open while row zero changes. Export must retain
	// the selected text even after the old entries leave the bounded model.
	for (int i = 0; i < LogModel::maxEntries + 1; ++i)
		log.append(Severity::Info, QStringLiteral("Test"), QString::number(i));
	CHECK(log.count() == LogModel::maxEntries && log.text(rows) != captured && shown.rowCount() == 0);
	QTemporaryDir directory;
	CHECK(directory.isValid());
	const auto path = directory.filePath(QStringLiteral("log.txt"));
	CHECK(log.saveText(path, captured).isEmpty());
	QFile file(path);
	CHECK(file.open(QIODevice::ReadOnly | QIODevice::Text));
	CHECK(QString::fromUtf8(file.readAll()) == captured);
	file.close();
	log.clear();
	CHECK(log.saveText(path, captured).isEmpty());
	CHECK(file.open(QIODevice::ReadOnly | QIODevice::Text) && QString::fromUtf8(file.readAll()) == captured);
	CHECK(!log.saveText(directory.filePath(QStringLiteral("missing/log.txt")), captured).isEmpty());
}

void problemLifecycle()
{
	LogModel log;
	ProblemsModel problems(&log);
	QAbstractItemModelTester tester(&problems, QAbstractItemModelTester::FailureReportingMode::Fatal);
	problems.raise(QStringLiteral("first"), Severity::Warning, QStringLiteral("Device"), QStringLiteral("First"));
	problems.raise(QStringLiteral("second"), Severity::Error, QStringLiteral("Connection"), QStringLiteral("Second"),
	               QStringLiteral("device"));
	CHECK(problems.errorCount() == 1 && problems.warningCount() == 1 && log.count() == 2);
	CHECK(problems.index(0).data(ProblemsModel::KeyRole).toString() == QStringLiteral("second"));
	problems.raise(QStringLiteral("first"), Severity::Error, QStringLiteral("Device"), QStringLiteral("First updated"));
	CHECK(problems.errorCount() == 2 && log.count() == 2);
	CHECK(problems.index(1).data(ProblemsModel::TextRole).toString() == QStringLiteral("First updated"));
	problems.resolveScope(QStringLiteral("device"));
	CHECK(problems.count() == 1 && problems.has(QStringLiteral("first")) && log.count() == 3);
	problems.resolve(QStringLiteral("first"));
	CHECK(problems.count() == 0 && log.count() == 4);
	problems.resolve(QStringLiteral("first"));
	CHECK(log.count() == 4);
}

void publicationLifetime()
{
	// A host may remove a panel and its model from the final notification.
	{
		auto log = std::make_unique<LogModel>();
		const QPointer<LogModel> alive(log.get());
		QObject::connect(log.get(), &LogModel::countChanged, [&] { log.reset(); });
		log->append(Severity::Info, QStringLiteral("Test"), QStringLiteral("A final event"));
		CHECK(!alive);
	}
	{
		LogModel log;
		auto problems = std::make_unique<ProblemsModel>(&log);
		const QPointer<ProblemsModel> alive(problems.get());
		QObject::connect(problems.get(), &ProblemsModel::countsChanged, [&] { problems.reset(); });
		problems->raise(QStringLiteral("one"), Severity::Error, QStringLiteral("Test"), QStringLiteral("One"));
		CHECK(!alive && log.count() == 1);
	}
	{
		LogModel log;
		auto problems = std::make_unique<ProblemsModel>(&log);
		const QPointer<ProblemsModel> alive(problems.get());
		QObject::connect(&log, &LogModel::appended, [&] { problems.reset(); });
		problems->raise(QStringLiteral("one"), Severity::Error, QStringLiteral("Test"), QStringLiteral("One"));
		CHECK(!alive && log.count() == 1);
	}
	// Resolving immediately from the count notification is a completed model
	// operation; the event history still says raised, then resolved.
	{
		LogModel log;
		ProblemsModel problems(&log);
		QObject::connect(&problems, &ProblemsModel::countsChanged, [&] {
			if (problems.has(QStringLiteral("one")))
				problems.resolve(QStringLiteral("one"));
		});
		problems.raise(QStringLiteral("one"), Severity::Error, QStringLiteral("Test"), QStringLiteral("One"));
		CHECK(problems.count() == 0 && log.count() == 2);
		CHECK(log.index(1).data(LogModel::TextRole).toString() == QStringLiteral("One"));
		CHECK(log.index(0).data(LogModel::TextRole).toString() == QStringLiteral("Resolved: One"));
	}
}

void filesOnlyLoss()
{
	Peer peer;
	qt::ConnectionSession session;
	LogModel log;
	ProblemsModel problems(&log);
	SessionProblems adapter(&session, &problems);
	CHECK(session.connectTcpReference(QStringLiteral("127.0.0.1"), peer.listener.serverPort()));
	CHECK(await([&] { return session.filesOnly(); }));
	CHECK(problems.has(QStringLiteral("session.limitation")) && problems.warningCount() == 1);
	CHECK(peer.socket && !session.recoveryRequired() && session.lastError().isEmpty());
	peer.socket->abort();
	CHECK(await([&] { return !session.filesSupported() && !session.lastError().isEmpty(); }));
	CHECK(!session.recoveryRequired());
	CHECK(problems.has(QStringLiteral("session.connection")) && problems.errorCount() == 1);
	CHECK(log.newest() == session.lastError());
	const int beforeRepeat = log.count();
	QMetaObject::invokeMethod(&session, "stateChanged", Qt::DirectConnection);
	CHECK(log.count() == beforeRepeat);
	CHECK(session.connectTcpReference(QStringLiteral("127.0.0.1"), peer.listener.serverPort()));
	CHECK(await([&] { return session.filesOnly(); }));
	CHECK(!problems.has(QStringLiteral("session.connection")) && problems.errorCount() == 0);
	session.disconnect();
	CHECK(problems.count() == 0 && session.lastError().isEmpty());
	// A refused initial connection is an error too, although no request was
	// handed off and there is no recovery obligation.
	const auto port = peer.listener.serverPort();
	peer.listener.close();
	CHECK(session.connectTcpReference(QStringLiteral("127.0.0.1"), port));
	CHECK(await([&] { return !session.connecting() && !session.lastError().isEmpty(); }));
	CHECK(!session.recoveryRequired() && problems.errorCount() == 1 &&
	      problems.has(QStringLiteral("session.connection")));
	CHECK(log.newest() == session.lastError());
}

void sessionPublicationLifetime()
{
	Peer peer;
	const auto port = peer.listener.serverPort();
	peer.listener.close();
	Peer reconnected;
	for (int scenario = 0; scenario < 4; ++scenario) {
		auto session = std::make_unique<qt::ConnectionSession>();
		CHECK(session->connectTcpReference(QStringLiteral("127.0.0.1"), port));
		CHECK(await([&] { return !session->connecting() && !session->lastError().isEmpty(); }));
		LogModel log;
		auto problems = std::make_unique<ProblemsModel>(&log);
		auto adapter = std::make_unique<SessionProblems>(session.get(), problems.get());
		CHECK(problems->has(QStringLiteral("session.connection")));
		// Start with no problem, so the next update inserts the connection
		// error and publishes a final countsChanged, outside native progress.
		problems->resolveScope(QStringLiteral("device"));
		CHECK(problems->count() == 0);
		const QPointer<SessionProblems> adapterAlive(adapter.get());
		const QPointer<qt::ConnectionSession> sessionAlive(session.get());
		const QPointer<ProblemsModel> problemsAlive(problems.get());
		int publications = 0;
		QObject::connect(problems.get(), &ProblemsModel::countsChanged, &log, [&] {
			if (++publications != 1)
				return;
			if (scenario == 0)
				adapter.reset();
			else if (scenario == 1)
				session.reset();
			else if (scenario == 2)
				problems.reset();
			else
				// A new attempt clears lastError, while its previous Problem
				// stays until admission confirms the replacement connection.
				CHECK(session->connectTcpReference(QStringLiteral("127.0.0.1"),
				                                  reconnected.listener.serverPort()));
		});
		CHECK(QMetaObject::invokeMethod(session.get(), "stateChanged", Qt::DirectConnection));
		CHECK(publications >= 1);
		if (scenario == 0) {
			CHECK(!adapterAlive && sessionAlive && problemsAlive);
			CHECK(problems->count() == 1);
		} else if (scenario == 1) {
			CHECK(adapterAlive && !sessionAlive && problemsAlive);
			CHECK(problems->count() == 1);
		} else if (scenario == 2) {
			CHECK(adapterAlive && sessionAlive && !problemsAlive);
		} else {
			CHECK(adapterAlive && sessionAlive && problemsAlive);
			std::printf("SessionProblems reentrant admission: error='%s' count=%d publications=%d connecting=%d\n",
			            qPrintable(session->lastError()), problems->count(), publications,
			            int(session->connecting()));
			CHECK(session->lastError().isEmpty() && session->connecting());
			const bool admitted = await([&] {
				return session->filesOnly() && !problems->has(QStringLiteral("session.connection")) &&
				       problems->has(QStringLiteral("session.limitation"));
			});
			std::printf("SessionProblems reentrant admitted: filesOnly=%d errors=%d warnings=%d publications=%d\n",
			            int(session->filesOnly()), problems->errorCount(), problems->warningCount(), publications);
			CHECK(admitted && problems->errorCount() == 0 && problems->warningCount() == 1);
			// Error inserted, resolved at admission, then the files-only
			// limitation inserted: no obsolete connection Problem survives.
			CHECK(publications == 3);
		}
	}
}
// LG-1: a line names its target and its outcome, its severity from the
// outcome's kind and never from its words; one line per operation, its
// pending shown only past the threshold; a grouped event is one line with
// its list, and its resolution says what changed.
QVariantMap operation(const QString& key, const QString& result, const QString& text,
                      const QString& path = QStringLiteral("control/Reset"))
{
	return {{QStringLiteral("key"), key},       {QStringLiteral("source"), QStringLiteral("Command")},
	        {QStringLiteral("kind"), QStringLiteral("command")}, {QStringLiteral("path"), path},
	        {QStringLiteral("result"), result}, {QStringLiteral("text"), text}};
}

QStringList paths(int count)
{
	QStringList out;
	for (int i = 1; i <= count; ++i)
		out << QStringLiteral("signals/Signal%1").arg(i, 2, 10, QLatin1Char('0'));
	return out;
}

void operationLines()
{
	LogModel log;
	QAbstractItemModelTester tester(&log, QAbstractItemModelTester::FailureReportingMode::Fatal);
	// An operation answered under the threshold: one line, its outcome only.
	log.record(operation(QStringLiteral("op:1"), QStringLiteral("pending"), QStringLiteral("executing…")));
	CHECK(log.count() == 0);
	log.record(operation(QStringLiteral("op:1"), QStringLiteral("ok"), QStringLiteral("executed")));
	CHECK(log.count() == 1 && log.index(0).data(LogModel::TextRole) == QStringLiteral("executed") &&
	      log.index(0).data(LogModel::PathRole) == QStringLiteral("control/Reset") &&
	      log.index(0).data(LogModel::KindRole) == QStringLiteral("command") &&
	      log.index(0).data(LogModel::SourceRole) == QStringLiteral("Command") &&
	      log.index(0).data(LogModel::SeverityRole).toInt() == Severity::Info);
	CHECK(log.newest() == QStringLiteral("control/Reset  executed"));
	QElapsedTimer waited;
	waited.start();
	await([&] { return waited.elapsed() > LogModel::progressThresholdMs + 200; });
	CHECK(log.count() == 1);
	// One that outlasts it: a pending line, which its outcome then replaces in
	// place, keeping its time; an error counted once it is one.
	log.record(operation(QStringLiteral("op:2"), QStringLiteral("pending"), QStringLiteral("executing…")));
	CHECK(log.count() == 1);
	CHECK(await([&] { return log.count() == 2; }));
	CHECK(log.index(0).data(LogModel::TextRole) == QStringLiteral("executing…") && log.errorCount() == 0);
	const auto shownAt = log.index(0).data(LogModel::TimeRole);
	log.record(operation(QStringLiteral("op:2"), QStringLiteral("failed"), QStringLiteral("failed · no reply in time")));
	CHECK(log.count() == 2 && log.index(0).data(LogModel::TextRole) == QStringLiteral("failed · no reply in time") &&
	      log.index(0).data(LogModel::TimeRole) == shownAt && log.errorCount() == 1 &&
	      log.index(0).data(LogModel::SeverityRole).toInt() == Severity::Error);
	// A later word on the same operation (what a refused Field holds) too.
	log.record(operation(QStringLiteral("op:2"), QStringLiteral("refused"), QStringLiteral("refused · it holds 3")));
	CHECK(log.count() == 2 && log.errorCount() == 1);
	// Severity from the outcome's kind, never from its words.
	log.clear();
	log.record(operation(QStringLiteral("op:3"), QStringLiteral("ok"), QStringLiteral("failed refused error timeout")));
	log.record(operation(QStringLiteral("op:4"), QStringLiteral("doubt"), QStringLiteral("written")));
	log.record(operation(QStringLiteral("op:5"), QStringLiteral("refused"), QStringLiteral("executed")));
	CHECK(log.index(2).data(LogModel::SeverityRole).toInt() == Severity::Info &&
	      log.index(1).data(LogModel::SeverityRole).toInt() == Severity::Warning &&
	      log.index(0).data(LogModel::SeverityRole).toInt() == Severity::Error && log.errorCount() == 1);
	// A Service's target: its request and its answer after the path.
	auto called = operation(QStringLiteral("op:6"), QStringLiteral("ok"), QStringLiteral("called · 19 ms"),
	                        QStringLiteral("control/Scale"));
	called[QStringLiteral("values")] = QStringLiteral("(2, 3) → 6");
	log.record(called);
	CHECK(log.index(0).data(LogModel::LineRole) == QStringLiteral("control/Scale (2, 3) → 6  called · 19 ms"));
	CHECK(log.text({0}).contains(QStringLiteral("  Info  Command  control/Scale (2, 3) → 6  called · 19 ms\n")));
	// A plain event stays its words.
	log.append(Severity::Info, QStringLiteral("Connection"), QStringLiteral("Connected."));
	CHECK(log.index(0).data(LogModel::LineRole) == QStringLiteral("Connected.") &&
	      log.index(0).data(LogModel::PathRole).toString().isEmpty());

	// A grouped warning: one line, its list held in it.
	LogFilterModel shown(&log);
	ProblemsModel problems(&log);
	const int before = log.count();
	problems.raise(QStringLiteral("project.watch"), Severity::Warning, QStringLiteral("Watch"),
	               QStringLiteral("8 watched values wait for a device"), {}, paths(8));
	CHECK(log.count() == before + 1 && log.newest() == QStringLiteral("8 watched values wait for a device"));
	CHECK(log.index(0).data(LogModel::DetailRole).toString().split(QLatin1Char('\n')) == paths(8) &&
	      problems.index(0).data(ProblemsModel::DetailRole).toString().split(QLatin1Char('\n')) == paths(8));
	CHECK(log.text({0}).count(QLatin1Char('\n')) == 9 && log.text({0}).contains(QStringLiteral("\n    signals/Signal08\n")));
	shown.setText(QStringLiteral("Signal07"));
	CHECK(shown.rowCount() == 1);
	shown.setText({});
	// Raised again with another list: in place, the Log hears nothing.
	problems.raise(QStringLiteral("project.watch"), Severity::Warning, QStringLiteral("Watch"),
	               QStringLiteral("32 watched values wait for a device"), {}, paths(32));
	const auto opened = problems.index(0).data(ProblemsModel::DetailRole).toString().split(QLatin1Char('\n'));
	CHECK(log.count() == before + 1 && opened.size() == LogModel::detailLines + 1 &&
	      opened.back() == QStringLiteral("and 12 more") && opened.front() == QStringLiteral("signals/Signal01"));
	// Its resolution says what changed, never the old words.
	problems.resolveWith(QStringLiteral("project.watch"), QStringLiteral("All 32 watched values found on Emulated device"));
	CHECK(problems.count() == 0 && log.count() == before + 2 &&
	      log.newest() == QStringLiteral("All 32 watched values found on Emulated device") &&
	      log.index(0).data(LogModel::SeverityRole).toInt() == Severity::Info &&
	      log.index(0).data(LogModel::SourceRole) == QStringLiteral("Watch"));
	// A resolution that leaves something behind: a warning with what is left.
	problems.raise(QStringLiteral("project.watch"), Severity::Warning, QStringLiteral("Watch"),
	               QStringLiteral("8 watched values wait for a device"), {}, paths(8));
	problems.resolveWith(QStringLiteral("project.watch"), QStringLiteral("5 of 8 found on Emulated device · 3 are not on it"),
	                     Severity::Warning, paths(3));
	CHECK(log.index(0).data(LogModel::SeverityRole).toInt() == Severity::Warning &&
	      log.index(0).data(LogModel::DetailRole).toString().split(QLatin1Char('\n')) == paths(3));
	// Resolved without words: "Resolved: " and the summary, never a line of a
	// list read as if it still held.
	problems.raise(QStringLiteral("old"), Severity::Warning, QStringLiteral("Project"),
	               QStringLiteral("measure/Voltage: waiting for a device\nmeasure/Current: waiting for a device"));
	problems.resolve(QStringLiteral("old"));
	CHECK(log.newest() == QStringLiteral("Resolved: measure/Voltage: waiting for a device"));
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	exportSnapshot();
	problemLifecycle();
	publicationLifetime();
	operationLines();
	filesOnlyLoss();
	sessionPublicationLifetime();
	std::printf("Event log: %u checks, 0 failures\n", checks);
	return 0;
}
