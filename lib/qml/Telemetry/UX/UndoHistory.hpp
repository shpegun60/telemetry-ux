// The central Undo (UR-1): one history of steps over a QUndoStack. MIT.
#ifndef TELEMETRY_UX_UNDO_HISTORY_HPP
#define TELEMETRY_UX_UNDO_HISTORY_HPP
#pragma once
#include "UndoStep.hpp"
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QUndoStack>
#include <QtQml/qqmlregistration.h>
#include <functional>

namespace telemetry::ux {
// A history of the steps a host's user can take back: the workspace's (the
// Watchlist's rows and formats, and later the Plot's curves and the Batch),
// or one file's bytes. A host makes one - `UndoHistory {}` in QML - and gives
// it to what records into it: a provider's `history`. Without one nothing is
// recorded and there is nothing to undo. A FileContent owns its own.
//
// A step is recorded once its change is made, so taking it does nothing; undo
// puts every part's state before back, redo its state after, by stable
// identity through each target (UndoTarget). A step of the same mergeKey within
// two seconds merges into the one before it; back where it started, it goes.
// One whose target cannot change now is blocked: its words stay, greyed
// (canUndo false, blockedReason says why). One that finds nothing left to
// change is skipped and goes; one that changes some of it is partial. Never
// persisted; at most `limit` steps, the oldest dropped first.
//
// Actions that act on the device are never steps and clear nothing:
// noteIrreversible(words) keeps their words ("The write to device.Gain is not
// undone; write 1.5 to go back.") until the next step or
// acknowledgeIrreversible(), for whatever answers Ctrl+Z to say first.
class UndoHistory : public QObject {
	Q_OBJECT
	QML_ELEMENT
	Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
	Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
	// The step's words, kept while it is blocked; empty when there is none.
	Q_PROPERTY(QString undoText READ undoText NOTIFY changed)
	Q_PROPERTY(QString redoText READ redoText NOTIFY changed)
	// The step undo (redo) would take, by serial: a notice that said a step
	// offers its Undo while undoSerial is that step's. 0 when there is none.
	Q_PROPERTY(int undoSerial READ undoSerial NOTIFY changed)
	Q_PROPERTY(int redoSerial READ redoSerial NOTIFY changed)
	// Why the step undo would take cannot be taken now, in words; empty when
	// it can or there is none.
	Q_PROPERTY(QString blockedReason READ blockedReason NOTIFY changed)
	Q_PROPERTY(QString irreversibleNote READ irreversibleNote NOTIFY changed)
	// Steps kept, 100 unless set (a file's bytes keep 1000). Setting it drops
	// every step.
	Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY changed)
	// No step taken or undone since setClean(): a file as saved.
	Q_PROPERTY(bool clean READ clean NOTIFY changed)
	// Steps kept, undone ones included.
	Q_PROPERTY(int count READ count NOTIFY changed)
public:
	explicit UndoHistory(QObject* parent = nullptr);
	~UndoHistory() override;

	[[nodiscard]] bool canUndo() const;
	[[nodiscard]] bool canRedo() const;
	[[nodiscard]] QString undoText() const;
	[[nodiscard]] QString redoText() const;
	[[nodiscard]] int undoSerial() const;
	[[nodiscard]] int redoSerial() const;
	[[nodiscard]] QString blockedReason() const;
	[[nodiscard]] QString irreversibleNote() const { return irreversible_; }
	[[nodiscard]] int limit() const { return stack_.undoLimit() > 0 ? stack_.undoLimit() : limit_; }
	void setLimit(int limit);
	[[nodiscard]] bool clean() const { return stack_.isClean(); }
	[[nodiscard]] int count() const { return stack_.count(); }

	// Takes the step back (forward); false, and nothing changed, when there is
	// none or it is blocked. Emits done().
	Q_INVOKABLE bool undo();
	Q_INVOKABLE bool redo();
	// Every step goes: a project closed or another opened, a file read again.
	Q_INVOKABLE void clear();
	Q_INVOKABLE void noteIrreversible(const QString& words);
	Q_INVOKABLE void acknowledgeIrreversible();
	Q_INVOKABLE void setClean();
	// The words a host says after an undo or a redo, from done()'s arguments:
	// "Undone: Remove 3 values from the Watchlist", with the note after a
	// partial one; "Skipped: Add device.Old to the Watchlist; the device no
	// longer has it." when nothing could be put back.
	Q_INVOKABLE QString sentence(const QString& text, bool undone, const QString& note,
	                             const QString& outcome) const;

	// Records a step whose change is already made: its words, its parts and
	// the key it merges by (empty: never). The serial of the step that now
	// holds the change - this one, or the one it merged into - or 0 when
	// nothing is kept: no part changes anything, a merge came back to where it
	// started, a part's kind is not one of kinds() or its target is not an
	// UndoTarget, or an undo or redo is under way. A new step ends the
	// irreversible note.
	int record(const QString& text, QVector<UndoPart> parts, const QString& mergeKey = {});
	// A target's state changed: whether the steps can be taken now may have.
	void refresh();
	// The stack itself, to read, for C++.
	[[nodiscard]] const QUndoStack& stack() const noexcept { return stack_; }
	// The clock merging is measured by, in milliseconds, and setting it, for
	// tests: a recorder that names a merged step by what it holds asks when.
	[[nodiscard]] qint64 now() const;
	void setClock(std::function<qint64()> nowMs);

	// Every kind a step may hold: the library's ("watch.rows",
	// "watch.format", "file.bytes"), then those a host declares for its own
	// targets. A check of every kind finds them here.
	[[nodiscard]] static QStringList kinds();
	static void declareKind(const QString& kind);

Q_SIGNALS:
	void changed();
	// A step was recorded, or merged into the one before, now under serial.
	void recorded(int serial);
	// undo() or redo() took the step with serial; outcome is "done",
	// "partial" or "skipped", note the target's words for what it could not.
	void done(int serial, const QString& text, bool undone, const QString& note, const QString& outcome);

private:
	const UndoStep* step(int index) const;
	bool take(bool undo);
	void publish();

	QUndoStack stack_;
	int limit_ = 100;
	int serial_ = 0;
	bool taking_ = false;
	QString irreversible_;
	std::function<qint64()> clock_;
	// What was last published, so changed() is said only when it moved.
	struct Shown {
		bool canUndo = false, canRedo = false, clean = true;
		QString undoText, redoText, blocked, irreversible;
		int undoSerial = 0, redoSerial = 0, count = 0, limit = 0;
	} shown_;
	Shown current() const;
};
} // namespace telemetry::ux
#endif
