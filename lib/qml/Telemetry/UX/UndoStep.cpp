// One step of the central Undo (UR-1): what changed, as states put back. MIT.
#include "UndoStep.hpp"
#include <QtCore/QSet>
#include <algorithm>
#include <utility>

namespace telemetry::ux {
UndoFunctions::UndoFunctions(Apply apply, Blocked blocked, QObject* parent)
    : QObject(parent), apply_(std::move(apply)), blocked_(std::move(blocked))
{}

UndoOutcome UndoFunctions::applyUndoState(const QString& kind, const QVariantMap& state, const QStringList& keys)
{
	return apply_ ? apply_(kind, state, keys) : UndoOutcome{UndoOutcome::Kind::Skipped, {}};
}

QString UndoFunctions::undoBlocked() const
{
	return blocked_ ? blocked_() : QString{};
}

QStringList UndoPart::keys() const
{
	QStringList all = before.keys();
	for (auto at = after.cbegin(); at != after.cend(); ++at)
		if (!before.contains(at.key()))
			all << at.key();
	std::sort(all.begin(), all.end());
	return all;
}

UndoStep::UndoStep(QString text, QVector<UndoPart> parts, int serial, QString mergeKey, qint64 atMs,
                   qint64 windowMs)
    : parts_(std::move(parts)), serial_(serial), mergeKey_(std::move(mergeKey)), at_(atMs),
      window_(windowMs < 0 ? mergeWindowMs : windowMs)
{
	setText(text);
}

int UndoStep::id() const
{
	// One id for every step that may merge; mergeWith compares the keys.
	return mergeKey_.isEmpty() ? -1 : 1;
}

bool UndoStep::mergeWith(const QUndoCommand* command)
{
	const auto* next = dynamic_cast<const UndoStep*>(command);
	// The step that comes says how long it still joins the one before: a
	// window, or 0 for as long as its key is the same (the caller changes the
	// key to end it, as an editor does when its cell is left).
	if (!next || mergeKey_.isEmpty() || next->mergeKey_ != mergeKey_ || next->at_ < at_ ||
	    (next->window_ > 0 && next->at_ - at_ > next->window_))
		return false;
	for (const auto& later : next->parts_) {
		const auto found = std::find_if(parts_.begin(), parts_.end(), [&later](const UndoPart& part) {
			return part.target == later.target && part.kind == later.kind;
		});
		if (found == parts_.end()) {
			parts_.push_back(later);
			continue;
		}
		auto& part = *found;
		const auto mine = part.keys();
		const QSet<QString> covered(mine.cbegin(), mine.cend());
		// The earliest before: ours where we cover the key, theirs where only
		// they do. The latest after: theirs where they cover it, ours elsewhere.
		for (const auto& key : later.keys()) {
			if (!covered.contains(key)) {
				if (const auto was = later.before.constFind(key); was != later.before.cend())
					part.before.insert(key, was.value());
			}
			if (const auto is = later.after.constFind(key); is != later.after.cend())
				part.after.insert(key, is.value());
			else
				part.after.remove(key);
		}
	}
	setText(next->text());
	serial_ = next->serial_;
	at_ = next->at_;
	// Back where it started: nothing to undo; the history drops the step.
	if (changesNothing())
		setObsolete(true);
	return true;
}

bool UndoStep::changesNothing() const
{
	return std::all_of(parts_.cbegin(), parts_.cend(),
	                   [](const UndoPart& part) { return part.before == part.after; });
}

QString UndoStep::blocked() const
{
	for (const auto& part : parts_) {
		const QObject* object = part.target.data();
		const auto* target = object ? qobject_cast<const UndoTarget*>(object) : nullptr;
		if (!target)
			continue;
		if (auto reason = target->undoBlocked(); !reason.isEmpty())
			return reason;
	}
	return {};
}

void UndoStep::undo()
{
	apply(true);
}

void UndoStep::redo()
{
	// The history takes a step once its change is made: nothing to do then.
	if (!std::exchange(taken_, true))
		return;
	apply(false);
}

void UndoStep::apply(bool undo)
{
	int done = 0;
	bool partial = false;
	QStringList notes;
	// Undo puts the parts back last first, as a stack would; redo in order.
	for (qsizetype i = 0; i < parts_.size(); ++i) {
		const auto& part = parts_[undo ? parts_.size() - 1 - i : i];
		auto* target = part.target ? qobject_cast<UndoTarget*>(part.target.data()) : nullptr;
		if (!target) {
			partial = true;
			continue;
		}
		const auto outcome = target->applyUndoState(part.kind, undo ? part.before : part.after, part.keys());
		if (!outcome.note.isEmpty() && !notes.contains(outcome.note))
			notes << outcome.note;
		switch (outcome.kind) {
			case UndoOutcome::Kind::Done:
				++done;
				break;
			case UndoOutcome::Kind::Partial:
				++done;
				partial = true;
				break;
			// The history asks undoBlocked() first, so a target that refuses
			// here changed in between; it is said like a part that is gone.
			case UndoOutcome::Kind::Blocked:
			case UndoOutcome::Kind::Skipped:
				partial = true;
				break;
		}
	}
	outcome_.note = notes.join(QStringLiteral("; "));
	if (done == 0) {
		// Nothing it names is there any more: the step goes from the history.
		outcome_.kind = UndoOutcome::Kind::Skipped;
		setObsolete(true);
	} else {
		outcome_.kind = partial ? UndoOutcome::Kind::Partial : UndoOutcome::Kind::Done;
	}
	if (report_)
		report_(outcome_);
}
} // namespace telemetry::ux
