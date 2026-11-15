// A fixture's steps in a real UndoHistory (UR-1): what a stand-in provider
// does, recorded with the JavaScript that takes it back and does it again, so
// the window's UndoRouter, the panels' UndoNotice and the history are the
// library's own. MIT.
#pragma once
#include <Telemetry/UX/UndoHistory.hpp>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtQml/QJSValue>
#include <functional>

class ScriptSteps final : public QObject, public telemetry::ux::UndoTarget {
	Q_OBJECT
	Q_INTERFACES(telemetry::ux::UndoTarget)
	// Why a step cannot be taken now; empty when it can.
	Q_PROPERTY(QString blocked READ undoBlocked WRITE setBlocked NOTIFY blockedChanged)
public:
	ScriptSteps() { telemetry::ux::UndoHistory::declareKind(kind()); }
	static QString kind() { return QStringLiteral("test.script"); }

	// Records a step whose change is made: undo takes it back, redo (when
	// given) does it again. The serial the history answers.
	Q_INVOKABLE int record(telemetry::ux::UndoHistory* history, const QString& text, const QJSValue& undo,
	                       const QJSValue& redo = QJSValue(), const QString& mergeKey = QString())
	{
		if (!history)
			return 0;
		const auto key = QString::number(++next_);
		calls_.insert(key, Calls{undo, redo, {}, {}});
		return history->record(text, {telemetry::ux::UndoPart{this, kind(), {{key, QStringLiteral("before")}},
		                                                      {{key, QStringLiteral("after")}}}},
		                       mergeKey);
	}
	// The same from C++.
	int record(telemetry::ux::UndoHistory* history, const QString& text, std::function<void()> undo,
	           std::function<void()> redo = {}, const QString& mergeKey = QString())
	{
		if (!history)
			return 0;
		const auto key = QString::number(++next_);
		calls_.insert(key, Calls{{}, {}, std::move(undo), std::move(redo)});
		return history->record(text, {telemetry::ux::UndoPart{this, kind(), {{key, QStringLiteral("before")}},
		                                                      {{key, QStringLiteral("after")}}}},
		                       mergeKey);
	}
	telemetry::ux::UndoOutcome applyUndoState(const QString&, const QVariantMap& state, const QStringList& keys) override
	{
		for (const auto& key : keys) {
			const auto found = calls_.constFind(key);
			if (found == calls_.cend())
				continue;
			const bool undo = state.value(key).toString() == QLatin1String("before");
			auto call = undo ? found->undo : found->redo;
			if (call.isCallable())
				call.call();
			const auto& native = undo ? found->undoFn : found->redoFn;
			if (native)
				native();
		}
		return {};
	}
	QString undoBlocked() const override { return blocked_; }
	void setBlocked(const QString& blocked)
	{
		if (blocked_ == blocked)
			return;
		blocked_ = blocked;
		Q_EMIT blockedChanged();
	}

Q_SIGNALS:
	void blockedChanged();

private:
	struct Calls {
		QJSValue undo, redo;
		std::function<void()> undoFn, redoFn;
	};
	QHash<QString, Calls> calls_;
	int next_ = 0;
	QString blocked_;
};
