// Search against actual static/dynamic provider implementations. MIT.
#include <Telemetry/UX/QmlTypes.hpp>
#include "../dynamic_qml_configurator/Fixture.hpp"
#include <Telemetry/UX/StaticConfiguratorProvider.hpp>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTest>
#include <functional>

using namespace dynamic_qml_fixture;
namespace ux = telemetry::ux;
static std::function<void()> staticChanged;

static QQuickItem* findItem(QQuickItem* root, const char* name)
{
    if (root->objectName() == QString::fromLatin1(name)) return root;
    for (auto* child : root->childItems())
        if (auto* found = findItem(child, name)) return found;
    return nullptr;
}

// Whether a row delegate is the one its view shows at its row, not one the
// view keeps in its pool for reuse (pooled delegates stay visible, culled).
static bool live(QQuickItem* row)
{
    auto* view = row->parentItem() ? row->parentItem()->parentItem() : nullptr;
    QQuickItem* shown = nullptr;
    return view && QMetaObject::invokeMethod(view, "itemAtCell", Q_RETURN_ARG(QQuickItem*, shown),
                                             Q_ARG(QPoint, QPoint(0, row->property("row").toInt()))) && shown == row;
}

// The tree's visible row of that name: find filters the tree in place.
static QQuickItem* findRow(QQuickItem* root, const QString& name)
{
    if (!root->isVisible()) return nullptr;
    if (root->objectName() == QStringLiteral("endpointRow") && root->property("name").toString() == name && live(root)) return root;
    for (auto* child : root->childItems())
        if (auto* found = findRow(child, name)) return found;
    return nullptr;
}

static void activate(QQuickWindow* window, QQuickItem* item)
{
    if (!item || !item->isVisible())
        std::fprintf(stderr, "search row is %s\n", item ? "hidden" : "absent");
    CHECK(item && item->isVisible());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    QTest::qWait(80);
}

static QQuickWindow* load(QQmlApplicationEngine& engine, QObject* provider)
{
    engine.setInitialProperties({{"ownerProvider", QVariant::fromValue(provider)}});
    engine.load(QUrl(QStringLiteral("qrc:/SearchNative.qml")));
    return qobject_cast<QQuickWindow*>(engine.rootObjects().last());
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // The Device tree shows its rows through the module's C++ types.
    telemetry::ux::registerQmlTypes();
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [](const QList<QQmlError>& errors) {
        warnings += unsigned(errors.size());
        for (const auto& error : errors) std::fprintf(stderr, "%s\n", error.toString().toUtf8().constData());
    });
    {
        Harness h;
        Provider p{h.binding.get()};
        CHECK(p.connected());
        auto* window = load(engine, &p);
        CHECK(window);
        QTest::qWait(100);
        auto* query = findItem(window->contentItem(), "endpointSearch");
        auto* status = findItem(window->contentItem(), "endpointSearchStatus");
        CHECK(query && status && query->setProperty("text", QStringLiteral("Config")));
        QTest::qWait(250);
        CHECK(status->property("text").toString() == QStringLiteral("1 match"));
        CHECK(h.storage->owner.pending() == 0 && device.reads == 0);
        auto* row = findRow(window->contentItem(), QStringLiteral("Config"));
        const auto expected = endpoint(p, "Config");
        CHECK(expected.isValid());
        activate(window, row);
        CHECK(p.selectedEndpointIndex() == expected && h.storage->owner.pending() == 0);
        QTest::qWait(150);
        QPointer<QQuickItem> oldRow(findRow(window->contentItem(), QStringLiteral("Config")));
        h.binding->retire();
        h.storage->owner.invalidate();
        CHECK(!p.connected());
        // The visible result can persist until poll, but cannot execute/select.
        if (oldRow && oldRow->isVisible()) activate(window, oldRow);
        CHECK(!p.selectEndpoint(expected) && h.storage->owner.pending() == 0 && device.reads == 0);
        h.storage->owner.process(h.storage->now);
        h.binding->poll();
        QTest::qWait(250);
        CHECK(!findRow(window->contentItem(), QStringLiteral("Config")) && !p.connected());
        CHECK(status->property("text").toString() == QStringLiteral("Nothing matches “Config”."));
        delete window;
    }
    {
        QObject api;
        api.setProperty("ready", true);
        ux::StaticConfiguratorBinding binding;
        binding.api = &api;
        ux::StaticType type;
        type.id = 0; type.kind = ux::StaticTypeKind::Scalar; type.scalarCode = 6;
        binding.types.append(type);
        ux::StaticEndpoint entry;
        entry.category = 1; entry.id = 0; entry.catalog = QStringLiteral("measure");
        entry.name = QStringLiteral("Voltage"); entry.valueType = 0; entry.readable = true;
        binding.endpoints.append(entry);
        entry.id = 1; entry.name = QStringLiteral("Current");
        binding.endpoints.append(entry);
        binding.ready = +[](QObject* obj) { return obj->property("ready").toBool(); };
        binding.session = +[](QObject*) { return quint32(1); };
        binding.connect = +[](QObject*, QObject*, std::function<void()> changed,
                              std::function<void(QString, QVariantMap)>) { staticChanged = std::move(changed); };
        ux::StaticConfiguratorProvider p{binding};
        CHECK(p.connected());
        auto* window = load(engine, &p);
        CHECK(window);
        QTest::qWait(100);
        auto* query = findItem(window->contentItem(), "endpointSearch");
        auto* status = findItem(window->contentItem(), "endpointSearchStatus");
        CHECK(query && status && query->setProperty("text", QStringLiteral("measure.Voltage")));
        QTest::qWait(250);
        CHECK(status->property("text").toString() == QStringLiteral("1 match"));
        CHECK(!findRow(window->contentItem(), QStringLiteral("Current")));
        auto* row = findRow(window->contentItem(), QStringLiteral("Voltage"));
        CHECK(row);
        activate(window, row);
        CHECK(p.selectedEndpointIndex().isValid());
        const auto selected = p.selectedEndpointIndex();
        QTest::qWait(150);
        api.setProperty("ready", false);
        CHECK(!p.connected());
        CHECK(query->setProperty("text", QStringLiteral("measure.Current")));
        QTest::qWait(250);
        auto* offlineRow = findRow(window->contentItem(), QStringLiteral("Current"));
        CHECK(offlineRow && !findRow(window->contentItem(), QStringLiteral("Voltage")));
        activate(window, offlineRow);
        CHECK(p.selectedEndpointIndex().isValid() && p.selectedEndpointIndex() != selected);
        const auto offlineSelected = p.selectedEndpointIndex();
        CHECK(!p.connected());
        CHECK(bool(staticChanged));
        staticChanged();
        QTest::qWait(250);
        CHECK(!p.connected() && p.selectedEndpointIndex() == offlineSelected);
        delete window;
        staticChanged = {};
    }
    CHECK(warnings == 0);
    std::printf("QML search native-provider checks=%u warnings=%u\n", checks, warnings);
    return 0;
}
