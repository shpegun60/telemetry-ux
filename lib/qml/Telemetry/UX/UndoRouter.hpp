// The one handler of Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z in a window (UR-1). MIT.
#ifndef TELEMETRY_UX_UNDO_ROUTER_HPP
#define TELEMETRY_UX_UNDO_ROUTER_HPP
#pragma once
#include "UndoHistory.hpp"
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickItem>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace telemetry::ux {
// One per window, anywhere in it: `UndoRouter { workspace: history }`. It is
// the only thing that answers Ctrl+Z (undo), Ctrl+Y and Ctrl+Shift+Z (redo);
// panels take none of them. A window that the router's window owns (its
// transient parent: a dock floated out of it) counts as part of it: the keys
// there are the router's as well.
//
// What a press does depends on where the keys are:
//   - a text field takes back its own typing (Qt's), never falling through;
//   - an area whose UndoScope sets `inert`, and a modal dialog or an open
//     menu, do nothing;
//   - an area whose UndoScope sets `refusal` says those words and undoes
//     nothing (Details: a write to the device is not taken back);
//   - anywhere else it takes a step of `active`: the history last focused (a
//     scope's own, or the workspace's) or last recorded into, whichever came
//     later.
// After an action on the device the active history's irreversibleNote is
// said by the first Ctrl+Z, which undoes nothing; the next press goes on.
//
// undo() and redo() are what the Edit menu, a toolbar (UndoButtons) and a
// command palette call: they act on `active` - the context from before a menu
// or the palette took the keys - and name its step, so they act from Details
// too. Every press and every call says what it did once, in announced(text,
// noticed): to a screen reader by the router itself, and to the host, whose
// status line shows the words unless a panel's UndoNotice shows them already
// (noticed).
//
// canUndo, undoText and the rest follow `active`: a blocked step keeps its
// words with canUndo false and undoReason saying why. undoText and redoText
// cut a word longer than 50 characters - a long path - in the middle, for a
// menu or a tooltip; the history keeps the whole words.
class UndoRouter : public QQuickItem {
	Q_OBJECT
	QML_ELEMENT
	Q_PROPERTY(telemetry::ux::UndoHistory* workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged)
	Q_PROPERTY(telemetry::ux::UndoHistory* active READ active NOTIFY changed)
	Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
	Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
	Q_PROPERTY(QString undoText READ undoText NOTIFY changed)
	Q_PROPERTY(QString redoText READ redoText NOTIFY changed)
	// Why Undo (Redo) does nothing now, in words: "Nothing to undo.", or why
	// the step waits ("Stop polling to change the Watchlist."); empty when it
	// can act.
	Q_PROPERTY(QString undoReason READ undoReason NOTIFY changed)
	Q_PROPERTY(QString redoReason READ redoReason NOTIFY changed)
public:
	explicit UndoRouter(QQuickItem* parent = nullptr);
	~UndoRouter() override;

	[[nodiscard]] UndoHistory* workspace() const { return workspace_; }
	void setWorkspace(UndoHistory* workspace);
	[[nodiscard]] UndoHistory* active() const;
	[[nodiscard]] bool canUndo() const;
	[[nodiscard]] bool canRedo() const;
	[[nodiscard]] QString undoText() const;
	[[nodiscard]] QString redoText() const;
	[[nodiscard]] QString undoReason() const;
	[[nodiscard]] QString redoReason() const;

	// The Edit menu's, a toolbar's and a palette's Undo and Redo: the active
	// history's step, said once. False when nothing was taken.
	Q_INVOKABLE bool undo();
	Q_INVOKABLE bool redo();

	// words with every word longer than limit characters cut in the middle.
	[[nodiscard]] static QString shortened(const QString& words, int limit = 50);
	// A history that a scope in window gives recorded a step: the routers of
	// that window act on it next. Called by UndoScope.
	static void scopeRecorded(const QQuickWindow* window, UndoHistory* history);

Q_SIGNALS:
	void workspaceChanged();
	void changed();
	// What a press or a call did, in words, once; noticed when a panel's
	// notice shows it already.
	void announced(const QString& text, bool noticed);

protected:
	void componentComplete() override;
	void itemChange(ItemChange change, const ItemChangeData& value) override;

private Q_SLOTS:
	void keyUndo();
	void keyRedo();

private:
	void press(bool undo);
	bool take(UndoHistory* history, bool undo);
	void say(const QString& text, bool noticed = false);
	void focusMoved();
	void activate(UndoHistory* history);
	void watchWindow(QQuickWindow* window);
	[[nodiscard]] QQuickItem* keysItem() const;

	QPointer<UndoHistory> workspace_;
	QPointer<UndoHistory> active_;
	QPointer<QQuickWindow> window_;
	QMetaObject::Connection focus_, appFocus_, activeChanged_, activeGone_, workspaceRecorded_;
};
} // namespace telemetry::ux
#endif
