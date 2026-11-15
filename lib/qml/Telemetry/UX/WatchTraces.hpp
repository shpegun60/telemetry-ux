// The Watchlist's readings kept in memory for the live Plot (LP-1). MIT.
#ifndef TELEMETRY_UX_WATCH_TRACES_HPP
#define TELEMETRY_UX_WATCH_TRACES_HPP
#pragma once
#include "WatchFrame.hpp"
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtQml/qqmlregistration.h>
#include <deque>
#include <optional>
#include <vector>

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
	// Measurement points on every curve whose own setting is "as the Plot".
	Q_PROPERTY(bool showPoints READ showPoints WRITE setShowPoints NOTIFY appearanceChanged)
	// Bumped whenever a curve's colour, multiplier, offset or points change.
	Q_PROPERTY(quint64 appearanceRevision READ appearanceRevision NOTIFY appearanceChanged)
public:
	static constexpr qint64 keptNs = 600'000'000'000; // ten minutes
	static constexpr std::size_t keptReadings = 65'536;
	// Every ring together: 16,777,216 readings, 256 MB (64 MB until WL-7:
	// this runs on a PC, and 8192 values read once a second fill ten
	// minutes with 4.9 million). Past it the oldest go first - of values no
	// longer watched, then of values not plotted, the plotted last.
	static constexpr std::size_t keptTotal = 16'777'216;
	static constexpr int limit = 16;

	explicit WatchTraces(QObject* parent = nullptr);

	// The paths the next frames' rows are: a row's series (its Field id and
	// its leaf, WatchFrameRow) to the value's path, for one membership of the
	// Watchlist. The provider gives them again when the membership changes,
	// at the time of the frame that brought the change (0: now): a value
	// that left or came back breaks its line just before it.
	[[nodiscard]] quint64 membership() const noexcept { return membership_; }
	void setPaths(quint64 membership, QHash<quint64, QString> paths, QHash<QString, bool> categorical = {}, qint64 at = 0);
	// An enum value's state names by code, for its lane and its chip (LP-1d).
	void setStates(const QString& path, QHash<qint64, QString> names);
	// Every reading kept, over every ring.
	[[nodiscard]] std::size_t total() const noexcept { return total_; }
	// A completed read: each of its rows appended to its value's ring.
	void append(const WatchFrame& frame);

	[[nodiscard]] const std::deque<TraceReading>* readings(const QString& path) const;
	[[nodiscard]] bool categorical(const QString& path) const;
	// A bool's or an enum's reading in words: "true", "Idle"; an enum code
	// with no name "Code 7"; empty for a number.
	Q_INVOKABLE QString stateText(const QString& path, const QVariant& value) const;
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

	// A recording shown with the same view (TR-2): the host gives a path's
	// readings - one run's, in time order, NaN for a gap - in place of the
	// frames, and the anchor that gives them this PC's time of day. The live
	// rules do not apply to them: no ten minutes, no cap per path, and the
	// path counts as watched, so its chip never says "not read since". A
	// state value (a bool or an enum) is drawn in a lane, its names from
	// setStates.
	void setRecorded(const QString& path, std::vector<TraceReading> readings, bool states);
	void setAnchor(const WatchTimestamp& anchor);

	// A curve's look (LP-1f), kept for its path whether it is plotted or not:
	// its own colour ("#rrggbb", or "" for its palette place), and the
	// multiplier k and offset b it is drawn with, y = k x value + b - for
	// display only, the readings stay as they were read. False, and nothing
	// changed, for a colour that is not #rrggbb or a k or b not finite.
	Q_INVOKABLE bool setAppearance(const QString& path, const QString& color, double multiplier, double offset);
	Q_INVOKABLE QString color(const QString& path) const { return appearance_.value(path).color; }
	Q_INVOKABLE double multiplier(const QString& path) const { return appearance_.value(path).multiplier; }
	Q_INVOKABLE double offset(const QString& path) const { return appearance_.value(path).offset; }
	// Its measurement points: 0 as the Plot, 1 shown, 2 hidden.
	Q_INVOKABLE void setPoints(const QString& path, int mode);
	Q_INVOKABLE int points(const QString& path) const { return appearance_.value(path).points; }
	// Whether its points are drawn, the Plot's setting applied.
	Q_INVOKABLE bool pointsShown(const QString& path) const;
	bool showPoints() const noexcept { return showPoints_; }
	void setShowPoints(bool show);
	quint64 appearanceRevision() const noexcept { return appearanceRevision_; }
	// What a project keeps of the Plot: the curves plotted, in order, and
	// every curve's look, and the Plot's points; restore() plots them again.
	Q_INVOKABLE QVariantMap capture() const;
	Q_INVOKABLE bool restore(const QVariantMap& state);
	// Every line ends here - the Watchlist stopped, the device went - so
	// the time not read is never drawn as if it were.
	// Why nothing was read for a while (LP-1g): the Watchlist stopped, or
	// the device gone. From the break to the next reading; to 0 while it lasts.
	enum class PauseReason : quint8 { Stopped, Disconnected };
	struct Pause {
		qint64 fromNs = 0, toNs = 0;
		PauseReason reason = PauseReason::Stopped;
	};
	void breakAll(PauseReason reason = PauseReason::Stopped);
	// The last 1,024 pauses, oldest first.
	[[nodiscard]] const std::deque<Pause>& pauses() const noexcept { return pauses_; }
	// Whether path is in the Watchlist now; one that left keeps its readings.
	Q_INVOKABLE bool watched(const QString& path) const { return watched_.contains(path); }
	// This PC's time of path's newest reading with a value, NaN when none.
	Q_INVOKABLE double lastReadUtcMs(const QString& path) const;
	// Why path cannot be plotted, or empty: a value whose readings are not a
	// number, a bool or an enum (text).
	[[nodiscard]] QString refusal(const QString& path) const;

Q_SIGNALS:
	void appended();
	void plottedChanged();
	void appearanceChanged();

private:
	struct Trace {
		std::deque<TraceReading> readings;
		WatchValueType type;
		bool categorical = false;
	};
	void push(Trace& trace, const TraceReading& reading);
	void enforceCap();
	void announce();

	QHash<QString, Trace> traces_;
	QHash<quint64, QString> paths_;
	QSet<QString> watched_;
	std::deque<Pause> pauses_;
	QHash<QString, bool> categorical_;
	QHash<QString, QHash<qint64, QString>> states_;
	QStringList plotted_;
	QHash<QString, int> colors_;
	struct Appearance {
		QString color;
		double multiplier = 1, offset = 0;
		int points = 0;
		bool plain() const { return color.isEmpty() && multiplier == 1 && offset == 0 && points == 0; }
	};
	QHash<QString, Appearance> appearance_;
	bool showPoints_ = false;
	quint64 appearanceRevision_ = 0;
	WatchTimestamp anchor_;
	quint64 membership_ = ~quint64{0};
	qint64 newestNs_ = 0;
	std::size_t total_ = 0;
	quint64 revision_ = 0;
	bool announcing_ = false;
};
} // namespace telemetry::ux
#endif
