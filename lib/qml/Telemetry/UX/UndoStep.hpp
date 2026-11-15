// One step of the central Undo (UR-1): what changed, as states put back. MIT.
#ifndef TELEMETRY_UX_UNDO_STEP_HPP
#define TELEMETRY_UX_UNDO_STEP_HPP
#pragma once
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>
#include <QtGui/QUndoCommand>
#include <functional>

namespace telemetry::ux {
// What putting a state back did: all of it (Done), some (Partial), none
// (Skipped - nothing it names is there to change), or nothing yet (Blocked -
// the target cannot change now). note says the rest in words: "device.Old.Gain
// is not on this device; it is back as a missing row."
struct UndoOutcome {
	enum class Kind { Done, Partial, Skipped, Blocked };
	Kind kind = Kind::Done;
	QString note;
};

// Something a step changes: a provider's Watchlist, a file's bytes. It brings
// one kind of its state to what a step says, by stable identity - a path, an
// offset - never by row index. keys are every key the step covers; a key in
// keys and not in state is made absent. Putting a state back is idempotent:
// a key already as asked is left as it is. It is called only by the history,
// and records nothing itself.
class UndoTarget {
public:
	virtual ~UndoTarget() = default;
	virtual UndoOutcome applyUndoState(const QString& kind, const QVariantMap& state, const QStringList& keys) = 0;
	// Why nothing can be put back now, in words ("Stop polling to change the
	// Watchlist."); empty when it can.
	[[nodiscard]] virtual QString undoBlocked() const = 0;
};
} // namespace telemetry::ux

#define TELEMETRY_UX_UNDO_TARGET_IID "telemetry.ux.UndoTarget/1.0"
Q_DECLARE_INTERFACE(telemetry::ux::UndoTarget, TELEMETRY_UX_UNDO_TARGET_IID)

namespace telemetry::ux {
// A target made of two functions, for a class that keeps its Undo private: a
// provider owns one and gives it as the target of the steps it records. Its
// lifetime is the owner's: a step whose target is gone is skipped.
class UndoFunctions final : public QObject, public UndoTarget {
	Q_OBJECT
	Q_INTERFACES(telemetry::ux::UndoTarget)
public:
	using Apply = std::function<UndoOutcome(const QString& kind, const QVariantMap& state, const QStringList& keys)>;
	using Blocked = std::function<QString()>;
	UndoFunctions(Apply apply, Blocked blocked, QObject* parent = nullptr);
	UndoOutcome applyUndoState(const QString& kind, const QVariantMap& state, const QStringList& keys) override;
	[[nodiscard]] QString undoBlocked() const override;

private:
	Apply apply_;
	Blocked blocked_;
};

// One part of a step: a target's kind of state before and after, as key ->
// fragment. A key missing from before (after) was absent before (after).
struct UndoPart {
	QPointer<QObject> target; // implements UndoTarget
	QString kind;
	QVariantMap before, after;
	// Every key the part covers, before's and after's, sorted.
	[[nodiscard]] QStringList keys() const;
};

// A step: its words ("Remove 3 values from the Watchlist"), its parts, its
// serial in the history, the key steps merge by and when it was recorded.
// Undo puts every part's before back, redo its after. The first redo, when
// the history takes the step, does nothing: the change is already made.
class UndoStep final : public QUndoCommand {
public:
	// Steps with the same mergeKey within this long of each other merge into
	// one: the earliest before with the latest after - unless the step that
	// comes asks for another window (0: as long as the key stays the same).
	static constexpr qint64 mergeWindowMs = 2000;

	UndoStep(QString text, QVector<UndoPart> parts, int serial, QString mergeKey, qint64 atMs,
	         qint64 windowMs = mergeWindowMs);
	int id() const override;
	bool mergeWith(const QUndoCommand* other) override;
	void undo() override;
	void redo() override;

	[[nodiscard]] const QVector<UndoPart>& parts() const noexcept { return parts_; }
	[[nodiscard]] int serial() const noexcept { return serial_; }
	[[nodiscard]] QString mergeKey() const { return mergeKey_; }
	[[nodiscard]] qint64 at() const noexcept { return at_; }
	// How long after the step before it this one still merges into it; 0
	// for as long as the key stays the same.
	[[nodiscard]] qint64 window() const noexcept { return window_; }
	// What the last undo or redo did; Done before either.
	[[nodiscard]] UndoOutcome outcome() const { return outcome_; }
	// Why a target of this step cannot change now, the first said; empty when
	// every target can.
	[[nodiscard]] QString blocked() const;
	// Every part's before equals its after: the step changes nothing.
	[[nodiscard]] bool changesNothing() const;
	// Told each outcome as it happens: a skipped step is deleted by the stack
	// before anyone could ask it.
	void setReport(std::function<void(const UndoOutcome&)> report) { report_ = std::move(report); }

private:
	void apply(bool undo);

	QVector<UndoPart> parts_;
	int serial_ = 0;
	QString mergeKey_;
	qint64 at_ = 0;
	qint64 window_ = mergeWindowMs;
	bool taken_ = false;
	UndoOutcome outcome_;
	std::function<void(const UndoOutcome&)> report_;
};
} // namespace telemetry::ux
#endif
