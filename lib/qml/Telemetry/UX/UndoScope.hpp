// What Ctrl+Z means in an area of a window (UR-1), attached to its item. MIT.
#ifndef TELEMETRY_UX_UNDO_SCOPE_HPP
#define TELEMETRY_UX_UNDO_SCOPE_HPP
#pragma once
#include "UndoHistory.hpp"
#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQuickItem;
QT_END_NAMESPACE

namespace telemetry::ux {
// Set on an item, it tells the window's UndoRouter what Ctrl+Z does while the
// keys are anywhere in it:
//   UndoScope.history: content.history   - takes that history's step (a
//                                          file's bytes in a FileViewer)
//   UndoScope.refusal: qsTr("…")         - undoes nothing and says these words
//                                          (an area that acts on the device)
//   UndoScope.inert: true                - does nothing at all (a second
//                                          device's tree beside the first)
// The nearest item up from the focus that sets any of them decides - but an
// inert one anywhere up from it wins; with none the router takes the
// workspace's step. A text field inside any of them keeps
// its own typing undo. A scope's history that records a step becomes the one
// the router acts on, as focusing it does.
class UndoScope : public QObject {
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("UndoScope is an attached property: UndoScope.history, UndoScope.refusal, UndoScope.inert.")
	QML_ATTACHED(UndoScope)
	Q_PROPERTY(telemetry::ux::UndoHistory* history READ history WRITE setHistory NOTIFY changed)
	Q_PROPERTY(QString refusal READ refusal WRITE setRefusal NOTIFY changed)
	Q_PROPERTY(bool inert READ inert WRITE setInert NOTIFY changed)
public:
	explicit UndoScope(QObject* parent = nullptr);
	static UndoScope* qmlAttachedProperties(QObject* object);

	[[nodiscard]] UndoHistory* history() const { return history_; }
	void setHistory(UndoHistory* history);
	[[nodiscard]] QString refusal() const { return refusal_; }
	void setRefusal(const QString& refusal);
	[[nodiscard]] bool inert() const noexcept { return inert_; }
	void setInert(bool inert);

	// The scope that decides for item: an inert one up from it (item
	// included), else the nearest that sets anything; null when none does.
	[[nodiscard]] static const UndoScope* of(const QQuickItem* item);

Q_SIGNALS:
	void changed();

private:
	[[nodiscard]] bool decides() const { return history_ || !refusal_.isEmpty() || inert_; }

	QPointer<UndoHistory> history_;
	QString refusal_;
	bool inert_ = false;
	QMetaObject::Connection recorded_;
};
} // namespace telemetry::ux
#endif
