// The one handler of Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z in a window (UR-1). MIT.
#include "UndoRouter.hpp"
#include "UndoScope.hpp"
#include <QtCore/QList>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
#include <QtGui/QAccessible>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickWindow>

namespace telemetry::ux {
namespace {
// Every router alive, for the scopes that tell them of a step recorded.
QList<UndoRouter*>& routers()
{
	static QList<UndoRouter*> all;
	return all;
}

// Whether item is in a popup - a menu, a dialog, a palette - whose keys come
// back to where they were when it closes.
bool inPopup(const QQuickItem* item)
{
	for (; item; item = item->parentItem())
		if (item->inherits("QQuickPopupItem"))
			return true;
	return false;
}
} // namespace

UndoRouter::UndoRouter(QQuickItem* parent) : QQuickItem(parent)
{
	routers().append(this);
}

UndoRouter::~UndoRouter()
{
	routers().removeAll(this);
}

void UndoRouter::setWorkspace(UndoHistory* workspace)
{
	if (workspace_ == workspace)
		return;
	QObject::disconnect(workspaceRecorded_);
	const bool followed = !active_ || active_ == workspace_;
	workspace_ = workspace;
	// A step recorded in the workspace makes it the one acted on.
	if (workspace)
		workspaceRecorded_ = connect(workspace, &UndoHistory::recorded, this, [this] { activate(workspace_); });
	Q_EMIT workspaceChanged();
	// The workspace stands in for no history focused; it follows the new one.
	if (followed)
		activate(workspace);
	else
		Q_EMIT changed();
}

UndoHistory* UndoRouter::active() const
{
	return active_ ? active_.data() : workspace_.data();
}

bool UndoRouter::canUndo() const
{
	const auto* history = active();
	return history && history->canUndo();
}

bool UndoRouter::canRedo() const
{
	const auto* history = active();
	return history && history->canRedo();
}

QString UndoRouter::undoText() const
{
	const auto* history = active();
	return history ? shortened(history->undoText()) : QString{};
}

QString UndoRouter::redoText() const
{
	const auto* history = active();
	return history ? shortened(history->redoText()) : QString{};
}

QString UndoRouter::undoReason() const
{
	const auto* history = active();
	if (history && history->canUndo())
		return {};
	const auto why = history && !history->undoText().isEmpty() ? history->blockedReason() : QString{};
	return why.isEmpty() ? tr("Nothing to undo.") : why;
}

QString UndoRouter::redoReason() const
{
	const auto* history = active();
	if (history && history->canRedo())
		return {};
	const auto why = history && !history->redoText().isEmpty() ? history->redoBlockedReason() : QString{};
	return why.isEmpty() ? tr("Nothing to redo.") : why;
}

bool UndoRouter::undo()
{
	return take(active(), true);
}

bool UndoRouter::redo()
{
	return take(active(), false);
}

QString UndoRouter::shortened(const QString& words, int limit)
{
	if (limit < 3 || words.size() <= limit)
		return words;
	auto parts = words.split(QLatin1Char(' '));
	for (auto& word : parts) {
		if (word.size() <= limit)
			continue;
		const auto head = (limit - 1) / 2;
		const auto tail = limit - 1 - head;
		word = word.left(head) + QChar(0x2026) + word.right(tail);
	}
	return parts.join(QLatin1Char(' '));
}

void UndoRouter::scopeRecorded(const QQuickWindow* window, UndoHistory* history)
{
	if (!window || !history)
		return;
	for (auto* router : routers())
		if (router->window_ == window)
			router->activate(history);
}

void UndoRouter::componentComplete()
{
	QQuickItem::componentComplete();
	auto* engine = qmlEngine(this);
	if (!engine)
		return;
	// Qt Quick's own Shortcut: a text field that takes the keys keeps them,
	// and a modal dialog or an open menu blocks them, as for every shortcut
	// of the window.
	QQmlComponent shortcut(engine);
	shortcut.setData(QByteArrayLiteral("import QtQuick\nShortcut { context: Qt.WindowShortcut }\n"), QUrl{});
	const auto make = [&](const QVariantList& keys, const char* slot) {
		auto* made = shortcut.beginCreate(qmlContext(this));
		if (!made) {
			qWarning("UndoRouter: %s", qPrintable(shortcut.errorString()));
			return;
		}
		made->setParent(this);
		made->setProperty("sequences", keys);
		shortcut.completeCreate();
		connect(made, SIGNAL(activated()), this, slot);
	};
	make({QStringLiteral("Ctrl+Z")}, SLOT(keyUndo()));
	make({QStringLiteral("Ctrl+Y"), QStringLiteral("Ctrl+Shift+Z")}, SLOT(keyRedo()));
}

void UndoRouter::itemChange(ItemChange change, const ItemChangeData& value)
{
	if (change == ItemSceneChange)
		watchWindow(value.window);
	QQuickItem::itemChange(change, value);
}

void UndoRouter::keyUndo()
{
	press(true);
}

void UndoRouter::keyRedo()
{
	press(false);
}

void UndoRouter::press(bool undo)
{
	const auto* scope = UndoScope::of(window_ ? window_->activeFocusItem() : nullptr);
	if (scope && scope->inert())
		return;
	if (scope && !scope->refusal().isEmpty()) {
		say(scope->refusal());
		return;
	}
	auto* history = active();
	// After an action on the device the first Ctrl+Z says it is not undone.
	if (undo && history && !history->irreversibleNote().isEmpty()) {
		const auto note = history->irreversibleNote();
		history->acknowledgeIrreversible();
		say(note);
		return;
	}
	take(history, undo);
}

bool UndoRouter::take(UndoHistory* history, bool undo)
{
	if (!history || !(undo ? history->canUndo() : history->canRedo())) {
		say(undo ? undoReason() : redoReason());
		return false;
	}
	QString words;
	const auto heard = connect(history, &UndoHistory::done, this,
	                           [&words, history](int, const QString& text, bool undone, const QString& note,
	                                             const QString& outcome) {
		                           words = history->sentence(text, undone, note, outcome);
	                           });
	const bool taken = undo ? history->undo() : history->redo();
	QObject::disconnect(heard);
	if (taken)
		say(words, history->noticed());
	return taken;
}

void UndoRouter::say(const QString& text, bool noticed)
{
	if (text.isEmpty())
		return;
#if QT_CONFIG(accessibility)
	if (QAccessible::isActive()) {
		QObject* speaker = window_ && window_->activeFocusItem() ? window_->activeFocusItem() : this;
		QAccessibleAnnouncementEvent event(speaker, text);
		QAccessible::updateAccessibility(&event);
	}
#endif
	Q_EMIT announced(text, noticed);
}

void UndoRouter::focusMoved()
{
	const auto* item = window_ ? window_->activeFocusItem() : nullptr;
	// The window lost the keys, or a menu, a dialog or the palette has them:
	// what it acts on is what came before.
	if (!item || inPopup(item))
		return;
	const auto* scope = UndoScope::of(item);
	if (scope && scope->inert())
		return;
	activate(scope && scope->history() ? scope->history() : workspace_.data());
}

void UndoRouter::activate(UndoHistory* history)
{
	if (active_ == history && active_)
		return;
	QObject::disconnect(activeChanged_);
	QObject::disconnect(activeGone_);
	active_ = history;
	if (history) {
		activeChanged_ = connect(history, &UndoHistory::changed, this, &UndoRouter::changed);
		// A file closed: the workspace again.
		activeGone_ = connect(history, &QObject::destroyed, this, [this] { activate(workspace_); });
	}
	Q_EMIT changed();
}

void UndoRouter::watchWindow(QQuickWindow* window)
{
	if (window_ == window)
		return;
	QObject::disconnect(focus_);
	window_ = window;
	if (window)
		focus_ = connect(window, &QQuickWindow::activeFocusItemChanged, this, &UndoRouter::focusMoved);
}
} // namespace telemetry::ux
