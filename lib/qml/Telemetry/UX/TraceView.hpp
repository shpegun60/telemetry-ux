// The live Plot's drawing: plotted values over time of day (LP-1). MIT.
#ifndef TELEMETRY_UX_TRACE_VIEW_HPP
#define TELEMETRY_UX_TRACE_VIEW_HPP
#pragma once
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickPaintedItem>
#include <utility>

namespace telemetry::ux {
class WatchTraces;

// Draws the plotted values of a WatchTraces: the grid, the time of day under
// it, one value axis every number shares, and a line per value - its raw
// readings while there are at most two for each device pixel of the width,
// else each pixel's first, lowest, highest and last reading, so no peak is
// lost. A reading that brought no number, or a pause in reading, breaks the
// line. Following, the right edge is now and moves with it; otherwise it
// stays where it was. Painted by QPainter, so it draws alike on every Qt Quick
// backend, the software one included.
//
// Moving it (LP-1b): a drag pans, the wheel zooms about the pointer, Left and
// Right pan a tenth, + and - zoom about the middle, Home goes to the oldest
// reading kept, End follows again, Space and H hold or follow, 1 to 4 choose
// 10 s, 1 min, 10 min or All. Panning, zooming and Home stop following;
// reading goes on.
//
// The cursor (LP-1c): under the pointer, snapped to the nearest reading within
// 8 px, else free; Shift+Left and Shift+Right step it reading by reading. A
// click, or Enter, pins a reference there - its readings copied, so a ring
// trimmed meanwhile never changes them - and Esc takes it away. valueAt() and
// referenceValue() give each value at the cursor and at the reference, for
// the chips and their differences.
//
// Lanes (LP-1d): a bool or an enum is drawn under the numbers in a lane of its
// own, a row high, never on their axis - a bool as a band while true and a
// line while false, an enum as a band per state with its name where it fits
// and a mark where it changes. Lanes take at most 40 % of the height.
//
// The values (LP-1e): Auto fits every number in view, or only the focused
// curve while one is focused; Manual starts with a value zoom or pan - Ctrl
// and the wheel over the graph, or the wheel over the value labels, zoom about
// the pointer; a drag over the labels pans; Up and Down pan a tenth, Ctrl+Up
// and Ctrl+Down zoom - and stays through Follow, the presets and time zoom.
// A double-click on the labels, or A, returns to Auto.
//
// A curve's look (LP-1f): its own colour, if it has one, and its multiplier
// and offset - a number is drawn and fitted as k x value + b, while every
// value the chips and the cursor give stays the reading - and its
// measurement points: a 5 px dot at each reading while the curve is drawn raw,
// never closer than 6 px to the last.
class TraceView : public QQuickPaintedItem {
	Q_OBJECT
	QML_ELEMENT
	Q_PROPERTY(QObject* traces READ traces WRITE setTraces NOTIFY tracesChanged)
	Q_PROPERTY(qreal spanSeconds READ spanSeconds WRITE setSpanSeconds NOTIFY viewChanged)
	Q_PROPERTY(bool following READ following WRITE setFollowing NOTIFY viewChanged)
	// Everything kept, oldest to newest, growing while following.
	Q_PROPERTY(bool showAll READ showAll WRITE setShowAll NOTIFY viewChanged)
	// The chosen span: 10, 60 or 600 seconds, 0 for All, -1 after a zoom.
	Q_PROPERTY(int preset READ preset NOTIFY viewChanged)
	// Held, how far the right edge is behind the newest reading.
	Q_PROPERTY(qreal behindSeconds READ behindSeconds NOTIFY drawn)
	// Auto (fitted) or Manual (zoomed or panned by hand) values.
	Q_PROPERTY(bool valueAuto READ valueAuto NOTIFY viewChanged)
	// The curve the values are fitted to while Auto; empty for every one.
	Q_PROPERTY(QString focusedPath READ focusedPath WRITE setFocusedPath NOTIFY viewChanged)
	// The numbers' part of the plot area, above the lanes.
	Q_PROPERTY(QRectF valuesArea READ valuesArea NOTIFY drawn)
	// Measurement points the last paint drew, for its checks.
	Q_PROPERTY(int drawnDots READ drawnDots NOTIFY drawn)
	// Bands the last paint drew where nothing was read, and the open circles
	// that end a value no longer watched.
	Q_PROPERTY(int drawnGaps READ drawnGaps NOTIFY drawn)
	Q_PROPERTY(int drawnEnds READ drawnEnds NOTIFY drawn)
	// A band where the Watchlist was stopped or the device gone.
	Q_PROPERTY(QColor gapColor READ gapColor WRITE setGapColor NOTIFY styleChanged)
	// The cursor: shown, its time on this PC's clock (ms since the epoch),
	// how long before the newest reading, and where it is drawn (item x;
	// outside the plot area when it has scrolled out).
	Q_PROPERTY(bool cursorShown READ cursorShown NOTIFY cursorChanged)
	Q_PROPERTY(qreal cursorUtcMs READ cursorUtcMs NOTIFY cursorChanged)
	Q_PROPERTY(qreal cursorAgoSeconds READ cursorAgoSeconds NOTIFY cursorChanged)
	Q_PROPERTY(qreal cursorX READ cursorX NOTIFY drawn)
	// The pinned reference, likewise.
	Q_PROPERTY(bool referenceShown READ referenceShown NOTIFY cursorChanged)
	Q_PROPERTY(qreal referenceUtcMs READ referenceUtcMs NOTIFY cursorChanged)
	Q_PROPERTY(qreal referenceX READ referenceX NOTIFY drawn)
	Q_PROPERTY(QColor cursorColor READ cursorColor WRITE setCursorColor NOTIFY styleChanged)
	Q_PROPERTY(QColor referenceColor READ referenceColor WRITE setReferenceColor NOTIFY styleChanged)
	Q_PROPERTY(QColor dotRingColor READ dotRingColor WRITE setDotRingColor NOTIFY styleChanged)
	// Room kept under the time labels for the cursor's and the reference's
	// chips, so they never cover a label.
	Q_PROPERTY(qreal bottomInset READ bottomInset WRITE setBottomInset NOTIFY styleChanged)
	// A lane's height, and its colours: an enum state's band and its text.
	Q_PROPERTY(qreal laneHeight READ laneHeight WRITE setLaneHeight NOTIFY styleChanged)
	Q_PROPERTY(QColor laneFillColor READ laneFillColor WRITE setLaneFillColor NOTIFY styleChanged)
	Q_PROPERTY(QColor laneTextColor READ laneTextColor WRITE setLaneTextColor NOTIFY styleChanged)
	// The lanes the last paint drew, by their leaf names, and the states it
	// named in them.
	Q_PROPERTY(QStringList laneLabels READ laneLabels NOTIFY drawn)
	Q_PROPERTY(QStringList laneStates READ laneStates NOTIFY drawn)
	// A colour per palette place (WatchTraces::colorIndex).
	Q_PROPERTY(QVariantList colors READ colors WRITE setColors NOTIFY styleChanged)
	Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY styleChanged)
	Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY styleChanged)
	Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY styleChanged)
	// What the last paint drew, for its checks and its readers.
	Q_PROPERTY(QStringList timeLabels READ timeLabels NOTIFY drawn)
	Q_PROPERTY(QStringList valueLabels READ valueLabels NOTIFY drawn)
	Q_PROPERTY(int drawnPoints READ drawnPoints NOTIFY drawn)
	Q_PROPERTY(int rawCurves READ rawCurves NOTIFY drawn)
	Q_PROPERTY(int drawnCurves READ drawnCurves NOTIFY drawn)
	Q_PROPERTY(QRectF plotArea READ plotArea NOTIFY drawn)
	// The time at the right edge, ms since the epoch on this PC's clock.
	Q_PROPERTY(qreal rightUtcMs READ rightUtcMs NOTIFY drawn)
	// The time axis (LP-1h): the time of day, or the time since reading last
	// started - the end of the last stop or disconnect before the right edge,
	// else the first reading - whose time of day is originUtcMs.
	Q_PROPERTY(bool fromStart READ fromStart WRITE setFromStart NOTIFY viewChanged)
	// Box zoom and Back (analysis): Ctrl and a drag draw a box whose width
	// becomes the time in view and whose height the values; every move of the
	// view by hand can be gone back on (Backspace, Alt+Left), one gesture a
	// step, the last 50. Not the Undo of edits: views are not changes.
	Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY viewChanged)
	// The end pin (analysis): with the reference, the range measured. Shift
	// and a click, or Shift+Enter, pin it at the cursor - the reference first
	// when there is none; a click on it, or Esc, takes it away.
	Q_PROPERTY(bool endShown READ endShown NOTIFY cursorChanged)
	Q_PROPERTY(qreal endUtcMs READ endUtcMs NOTIFY cursorChanged)
	Q_PROPERTY(qreal endX READ endX NOTIFY drawn)
	Q_PROPERTY(bool boxShown READ boxShown NOTIFY cursorChanged)
	Q_PROPERTY(qreal originUtcMs READ originUtcMs NOTIFY drawn)
public:
	explicit TraceView(QQuickItem* parent = nullptr);

	QObject* traces() const;
	void setTraces(QObject* traces);
	qreal spanSeconds() const { return spanSeconds_; }
	void setSpanSeconds(qreal seconds);
	bool following() const { return following_; }
	void setFollowing(bool following);
	bool showAll() const { return showAll_; }
	void setShowAll(bool all);
	int preset() const;
	qreal behindSeconds() const { return behindSeconds_; }

	// 10, 60 or 600 seconds, or 0 for All; following the newest reading.
	Q_INVOKABLE void choosePreset(int seconds);
	// The span times factor (below 1 zooms in), about a point of the width
	// (0 left, 1 right); stops following.
	Q_INVOKABLE void zoom(qreal factor, qreal at = 0.5);
	// By a part of the span (negative: back in time); stops following.
	Q_INVOKABLE void pan(qreal part);
	// The oldest reading kept at the left edge; stops following.
	Q_INVOKABLE void toOldest();
	Q_INVOKABLE void follow() { setFollowing(true); }

	bool valueAuto() const { return valueAuto_; }
	QString focusedPath() const { return focusedPath_; }
	void setFocusedPath(const QString& path);
	QRectF valuesArea() const { return valuesArea_; }
	int drawnDots() const { return drawnDots_; }
	int drawnGaps() const { return drawnGaps_; }
	int drawnEnds() const { return drawnEnds_; }
	QColor gapColor() const { return gapColor_; }
	void setGapColor(const QColor& color);
	// The colour a path is drawn in: its own, else its palette place's.
	Q_INVOKABLE QColor curveColor(const QString& path) const;
	// The values times factor (below 1 zooms in) about a point of the
	// height (0 top, 1 bottom); Manual.
	Q_INVOKABLE void zoomValues(qreal factor, qreal at = 0.5);
	// The values by a part of their range (positive: higher ones); Manual.
	Q_INVOKABLE void panValues(qreal part);
	// Fitted again: Auto.
	Q_INVOKABLE void autoValues();

	bool cursorShown() const { return cursorShown_; }
	qreal cursorUtcMs() const;
	qreal cursorAgoSeconds() const;
	qreal cursorX() const { return cursorX_; }
	bool referenceShown() const { return referenceShown_; }
	qreal referenceUtcMs() const;
	qreal referenceX() const { return referenceX_; }
	QColor cursorColor() const { return cursorColor_; }
	void setCursorColor(const QColor& color);
	QColor referenceColor() const { return referenceColor_; }
	void setReferenceColor(const QColor& color);
	QColor dotRingColor() const { return dotRingColor_; }
	void setDotRingColor(const QColor& color);
	qreal bottomInset() const { return bottomInset_; }
	void setBottomInset(qreal inset);
	qreal laneHeight() const { return laneHeight_; }
	void setLaneHeight(qreal height);
	QColor laneFillColor() const { return laneFillColor_; }
	void setLaneFillColor(const QColor& color);
	QColor laneTextColor() const { return laneTextColor_; }
	void setLaneTextColor(const QColor& color);
	QStringList laneLabels() const { return laneLabels_; }
	QStringList laneStates() const { return laneStates_; }
	// The cursor at item x, snapped to the nearest reading within 8 px.
	Q_INVOKABLE void setCursorAt(qreal x);
	Q_INVOKABLE void hideCursor();
	// The cursor to the previous (-1) or next (1) reading of any value.
	Q_INVOKABLE void stepCursor(int direction);
	// A reference where the cursor is; taken away again.
	Q_INVOKABLE void pinReference();
	Q_INVOKABLE void clearReference();
	// path's reading at or before the cursor, and at the reference: its
	// number, or undefined when there is none or it brought none.
	Q_INVOKABLE QVariant valueAt(const QString& path) const;
	Q_INVOKABLE QVariant referenceValue(const QString& path) const;
	QVariantList colors() const { return colors_; }
	void setColors(const QVariantList& colors);
	QColor gridColor() const { return gridColor_; }
	void setGridColor(const QColor& color);
	QColor labelColor() const { return labelColor_; }
	void setLabelColor(const QColor& color);
	QFont font() const { return font_; }
	void setFont(const QFont& font);

	QStringList timeLabels() const { return timeLabels_; }
	QStringList valueLabels() const { return valueLabels_; }
	int drawnPoints() const { return drawnPoints_; }
	int rawCurves() const { return rawCurves_; }
	int drawnCurves() const { return drawnCurves_; }
	QRectF plotArea() const { return plotArea_; }
	qreal rightUtcMs() const { return rightUtcMs_; }
	bool fromStart() const { return fromStart_; }
	bool canGoBack() const { return !back_.empty(); }
	bool boxShown() const { return boxing_; }
	Q_INVOKABLE void goBack();
	bool endShown() const { return endShown_; }
	qreal endUtcMs() const;
	qreal endX() const { return endX_; }
	Q_INVOKABLE void pinEnd();
	Q_INVOKABLE void clearEnd();
	// The range measured, copied and saved: the reference to the end when
	// both are pinned, else the time in view - {fromUtcMs, toUtcMs, seconds,
	// pinned, readings}.
	Q_INVOKABLE QVariantMap measureRange() const;
	// A plotted value over that range, from its readings as read (never
	// k x value + b). A number: {count, failed, min, max, minUtcMs, maxUtcMs,
	// mean, peakToPeak, deviation}; a state: {count, failed, changes,
	// shares: [{name, share}]}, the shares by time.
	Q_INVOKABLE QVariantMap statistics(const QString& path) const;
	// Puts the cursor at utcMs, bringing it into view (Go to minimum).
	Q_INVOKABLE void putCursorAt(qreal utcMs);
	// The readings of every plotted value in the time in view, one row per
	// reading time: "Time of day", "Seconds from start" and a column per
	// path; a missing or failed read is an empty cell, a state is its word,
	// a number is the reading (never k x value + b). For the clipboard (tabs,
	// this PC's decimal sign) or a CSV file ("." and ","). readingsInRange
	// counts its rows.
	Q_INVOKABLE QString valuesText(bool csv) const;
	Q_INVOKABLE int readingsInRange() const;
	// Writes valuesText(true) to the file at url; empty, or why not.
	Q_INVOKABLE QString saveValues(const QUrl& url) const;
	// Zooms to the box between two points of the item: the time to its width,
	// the values to its height when it is at least 8 px tall and over the
	// numbers.
	Q_INVOKABLE void zoomToBox(QPointF from, QPointF to);
	void setFromStart(bool fromStart);
	qreal originUtcMs() const { return originUtcMs_; }

	void paint(QPainter* painter) override;

protected:
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void hoverMoveEvent(QHoverEvent* event) override;
	void hoverLeaveEvent(QHoverEvent* event) override;

Q_SIGNALS:
	void tracesChanged();
	void viewChanged();
	void cursorChanged();
	void styleChanged();
	void drawn();

private:
	struct Range {
		double low = 0, high = 0;
		bool valid() const { return high > low; }
	};
	qint64 rightNs() const;
	qint64 spanNs() const;
	// The oldest and newest reading of the plotted values; 0 when none.
	std::pair<qint64, qint64> kept() const;
	// Stops following with the right edge at right, inside what is kept.
	void hold(qint64 right);
	void tick();
	void schedule();

	QPointer<WatchTraces> traces_;
	QTimer frames_;
	qreal spanSeconds_ = 60;
	bool following_ = true, showAll_ = false;
	// All, held: its span when it was held, so new readings do not
	// rescale a picture that should stand still.
	qint64 heldAllNs_ = 0;
	// The cursor's time, and the reference's with each value's reading then.
	bool cursorShown_ = false, referenceShown_ = false;
	qint64 cursorNs_ = 0, referenceNs_ = 0;
	QHash<QString, double> referenceValues_;
	qreal cursorX_ = -1, referenceX_ = -1;
	QPointF hoverAt_{-1, -1};
	qreal bottomInset_ = 0, laneHeight_ = 22;
	QColor laneFillColor_{0x2a, 0x2f, 0x37}, laneTextColor_{0xe6, 0xed, 0xf3};
	QStringList laneLabels_, laneStates_;
	QColor cursorColor_{0x8b, 0x94, 0x9e}, referenceColor_{0x58, 0xa6, 0xff}, dotRingColor_{0x0d, 0x11, 0x17};
	qreal behindSeconds_ = 0;
	// A drag: where it started and the right edge then.
	qreal pressX_ = 0;
	qint64 pressRightNs_ = 0;
	bool dragging_ = false;
	// A press on the value labels pans the values.
	bool valueDrag_ = false;
	qreal pressY_ = 0;
	qint64 frozenNs_ = 0;
	QVariantList colors_;
	QColor gridColor_{0x3a, 0x3f, 0x47}, labelColor_{0x9a, 0xa4, 0xb0};
	QFont font_;
	// The value axis widens at once and narrows after two seconds, so a
	// following plot does not jump with every reading.
	Range range_;
	// Manual values, the range last drawn, the one at a press.
	bool valueAuto_ = true;
	Range manual_, shown_, pressRange_;
	QString focusedPath_;
	QRectF valuesArea_;
	int drawnDots_ = 0, drawnGaps_ = 0, drawnEnds_ = 0;
	QColor gapColor_{0x15, 0x1d, 0x27};
	qint64 narrowSinceNs_ = 0;
	// Per path: drawn per pixel rather than raw, for the hysteresis.
	QHash<QString, bool> aggregated_;
	QStringList timeLabels_, valueLabels_;
	int drawnPoints_ = 0, rawCurves_ = 0, drawnCurves_ = 0;
	QRectF plotArea_;
	qreal rightUtcMs_ = 0;
	bool fromStart_ = false;
	// What a view by hand was, to go back to.
	struct ViewState {
		bool following = true, showAll = false, valueAuto = true;
		qint64 heldAllNs = 0, frozenNs = 0;
		qreal spanSeconds = 60;
		double manualLow = 0, manualHigh = 0;
	};
	std::vector<ViewState> back_;
	qint64 rememberedAtMs_ = 0;
	// Before a view changes by hand: what it was, once a gesture.
	void remember();
	bool boxing_ = false;
	bool endShown_ = false;
	qint64 endNs_ = 0;
	qreal endX_ = -1;
	// The range measured, in the readings' time.
	std::pair<qint64, qint64> measuredNs() const;
	QPointF boxFrom_, boxTo_;
	qreal originUtcMs_ = 0;
};
} // namespace telemetry::ux
#endif
