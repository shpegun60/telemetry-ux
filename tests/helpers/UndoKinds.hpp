// Every kind of step the central Undo records (UR-1), held to one property:
// what a step does, undo takes back and redo does again - the same each
// time, and across a disconnect and a changed descriptor. MIT.
//
// A suite gives a case per kind it proves (see coverage()) and calls run().
// A host with kinds of its own (declared with UndoHistory::declareKind) names
// the suites that prove them in run()'s `more`. It fails on:
// - a kind UndoHistory::kinds() names that coverage() gives no suite: a kind
//   added to the library with no case anywhere;
// - a kind coverage() gives this suite with no case here;
// - a case whose step is not one step of its kind, or whose state after undo
//   or redo is not the state before or after the step.
#pragma once
#include <Telemetry/UX/UndoHistory.hpp>
#include <QtCore/QDebug>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <vector>

namespace undo_kinds {
// Which suites prove each kind the library records.
inline const std::map<QString, QStringList>& coverage()
{
	static const std::map<QString, QStringList> table{
	    {QStringLiteral("file.bytes"), {QStringLiteral("device_files")}},
	    {QStringLiteral("watch.format"), {QStringLiteral("qt_connection")}},
	    {QStringLiteral("watch.rows"), {QStringLiteral("qt_connection"), QStringLiteral("static_qml_configurator")}},
	};
	return table;
}

struct Case {
	QString kind;
	// What the case does, for a failure's words.
	QString name;
	telemetry::ux::UndoHistory* history = nullptr;
	// Back to where the case starts, its history empty.
	std::function<void()> reset;
	// Makes exactly one step of `kind`.
	std::function<void()> act;
	// What the step changes, comparable: rows by path, bytes.
	std::function<QVariant()> state;
	// The device goes after the step and comes back after its undo; the undo
	// in between still takes the step back (rows kept by path, bytes in
	// memory), and once the device is back the state is the one before.
	std::function<void()> disconnect, reconnect;
	// The device comes back with another descriptor after the step; the undo
	// then takes back what it can and says the rest: `changed` judges the
	// outcome ("done", "partial", "skipped") and the note.
	std::function<void()> changeDescriptor;
	std::function<bool(const QString& outcome, const QString& note)> changed;
};

struct Report {
	int cases = 0, steps = 0, disconnects = 0, descriptors = 0;
};

namespace detail {
inline void require(bool ok, const Case& c, const char* what)
{
	if (ok)
		return;
	std::fprintf(stderr, "UndoKinds %s (%s): %s\n", qPrintable(c.name), qPrintable(c.kind), what);
	std::abort();
}

// One step of the case's kind is on top of its history.
inline void oneStep(const Case& c, int before)
{
	const auto& stack = c.history->stack();
	require(c.history->undoSerial() > 0 && c.history->undoSerial() != before, c, "the action recorded no step");
	const auto* step = dynamic_cast<const telemetry::ux::UndoStep*>(stack.command(stack.index() - 1));
	require(step && !step->parts().isEmpty(), c, "the step on top is not an UndoStep");
	for (const auto& part : step->parts())
		require(part.kind == c.kind, c, "the step holds a part of another kind");
	require(!c.history->undoText().isEmpty(), c, "the step has no words");
}

// Takes the step on top back (forward) and returns what done() said.
inline std::pair<QString, QString> take(const Case& c, bool undo)
{
	QString outcome, note;
	const auto connection = QObject::connect(c.history, &telemetry::ux::UndoHistory::done, c.history,
	                                         [&](int, const QString&, bool, const QString& said, const QString& kind) {
		                                         outcome = kind;
		                                         note = said;
	                                         });
	const bool taken = undo ? c.history->undo() : c.history->redo();
	QObject::disconnect(connection);
	require(taken, c, undo ? "undo was refused" : "redo was refused");
	return {outcome, note};
}
} // namespace detail

// Runs every case of `suite`, and checks that the cases and coverage() - with
// a host's `more` - between them hold every kind declared.
inline Report run(const QString& suite, const std::vector<Case>& cases,
                  const std::map<QString, QStringList>& more = {})
{
	auto table = coverage();
	table.insert(more.cbegin(), more.cend());
	Report report;
	Case none;
	none.name = suite;
	for (const auto& kind : telemetry::ux::UndoHistory::kinds()) {
		none.kind = kind;
		const auto covered = table.find(kind);
		detail::require(covered != table.end(), none, "a kind no suite proves");
		if (!covered->second.contains(suite))
			continue;
		bool found = false;
		for (const auto& c : cases)
			found = found || c.kind == kind;
		detail::require(found, none, "a kind this suite must prove has no case");
	}
	for (const auto& c : cases) {
		const auto covered = table.find(c.kind);
		detail::require(covered != table.end() && covered->second.contains(suite), c,
		                "a case of a kind this suite is not given");
		detail::require(c.history && c.reset && c.act && c.state, c, "an incomplete case");
		// Do, undo, redo, undo, redo: the states before and after, each time.
		c.reset();
		const auto before = c.state();
		int serial = c.history->undoSerial();
		c.act();
		detail::oneStep(c, serial);
		const auto after = c.state();
		detail::require(after != before, c, "the step changed nothing");
		for (int round = 0; round < 2; ++round) {
			detail::require(detail::take(c, true).first == QStringLiteral("done"), c, "undo was not done whole");
			if (c.state() != before)
				qWarning().noquote() << "UndoKinds" << c.name << "after undo:" << c.state() << "expected" << before;
			detail::require(c.state() == before, c, "undo did not give the state before");
			detail::require(detail::take(c, false).first == QStringLiteral("done"), c, "redo was not done whole");
			if (c.state() != after)
				qWarning().noquote() << "UndoKinds" << c.name << "after redo:" << c.state() << "expected" << after;
			detail::require(c.state() == after, c, "redo did not give the state after");
		}
		++report.steps;
		// Across a disconnect: undone with no device, the state before once
		// the device is back; redone then, the state after.
		if (c.disconnect) {
			detail::require(bool(c.reconnect), c, "a disconnect with no reconnect");
			c.reset();
			const auto start = c.state();
			serial = c.history->undoSerial();
			c.act();
			detail::oneStep(c, serial);
			const auto made = c.state();
			c.disconnect();
			detail::require(detail::take(c, true).first == QStringLiteral("done"), c,
			                "undo with no device was not done whole");
			c.reconnect();
			if (c.state() != start)
				qWarning().noquote() << "UndoKinds" << c.name << "after reconnect:" << c.state() << "expected" << start;
			detail::require(c.state() == start, c, "undone with no device, not the state before once back");
			detail::require(detail::take(c, false).first == QStringLiteral("done"), c, "redo after a reconnect");
			detail::require(c.state() == made, c, "redo after a reconnect did not give the state after");
			++report.disconnects;
		}
		// Across a changed descriptor: the undo says what it could not do.
		if (c.changeDescriptor) {
			detail::require(bool(c.changed), c, "a changed descriptor with no judgement");
			c.reset();
			serial = c.history->undoSerial();
			c.act();
			detail::oneStep(c, serial);
			c.changeDescriptor();
			const auto [outcome, note] = detail::take(c, true);
			if (!c.changed(outcome, note))
				qWarning().noquote() << "UndoKinds" << c.name << "after a changed descriptor:" << outcome << note;
			detail::require(c.changed(outcome, note), c, "the undo after a changed descriptor said the wrong thing");
			++report.descriptors;
		}
		++report.cases;
	}
	return report;
}
} // namespace undo_kinds
