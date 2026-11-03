/*
 * A user-owned presentation provider, without Telemetry SDK or I/O.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QTimer>

#include <cstdio>
#include <memory>
#include <utility>

class OwnerProvider final : public QObject {

	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel* endpoints READ endpoints CONSTANT)
	Q_PROPERTY(QAbstractItemModel* values READ values CONSTANT)
	Q_PROPERTY(QAbstractItemModel* responses READ responses CONSTANT)
	Q_PROPERTY(QAbstractListModel* watchlist READ watchlist CONSTANT)
	Q_PROPERTY(QString selection READ selection NOTIFY stateChanged)
	Q_PROPERTY(QString serviceResponseEndpoint READ serviceResponseEndpoint NOTIFY stateChanged)
	Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
	Q_PROPERTY(bool editing READ editing NOTIFY stateChanged)
	Q_PROPERTY(bool canEdit READ canEdit NOTIFY stateChanged)
	Q_PROPERTY(bool canApply READ canApply NOTIFY stateChanged)
	Q_PROPERTY(bool canRefresh READ canRefresh NOTIFY stateChanged)
	Q_PROPERTY(bool commandSelected READ commandSelected NOTIFY stateChanged)
	Q_PROPERTY(bool canExecute READ canExecute NOTIFY stateChanged)
	Q_PROPERTY(bool serviceSelected READ serviceSelected NOTIFY stateChanged)
	Q_PROPERTY(bool canCallService READ canCallService NOTIFY stateChanged)
	Q_PROPERTY(bool watchActive READ watchActive NOTIFY stateChanged)
	Q_PROPERTY(bool watchPending READ watchPending NOTIFY stateChanged)
	Q_PROPERTY(bool canAddWatch READ canAddWatch NOTIFY stateChanged)
	Q_PROPERTY(int watchCount READ watchCount NOTIFY stateChanged)
	Q_PROPERTY(QString watchStatus READ watchStatus NOTIFY stateChanged)
	Q_PROPERTY(QString writeOutcome READ writeOutcome NOTIFY stateChanged)
	Q_PROPERTY(QString commandOutcome READ commandOutcome NOTIFY stateChanged)
	Q_PROPERTY(QString serviceOutcome READ serviceOutcome NOTIFY stateChanged)
	Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
	Q_PROPERTY(
	    bool watchIndividual READ watchIndividual WRITE setWatchIndividual NOTIFY stateChanged)
	Q_PROPERTY(
	    int watchIntervalMs READ watchIntervalMs WRITE setWatchIntervalMs NOTIFY stateChanged)

public:
	QAbstractItemModel* endpoints() noexcept
	{
		return &tree_;
	}

	QAbstractItemModel* values() noexcept
	{
		return &tree_;
	}

	QAbstractItemModel* responses() noexcept
	{
		return &tree_;
	}

	QAbstractListModel* watchlist() noexcept
	{
		return &list_;
	}

	QString selection() const
	{
		return QStringLiteral("User-owned provider");
	}

	QString serviceResponseEndpoint() const
	{
		return {};
	}

	bool connected() const noexcept
	{
		return false;
	}

	bool busy() const noexcept
	{
		return false;
	}

	bool editing() const noexcept
	{
		return false;
	}

	bool canEdit() const noexcept
	{
		return false;
	}

	bool canApply() const noexcept
	{
		return false;
	}

	bool canRefresh() const noexcept
	{
		return false;
	}

	bool commandSelected() const noexcept
	{
		return category_ == 2;
	}

	bool canExecute() const noexcept
	{
		return false;
	}

	bool serviceSelected() const noexcept
	{
		return category_ == 3;
	}

	bool canCallService() const noexcept
	{
		return false;
	}

	bool watchActive() const noexcept
	{
		return false;
	}

	bool watchPending() const noexcept
	{
		return false;
	}

	bool canAddWatch() const noexcept
	{
		return false;
	}

	int watchCount() const noexcept
	{
		return 0;
	}

	QString watchStatus() const
	{
		return QStringLiteral("Not polling");
	}

	QString writeOutcome() const
	{
		return {};
	}

	QString commandOutcome() const
	{
		return {};
	}

	QString serviceOutcome() const
	{
		return {};
	}

	QString statusText() const
	{
		return status_;
	}

	bool watchIndividual() const noexcept
	{
		return false;
	}

	void setWatchIndividual(bool)
	{}

	int watchIntervalMs() const noexcept
	{
		return 1000;
	}

	void setWatchIntervalMs(int)
	{}

	void publishStatus(QString text)
	{
		status_ = std::move(text);
		Q_EMIT stateChanged();
	}

	void selectCategory(int category)
	{
		category_ = category;
		Q_EMIT stateChanged();
	}

	Q_INVOKABLE bool selectEndpoint(const QModelIndex&)
	{
		return false;
	}

	Q_INVOKABLE bool refresh()
	{
		return false;
	}

	Q_INVOKABLE bool beginEdit()
	{
		return false;
	}

	Q_INVOKABLE void cancelEdit()
	{}

	Q_INVOKABLE bool apply()
	{
		return false;
	}

	Q_INVOKABLE bool executeCommand()
	{
		return false;
	}

	Q_INVOKABLE bool callService()
	{
		return false;
	}

	Q_INVOKABLE bool setScalarText(const QModelIndex&, const QString&)
	{
		return false;
	}

	Q_INVOKABLE bool addWatch()
	{
		return false;
	}

	Q_INVOKABLE bool removeWatch(int)
	{
		return false;
	}

	Q_INVOKABLE void clearWatch()
	{}

	Q_INVOKABLE bool refreshWatch()
	{
		return false;
	}

	Q_INVOKABLE bool startWatch()
	{
		return false;
	}

	Q_INVOKABLE void stopWatch()
	{}

Q_SIGNALS:
	void stateChanged();
	void readFinished(bool);

private:
	int category_ = 1;
	QStandardItemModel tree_{0, 1, this};
	QStringListModel list_{this};
	QString status_ = QStringLiteral("Owner initial status");
};

namespace {
void drainViewEvents()
{
	QEventLoop events;
	QTimer::singleShot(50, &events, &QEventLoop::quit);
	events.exec();
}

bool containsText(QObject& object, const QString& text)
{
	if (object.property("text").toString() == text)
		return true;
	for (auto* child : object.children())
		if (containsText(*child, text))
			return true;
	return false;
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	if (argc != 2)
		return 1;
	OwnerProvider owner;
	bool warnings = false;
	QQmlApplicationEngine engine;
	QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError>&) {
		warnings = true;
	});
	engine.setInitialProperties({{"deviceProvider", QVariant::fromValue(&owner)}});
	engine.load(QUrl::fromLocalFile(QString::fromUtf8(argv[1])));
	unsigned checks = 0;
	const auto check = [&](bool condition) {
		++checks;
		if (!condition)
			std::fprintf(stderr, "Configurator package failed check=%u\n", checks);
		return condition;
	};
	if (!check(engine.rootObjects().size() == 1))
		return 2;
	drainViewEvents();
	auto* view = engine.rootObjects().first()->findChild<QObject*>("ownedProviderView");
	if (!check(view != nullptr) || !check(view->property("provider").value<QObject*>() == &owner) ||
	    !check(containsText(*view, owner.statusText())))
		return 3;
	owner.publishStatus(QStringLiteral("Owner changed status"));
	drainViewEvents();
	if (!check(containsText(*view, owner.statusText())) || !check(!warnings))
		return 4;
	// Three installed components observe one owner. Mounting, changing selection
	// and unmounting are presentation-only; the provider never supplies I/O.
	QQmlComponent panels(&engine);
	panels.setData(R"(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX
ApplicationWindow {
    id: host
    required property var deviceProvider
    visible: true; width: 1200; height: 650
    RowLayout {
        anchors.fill: parent
        FieldEditor { objectName: "field"; provider: host.deviceProvider; Layout.fillWidth: true; Layout.fillHeight: true
            headerComponent: Component { Label { text: "Owner header" } }
            footerComponent: Component { Label { text: "Owner footer" } }
        }
        CommandPanel { objectName: "command"; provider: host.deviceProvider; Layout.fillWidth: true; Layout.fillHeight: true }
        ServicePanel { objectName: "service"; provider: host.deviceProvider; Layout.fillWidth: true; Layout.fillHeight: true }
    }
})", QUrl(QStringLiteral("qrc:/OwnedPanels.qml")));
	if (!check(panels.isReady())) {
		std::fprintf(stderr, "%s\n", qPrintable(panels.errorString()));
		return 5;
	}
	std::unique_ptr<QObject> independent(panels.createWithInitialProperties(
	    {{QStringLiteral("deviceProvider"), QVariant::fromValue(&owner)}}));
	if (!check(bool(independent)))
		return 6;
	drainViewEvents();
	for (int category = 1; category <= 3; ++category) {
		owner.selectCategory(category);
		drainViewEvents();
		int index = 1;
		for (const char* name : {"field", "command", "service"}) {
			auto* panel = independent->findChild<QObject*>(QString::fromLatin1(name));
			if (!check(panel != nullptr) ||
			    !check(panel->property("provider").value<QObject*>() == &owner) ||
			    !check(panel->property("selectionMatches").toBool() == (index++ == category)))
				return 7;
		}
	}
	owner.selectCategory(1);
	drainViewEvents();
	if (!check(containsText(*independent, QStringLiteral("Owner header"))) ||
	    !check(containsText(*independent, QStringLiteral("Owner footer"))))
		return 8;
	independent.reset();
	drainViewEvents();
	if (!check(!warnings))
		return 9;
	std::printf(
	    "Configurator package checks=%u provider=owned sdk=0 scanner=0 plugin=0 warnings=0\n",
	    checks);
}

#include "Consumer.moc"
