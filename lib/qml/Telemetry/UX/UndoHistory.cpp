// The central Undo (UR-1): one history of steps over a QUndoStack. MIT.
#include "UndoHistory.hpp"
#include <QtCore/QElapsedTimer>
#include <QtCore/QtGlobal>
#include <algorithm>
#include <utility>

namespace telemetry::ux {
namespace {
// The library's own kinds; a host adds its own with declareKind().
QStringList& declared()
{
	static QStringList kinds{QStringLiteral("batch.items"), QStringLiteral("file.bytes"),
	                         QStringLiteral("plot.curves"), QStringLiteral("plot.look"),
	                         QStringLiteral("plot.points"), QStringLiteral("watch.format"),
	                         QStringLiteral("watch.rows")};
	return kinds;
}

qint64 monotonicMs()
{
	static QElapsedTimer clock;
	if (!clock.isValid())
		clock.start();
	return clock.elapsed();
}

QString outcomeName(UndoOutcome::Kind kind)
{
	switch (kind) {
		case UndoOutcome::Kind::Partial: return QStringLiteral("partial");
		case UndoOutcome::Kind::Skipped: return QStringLiteral("skipped");
		case UndoOutcome::Kind::Blocked: return QStringLiteral("blocked");
		case UndoOutcome::Kind::Done: break;
	}
	return QStringLiteral("done");
}
} // namespace

UndoHistory::UndoHistory(QObject* parent) : QObject(parent)
{
	stack_.setUndoLimit(limit_);
	shown_ = current();
}

UndoHistory::~UndoHistory() = default;

const UndoStep* UndoHistory::step(int index) const
{
	return index >= 0 && index < stack_.count() ? dynamic_cast<const UndoStep*>(stack_.command(index)) : nullptr;
}

bool UndoHistory::canUndo() const
{
	const auto* top = step(stack_.index() - 1);
	return !taking_ && top && top->blocked().isEmpty();
}

bool UndoHistory::canRedo() const
{
	const auto* next = step(stack_.index());
	return !taking_ && next && next->blocked().isEmpty();
}

QString UndoHistory::undoText() const
{
	const auto* top = step(stack_.index() - 1);
	return top ? top->text() : QString{};
}

QString UndoHistory::redoText() const
{
	const auto* next = step(stack_.index());
	return next ? next->text() : QString{};
}

int UndoHistory::undoSerial() const
{
	const auto* top = step(stack_.index() - 1);
	return top ? top->serial() : 0;
}

int UndoHistory::redoSerial() const
{
	const auto* next = step(stack_.index());
	return next ? next->serial() : 0;
}

QString UndoHistory::blockedReason() const
{
	const auto* top = step(stack_.index() - 1);
	return top ? top->blocked() : QString{};
}

void UndoHistory::setLimit(int limit)
{
	if (limit < 1 || limit == this->limit())
		return;
	// QUndoStack takes a limit only while it is empty.
	stack_.clear();
	stack_.setUndoLimit(limit);
	limit_ = limit;
	publish();
}

bool UndoHistory::undo()
{
	return take(true);
}

bool UndoHistory::redo()
{
	return take(false);
}

bool UndoHistory::take(bool undo)
{
	if (taking_)
		return false;
	const auto* taken = step(undo ? stack_.index() - 1 : stack_.index());
	if (!taken || !taken->blocked().isEmpty())
		return false;
	const auto serial = taken->serial();
	const auto text = taken->text();
	UndoOutcome outcome;
	auto* reporting = const_cast<UndoStep*>(taken);
	reporting->setReport([&outcome](const UndoOutcome& said) { outcome = said; });
	taking_ = true;
	if (undo)
		stack_.undo();
	else
		stack_.redo();
	taking_ = false;
	// A skipped step is gone already; one kept stops reporting to this frame.
	if (const auto* kept = step(undo ? stack_.index() : stack_.index() - 1); kept && kept->serial() == serial)
		const_cast<UndoStep*>(kept)->setReport({});
	publish();
	Q_EMIT done(serial, text, undo, outcome.note, outcomeName(outcome.kind));
	return true;
}

void UndoHistory::clear()
{
	if (taking_)
		return;
	stack_.clear();
	irreversible_.clear();
	publish();
}

void UndoHistory::noteIrreversible(const QString& words)
{
	irreversible_ = words;
	publish();
}

void UndoHistory::acknowledgeIrreversible()
{
	irreversible_.clear();
	publish();
}

void UndoHistory::setClean()
{
	stack_.setClean();
	publish();
}

QString UndoHistory::sentence(const QString& text, bool undone, const QString& note, const QString& outcome) const
{
	if (outcome == QStringLiteral("skipped"))
		return note.isEmpty() ? tr("Skipped: %1.").arg(text) : tr("Skipped: %1; %2.").arg(text, note);
	const auto said = undone ? tr("Undone: %1").arg(text) : tr("Redone: %1").arg(text);
	return note.isEmpty() ? said : tr("%1. %2.").arg(said, note);
}

int UndoHistory::record(const QString& text, QVector<UndoPart> parts, const QString& mergeKey)
{
	if (taking_)
		return 0;
	const auto& known = declared();
	for (const auto& part : parts) {
		if (!known.contains(part.kind)) {
			qWarning("UndoHistory: the kind %s is not declared", qPrintable(part.kind));
			return 0;
		}
		if (!part.target || !qobject_cast<UndoTarget*>(part.target.data())) {
			qWarning("UndoHistory: a %s part's target is not an UndoTarget", qPrintable(part.kind));
			return 0;
		}
	}
	parts.erase(std::remove_if(parts.begin(), parts.end(),
	                           [](const UndoPart& part) { return part.before == part.after; }),
	            parts.end());
	if (parts.isEmpty())
		return 0;
	const int serial = ++serial_;
	auto* made = new UndoStep(text, std::move(parts), serial, mergeKey, now());
	// The next step taken after an action on the device ends its note.
	irreversible_.clear();
	stack_.push(made);
	// Merged into the step before, it took this serial; merged back to where
	// it started, that step is gone and nothing holds the change.
	const auto* top = step(stack_.index() - 1);
	const int held = top && top->serial() == serial ? serial : 0;
	publish();
	if (held)
		Q_EMIT recorded(held);
	return held;
}

void UndoHistory::refresh()
{
	// While a step is taken its targets notify; the take publishes once after.
	if (!taking_)
		publish();
}

qint64 UndoHistory::now() const
{
	return clock_ ? clock_() : monotonicMs();
}

void UndoHistory::setClock(std::function<qint64()> nowMs)
{
	clock_ = std::move(nowMs);
}

QStringList UndoHistory::kinds()
{
	auto all = declared();
	std::sort(all.begin(), all.end());
	return all;
}

void UndoHistory::declareKind(const QString& kind)
{
	if (!kind.isEmpty() && !declared().contains(kind))
		declared() << kind;
}

UndoHistory::Shown UndoHistory::current() const
{
	Shown now;
	now.canUndo = canUndo();
	now.canRedo = canRedo();
	now.clean = stack_.isClean();
	now.undoText = undoText();
	now.redoText = redoText();
	now.blocked = blockedReason();
	now.irreversible = irreversible_;
	now.undoSerial = undoSerial();
	now.redoSerial = redoSerial();
	now.count = stack_.count();
	now.limit = limit();
	return now;
}

void UndoHistory::publish()
{
	const auto now = current();
	const bool same = now.canUndo == shown_.canUndo && now.canRedo == shown_.canRedo && now.clean == shown_.clean &&
	                  now.undoText == shown_.undoText && now.redoText == shown_.redoText &&
	                  now.blocked == shown_.blocked && now.irreversible == shown_.irreversible &&
	                  now.undoSerial == shown_.undoSerial && now.redoSerial == shown_.redoSerial &&
	                  now.count == shown_.count && now.limit == shown_.limit;
	if (same)
		return;
	shown_ = now;
	Q_EMIT changed();
}
} // namespace telemetry::ux
