// What Ctrl+Z means in an area of a window (UR-1), attached to its item. MIT.
#include "UndoScope.hpp"
#include "UndoRouter.hpp"
#include <QtQml/qqml.h>
#include <QtQuick/QQuickItem>

namespace telemetry::ux {
UndoScope::UndoScope(QObject* parent) : QObject(parent) {}

UndoScope* UndoScope::qmlAttachedProperties(QObject* object)
{
	return new UndoScope(object);
}

void UndoScope::setHistory(UndoHistory* history)
{
	if (history_ == history)
		return;
	QObject::disconnect(recorded_);
	history_ = history;
	// A step recorded here makes this history the one the window's router
	// acts on, as the keys coming here do.
	if (history)
		recorded_ = connect(history, &UndoHistory::recorded, this, [this] {
			if (const auto* item = qobject_cast<const QQuickItem*>(parent()); item && history_)
				UndoRouter::scopeRecorded(item->window(), history_);
		});
	Q_EMIT changed();
}

void UndoScope::setRefusal(const QString& refusal)
{
	if (refusal_ == refusal)
		return;
	refusal_ = refusal;
	Q_EMIT changed();
}

void UndoScope::setInert(bool inert)
{
	if (inert_ == inert)
		return;
	inert_ = inert;
	Q_EMIT changed();
}

const UndoScope* UndoScope::of(const QQuickItem* item)
{
	const UndoScope* nearest = nullptr;
	for (; item; item = item->parentItem()) {
		// Read without making one: most items have no scope.
		const auto* scope = qobject_cast<const UndoScope*>(qmlAttachedPropertiesObject<UndoScope>(item, false));
		if (!scope || !scope->decides())
			continue;
		// An inert area stays inert whatever it holds: Details in a second
		// device's dock do nothing either.
		if (scope->inert())
			return scope;
		if (!nearest)
			nearest = scope;
	}
	return nearest;
}
} // namespace telemetry::ux
