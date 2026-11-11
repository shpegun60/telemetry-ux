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
class Source : public QAbstractListModel {
public:
    struct Row { QString name, status = QStringLiteral("NotRead"), text, rev = QStringLiteral("0"), epoch = QStringLiteral("1"); };
    enum { Name = Qt::UserRole + 1, Status, Text, Revision, Epoch };
    QList<Row> rows{{QStringLiteral("First"), QStringLiteral("NotRead"), {}, QStringLiteral("0"), QStringLiteral("1")},
                    {QStringLiteral("Second"), QStringLiteral("NotRead"), {}, QStringLiteral("0"), QStringLiteral("1")}};
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= rows.size()) return {};
        const auto& r = rows[index.row()];
        switch (role) { case Name: return r.name; case Status: return r.status; case Text: return r.text;
                        case Revision: return r.rev; case Epoch: return r.epoch; default: return {}; }
    }
    QHash<int,QByteArray> roleNames() const override {
        return {{Name,"name"},{Status,"status"},{Text,"sampleText"},{Revision,"sampleRevision"},{Epoch,"sampleEpoch"}};
    }
    void publish(int i, QString status, QString value, QString revision, QString epoch = QStringLiteral("1")) {
        auto& r = rows[i]; r.status = status; r.text = value; r.rev = revision; r.epoch = epoch;
        Q_EMIT dataChanged(index(0, 0), index(int(rows.size())-1, 0), {Status, Text, Revision, Epoch});
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
    CHECK(h.points().size() == 3 && h.state().contains("losing"));
    source.publish(1, "Ok", "6", "5");
    CHECK(h.points().size() == 4 && h.points().last().toMap().value("breakBefore").toBool());
    source.publish(1, "Ok", "7", "5", "2");
    CHECK(h.points().size() == 5 && h.points().last().toMap().value("breakBefore").toBool());
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
    CHECK(real.setResult(0, "Ok", "3.25"));
    CHECK(real.setResult(0, "Ok", "3.25"));
    CHECK(native.points().size() == 2 && !native.points().last().toMap().value("breakBefore").toBool());
    real.invalidateValues();
    const int sampleRole = real.roleNames().key("sampleText");
    CHECK(real.data(real.index(0, 0), sampleRole).toString().isEmpty());
    CHECK(real.setResult(0, "Ok", "3.5"));
    CHECK(native.points().size() == 3 && native.points().last().toMap().value("breakBefore").toBool());
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
        CHECK(real.setResult(0, "Ok", "7"));
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
