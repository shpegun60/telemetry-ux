#include "../../lib/qml/Telemetry/UX/TrendHistory.hpp"
#include "../../lib/qml/Telemetry/UX/WatchlistModel.hpp"
#include <QtCore/QAbstractListModel>
#include <QtCore/QDebug>
#include <QtCore/QThread>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtTest/QTest>
#include <cstdio>
using telemetry::ux::TrendHistory;
namespace {
int checks = 0;
int warnings = 0;
void messages(QtMsgType type, const QMessageLogContext&, const QString& message) {
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) ++warnings;
    std::fprintf(stderr, "%s\n", qPrintable(message));
}
#define CHECK(x) do { ++checks; if (!(x)) { qCritical() << "Trend failure" << __LINE__ << #x; return 1; } } while (false)
// A provider model supplies typed sample facts beside its display text; the fake
// derives them from the test's literal the way a provider derives them from a value.
telemetry::ux::WatchSample sampleFrom(const QString& text) {
    using telemetry::ux::WatchSample;
    bool ok = false;
    if (const auto s = text.toLongLong(&ok); ok) return WatchSample::ofSigned(s);
    if (const auto u = text.toULongLong(&ok); ok) return WatchSample::ofUnsigned(u);
    const double d = text.toDouble(&ok);
    return ok ? WatchSample::ofFloating(d) : WatchSample{};
}
class Source : public QAbstractListModel {
public:
    struct Row { QString name, status = QStringLiteral("NotRead"), text, rev = QStringLiteral("0"), epoch = QStringLiteral("1"); telemetry::ux::WatchSample sample; QVariant code; };
    enum { Name = Qt::UserRole + 1, Status, Text, Revision, Epoch, Value, Exact, Code };
    QList<Row> rows{{QStringLiteral("First"), QStringLiteral("NotRead"), {}, QStringLiteral("0"), QStringLiteral("1"), {}, {}},
                    {QStringLiteral("Second"), QStringLiteral("NotRead"), {}, QStringLiteral("0"), QStringLiteral("1"), {}, {}}};
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= rows.size()) return {};
        const auto& r = rows[index.row()];
        switch (role) { case Name: return r.name; case Status: return r.status; case Text: return r.text;
                        case Revision: return r.rev; case Epoch: return r.epoch;
                        case Value: return r.sample.role(); case Exact: return r.sample.exact;
                        case Code: return r.code.isValid() ? r.code : QVariant(int(telemetry::ux::watchStatusCode(r.status)));
                        default: return {}; }
    }
    QHash<int,QByteArray> roleNames() const override {
        return {{Name,"name"},{Status,"status"},{Text,"sampleText"},{Revision,"sampleRevision"},{Epoch,"sampleEpoch"},
                {Value,"sampleValue"},{Exact,"sampleExact"},{Code,"statusCode"}};
    }
    void publish(int i, QString status, QString value, QString revision, QString epoch = QStringLiteral("1")) {
        publishSample(i, status, value, sampleFrom(value), revision, epoch);
    }
    void publishSample(int i, QString status, QString value, telemetry::ux::WatchSample sample, QString revision,
                       QString epoch = QStringLiteral("1")) {
        auto& r = rows[i]; r.status = status; r.text = value; r.rev = revision; r.epoch = epoch; r.sample = sample; r.code = {};
        Q_EMIT dataChanged(index(0, 0), index(int(rows.size())-1, 0), {Status, Text, Revision, Epoch, Value, Exact, Code});
    }
    // A third-party model's own code, valid or not.
    void publishCode(int i, QString status, QVariant code, QString revision, QString epoch = QStringLiteral("1")) {
        auto& r = rows[i]; r.status = status; r.code = code; r.rev = revision; r.epoch = epoch;
        Q_EMIT dataChanged(index(i, 0), index(i, 0), {Status, Code, Revision, Epoch});
    }
    void remove(int i) { beginRemoveRows({}, i, i); rows.removeAt(i); endRemoveRows(); }
    void move(int from, int to) {
        beginMoveRows({}, from, from, {}, from < to ? to + 1 : to);
        rows.move(from, to);
        endMoveRows();
    }
    void reset() { beginResetModel(); endResetModel(); }
};
class Provider : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* watchlist READ watchlist CONSTANT)
public:
    Source source;
    QAbstractItemModel* watchlist() { return &source; }
};
}
int main(int argc, char** argv) {
    qInstallMessageHandler(messages);
    QGuiApplication app(argc, argv);
    Source source;
    TrendHistory h;
    h.setSource(&source); h.setRow(1);
    CHECK(h.points().isEmpty() && h.row() == 1);
    source.publish(1, "Ok", "4.25", "1");
    CHECK(h.points().size() == 1 && h.points().last().toMap().value("text") == "4.25");
    CHECK(h.points().last().toMap().value("breakBefore").toBool());
    source.publish(0, "Ok", "99", "1");
    CHECK(h.points().size() == 1);
    source.publish(1, "Pending", {}, "1");
    source.publish(1, "Ok", "4.5", "2");
    CHECK(h.points().size() == 2 && !h.points().last().toMap().value("breakBefore").toBool());
    source.publish(1, "Unavailable", {}, "2");
    source.publish(1, "Ok", "5", "3");
    CHECK(h.points().size() == 3 && h.points().last().toMap().value("breakBefore").toBool());
    source.publish(1, "Ok", "9007199254740993", "4");
    CHECK(h.points().size() == 3 && h.state().contains("Not plotted"));
    source.publish(1, "Ok", "6", "5");
    CHECK(h.points().size() == 4 && h.points().last().toMap().value("breakBefore").toBool());
    source.publish(1, "Ok", "7", "5", "2");
    CHECK(h.points().size() == 5 && h.points().last().toMap().value("breakBefore").toBool());
    // The number comes from the typed sample, never from the display text: a
    // plain-looking text without an exact sample is not plotted, and an enum's
    // display text ("Known (1)") with an exact sample is plotted as its code.
    source.publishSample(1, "Ok", "8.5", {}, "6", "2");
    CHECK(h.points().size() == 5 && h.state().contains("Not plotted"));
    source.publishSample(1, "Ok", "Known (1)", telemetry::ux::WatchSample::ofSigned(1), "7", "2");
    CHECK(h.points().size() == 6 && h.points().last().toMap().value("value").toDouble() == 1.0);
    CHECK(h.points().last().toMap().value("text") == "Known (1)");
    // Both providers' wordings of "not read" leave the line unbroken.
    source.publish(1, "Not read", {}, "7", "2");
    source.publish(1, "Ok", "9", "8", "2");
    CHECK(h.points().size() == 7 && !h.points().last().toMap().value("breakBefore").toBool());
    // An unknown or missing status code counts as a failure and breaks the line.
    source.publishCode(1, "ReadError", 77, "8", "2");
    source.publish(1, "Ok", "10", "9", "2");
    CHECK(h.points().size() == 8 && h.points().last().toMap().value("breakBefore").toBool());
    source.publishCode(1, "ReadError", QVariant(), "9", "2");
    source.publish(1, "Ok", "11", "10", "2");
    CHECK(h.points().size() == 9 && h.points().last().toMap().value("breakBefore").toBool());
    source.remove(0);
    CHECK(h.row() == 0);
    for (int i=6; i<540; ++i) source.publish(0, "Ok", QString::number(i), QString::number(i), "2");
    CHECK(h.points().size() == 512);
    source.reset(); CHECK(h.row() == -1);
    telemetry::ux::WatchlistModel real;
    real.bind([] { return true; });
    CHECK(real.add(42, QStringLiteral("Real")));
    TrendHistory native;
    native.setSource(&real); native.setRow(0);
    using telemetry::ux::WatchSample;
    CHECK(real.setResult(0, "Ok", "3.25", WatchSample::ofFloating(3.25)));
    CHECK(real.setResult(0, "Ok", "3.25", WatchSample::ofFloating(3.25)));
    CHECK(native.points().size() == 2 && !native.points().last().toMap().value("breakBefore").toBool());
    const int valueRole = real.roleNames().key("sampleValue");
    const int exactRole = real.roleNames().key("sampleExact");
    const int codeRole = real.roleNames().key("statusCode");
    CHECK(real.data(real.index(0, 0), valueRole).toDouble() == 3.25);
    CHECK(real.data(real.index(0, 0), exactRole).toBool());
    CHECK(real.data(real.index(0, 0), codeRole).toInt() == int(telemetry::ux::WatchStatus::Ok));
    real.invalidateValues();
    const int sampleRole = real.roleNames().key("sampleText");
    CHECK(real.data(real.index(0, 0), sampleRole).toString().isEmpty());
    CHECK(!real.data(real.index(0, 0), valueRole).isValid() && !real.data(real.index(0, 0), exactRole).toBool());
    CHECK(real.data(real.index(0, 0), codeRole).toInt() == int(telemetry::ux::WatchStatus::Stale));
    CHECK(real.setResult(0, "Ok", "3.5", WatchSample::ofFloating(3.5)));
    CHECK(native.points().size() == 3 && native.points().last().toMap().value("breakBefore").toBool());
    // U64 beyond 2^53 - 1 keeps its exact text and is not plotted.
    CHECK(real.setResult(0, "Ok", "18446744073709551615", WatchSample::ofUnsigned(18446744073709551615ULL)));
    CHECK(native.points().size() == 3 && !real.data(real.index(0, 0), exactRole).toBool());
    CHECK(!real.data(real.index(0, 0), valueRole).isValid());
    // Slots may destroy or replace a history synchronously during publication.
    {
        auto* doomed = new TrendHistory;
        QObject::connect(doomed, &TrendHistory::pointsChanged, [&] { delete doomed; doomed = nullptr; });
        doomed->setSource(&real);
        CHECK(doomed == nullptr);
    }
    {
        auto* doomed = new TrendHistory;
        QObject::connect(doomed, &TrendHistory::rowChanged, [&] { delete doomed; doomed = nullptr; });
        doomed->setSource(&real);
        CHECK(doomed == nullptr);
    }
    {
        auto* doomed = new TrendHistory;
        doomed->setSource(&real);
        QObject::connect(doomed, &TrendHistory::stateChanged, [&] { delete doomed; doomed = nullptr; });
        doomed->setRow(0);
        CHECK(doomed == nullptr);
    }
    {
        Source replacement;
        TrendHistory reentrant;
        bool changed = false;
        QObject::connect(&reentrant, &TrendHistory::pointsChanged, [&] {
            if (!changed) { changed = true; reentrant.setSource(&replacement); }
        });
        reentrant.setSource(&real);
        CHECK(changed && reentrant.source() == &replacement && reentrant.row() == -1);
    }
    {
        TrendHistory reentrant;
        reentrant.setSource(&real);
        reentrant.setRow(0);
        int sourceSignals = 0;
        QObject::connect(&reentrant, &TrendHistory::sourceChanged, [&] { ++sourceSignals; });
        QObject::connect(&reentrant, &TrendHistory::stateChanged, [&] { reentrant.clear(); });
        Source replacement;
        reentrant.setSource(&replacement);
        CHECK(reentrant.source() == &replacement && sourceSignals == 1);
    }
    {
        auto* doomed = new TrendHistory;
        doomed->setSource(&real); doomed->setRow(0);
        QObject::connect(doomed, &TrendHistory::stateChanged, [&] { delete doomed; doomed = nullptr; });
        CHECK(real.setResult(0, "Ok", "7", WatchSample::ofSigned(7)));
        CHECK(doomed == nullptr);
    }
    {
        Source sourceToReset;
        auto* doomed = new TrendHistory;
        doomed->setSource(&sourceToReset); doomed->setRow(0);
        QObject::connect(doomed, &TrendHistory::stateChanged, [&] { delete doomed; doomed = nullptr; });
        sourceToReset.reset();
        CHECK(doomed == nullptr);
    }
    {
        auto* destroyedSource = new Source;
        auto* doomed = new TrendHistory;
        doomed->setSource(destroyedSource); doomed->setRow(0);
        QObject::connect(doomed, &TrendHistory::stateChanged, [&] { delete doomed; doomed = nullptr; });
        delete destroyedSource;
        CHECK(doomed == nullptr);
    }
    {
        QThread worker;
        auto* foreign = new Source;
        foreign->moveToThread(&worker);
        QObject::connect(&worker, &QThread::finished, foreign, &QObject::deleteLater);
        worker.start();
        TrendHistory owner;
        owner.setSource(&real);
        owner.setRow(0);
        owner.setSource(foreign);
        CHECK(owner.source() == nullptr && owner.row() == -1 && owner.state().contains("thread"));
        worker.quit(); worker.wait();
    }
    Provider provider;
    TrendHistory panelHistory;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("testProvider", &provider);
    engine.rootContext()->setContextProperty("testHistory", &panelHistory);
    engine.load(QUrl("qrc:/Trend.qml"));
    CHECK(!engine.rootObjects().isEmpty());
    QTest::qWait(30);
    CHECK(engine.rootObjects().first()->findChild<QObject*>("trendPanel"));
    auto* choice = engine.rootObjects().first()->findChild<QObject*>("trendField");
    CHECK(choice);
    panelHistory.setRow(1);
    CHECK(choice->property("currentIndex").toInt() == 1);
    provider.source.move(1, 0);
    QCoreApplication::processEvents();
    CHECK(panelHistory.row() == 0 && choice->property("currentIndex").toInt() == 0);
    provider.source.move(0, 1);
    QCoreApplication::processEvents();
    CHECK(panelHistory.row() == 1 && choice->property("currentIndex").toInt() == 1);
    provider.source.remove(0);
    QCoreApplication::processEvents();
    CHECK(panelHistory.row() == 0 && choice->property("currentIndex").toInt() == 0);
    provider.source.publish(0, "Ok", "-1.7976931348623157e308", "1");
    provider.source.publish(0, "Ok", "1.7976931348623157e308", "2");
    CHECK(panelHistory.points().size() == 2);
    QTest::qWait(30);
    CHECK(warnings == 0);
    qInfo().noquote() << QStringLiteral("Trend checks=%1 warnings=%2").arg(checks).arg(warnings);
    return 0;
}
#include "Trend.moc"
