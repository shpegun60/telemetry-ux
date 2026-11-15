// The Watchlist's readings kept in memory for the live Plot (LP-1). MIT.
#ifndef TELEMETRY_UX_WATCH_TRACES_HPP
#define TELEMETRY_UX_WATCH_TRACES_HPP
#pragma once
#include "WatchFrame.hpp"
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtQml/qqmlregistration.h>
#include <deque>
#include <optional>

namespace telemetry::ux {

// A value's reading as a number: an integer while a double holds it exactly,
// a finite float, a bool as 0 or 1; nothing for anything else. One rule for
// the live Plot and the recorded history.
[[nodiscard]] std::optional<double> plotValue(const WatchValueBits& value) noexcept;

// One reading of a value: when this PC received it (the steady clock the
// frames carry), and its number; NaN marks a read that brought none - a
// failure, a drained request, a value no number holds - and breaks the line.
struct TraceReading {
	qint64 ns = 0;
	double value = 0;
};

// Every Watchlist value's recent readings, kept by its path whether it is
// plotted or not: the last ten minutes, at most 65,536 readings each. A value
// removed from the Watchlist keeps its readings; watched again, it goes on in
// the same ring. Which values the Plot shows, and their colours, live here
// too, so the Watchlist's menu and the Plot agree. Fed on the GUI thread by
// the provider's completed reads; one appended() per event-loop turn.
class WatchTraces final : public QObject {
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("A provider owns its traces.")
	Q_PROPERTY(QStringList plotted READ plotted NOTIFY plottedChanged)
	Q_PROPERTY(int plotLimit READ plotLimit CONSTANT)
	Q_PROPERTY(quint64 revision READ revision NOTIFY appended)
public:
	static constexpr qint64 keptNs = 600'000'000'000; // ten minutes
	static constexpr std::size_t keptReadings = 65'536;
	static constexpr int limit = 16;

	explicit WatchTraces(QObject* parent = nullptr);

	// The paths the next frames' rows are: a row's series (its Field id and
	// its leaf, WatchFrameRow) to the value's path, for one membership of the
	// Watchlist. The provider gives them again when the membership changes.
	[[nodiscard]] quint64 membership() const noexcept { return membership_; }
	void setPaths(quint64 membership, QHash<quint64, QString> paths, QHash<QString, bool> categorical = {});
	// A completed read: each of its rows appended to its value's ring.
	void append(const WatchFrame& frame);

	[[nodiscard]] const std::deque<TraceReading>* readings(const QString& path) const;
	[[nodiscard]] bool categorical(const QString& path) const;
	// The time origin every reading's ns is measured against, as this PC's
	// clock: utcMs at monotonicNs (the first frame's stamp).
	[[nodiscard]] WatchTimestamp anchor() const noexcept { return anchor_; }
	// The newest reading's time over every ring, or 0 before the first.
	[[nodiscard]] qint64 newestNs() const noexcept { return newestNs_; }
	// A steady-clock now, the frames' own clock.
	[[nodiscard]] static qint64 nowNs() noexcept;

	QStringList plotted() const { return plotted_; }
	int plotLimit() const noexcept { return limit; }
	quint64 revision() const noexcept { return revision_; }

	// Puts path on the Plot: empty when done, else why not, in words.
	Q_INVOKABLE QString plot(const QString& path);
	Q_INVOKABLE void unplot(const QString& path);
	Q_INVOKABLE bool isPlotted(const QString& path) const { return plotted_.contains(path); }
	// Its colour's place in the plot palette: the first free one when it was
	// first plotted, kept for the path.
	Q_INVOKABLE int colorIndex(const QString& path) const { return colors_.value(path, -1); }
	// The newest reading's number, or undefined when it brought none.
	Q_INVOKABLE QVariant latest(const QString& path) const;
	Q_INVOKABLE int count(const QString& path) const;
	// Every ring emptied; what is plotted stays.
	Q_INVOKABLE void clear();

Q_SIGNALS:
	void appended();
	void plottedChanged();

private:
	struct Trace {
		std::deque<TraceReading> readings;
		WatchValueType type;
		bool categorical = false;
	};
	void push(Trace& trace, const TraceReading& reading);
	void announce();

	QHash<QString, Trace> traces_;
	QHash<quint64, QString> paths_;
	QHash<QString, bool> categorical_;
	QStringList plotted_;
	QHash<QString, int> colors_;
	WatchTimestamp anchor_;
	quint64 membership_ = ~quint64{0};
	qint64 newestNs_ = 0;
	quint64 revision_ = 0;
	bool announcing_ = false;
};
} // namespace telemetry::ux
#endif
